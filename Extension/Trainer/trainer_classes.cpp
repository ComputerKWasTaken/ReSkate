#include "trainer_classes.h"
#include "Engine/Core/Log/logging.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <string_view>
#include <vector>

namespace dingosdk::trainer {
namespace {
constexpr std::size_t max_copies = 12; // a class found more often than this is not told apart from chance
// A copy: where it starts, and how it is laid out. The class's own layout has every field at
// its natural alignment (curve references between the numbers); the other holds only the
// numbers, one eight-byte slot each in the same order.
struct Copy {
    std::uintptr_t base{};
    bool slots{};
    bool operator==(const Copy &) const = default;
};
using Copies = std::array<std::vector<Copy>, class_count>;
std::uintptr_t field_offset(const ClassSpec &spec, std::size_t i, bool slots) noexcept {
    return slots ? i * 8 : class_fields[spec.first + i].offset;
}
// The game's native code keeps its own packed copy of some of these numbers, filled when a
// skater spawns, and reads that instead of the class: found the same way and written together
// with it. Seen in memory beside the push class (four-byte numbers in a row):
//   0.25 0.85 2.356 1.5 0.7 [light] [medium] [strong] [long hold] [short hold] [medium override] [horse push] 0.9 0 6.25
struct MirrorSlot {
    float value;       // what a fresh copy holds here
    const char *field; // the class field this slot follows ("push.MaxPushSpeedLight"), or null
    bool checked;      // part of the pattern a copy is recognised by
};
struct Mirror {
    std::size_t anchor; // the slot searched for
    std::array<MirrorSlot, 15> slots;
};
constexpr Mirror mirrors[]{
    {10, {{{0.25f, nullptr, true}, {0.85f, nullptr, true}, {0, nullptr, false}, {1.5f, nullptr, true}, {0.7f, nullptr, true},
           {4.0f, "push.MaxPushSpeedLight", true}, {8.5f, "push.MaxPushSpeedMedium", true}, {9.5f, "push.MaxPushSpeedStrong", true},
           {0.45f, "push.LongInputHoldTime_S", true}, {0.15f, "push.ShortInputHoldTime_S", true},
           {7.2f, "push.MinMediumPushOverrideSpeed", true}, {12.0f, "push.ForwardToPushEngageHorsePushSpeed", true},
           {0.9f, nullptr, true}, {0, nullptr, false}, {6.25f, nullptr, true}}}},
};
constexpr std::size_t mirror_count = sizeof(mirrors) / sizeof(mirrors[0]);
using MirrorCopies = std::array<std::vector<std::uintptr_t>, mirror_count>;
// The class field a mirror slot follows, as an index into class_fields (-1: none).
int mirror_field(const MirrorSlot &slot) noexcept {
    if (!slot.field) return -1;
    const std::string_view wanted(slot.field);
    for (std::size_t c = 0; c < class_count; ++c) {
        const std::string_view key(class_specs[c].key);
        if (wanted.size() <= key.size() || !wanted.starts_with(key) || wanted[key.size()] != '.') continue;
        for (std::size_t i = 0; i < class_specs[c].count; ++i)
            if (wanted.substr(key.size() + 1) == class_fields[class_specs[c].first + i].name) return static_cast<int>(class_specs[c].first + i);
    }
    return -1;
}
struct Found {
    std::mutex mutex;
    Copies copies;
    MirrorCopies mirror_copies;
    std::array<std::array<int, 15>, mirror_count> mirror_fields{};
    std::array<float, class_field_count> wanted{};               // what each field should hold
    std::array<float, class_field_count> written{};              // what the copies hold (the defaults until written)
    bool ready{};
    std::atomic<bool> searching{};
    std::atomic<std::uint64_t> searches{};
    bool dirty{};
};
Found &found() {
    static auto *value = [] {
        auto *f = new Found;
        for (std::size_t i = 0; i < class_field_count; ++i) f->wanted[i] = f->written[i] = class_fields[i].stock;
        for (std::size_t m = 0; m < mirror_count; ++m)
            for (std::size_t i = 0; i < mirrors[m].slots.size(); ++i) f->mirror_fields[m][i] = mirror_field(mirrors[m].slots[i]);
        return f;
    }();
    return *value;
}
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
// Does the memory at `base` hold class `spec` with every field at its default (or, for a copy
// the trainer has written, at what it wrote)? `low`/`high` bound the readable region.
bool matches(const ClassSpec &spec, Copy copy, std::uintptr_t low, std::uintptr_t high, const float *expected) noexcept {
    for (std::size_t i = 0; i < spec.count; ++i) {
        const auto address = copy.base + field_offset(spec, i, copy.slots);
        if (address < low || address + 4 > high) return false;
        const float want = expected[spec.first + i];
        if (want == 0) continue; // a field with no default of its own: whatever the game put there
        if (*reinterpret_cast<const std::uint32_t *>(address) != bits(want)) return false;
    }
    return true;
}
// Does the memory at `base` hold mirror `m`, its followed slots at `expected` (indexed like class_fields)?
bool mirror_matches(std::size_t m, const std::array<int, 15> &fields, std::uintptr_t base, std::uintptr_t low, std::uintptr_t high,
                    const float *expected) noexcept {
    const auto &mirror = mirrors[m];
    if (base < low || base + mirror.slots.size() * 4 > high) return false;
    for (std::size_t i = 0; i < mirror.slots.size(); ++i) {
        if (!mirror.slots[i].checked) continue;
        const float want = fields[i] >= 0 ? expected[fields[i]] : mirror.slots[i].value;
        if (*reinterpret_cast<const std::uint32_t *>(base + i * 4) != bits(want)) return false;
    }
    return true;
}
struct Anchor {
    std::uint32_t pattern;
    std::uint16_t spec; // a class, or class_count + a mirror
};
void search_region(std::uintptr_t start, std::size_t size, const std::vector<Anchor> &anchors, std::uint64_t filter,
                   Copies &copies, MirrorCopies &mirror_copies, const std::array<std::array<int, 15>, mirror_count> &mirror_fields,
                   const float *expected) noexcept {
    __try {
        const auto *words = reinterpret_cast<const std::uint32_t *>(start);
        const std::size_t total = size / 4;
        for (std::size_t i = 0; i < total; ++i) {
            const auto word = words[i];
            if (!((filter >> (word % 61)) & 1)) continue;
            for (const auto &anchor : anchors) {
                if (anchor.pattern != word) continue;
                if (anchor.spec >= class_count) {
                    const std::size_t m = anchor.spec - class_count;
                    const auto base = start + i * 4 - mirrors[m].anchor * 4;
                    if (mirror_matches(m, mirror_fields[m], base, start, start + size, expected) && mirror_copies[m].size() <= max_copies)
                        mirror_copies[m].push_back(base);
                    continue;
                }
                const auto &spec = class_specs[anchor.spec];
                for (const bool slots : {false, true}) {
                    const Copy copy{start + i * 4 - field_offset(spec, spec.anchor, slots), slots};
                    if (copy.base < start || !matches(spec, copy, start, start + size, expected)) continue;
                    auto &list = copies[anchor.spec];
                    if (list.size() <= max_copies) list.push_back(copy);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
bool still_there(const ClassSpec &spec, Copy copy, const float *expected) noexcept {
    __try {
        const auto base = copy.base;
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void *>(base), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE)
            return false;
        const auto low = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        return matches(spec, copy, low, low + info.RegionSize, expected);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool mirror_still_there(std::size_t m, const std::array<int, 15> &fields, std::uintptr_t base, const float *expected) noexcept {
    __try {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void *>(base), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE)
            return false;
        const auto low = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        return mirror_matches(m, fields, base, low, low + info.RegionSize, expected);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool put(std::uintptr_t address, float value) noexcept {
    __try {
        *reinterpret_cast<volatile float *>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
DWORD WINAPI search(void *) noexcept {
    auto &f = found();
    try {
        // The defaults are what a fresh copy holds.
        std::array<float, class_field_count> stock{};
        for (std::size_t i = 0; i < class_field_count; ++i) stock[i] = class_fields[i].stock;
        std::vector<Anchor> anchors;
        std::uint64_t filter{};
        for (std::size_t c = 0; c < class_count; ++c) {
            const auto pattern = bits(class_fields[class_specs[c].first + class_specs[c].anchor].stock);
            anchors.push_back({pattern, static_cast<std::uint16_t>(c)});
            filter |= std::uint64_t{1} << (pattern % 61);
        }
        for (std::size_t m = 0; m < mirror_count; ++m) {
            const auto pattern = bits(mirrors[m].slots[mirrors[m].anchor].value);
            anchors.push_back({pattern, static_cast<std::uint16_t>(class_count + m)});
            filter |= std::uint64_t{1} << (pattern % 61);
        }
        Copies fresh;
        MirrorCopies fresh_mirrors;
        const auto started = GetTickCount64();
        std::size_t bytes{};
        MEMORY_BASIC_INFORMATION info{};
        for (std::uintptr_t at = 0x10000; at < 0x7fffffff0000ull && VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) == sizeof(info);
             at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize) {
            if (info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE || info.Type == MEM_IMAGE) continue;
            search_region(reinterpret_cast<std::uintptr_t>(info.BaseAddress), info.RegionSize, anchors, filter, fresh, fresh_mirrors, f.mirror_fields,
                          stock.data());
            bytes += info.RegionSize;
        }
        std::size_t classes{}, copies{};
        {
            std::lock_guard lock(f.mutex);
            for (std::size_t c = 0; c < class_count; ++c) {
                // Copies the trainer has already written no longer hold the defaults: keep the
                // ones that still hold what it wrote.
                std::vector<Copy> kept;
                for (const auto copy : f.copies[c])
                    if (std::ranges::find(fresh[c], copy) == fresh[c].end() && still_there(class_specs[c], copy, f.written.data())) kept.push_back(copy);
                if (fresh[c].size() > max_copies) fresh[c].clear();
                // A fresh copy holds the defaults: bring it level with what the older ones hold.
                for (const auto copy : fresh[c])
                    for (std::size_t i = 0; i < class_specs[c].count; ++i)
                        if (const auto index = class_specs[c].first + i; f.written[index] != class_fields[index].stock)
                            (void)put(copy.base + field_offset(class_specs[c], i, copy.slots), f.written[index]);
                kept.insert(kept.end(), fresh[c].begin(), fresh[c].end());
                f.copies[c] = std::move(kept);
                classes += !f.copies[c].empty();
                copies += f.copies[c].size();
            }
            for (std::size_t m = 0; m < mirror_count; ++m) {
                std::vector<std::uintptr_t> kept;
                for (const auto base : f.mirror_copies[m])
                    if (std::ranges::find(fresh_mirrors[m], base) == fresh_mirrors[m].end() && mirror_still_there(m, f.mirror_fields[m], base, f.written.data()))
                        kept.push_back(base);
                if (fresh_mirrors[m].size() > max_copies) fresh_mirrors[m].clear();
                for (const auto base : fresh_mirrors[m])
                    for (std::size_t i = 0; i < mirrors[m].slots.size(); ++i)
                        if (const auto index = f.mirror_fields[m][i]; index >= 0 && f.written[index] != class_fields[index].stock)
                            (void)put(base + i * 4, f.written[index]);
                kept.insert(kept.end(), fresh_mirrors[m].begin(), fresh_mirrors[m].end());
                f.mirror_copies[m] = std::move(kept);
                copies += f.mirror_copies[m].size();
            }
            f.ready = true;
            f.dirty = true;
        }
        logging::write(logging::Level::info, logging::Channel::skater,
                       std::format("Trainer: found {} of {} game tuning classes ({} copies) in {} MB, {} ms.", classes, class_count, copies, bytes >> 20,
                                   GetTickCount64() - started));
    } catch (...) {}
    f.searches.fetch_add(1, std::memory_order_release);
    f.searching.store(false, std::memory_order_release);
    return 0;
}
} // namespace

void find_classes() noexcept {
    auto &f = found();
    if (f.searching.exchange(true, std::memory_order_acq_rel)) return;
    const auto thread = CreateThread(nullptr, 0, search, nullptr, 0, nullptr);
    if (thread) {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    } else {
        f.searching.store(false, std::memory_order_release);
    }
}
bool finding_classes() noexcept { return found().searching.load(std::memory_order_acquire); }
std::uint64_t class_searches() noexcept { return found().searches.load(std::memory_order_acquire); }
std::size_t class_copies(std::size_t index) noexcept {
    auto &f = found();
    if (index >= class_count) return 0;
    std::lock_guard lock(f.mutex);
    return f.copies[index].size();
}
void want_class_value(std::size_t field, float value) noexcept {
    auto &f = found();
    if (field >= class_field_count || !(value == value)) return;
    std::lock_guard lock(f.mutex);
    if (f.wanted[field] != value) {
        f.wanted[field] = value;
        f.dirty = true;
    }
}
std::size_t apply_classes() noexcept {
    auto &f = found();
    std::unique_lock lock(f.mutex, std::try_to_lock);
    if (!lock.owns_lock() || !f.ready || !f.dirty) return 0;
    f.dirty = false;
    std::size_t count{};
    for (std::size_t m = 0; m < mirror_count; ++m) {
        auto &list = f.mirror_copies[m];
        bool differs = false;
        for (const auto index : f.mirror_fields[m]) differs = differs || (index >= 0 && f.wanted[index] != f.written[index]);
        if (!differs || list.empty()) continue;
        std::erase_if(list, [&](std::uintptr_t base) { return !mirror_still_there(m, f.mirror_fields[m], base, f.written.data()); });
        for (std::size_t i = 0; i < mirrors[m].slots.size(); ++i)
            if (const auto index = f.mirror_fields[m][i]; index >= 0 && f.wanted[index] != f.written[index])
                for (const auto base : list)
                    if (put(base + i * 4, f.wanted[index])) ++count;
    }
    for (std::size_t c = 0; c < class_count; ++c) {
        const auto &spec = class_specs[c];
        auto &list = f.copies[c];
        bool differs = false;
        for (std::size_t i = 0; i < spec.count && !differs; ++i) differs = f.wanted[spec.first + i] != f.written[spec.first + i];
        if (!differs) continue;
        // A copy that no longer holds what was last written is someone else's memory now.
        std::erase_if(list, [&](Copy copy) { return !still_there(spec, copy, f.written.data()); });
        for (std::size_t i = 0; i < spec.count; ++i) {
            const auto index = spec.first + i;
            if (f.wanted[index] == f.written[index]) continue;
            for (const auto copy : list)
                if (put(copy.base + field_offset(spec, i, copy.slots), f.wanted[index])) ++count;
            f.written[index] = f.wanted[index];
        }
    }
    return count;
}
std::string classes_summary() {
    auto &f = found();
    std::lock_guard lock(f.mutex);
    std::string result = f.ready ? "" : "(no search has finished yet) ";
    for (std::size_t c = 0; c < class_count; ++c) {
        const auto slots = std::ranges::count_if(f.copies[c], [](Copy copy) { return copy.slots; });
        result += std::format("{}{}:{}+{}", c ? ", " : "", class_specs[c].key, f.copies[c].size() - static_cast<std::size_t>(slots), slots);
    }
    for (std::size_t m = 0; m < mirror_count; ++m) result += std::format(", native copy {}:{}", m, f.mirror_copies[m].size());
    return result;
}
} // namespace dingosdk::trainer
