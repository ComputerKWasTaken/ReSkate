#include "Engine/Core/Log/logging.h"
#include "Extension/Customization/local_cosmetic_catalog.h"
#include "Extension/Customization/local_customization_runtime.h"
#include "Extension/Customization/local_player_card_runtime.h"
#include "local_music_assets.h"
#include "local_music_ui.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/local_music.h"
#include "Engine/Game/World/location_travel.h"
#include "Extension/Profile/runtime_internal.h"
#include "Extension/UI/NativeMenu/native_menu_data.h"
#include <algorithm>
#include <utility>

namespace dingosdk::profile_runtime {

// Runtime-only music UI hydration: read_music_catalog copies actual registered

// MusicGraphAsset metadata/TagRefs. No generated catalog, guessed memberships,

// artwork, audio-residency gate, or preference persistence. Bounds: 1024 songs,

// 256 authored groups, 8192 membership edges. Include after assets/news/model helpers.

void music_release_weak(std::uintptr_t owner) noexcept {
    if (owner && InterlockedDecrement(reinterpret_cast<volatile LONG*>(owner + 12)) == 0) {
        const auto table = *reinterpret_cast<const std::uintptr_t**>(owner);
        reinterpret_cast<void (*)(void*)>(table[2])(reinterpret_cast<void*>(owner));
    }
}

MusicUiRuntime& music_ui_runtime() { static auto* r = new MusicUiRuntime; return *r; }

namespace {
// Format model handles for shelf publication logs.
std::string music_diag_hex(std::uint64_t value, int width) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(static_cast<std::size_t>(width), '0');
    for (int i = width - 1; i >= 0; --i, value >>= 4) out[static_cast<std::size_t>(i)] = digits[value & 15];
    return out;
}
namespace md = dingosdk::multiplayer::menu_data;
struct ModsShelf {
    std::uintptr_t manager{};
    md::Value anchor{}, tiles{}, featured_anchor{};
    ULONGLONG next_poll{};
    std::vector<std::byte> published_rows;
};
ModsShelf mods_shelf;
// Read inline typed fields without creating child handles during authored construction.
std::uintptr_t music_inline_path(const md::Context& context, std::uintptr_t data,
    std::uintptr_t type, std::initializer_list<std::uint32_t> hashes) {
    for (auto hash : hashes) {
        const auto field = context.member(type, hash);
        data += field.offset;
        type = field.type;
    }
    return data;
}
md::Ref music_tile_ref(const md::Context& context, md::Value anchor) {
    return md::read<md::Ref>(music_inline_path(context, context.address(anchor), anchor.type,
        {0x214d4984U, 0x25e4d6c8U}));
}
void music_sync_mods(const md::Context& context) {
    if (mods_shelf.manager != context.manager || !mods_shelf.tiles.handle) return;
    const auto ref = music_tile_ref(context, mods_shelf.featured_anchor);
    const auto handle = ref.handle & ~std::uint64_t{1};
    if (!handle) return;
    const md::Value featured{handle, context.type_of(handle)};
    md::require(featured.type == mods_shelf.tiles.type, "Featured tile schema differs.");
    const auto source_items = context.field(featured, 0x67223da7U);
    unsigned count{}, stride{};
    const auto source = context.array(source_items, 1024, count, stride);
    const auto row_type = md::read<std::uintptr_t>(md::read<std::uintptr_t>(source_items.type) + 0x30);
    md::require(stride == 1200 && md::read<std::uint32_t>(md::read<std::uintptr_t>(row_type)) == 0xdeb20e0fU,
        "Music tile row schema differs.");
    std::vector<std::byte> filtered;
    unsigned mods{};
    for (unsigned i = 0; i < count; ++i) {
        const auto at = reinterpret_cast<std::uintptr_t>(source.data() + std::size_t{i} * stride);
        const auto tile = md::read<md::Ref>(music_inline_path(context, at, row_type,
            {0x214d4984U, 0x0fb0d794U, 0xa704272aU, 0xc52416efU, 0x0abf7c31U}));
        const auto tile_handle = tile.handle & ~std::uint64_t{1};
        if (!tile_handle) continue;
        const md::Value tile_model{tile_handle, context.type_of(tile_handle)};
        const auto playlist_handle = md::read<std::uint64_t>(music_inline_path(context,
            context.address(tile_model), tile_model.type, {0xeb4f9b4cU})) & ~std::uint64_t{1};
        if (!playlist_handle) continue;
        const md::Value playlist{playlist_handle, context.type_of(playlist_handle)};
        const auto id = context.text(context.field(playlist, 0xa5b84b8aU), 2048);
        if (!id.starts_with("mod:")) continue;
        filtered.insert(filtered.end(), source.begin() + std::size_t{i} * stride,
            source.begin() + std::size_t{i + 1} * stride);
        ++mods;
    }
    const auto destination = context.field(mods_shelf.tiles, 0x67223da7U);
    unsigned previous{}, previous_stride{};
    context.array(destination, 1024, previous, previous_stride);
    md::require(previous_stride == stride, "Mods tile row schema differs.");
    if (previous == mods && mods_shelf.published_rows == filtered) return;
    context.array(destination, filtered, mods);
    mods_shelf.published_rows = std::move(filtered);
    dingosdk::logging::event(dingosdk::logging::Channel::music,
        dingosdk::Json{{"event", "music_mods_shelf_fill"}, {"count", mods}, {"featured", count}}.dump().c_str());
}
void music_poll_mods(std::uintptr_t base) {
    if (!mods_shelf.manager || GetTickCount64() < mods_shelf.next_poll) return;
    mods_shelf.next_poll = GetTickCount64() + 250;
    std::uintptr_t provider{}, manager{};
    if (!read(base + addr::local_music::ui_manager, provider) || !provider ||
        !read(provider + 0x80, manager) || manager != mods_shelf.manager) return;
    try {
        game::ModelWriteLock lock(mods_shelf.manager);
        music_sync_mods(md::Context{base, mods_shelf.manager});
    } catch (const std::exception& error) {
        static std::string last_error;
        if (last_error != error.what()) {
            last_error = error.what();
            dingosdk::logging::event(dingosdk::logging::Channel::music,
                dingosdk::Json{{"event", "music_mods_shelf_waiting"}, {"reason", last_error}}.dump().c_str());
        }
    }
}
// Publish the independent Mods shelf before native widgets bind.
std::string music_shelf_append_one(std::uintptr_t base, std::uintptr_t model, std::uint64_t exact_list = 0) {
    namespace md = dingosdk::multiplayer::menu_data;
    game::ModelWriteLock lock(model);
    md::Context shelf{base, model};
    std::string report = "shelf_append";
    unsigned candidates = 0;
    const auto roots = exact_list
        ? std::vector<md::Root>{{md::Value{exact_list, shelf.type_of(exact_list)}, 0}}
        : shelf.roots({0x48455d84U});
    for (const auto& root : roots) {
        try {
            const auto items = shelf.field(root.model, 0x67223da7U);
            unsigned count = 0, stride = 0;
            const auto bytes = shelf.array(items, 64, count, stride);
            if (stride != 16 || count < 1 || count > 8) continue;
            const auto first = shelf.element(items, 0);
            const auto element_schema = first.type ? md::read<std::uint32_t>(md::read<std::uintptr_t>(first.type)) : 0U;
            if (element_schema != 0x62088281U) continue;
            std::uint64_t target = 0;
            std::memcpy(&target, bytes.data() + 8, sizeof(target));
            std::uint32_t target_schema = 0;
            if (target) { try { const auto type = shelf.type_of(target & ~std::uint64_t{1}); target_schema = type ? md::read<std::uint32_t>(md::read<std::uintptr_t>(type)) : 0U; } catch (const std::exception&) {} }
            ++candidates;
            if (target_schema != 0x0e3be640U) continue;
            report += " list=0x" + music_diag_hex(root.model.handle, 16) + " count=" + std::to_string(count);
            if (count >= 4) { report += " already"; break; }
            if (count < 2) { report += " too_few"; break; }
            std::uint64_t record_ref = 0, source_vm = 0;
            std::memcpy(&record_ref, bytes.data() + stride, 8);
            std::memcpy(&source_vm, bytes.data() + stride + 8, 8);
            const auto source_type = shelf.type_of(source_vm & ~std::uint64_t{1});
            auto new_vm = shelf.create(md::Schema{0x0e3be640U, 192}, (GetTickCount64() << 12) | 0x4dU);
            md::Value new_tiles{};
            try {
                const md::Value source_anchor{source_vm, source_type};
                shelf.copy(new_vm, shelf.address(source_anchor));
                const auto tile_ref = music_tile_ref(shelf, source_anchor);
                auto tile_handle = tile_ref.handle & ~std::uint64_t{1};
                const auto tile_type = shelf.type(md::Schema{0x74b1ea1dU, 392});
                std::uintptr_t tile_data{};
                if (tile_handle && shelf.type_of(tile_handle) == tile_type)
                    tile_data = shelf.address(md::Value{tile_handle, tile_type});
                else if (tile_ref.record && md::read<std::uintptr_t>(tile_ref.record + 0x18) == tile_type)
                    tile_data = md::read<std::uintptr_t>(tile_ref.record + 0x20);
                md::require(tile_data != 0, "Music tile template is not available.");
                new_tiles = shelf.create(md::Schema{0x74b1ea1dU, 392}, (GetTickCount64() << 12) | 0x4eU);
                shelf.copy(new_tiles, tile_data);
                shelf.text(shelf.path(new_tiles, {0xfaec2d05U, 0xcb79482dU, 0x4d8e01b9U}), "Mods");
                // An independent list retains each native row's actions and artwork on publication.
                shelf.array(shelf.field(new_tiles, 0x67223da7U), std::span<const std::byte>{}, 0);
                shelf.set(shelf.path(new_vm, {0x214d4984U, 0x25e4d6c8U}), md::Ref{0, new_tiles.handle});
            } catch (...) {
                if (new_tiles.handle) shelf.destroy(new_tiles);
                shelf.destroy(new_vm);
                throw;
            }
            std::vector<std::byte> element(stride);
            std::memcpy(element.data(), &record_ref, 8);
            std::memcpy(element.data() + 8, &new_vm.handle, 8);
            auto merged = bytes;
            merged.insert(merged.begin(), element.begin(), element.end());
            shelf.array(items, merged, count + 1);
            const auto previous = mods_shelf;
            mods_shelf = {model, new_vm, new_tiles, md::Value{target, shelf.type_of(target)}, 0};
            // Release our previous generation after the new shelf has been published.
            if (previous.manager == model) {
                try { shelf.destroy(previous.anchor); shelf.destroy(previous.tiles); } catch (const std::exception&) {}
            }
            try { music_sync_mods(shelf); } catch (const std::exception&) {}
            unsigned after = 0, after_stride = 0;
            shelf.array(items, 64, after, after_stride);
            report += " new=0x" + music_diag_hex(new_vm.handle, 16) + " appended=" + std::to_string(count + 1) +
                      " after=" + std::to_string(after);
            break;
        } catch (const std::exception& error) { report += " error=" + std::string(error.what()); continue; }
    }
    report += " candidates=" + std::to_string(candidates);
    return report;
}
}

// CONFIRMED: the authored loader calls the shared constructor at RVA 0x1912670.
// The return path exposes the shelf list before its native widgets bind.
MusicModelConstruct music_model_construct_original{};
std::uint64_t music_model_construct_hook(std::uintptr_t manager, std::uint8_t mode,
    std::uint64_t id, std::uintptr_t type, bool flag, std::uintptr_t record) {
    const auto handle = music_model_construct_original(manager, mode, id, type, flag, record);
    PreserveError preserve;
    try {
        if (!handle || !type || md::read<std::uint32_t>(md::read<std::uintptr_t>(type)) != 0x48455d84U)
            return handle;
        game::ModelWriteLock lock(manager);
        md::Context context{local_runtime().base, manager};
        const auto items = context.field(md::Value{handle, type}, 0x67223da7U);
        unsigned count{}, stride{};
        const auto bytes = context.array(items, 1024, count, stride);
        if (count != 3 || stride != 16) return handle;
        constexpr const char* records[] = {
            "MusicPlaylistManager_Base_ContentResources/Featured_AnchoredContent_VM",
            "MusicPlaylistManager_Base_ContentResources/Liked_AnchoredContent_VM",
            "MusicPlaylistManager_Base_ContentResources/NewlyDiscovered_AnchoredContent_VM"};
        for (unsigned item = 0; item < 3; ++item) {
            std::uintptr_t item_record{};
            std::memcpy(&item_record, bytes.data() + item * stride, sizeof(item_record));
            // Named record header +0x28 is also used by the native UI dumper.
            if (!item_record || md::string(md::read<std::uintptr_t>(item_record + 0x28), 256) != records[item])
                return handle;
        }
        const auto report = music_shelf_append_one(local_runtime().base, manager, handle);
        dingosdk::logging::event(dingosdk::logging::Channel::music,
            dingosdk::Json{{"event", "music_shelf_append"}, {"report", report}}.dump().c_str());
    } catch (...) {} // A shelf failure must not interrupt the native constructor.
    return handle;
}

bool initialize_music_functions(std::uintptr_t base) {
    namespace music = addr::local_music;
    for (const auto& fp : music::music_ui_contracts) {
        std::array<unsigned char, 32> actual{};
        if (!read(base + fp.rva, actual) || actual != fp.bytes) return false;
    }
    std::array<std::uintptr_t, 4> playlist{}, song{};
    const auto rebased = [base](const std::array<std::uintptr_t, 4>& slots) {
        return std::array<std::uintptr_t, 4>{base + slots[0], base + slots[1], base + slots[2], base + slots[3]};
    };
    if (!read(base + music::playlist_control_vtable, playlist) || !read(base + music::song_control_vtable, song) ||
        playlist != rebased(music::playlist_control_slots) ||
        song != rebased(music::song_control_slots)) return false;
    auto& f = music_ui_runtime().functions;
    f.construct_playlist = reinterpret_cast<decltype(f.construct_playlist)>(base + music::construct_playlist);
    f.construct_song = reinterpret_cast<decltype(f.construct_song)>(base + music::construct_song);
    f.playlists = reinterpret_cast<decltype(f.playlists)>(base + music::publish_playlists);
    f.songs = reinterpret_cast<decltype(f.songs)>(base + music::publish_songs);
    f.insert = reinterpret_cast<decltype(f.insert)>(base + music::context_insert);
    f.complete = reinterpret_cast<decltype(f.complete)>(base + music::complete_delegate);
    f.allocator = {base + addr::engine::allocator_adapter_vtable, 0, 8};
    return true; // Root installs only the initialize hook after these contracts pass.
}

bool music_ui_identity(const MusicUiPending& pending) {
    std::uintptr_t manager{}, model{}, owner{}, vtable{}; std::int32_t strong{};
    const auto base = local_runtime().base;
    return pending.manager && pending.model && pending.owner &&
        read(base + addr::local_music::ui_manager, manager) && manager == pending.manager &&
        read(manager, vtable) && vtable == base + addr::local_music::ui_manager_vtable &&
        read(manager + 0x80, model) && model == pending.model &&
        read(manager + 0x58, owner) && owner == pending.owner &&
        read(owner + 8, strong) && strong > 0 && strong < 0x1000000;
}

bool music_ui_current(const MusicUiPending& pending) {
    return pending.generation == music_ui_runtime().generation && music_ui_identity(pending);
}

CosmeticShared music_ui_lease(const MusicUiPending& pending) {
    if (!music_ui_identity(pending)) return {};
    auto* counter = reinterpret_cast<volatile LONG*>(pending.owner + 8);
    LONG observed = InterlockedCompareExchange(counter, 0, 0);
    for (unsigned i = 0; i < 32 && observed > 0 && observed < 0x1000000; ++i) {
        const auto previous = InterlockedCompareExchange(counter, observed + 1, observed);
        if (previous == observed) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(pending.owner + 12));
            return {reinterpret_cast<void*>(pending.manager), reinterpret_cast<void*>(pending.owner)};
        }
        observed = previous;
    }
    return {};
}

bool music_ui_text(std::string_view value, std::size_t limit ) {
    return !value.empty() && value.size() <= limit &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool music_ui_catalog_valid(const MusicCatalog& catalog, const std::string& favorites) {
    if (catalog.songs.empty() || catalog.songs.size() > 1024 || catalog.playlists.empty() || catalog.playlists.size() > 256) return false;
    std::set<std::string> songs, playlists;
    std::set<std::pair<std::string, std::string>> declared, grouped;
    for (const auto& song : catalog.songs) {
        if (!music_ui_text(song.id) || !music_ui_text(song.artist, 512) || !music_ui_text(song.title, 512) ||
            song.id != song.artist + " - " + song.title || !songs.insert(song.id).second) return false;
        for (const auto& playlist : song.playlists)
            if (!music_ui_text(playlist) || !declared.emplace(playlist, song.id).second || declared.size() > 8192) return false;
    }
    for (const auto& playlist : catalog.playlists) {
        if (!music_ui_text(playlist.id) || playlist.id == favorites || !playlists.insert(playlist.id).second) return false;
        for (const auto& id : playlist.songs)
            if (!songs.contains(id) || !grouped.emplace(playlist.id, id).second || grouped.size() > 8192) return false;
    }
    return declared == grouped;
}

std::string music_ui_wire(std::string_view id, std::string_view artist, std::string_view title,
    const std::vector<std::string>* members, std::string_view name, std::string_view artwork) {
    std::string body, presentation, framed;
    cosmetic_wire_string(body, 1, id);
    if (members) {
        for (const auto& song : *members) cosmetic_wire_string(body, 2, song);
        // A local shelf choice for real authored groups, NOT recovered AMP classification.
        cosmetic_wire_number(body, 3, 1);
        // The catalogue's display name when known; the raw id otherwise.
        cosmetic_wire_string(presentation, 10, name.empty() ? id : name);
        // Artwork: the catalogue's cdn:/ id, resolved to the CDN rendition.
        if (const auto url = travel_artwork_url(artwork); !url.empty())
            cosmetic_wire_string(presentation, 11, url);
        cosmetic_wire_number(presentation, 12, 0);
    } else {
        cosmetic_wire_string(presentation, 10, artist);
        cosmetic_wire_string(presentation, 11, title);
        // Cover art: the content cache song record's cdn:/ id (its field 10.12), resolved like a playlist's.
        if (const auto url = travel_artwork_url(artwork); !url.empty())
            cosmetic_wire_string(presentation, 12, url);
    }
    cosmetic_wire_string(body, 10, presentation);
    cosmetic_varint(framed, body.size()); framed += body;
    return framed;
}

CosmeticShared music_ui_message(const std::string& wire, bool playlist) {
    auto& f = music_ui_runtime().functions; auto& native = cosmetic_catalog_functions();
    const auto base = local_runtime().base;
    const std::size_t size = playlist ? 0xf0 : 0xc0;
    const std::uintptr_t control = base +
        (playlist ? addr::local_music::playlist_control_vtable : addr::local_music::song_control_vtable);
    std::uintptr_t allocator{};
    auto* block = static_cast<std::byte*>(native.allocate(&allocator, size, 0));
    if (!block) throw std::bad_alloc();
    std::memset(block, 0, size); std::memcpy(block, &control, 8);
    const std::uint32_t one = 1;
    std::memcpy(block + 8, &one, 4); std::memcpy(block + 12, &one, 4);
    std::memcpy(block + size - 8, &allocator, 8);
    const auto adapter = reinterpret_cast<std::uintptr_t>(f.allocator.data());
    const std::array<std::uintptr_t, 3> context{adapter, adapter, 0};
    (playlist ? f.construct_playlist : f.construct_song)(block + 16, context.data());
    CosmeticSharedGuard result{{block + 16, block}};
    std::array<std::uintptr_t, 4> reader{reinterpret_cast<std::uintptr_t>(wire.data()), wire.size(), 0, 0};
    if (!native.decode(reader.data(), result.value.body) || reader[2] != wire.size())
        throw std::runtime_error("Native music DTO decode failed");
    const auto value = result.value; result.value = {}; return value;
}

bool music_ui_has_context(std::uintptr_t map, const std::string& id) {
    std::uintptr_t buckets{}, node{}, sentinel{}; std::uint32_t count{}, capacity{};
    if (!read(map, buckets) || !buckets || !read(map + 8, capacity) || !capacity || capacity > 4096 ||
        !read(map + 12, count) || count > 2048 || !read(buckets + capacity * 8ULL, sentinel) ||
        !read(buckets + (cosmetic_hash(id) % capacity) * 8ULL, node)) return false;
    std::set<std::uintptr_t> seen;
    while (node && node != sentinel) {
        std::array<std::uintptr_t, 3> row{}; std::string name;
        if (seen.size() >= count || !seen.insert(node).second || !read(node, row) || !cosmetic_text(row[0], name)) return false;
        if (name == id) return row[1] != 0;
        node = row[2];
    }
    return false;
}

void music_ui_initialize_hook(std::uint64_t all, std::uint64_t hidden, std::uint64_t featured,
    std::uint64_t liked, std::uint64_t discovered, std::uint64_t favorites, std::uint64_t songs,
    const std::uint32_t* handle, const void* callback) {
    auto& s = local_runtime(); auto& ui = music_ui_runtime();
    {
        PreserveError preserve; std::lock_guard lock(s.native_mutex);
        // Even a forwarded initialization supersedes old local contexts.
        const auto generation = ++ui.generation;
        ui.pending.reset();
        if (s.active.load(std::memory_order_acquire))
        try {
            const auto thread = GetCurrentThreadId();
            auto pending = std::make_unique<MusicUiPending>();
            pending->contexts = {all, hidden, featured, liked, discovered, favorites, songs};
            std::uintptr_t owner{}, vtable{}; std::int32_t strong{};
            if ((!cosmetic_runtime().update_thread || cosmetic_runtime().update_thread == thread) && callback &&
                std::all_of(pending->contexts.begin(), pending->contexts.end(), [](auto c) { return c != 0; }) &&
                read(reinterpret_cast<std::uintptr_t>(handle), pending->favorites) && pending->favorites &&
                read(s.base + addr::local_music::ui_manager, pending->manager) && pending->manager &&
                read(pending->manager, vtable) && vtable == s.base + addr::local_music::ui_manager_vtable &&
                read(pending->manager + 0x80, pending->model) && pending->model &&
                read(pending->manager + 0x58, owner) && owner && read(owner + 8, strong) && strong > 0 && strong < 0x1000000) {
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(owner + 12)); pending->owner = owner;
                if (music_ui_identity(*pending)) {
                    game::native_data().values.copy_delegate(&pending->delegate, callback);
                    if (pending->delegate) {
                        if (generation != ui.generation) return;
                        pending->thread = thread; pending->generation = generation;
                        ui.pending = std::move(pending);
                        dingosdk::logging::event(dingosdk::logging::Channel::music, "{\"event\":\"native_music_ui_queued\"}"); return;
                    }
                }
            }
        } catch (...) { dingosdk::logging::event(dingosdk::logging::Channel::music, "{\"event\":\"native_music_ui_queue_failed\"}"); }
    }
    ui.functions.initialize(all, hidden, featured, liked, discovered, favorites, songs, handle, callback);
}

void update_music_catalog() {
    auto& s = local_runtime(); auto& ui = music_ui_runtime();
    if (!s.active.load(std::memory_order_acquire) || ui.updating || cosmetic_runtime().update_thread != GetCurrentThreadId()) return;
    PreserveError preserve; std::lock_guard lock(s.native_mutex);
    music_poll_mods(s.base);
    if (!ui.pending) return;
    if (ui.pending->thread != GetCurrentThreadId() || !music_ui_current(*ui.pending)) { ui.pending.reset(); return; }
    const auto now = GetTickCount64(); if (now < ui.pending->next_poll) return;
    ui.pending->next_poll = now + 250;
    auto pending = std::move(ui.pending);
    ui.updating = true;
    bool retry = true;
    struct PendingScope {
        MusicUiRuntime& ui; std::unique_ptr<MusicUiPending>& pending; bool& retry;
        ~PendingScope() {
            if (retry && !ui.pending && pending && pending->generation == ui.generation)
                ui.pending = std::move(pending);
            ui.updating = false;
        }
    } pending_scope{ui, pending, retry};
    try {
        CosmeticSharedGuard lease{music_ui_lease(*pending)};
        if (!lease.value.control) { retry = false; return; }
        MusicCatalog catalog;
        if (!read_music_catalog(catalog) || catalog.playlists.empty()) {
            if (!pending->waiting_logged) { dingosdk::logging::event(dingosdk::logging::Channel::music, "{\"event\":\"native_music_ui_waiting_assets\"}"); pending->waiting_logged = true; }
            return;
        }
        if (!music_ui_current(*pending)) { retry = false; return; }
        std::string favorite_id;
        if (!identifier(reinterpret_cast<const void*>(pending->manager + 8), favorite_id) ||
            !music_ui_catalog_valid(catalog, favorite_id)) throw std::runtime_error("Invalid runtime music catalog");
        std::uintptr_t arena{}, vtable{};
        if (!read(s.base + addr::engine::default_arena, arena) || !arena || !read(arena, vtable) || vtable < s.base || vtable >= s.base + supported_build::game_image_size) return;
        ui.functions.allocator[1] = arena;
        MusicUiMessages playlists, songs;
        playlists.items.reserve(catalog.playlists.size()); songs.items.reserve(catalog.songs.size());
        for (const auto& playlist : catalog.playlists) playlists.items.push_back(music_ui_message(music_ui_wire(playlist.id, {}, {}, &playlist.songs, playlist.name, playlist.artwork), true));
        for (const auto& song : catalog.songs) songs.items.push_back(music_ui_message(music_ui_wire(song.id, song.artist, song.title, nullptr, {}, song.artwork), false));
        auto& model = game::native_data().models; auto& field = game::native_data().models.field;
        if (!music_ui_current(*pending)) { retry = false; return; }
        {
            game::ModelWriteLock model_lock(pending->model);
            for (auto context : pending->contexts)
                if (!model.value(pending->model, context, 0, 0)) throw std::runtime_error("Invalid music model context");
        }
        if (!music_ui_current(*pending)) { retry = false; return; }
        const auto favorite = pending->contexts[5], manager = pending->manager, native_model = pending->model;
        const auto song_field = field(native_model, favorite, 2, 0xffffffffU, false);
        const auto id_field = field(native_model, favorite, 0, 0xffffffffU, false);
        const auto title_field = field(native_model, favorite, 3, 0xffffffffU, false);
        if (!song_field || !id_field || !title_field) throw std::runtime_error("Invalid native Favorites fields");
        if (!music_ui_current(*pending)) { retry = false; return; }
        retry = false; // Once native publication starts, never replay a partial generation.
        std::memcpy(reinterpret_cast<void*>(manager + 0xf8), pending->contexts.data(), sizeof(pending->contexts));
        std::memcpy(reinterpret_cast<void*>(manager + 0x1f8), &pending->favorites, 4);
        std::memcpy(reinterpret_cast<void*>(manager + 0x130), &song_field, 8);
        std::array<std::uintptr_t, 3> inserted{};
        ui.functions.insert(manager + 0xa8, inserted.data(), reinterpret_cast<const void*>(manager + 8), &favorite);
        model.publish(native_model, id_field, s.base + addr::engine::string_type, reinterpret_cast<const void*>(manager + 8));
        if (!music_ui_current(*pending)) return;
        model.publish(native_model, title_field, s.base + addr::engine::string_type, reinterpret_cast<const void*>(manager + 8));
        if (!music_ui_current(*pending)) return;
        playlists.publish(manager, ui.functions.playlists);
        if (!music_ui_current(*pending)) return;
        songs.publish(manager, ui.functions.songs);
        if (!music_ui_current(*pending)) return;
        std::uint32_t playlist_count{}, song_count{};
        if (!read(manager + 0x1b8, playlist_count) || playlist_count != catalog.playlists.size() ||
            !read(manager + 0x1e8, song_count) || song_count != catalog.songs.size() ||
            !music_ui_has_context(manager + 0xa8, favorite_id)) throw std::runtime_error("Native music publication count mismatch");
        for (const auto& playlist : catalog.playlists)
            if (!music_ui_has_context(manager + 0xa8, playlist.id)) throw std::runtime_error("Native playlist identity missing");
        for (const auto& song : catalog.songs)
            if (!music_ui_has_context(manager + 0xd0, song.id)) throw std::runtime_error("Native song identity missing");
        if (!music_ui_current(*pending)) return;
        ui.functions.complete(&pending->delegate);
        dingosdk::logging::event(dingosdk::logging::Channel::music, dingosdk::Json{{"event", "native_music_ui_complete"}, {"playlists", playlist_count}, {"songs", song_count}, {"source", "runtime_music_assets"}}.dump().c_str());
    } catch (...) { retry = false; dingosdk::logging::event(dingosdk::logging::Channel::music, "{\"event\":\"native_music_ui_publish_failed\"}"); }
}
}
