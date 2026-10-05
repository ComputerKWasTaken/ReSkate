#pragma once
#include "developer_hoodie_material.h"
#include <cstddef>

namespace dingosdk {
namespace developer_board_detail {
using developer_hoodie_detail::Color;
using developer_hoodie_detail::material_hash;
using developer_hoodie_detail::Animation;
using developer_hoodie_detail::animation_for;
using developer_hoodie_detail::item_color;
using developer_hoodie_detail::settle_publishes;
inline constexpr std::uint64_t graphic_color = 0x873f48af36e50d59ULL;
inline constexpr std::uint64_t base_color = 0x8c46cd354b18a9e2ULL;
inline constexpr Color shader_default{.5f, .5f, .5f};
inline constexpr std::array<std::string_view, 3> slots{
    "Characters/Customization/Board/Slots/Skateboard_Deck_Slot",
    "Characters/Customization/Board/Slots/Skateboard_Trucks_Slot",
    "Characters/Customization/Board/Slots/Skateboard_Wheels_Slot"};
struct Target {
    std::string_view asset, material;
    std::uint32_t recipe_slot;
    std::size_t controller;
    std::uint64_t key;
};
// The board loader registers Deck_Mat / DeckTop_Mat for the compiled sections
// Skateboard_base_mat / mat_skateboard_top. Use the live named map identities.
// Recipe identities, assigned board controllers and serialized shader keys from
// the four pink cosmetics. The deck's wood follows its Solid Pink graphic.
inline constexpr std::array<Target, 5> targets{{
    {"Own_DeckGraphic_Gen_Popsicle_00021", "Deck_Mat", 0x66071628, 0, graphic_color},
    {"Own_DeckGraphic_Gen_Popsicle_00021", "Deck_Mat", 0x66071628, 0, base_color},
    {"Own_DeckGripColor_Gen_Popsicle_00006", "DeckTop_Mat", 0x755d5ee0, 0, graphic_color},
    {"Own_Truck_Gen_Default_00002", "Truck_Mat", 0x0add98be, 1, base_color},
    {"Own_WheelColor_Gen_Classic_00005", "Wheel_mat", 0xc83bb94e, 2, base_color},
}};
// Native ParamDbKey and inline NativeValue layout used by addEsVector's setter.
// The key's first word is the final two serialized GUID bytes. Its type index
// comes from the live ColorRgb descriptor, not a serialized Vec3 or boxed value.
struct ParameterKey {
    std::uint16_t identity{}, size{}, type{}, count{};
    std::uint64_t hash{};
};
struct NativeColor {
    Color color{};
    std::uint32_t padding{};
    std::uintptr_t type{};
    std::uint32_t flags{3}, reserved{};
};
static_assert(sizeof(ParameterKey) == 16 && offsetof(ParameterKey, hash) == 8);
static_assert(sizeof(NativeColor) == 32 && offsetof(NativeColor, type) == 16 && offsetof(NativeColor, flags) == 24);
inline ParameterKey parameter_key(std::uint64_t hash, std::uint16_t type) noexcept {
    return {static_cast<std::uint16_t>(hash == graphic_color ? 0xd442 : 0x8c54), sizeof(Color), type, 1, hash};
}
struct Binding {
    std::uintptr_t item{}, material{}, node{}, type{};
    ParameterKey key{};
    Color color{};
    bool eligible{};
};
struct SavedColor {
    Binding binding;
    Color original{}, last{};
    std::uint8_t settling{}; // publishes still owed after going back to `original`
};
inline constexpr std::size_t max_bindings = targets.size() * 4;
struct Materials {
    std::uintptr_t component{}, appearance{};
    std::array<Binding, max_bindings> bindings{};
    std::size_t count{};
    bool pending{};
};
template<class Memory>
std::uintptr_t parameter(Memory &m, std::uintptr_t material, std::uint64_t key) {
    const auto buckets = m.ptr(material, 0x268);
    const auto size = m.template get<std::uint32_t>(material, 0x270);
    const auto total = m.template get<std::uint32_t>(material, 0x274);
    // The native setter also uses these buckets when inserting a missing color.
    m.check(buckets && size && size <= 1024 && total <= 512, "Board parameter map exceeds bounds.");
    const auto sentinel = m.ptr(buckets, size * 8ULL);
    auto node = m.ptr(buckets, (key % size) * 8ULL);
    std::uint32_t visited{};
    for (; node && node != sentinel && visited < total; ++visited, node = m.ptr(node, 0x40))
        if (m.template get<std::uint64_t>(node, 8) == key) return node;
    m.check(!node || node == sentinel, "Board parameter chain exceeds bounds.");
    return 0;
}
template<class Read> Materials materials(Read &read, std::uintptr_t base, std::uintptr_t entity) {
    using namespace multiplayer;
    CosmeticMemory<Read &> m{read, base};
    Materials out;
    m.check(m.ptr(entity) == base + addr::engine::board_entity_vtable, "RGB board actor type differs.");
    out.component = m.component(entity);
    out.pending = m.template get<std::uint8_t>(out.component, 0x10a) || m.template get<std::uint8_t>(out.component, 0x10d);
    const auto recipe = m.ptr(out.component, 0x158);
    const auto count = m.count(recipe, sizeof(NativeCosmeticItem), max_cosmetic_slots);
    std::array<bool, targets.size()> eligible{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto item = m.template get<NativeCosmeticItem>(recipe, i * sizeof(NativeCosmeticItem));
        for (std::size_t t = 0; t < targets.size(); ++t) {
            const auto &target = targets[t];
            if (item.slot != target.recipe_slot || item.hash != cosmetic_asset_hash(target.asset)) continue;
            const auto name = item.asset ? m.text(reinterpret_cast<std::uintptr_t>(item.asset)) : std::string{};
            eligible[t] = name.empty() || name == target.asset;
        }
    }
    const auto type = base + addr::native_cosmetics::color_rgb_type;
    const auto info = m.ptr(type);
    m.check(info >= base && info < base + 0x09144000 && m.template get<std::uint32_t>(info) == 0x885eff59 &&
                m.template get<std::uint16_t>(info, 6) == sizeof(Color), "Board ColorRgb descriptor differs.");
    const auto type_index = m.template get<std::uint16_t>(type, 8);
    out.appearance = read_native_component(read, entity, base + addr::engine::skater_appearance_vtable);
    const auto begin = m.ptr(out.appearance, 0x90), end = m.ptr(out.appearance, 0x98), capacity = m.ptr(out.appearance, 0xa0);
    m.check(begin <= end && end <= capacity && (end - begin) % 8 == 0 && (capacity - begin) % 8 == 0 &&
                (capacity - begin) / 8 <= 64 && (begin || !capacity), "Board controller array exceeds bounds.");
    std::array<bool, slots.size()> seen{};
    for (auto entry = begin; entry < end; entry += 8) {
        const auto item = m.ptr(entry);
        if (!item) continue;
        m.check(m.ptr(item, 0x48) == out.appearance + 0x40, "Board controller ownership differs.");
        const auto name = m.text(m.ptr(item, 0x30));
        const auto assigned = std::find(slots.begin(), slots.end(), name);
        if (assigned == slots.end()) continue;
        const auto slot = static_cast<std::size_t>(assigned - slots.begin());
        m.check(!seen[slot], "Board has duplicate slot controllers.");
        seen[slot] = true;
        if (!(m.template get<std::uint32_t>(item, 0xb0) & 2)) { out.pending = true; continue; }
        const auto buckets = m.ptr(item, 0x1f0);
        const auto size = m.template get<std::uint32_t>(item, 0x1f8), total = m.template get<std::uint32_t>(item, 0x1fc);
        if (!total) { out.pending = true; continue; }
        m.check(buckets && size && size <= 256 && total <= 128, "Board material map exceeds bounds.");
        const auto sentinel = m.ptr(buckets, size * 8ULL);
        for (std::size_t t = 0; t < targets.size(); ++t) {
            const auto &target = targets[t];
            if (target.controller != slot) continue;
            const auto hash = material_hash(target.material);
            auto node = m.ptr(buckets, (hash % size) * 8ULL);
            std::uint32_t visited{};
            for (; node && node != sentinel && visited < total; ++visited, node = m.ptr(node, 0x28))
                if (m.template get<std::uint32_t>(node) == hash) break;
            m.check(!node || node == sentinel || visited < total, "Board material chain exceeds bounds.");
            if (!node || node == sentinel) continue;
            std::array<std::uintptr_t, 4> variants{};
            for (std::size_t v = 0; v < variants.size(); ++v) {
                const auto material = m.ptr(node, 8 + v * 8);
                if (!material || std::find(variants.begin(), variants.begin() + v, material) != variants.begin() + v) continue;
                variants[v] = material;
                const auto p = parameter(m, material, target.key);
                const auto key = parameter_key(target.key, type_index);
                Color color = shader_default;
                if (p) {
                    const auto native_key = m.template get<ParameterKey>(p);
                    m.check(native_key.identity == key.identity && native_key.size == key.size && native_key.type == key.type &&
                                native_key.count == key.count && m.ptr(p, 0x20) == type &&
                                (m.template get<std::uint32_t>(p, 0x28) & 1), "Board color parameter type differs.");
                    color = m.template get<Color>(p, 0x10);
                    m.check(std::all_of(color.begin(), color.end(), [](float c) { return std::isfinite(c); }), "Board color is invalid.");
                }
                m.check(out.count < out.bindings.size(), "Board colors exceed bounds.");
                out.bindings[out.count++] = {item, material, p, type, key, color, eligible[t]};
            }
        }
    }
    return out;
}
inline bool same_binding(const Binding &live, const Binding &saved) noexcept {
    // A successful native insertion acquires its node on the next fresh walk.
    return live.item == saved.item && live.material == saved.material && live.type == saved.type && live.key.hash == saved.key.hash &&
           (saved.node ? live.node == saved.node : live.node != 0);
}
} // namespace developer_board_detail
struct DeveloperBoardState {
    std::uintptr_t entity{}, component{}, appearance{};
    std::uint64_t generation{};
    std::array<developer_board_detail::SavedColor, developer_board_detail::max_bindings> colors{};
    std::size_t count{};
};
namespace developer_board_detail {
// Re-discover ownership before every write. Changing the recipe restores only
// colors that still equal our last write; native preset updates take precedence.
// Missing keys inherit .5 from these compiled board shaders. Keep inserted
// overrides at native preset priority and restore that default on deactivation.
// Never clear a NativeValue: the renderer still copies the key's full payload.
template<class Write, class Publish>
void animate(DeveloperBoardState &state, const Materials &live, std::uintptr_t entity, std::uint64_t generation,
             Animation animation, std::uint64_t milliseconds, Write &write, Publish &publish) {
    if (state.entity != entity || state.generation != generation || state.component != live.component || state.appearance != live.appearance)
        state = {};
    if (live.pending) return;
    DeveloperBoardState next;
    next.entity = entity; next.generation = generation; next.component = live.component; next.appearance = live.appearance;
    const auto color = item_color(animation, milliseconds);
    std::array<std::uintptr_t, max_bindings> changed_items{};
    std::size_t changed{};
    for (std::size_t i = 0; i < live.count; ++i) {
        const auto &binding = live.bindings[i];
        const SavedColor *saved{};
        for (std::size_t j = 0; j < state.count; ++j)
            if (same_binding(binding, state.colors[j].binding)) { saved = &state.colors[j]; break; }
        bool wrote{};
        if (animation != Animation::none && binding.eligible) {
            const auto original = saved && binding.color == saved->last ? saved->original : binding.color;
            if ((binding.node && binding.color == color) || (wrote = write(binding, color)))
                next.colors[next.count++] = {binding, original, color};
            else if (saved) next.colors[next.count++] = *saved;
        } else if (saved && (binding.color == saved->last || binding.color == saved->original)) {
            // Back to the part's own color, which takes a few publishes to show (settle_publishes):
            // `wrote` has the part published for each of them.
            wrote = binding.color == saved->original || write(binding, saved->original);
            const std::uint8_t left = saved->settling ? saved->settling - 1 : settle_publishes;
            if (!wrote) next.colors[next.count++] = *saved;
            else if (left) next.colors[next.count++] = {binding, saved->original, saved->original, left};
        }
        if (wrote && std::find(changed_items.begin(), changed_items.begin() + changed, binding.item) == changed_items.begin() + changed)
            changed_items[changed++] = binding.item;
    }
    state = next;
    for (std::size_t i = 0; i < changed; ++i) publish(changed_items[i]);
}
} // namespace developer_board_detail
} // namespace dingosdk
