#pragma once
// The game keeps much of its tuning outside Gameplay/SkatePhysicsTuning, in data-defined classes
// (push speeds, on-foot jumps, flips and rolls, dive and glide, bail speeds...). Their defaults
// and field order ship in the game's data (trainer_classes.inc); a live copy has no name to look
// up, so it is found by searching writable memory for the class's defaults laid out as the
// class lays them out. Game thread, except the search itself, which runs on its own thread.
#include <cstddef>
#include <cstdint>
#include <string>

namespace dingosdk::trainer {
struct ClassField {
    const char *name;
    float stock;
    std::uint16_t offset;
};
struct ClassSpec {
    const char *group; // shown in the menu
    const char *key;   // in value ids: "<key>.<field>"
    std::uint16_t first, count, anchor;
};
#include "trainer_classes.inc"
inline constexpr std::size_t class_count = sizeof(class_specs) / sizeof(class_specs[0]);
inline constexpr std::size_t class_field_count = sizeof(class_fields) / sizeof(class_fields[0]);

// Starts a search for every class's live copies unless one is running. Copies found earlier
// that still hold what the trainer last wrote are kept.
void find_classes() noexcept;
bool finding_classes() noexcept;
// How many searches have finished (0: none yet).
std::uint64_t class_searches() noexcept;
// Copies of class `index` found.
std::size_t class_copies(std::size_t index) noexcept;
// What field `field` (an index into class_fields) should hold. Written by apply_classes.
void want_class_value(std::size_t field, float value) noexcept;
// Writes the wanted values into every copy found; call each tick. Returns the fields written.
std::size_t apply_classes() noexcept;
std::string classes_summary();
} // namespace dingosdk::trainer
