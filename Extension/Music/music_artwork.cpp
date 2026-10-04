#include <winsock2.h>
#include <Windows.h>
#include "music_artwork.h"
#include "Engine/Vfs/mod_music.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace dingosdk::profile_runtime {
namespace {
constexpr std::size_t max_image = 4 * 1024 * 1024, max_total = 64 * 1024 * 1024;
struct Socket {
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};
bool send_all(SOCKET socket, const char* bytes, std::size_t count) {
    while (count) {
        const auto sent = send(socket, bytes, static_cast<int>(count), 0);
        if (sent <= 0) return false;
        bytes += sent;
        count -= static_cast<std::size_t>(sent);
    }
    return true;
}
bool png_ok(const std::vector<char>& bytes) {
    constexpr unsigned char magic[]{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 33) return false;
    for (std::size_t i = 0; i < 8; ++i)
        if (static_cast<unsigned char>(bytes[i]) != magic[i]) return false;
    if (std::string_view(bytes.data() + 12, 4) != "IHDR") return false;
    const auto dimension = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t i = offset; i < offset + 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[i]);
        return value;
    };
    const auto w = dimension(16), h = dimension(20);
    return w && h && w <= 2048 && h <= 2048;
}
}
struct MusicArtworkServer::Impl {
    bool winsock = false;
    Socket listener;
    unsigned short port = 0;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<const std::vector<char>>> images;
    std::map<std::filesystem::path, std::pair<std::filesystem::file_time_type, std::string>> paths;
    std::size_t total = 0, next = 0;
    std::jthread worker;

    Impl() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
        winsock = true;
        listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener.value == INVALID_SOCKET) return;
        const BOOL exclusive = TRUE;
        if (setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) return;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener.value, SOMAXCONN) != 0) return;
        int size = sizeof(address);
        if (getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &size) != 0) return;
        worker = std::jthread([this](std::stop_token stop) { serve(stop); });
        port = ntohs(address.sin_port);
    }
    ~Impl() {
        worker.request_stop();
        if (worker.joinable()) worker.join();
        if (listener.value != INVALID_SOCKET) { closesocket(listener.value); listener.value = INVALID_SOCKET; }
        if (winsock) WSACleanup();
    }
    void serve(std::stop_token stop) noexcept {
        while (!stop.stop_requested()) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(listener.value, &ready);
            timeval timeout{0, 100000};
            if (select(0, &ready, nullptr, nullptr, &timeout) <= 0) continue;
            Socket client{accept(listener.value, nullptr, nullptr)};
            if (client.value == INVALID_SOCKET) continue;
            const DWORD milliseconds = 1000;
            setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
            setsockopt(client.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
            try { respond(client.value); } catch (...) {} // Failed requests never take down the music catalog.
        }
    }
    void respond(SOCKET client) {
        std::string request;
        std::array<char, 1024> buffer{};
        const auto started = GetTickCount64();
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
            if (GetTickCount64() - started > 2000) return;
            const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0) return;
            request.append(buffer.data(), static_cast<std::size_t>(received));
        }
        const auto end = request.find("\r\n");
        const auto line = request.substr(0, end);
        std::shared_ptr<const std::vector<char>> bytes;
        if (request.find("\r\n\r\n") != std::string::npos && line.starts_with("GET ")) {
            const auto space = line.find(' ', 4);
            if (space != std::string::npos && (line.substr(space) == " HTTP/1.1" || line.substr(space) == " HTTP/1.0")) {
                std::lock_guard lock(mutex);
                if (const auto found = images.find(line.substr(4, space - 4)); found != images.end()) bytes = found->second;
            }
        }
        if (!bytes) {
            constexpr std::string_view missing = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client, missing.data(), missing.size());
            return;
        }
        const auto header = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + std::to_string(bytes->size()) +
            "\r\nCache-Control: private, max-age=31536000\r\nConnection: close\r\n\r\n";
        if (send_all(client, header.data(), header.size())) send_all(client, bytes->data(), bytes->size());
    }
};
MusicArtworkServer::MusicArtworkServer() : impl_(std::make_unique<Impl>()) {}
MusicArtworkServer::~MusicArtworkServer() = default;
std::string MusicArtworkServer::add(const std::filesystem::path& mod, const std::string& relative) {
    if (!impl_->port || !mods::music_artwork_path(relative)) return {};
    try {
        const auto root = std::filesystem::canonical(mod);
        const auto file = std::filesystem::canonical(root / std::filesystem::path(std::u8string(relative.begin(), relative.end())));
        auto a = root.begin(), b = file.begin();
        for (; a != root.end(); ++a, ++b)
            if (b == file.end() || _wcsicmp(a->c_str(), b->c_str()) != 0) return {};
        if (b == file.end()) return {};
        const auto stamp = std::filesystem::last_write_time(file);
        std::lock_guard lock(impl_->mutex);
        if (const auto found = impl_->paths.find(file); found != impl_->paths.end() && found->second.first == stamp)
            return found->second.second;
        const auto size = std::filesystem::file_size(file);
        if (size > max_image || impl_->total + size > max_total) return {};
        std::ifstream input(file, std::ios::binary);
        auto bytes = std::make_shared<std::vector<char>>(static_cast<std::size_t>(size));
        if (!input.read(bytes->data(), static_cast<std::streamsize>(size)) || !png_ok(*bytes)) return {};
        const auto route = "/music-art/" + std::to_string(impl_->next++) + ".png";
        const auto url = "http://127.0.0.1:" + std::to_string(impl_->port) + route;
        impl_->images.emplace(route, bytes);
        impl_->total += bytes->size();
        impl_->paths[file] = {stamp, url};
        return url;
    } catch (const std::exception&) { return {}; }
}
std::string mod_music_artwork_url(const std::filesystem::path& mod, const std::string& relative) {
    // The injected runtime has process lifetime. Pin it before starting a worker, so its
    // code cannot be unloaded while the image endpoint is serving requests. Avoid joining
    // a worker from CRT teardown under the loader lock. Tests use the owned server above.
    static auto* server = []() -> MusicArtworkServer* {
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&mod_music_artwork_url), &module)) return nullptr;
        return new MusicArtworkServer;
    }();
    return server ? server->add(mod, relative) : std::string{};
}
}
