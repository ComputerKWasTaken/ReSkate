// Lists the data assets of the game's level root bundle, or the values of one of them.
//   dingosdk_trainer_asset_list <Skate folder> <word> [<word> ...]   names containing any word
//   dingosdk_trainer_asset_list <Skate folder> --dump <asset name>   every plain value, by name
#include "Engine/Resource/ebx_document.h"
#include "Engine/Vfs/game_bundles.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ebx = dingosdk::frostbite::ebx;
namespace {
std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
std::size_t value_size(ebx::FieldType type) {
    switch (type) {
    case ebx::FieldType::boolean: case ebx::FieldType::int8: case ebx::FieldType::uint8: return 1;
    case ebx::FieldType::int16: case ebx::FieldType::uint16: return 2;
    case ebx::FieldType::int32: case ebx::FieldType::uint32: case ebx::FieldType::enumeration:
    case ebx::FieldType::float32: return 4;
    case ebx::FieldType::int64: case ebx::FieldType::uint64: case ebx::FieldType::float64: return 8;
    default: return 0;
    }
}
void walk(const ebx::Document &document, const ebx::TypeDescriptor &type, std::span<const std::byte> image, std::size_t start, int depth,
          const std::string &path) {
    if (depth > 16) return;
    for (std::size_t i = 0; i < type.fieldCount; ++i) {
        const auto index = static_cast<std::size_t>(type.fieldIndex) + i;
        if (index >= document.fields.size()) return;
        const auto &field = document.fields[index];
        const auto kind = field.type();
        const auto name = path.empty() ? field.name : field.name.empty() ? path : path + "." + field.name;
        if (kind == ebx::FieldType::inherited || (kind == ebx::FieldType::structure && field.category() != ebx::FieldCategory::array)) {
            if (field.classRef >= document.types.size()) continue;
            const bool inherited = kind == ebx::FieldType::inherited;
            walk(document, document.types[field.classRef], image, inherited ? start : start + field.dataOffset, depth + 1,
                 inherited ? path : name);
            continue;
        }
        const auto offset = start + field.dataOffset;
        if (field.category() == ebx::FieldCategory::array) {
            std::cout << name << "\t0x" << std::hex << offset << std::dec << "\tarray\n";
            continue;
        }
        if (kind == ebx::FieldType::pointer) {
            std::cout << name << "\t0x" << std::hex << offset << std::dec << "\tpointer\n";
            continue;
        }
        const auto size = value_size(kind);
        if (!size || offset + size > image.size()) continue;
        std::cout << name << "\t0x" << std::hex << offset << std::dec << '\t';
        if (kind == ebx::FieldType::float32) {
            float value;
            std::memcpy(&value, image.data() + offset, 4);
            std::cout << "real\t" << value;
        } else if (kind == ebx::FieldType::boolean) {
            std::cout << "flag\t" << static_cast<int>(image[offset]);
        } else {
            std::int64_t value{};
            std::memcpy(&value, image.data() + offset, size);
            std::cout << "int" << size * 8 << '\t' << value;
        }
        std::cout << '\n';
    }
}
} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: dingosdk_trainer_asset_list <Skate folder> <word>... | --dump <asset name> | --types <word>...\n";
        return 2;
    }
    try {
        const std::filesystem::path game_root(argv[1]);
        const dingosdk::vfs::GameData data(game_root);
        const auto found = data.read_bundle(data.read_toc("Win32/levels/game/bam_levelroot/bam_levelroot.toc"),
                                            "win32/levels/game/bam_levelroot/bam_levelroot");
        if (!found) throw std::runtime_error("the level root bundle was not found");
        const auto &bundle = *found;
        const std::string mode = argv[2];
        if (mode == "--floats" && argc >= 4) {
            // Every aligned four bytes of the asset's data that read as a plausible number.
            const auto wanted = lower(argv[3]);
            for (std::size_t i = 0; i < bundle.manifest.ebx.size(); ++i) {
                if (lower(bundle.manifest.ebx[i].name) != wanted) continue;
                const auto *payload = bundle.payload(dingosdk::frostbite::AssetKind::ebx, i);
                if (!payload) break;
                const auto bytes = data.read(*payload);
                const auto document = ebx::read_document(bytes);
                std::cerr << document.rootType << ": data " << document.dataStart << ".." << document.dataEnd << " of " << bytes.size() << "\n";
                for (std::size_t at = document.dataStart & ~std::size_t{3}; at + 4 <= bytes.size(); at += 4) {
                    float value;
                    std::memcpy(&value, bytes.data() + at, 4);
                    const float size = value < 0 ? -value : value;
                    if (size > 0.0009f && size < 100000.0f) std::cout << "0x" << std::hex << at << std::dec << "\t" << value << "\n";
                }
                return 0;
            }
            return 1;
        }
        if (mode == "--dump" && argc >= 4) {
            const auto wanted = lower(argv[3]);
            for (std::size_t i = 0; i < bundle.manifest.ebx.size(); ++i) {
                if (lower(bundle.manifest.ebx[i].name) != wanted) continue;
                const auto *payload = bundle.payload(dingosdk::frostbite::AssetKind::ebx, i);
                if (!payload) break;
                const auto bytes = data.read(*payload);
                const auto document = ebx::read_document(bytes);
                std::cerr << bundle.manifest.ebx[i].name << ": root type " << document.rootType << ", " << document.instances.size()
                          << " instances\n";
                // Every instance: an asset keeps its lists and sub-objects as separate ones.
                std::size_t number{};
                for (const auto &instance : document.instances) {
                    if (instance.descriptor < 0 || static_cast<std::size_t>(instance.descriptor) >= document.types.size()) continue;
                    const auto &type = document.types[static_cast<std::size_t>(instance.descriptor)];
                    std::cout << "# instance " << number++ << " " << type.name << " (" << instance.rawImage.size() << " bytes)\n";
                    walk(document, type, instance.rawImage, 0, 0, {});
                }
                return 0;
            }
            std::cerr << "no such asset\n";
            return 1;
        }
        std::vector<std::string> words;
        for (int i = (mode == "--types" ? 3 : 2); i < argc; ++i) words.push_back(lower(argv[i]));
        std::size_t shown{};
        for (std::size_t i = 0; i < bundle.manifest.ebx.size(); ++i) {
            const auto name = lower(bundle.manifest.ebx[i].name);
            std::string type;
            bool match = mode != "--types" && std::ranges::any_of(words, [&](const std::string &w) { return name.find(w) != std::string::npos; });
            if (mode == "--types") {
                // Slower: opens every asset to learn its root type.
                try {
                    if (const auto *payload = bundle.payload(dingosdk::frostbite::AssetKind::ebx, i)) {
                        type = ebx::read_document(data.read(*payload)).rootType;
                        const auto low = lower(type);
                        match = std::ranges::any_of(words, [&](const std::string &w) { return low.find(w) != std::string::npos; });
                    }
                } catch (const std::exception &) {}
            }
            if (!match) continue;
            std::cout << bundle.manifest.ebx[i].name << (type.empty() ? "" : "\t" + type) << '\n';
            ++shown;
        }
        std::cerr << shown << " of " << bundle.manifest.ebx.size() << " assets\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
