#include "mod_merge_internal.h"
#include "Engine/Resource/cas_codec.h"
#include "Engine/Resource/ebx_document.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace dingosdk::mods::detail {

AssetOverrides collect_asset_overrides(const std::vector<const Mod*>& mods,
                                       const std::map<const Mod*, RelativeFiles>& modFiles,
                                       const CasStore& store, const fs::path& baseRoot,
                                       const fs::path& gameRoot, MergeReport& report) {
    AssetOverrides out;
    // Highest priority first: an asset a higher mod already changed keeps that change.
    for (const auto* mod : mods) {
        // A map's edits to the game's assets serve its own levels; asset mods
        // (cameras, tuning, cosmetics) are the ones meant to apply everywhere.
        if (mod->provides_levels) continue;
        const auto files = modFiles.find(mod);
        if (files == modFiles.end()) continue;
        std::size_t changed{};
        // This mod's new assets, and the partitions its recorded changes import.
        // An added asset only follows a change that refers to it: copied on its
        // own, it could arrive in another mod's bundle without the resources
        // and chunks it was added with.
        std::vector<std::pair<std::string, AssetAddition>> candidates, companions;
        std::vector<fb::TocChunk> newChunks;   // TOC chunks the game's TOCs do not have
        std::set<fb::Guid> imported;
        const auto decoded = [&](std::span<const std::byte> encoded) {
            return fb::ebx::read_document(fb::decode_cas(encoded, {gameRoot}));
        };
        for (const auto& relative : files->second.tocs) {
            std::error_code error;
            const auto baseToc = baseRoot / fs::path(relative);
            if (!fs::is_regular_file(baseToc, error)) continue;
            try {
                const auto own = fb::read_toc(read_file(mod->directory / fs::path(relative)));
                const auto game = fb::read_toc(read_file(baseToc));
                std::map<std::string, const fb::TocBundle*, std::less<>> gameBundles;
                for (const auto& bundle : game.bundles) gameBundles.emplace(lower(bundle.name), &bundle);
                std::set<fb::Guid> gameChunks;
                for (const auto& chunk : game.chunks) gameChunks.insert(chunk.guid);
                for (const auto& chunk : own.chunks)
                    if (!chunk.removed && chunk.location.patch && !gameChunks.contains(chunk.guid)) newChunks.push_back(chunk);
                for (const auto& bundle : own.bundles) {
                    const auto shipped = gameBundles.find(lower(bundle.name));
                    if (shipped == gameBundles.end()) continue;
                    const auto modListing = list_bundle(store, mod->directory, baseRoot, bundle, gameRoot);
                    const auto gameListing = list_bundle(store, baseRoot, baseRoot, *shipped->second, gameRoot);
                    if (!modListing || !gameListing) continue;
                    std::map<std::string, fb::Sha1, std::less<>> original;
                    for (const auto& asset : gameListing->manifest.ebx) original.emplace(lower(asset.name), asset.sha1);
                    for (std::size_t index = 0; index < modListing->manifest.ebx.size(); ++index) {
                        const auto& asset = modListing->manifest.ebx[index];
                        const auto name = lower(asset.name);
                        const auto game_copy = original.find(name);
                        if (game_copy != original.end() && game_copy->second == asset.sha1) continue;
                        const auto at = modListing->first + index;
                        if (at >= modListing->files.size()) continue;
                        const auto& file = modListing->files[at];
                        const auto payload = [&] {
                            return store.read(file.location.patch ? mod->directory : baseRoot,
                                              file.location, file.offset, file.size);
                        };
                        if (game_copy == original.end()) {
                            candidates.push_back({lower(bundle.name), {mod->name, asset, payload(), lower(relative)}});
                            continue;
                        }
                        auto& versions = out.changed[name];
                        if (versions.contains(game_copy->second)) continue;
                        const auto& change = versions.emplace(game_copy->second,
                            AssetOverride{mod->name, asset.sha1, asset.originalSize, payload()}).first->second;
                        ++changed;
                        try {
                            for (const auto& reference : decoded(change.encoded).imports)
                                imported.insert(reference.fileGuid);
                        } catch (const std::exception&) {}
                    }
                    // Resources the mod adds, kept to go with an added EBX of the same name
                    // (a wave's sound-bank resource registers the wave with the audio system).
                    std::set<std::string, std::less<>> shippedResources;
                    for (const auto& asset : gameListing->manifest.resources) shippedResources.insert(lower(asset.name));
                    for (std::size_t index = 0; index < modListing->manifest.resources.size(); ++index) {
                        const auto& asset = modListing->manifest.resources[index];
                        const auto at = modListing->first + modListing->manifest.ebx.size() + index;
                        if (shippedResources.contains(lower(asset.name)) || at >= modListing->files.size()) continue;
                        const auto& file = modListing->files[at];
                        companions.push_back({lower(bundle.name), {mod->name, asset,
                            store.read(file.location.patch ? mod->directory : baseRoot, file.location, file.offset, file.size), lower(relative)}});
                    }
                }
            } catch (const std::exception& failure) {
                report.notes.push_back(mod->name + ": " + relative +
                    ": its changes could not be read for other mods' copies (" + failure.what() + ")");
            }
        }
        // One addition per kind and name in a bundle, the highest-priority mod's.
        const auto keep = [&](const std::string& bundle, AssetAddition addition) {
            auto& list = out.added[bundle];
            const auto name = lower(addition.asset.name);
            if (std::ranges::any_of(list, [&](const AssetAddition& other) {
                    return other.asset.kind == addition.asset.kind && lower(other.asset.name) == name; }))
                return false;
            list.push_back(std::move(addition));
            return true;
        };
        // Transitively: an addition a following addition imports follows too (a new song
        // imports its new wave, and only the song is named by the changed playlist).
        struct Candidate { fb::Guid file; std::vector<fb::Guid> imports; bool taken{}; };
        std::vector<Candidate> parsed(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            try {
                const auto document = decoded(candidates[index].second.encoded);
                parsed[index].file = document.fileGuid;
                for (const auto& reference : document.imports) parsed[index].imports.push_back(reference.fileGuid);
            } catch (const std::exception&) { parsed[index].taken = true; }   // unreadable: never follows
        }
        for (bool grew = true; grew;) {
            grew = false;
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                auto& candidate = parsed[index];
                if (candidate.taken || !imported.contains(candidate.file)) continue;
                candidate.taken = grew = true;
                imported.insert(candidate.imports.begin(), candidate.imports.end());
                auto& [bundle, addition] = candidates[index];
                keep(bundle, std::move(addition));
            }
        }
        for (auto& [bundle, companion] : companions) {
            const auto& list = out.added[bundle];
            const auto name = lower(companion.asset.name);
            if (std::ranges::any_of(list, [&](const AssetAddition& addition) {
                    return addition.mod == mod->name && addition.asset.kind == fb::AssetKind::ebx &&
                           lower(addition.asset.name) == name; }))
                keep(bundle, std::move(companion));
        }
        // The new TOC chunks this mod's carried EBX name (as raw GUID bytes, the way an
        // EBX stores a ChunkId), so they can follow those assets into other superbundles.
        if (!newChunks.empty()) {
            std::vector<std::vector<std::byte>> carried;
            for (const auto& [bundle, list] : out.added)
                for (const auto& addition : list)
                    if (addition.mod == mod->name && addition.asset.kind == fb::AssetKind::ebx) try {
                        carried.push_back(fb::decode_cas(addition.encoded, {gameRoot}));
                    } catch (const std::exception&) {}
            std::set<fb::Guid> taken;
            for (const auto& chunk : newChunks) {
                const auto& id = chunk.guid.bytes;
                if (!taken.contains(chunk.guid) && std::ranges::any_of(carried, [&](const std::vector<std::byte>& bytes) {
                        return std::search(bytes.begin(), bytes.end(), id.begin(), id.end()) != bytes.end(); })) {
                    out.chunks[mod->name].push_back(chunk);
                    taken.insert(chunk.guid);
                }
            }
            if (!taken.empty())
                report.notes.push_back(mod->name + ": " + std::to_string(taken.size()) +
                    " added chunk(s) follow its added assets into other mods' superbundles");
        }
        if (changed)
            report.notes.push_back(mod->name + ": " + std::to_string(changed) +
                " changed asset(s) also apply to the copies other mods carry");
    }
    return out;
}

} // namespace dingosdk::mods::detail
