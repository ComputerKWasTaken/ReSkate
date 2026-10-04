#include <Windows.h>
#include <winhttp.h>
#include "Extension/Music/music_artwork.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Handle {
    HINTERNET value{};
    ~Handle() { if (value) WinHttpCloseHandle(value); }
};
std::pair<DWORD, std::string> fetch(const std::string& url) {
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return {};
    Handle session{WinHttpOpen(L"Artwork test", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0)};
    if (!session.value) return {};
    WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000);
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
    Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    if (!connection.value) return {};
    Handle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, nullptr, nullptr, 0)};
    if (!request.value || !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &size, nullptr)) return {};
    std::string body;
    std::array<char, 1024> buffer{};
    DWORD received{};
    while (WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &received) && received)
        body.append(buffer.data(), received);
    return {status, body};
}
}
int main() try {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"ReSkateArtworkTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root / L"mod" / L"artwork");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    // A real one-pixel grayscale PNG. Bytes returned by HTTP must match it exactly.
    constexpr unsigned char png[]{
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,
        0,0,0,11,73,68,65,84,120,218,99,100,248,15,0,1,5,1,1,39,24,227,102,0,0,0,0,73,69,78,68,174,66,96,130};
    const std::string bytes(reinterpret_cast<const char*>(png), sizeof(png));
    { std::ofstream out(root / L"mod/artwork/cover.png", std::ios::binary); out.write(bytes.data(), bytes.size()); }
    { std::ofstream out(root / L"mod/artwork/bad.png", std::ios::binary); out << "not an image"; }
    { std::ofstream out(root / L"outside.png", std::ios::binary); out.write(bytes.data(), bytes.size()); }
    dingosdk::profile_runtime::MusicArtworkServer server;
    const auto url = server.add(root / L"mod", "artwork/cover.png");
    check(url.starts_with("http://127.0.0.1:"), "server uses an ephemeral loopback URL");
    check(server.add(root / L"mod", "artwork/cover.png") == url, "repeated registration keeps the URL");
    check(server.add(root / L"mod", "../outside.png").empty(), "parent traversal is refused");
    check(server.add(root / L"mod", "artwork/missing.png").empty(), "missing image is ignored");
    check(server.add(root / L"mod", "artwork/bad.png").empty(), "non-PNG bytes are ignored");
    if (!url.empty()) {
        const auto [status, body] = fetch(url);
        check(status == 200 && body == bytes, "HTTP returns the exact registered PNG");
        const auto base = url.substr(0, url.find('/', 7));
        check(fetch(base + "/music-art/unknown.png").first == 404, "unknown route is not served");
        check(fetch(base + "/outside.png").first == 404, "HTTP cannot read arbitrary files");
    }
    std::error_code ec;
    fs::create_directory_symlink(root, root / L"mod" / L"escape", ec);
    if (!ec) check(server.add(root / L"mod", "escape/outside.png").empty(), "symlink escape is refused");
    else std::cout << "SKIP: symlink test needs Windows symlink privileges\n";
    if (!failures) std::cout << "music artwork tests passed\n";
    return failures ? 1 : 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
