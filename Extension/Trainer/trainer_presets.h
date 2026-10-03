#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace dingosdk::trainer {
// One rule of a built-in preset. `pattern` is lower-case words that a value's id must all
// contain ("physicsmode. jump height"); a word starting with '!' must be absent. A rule
// multiplies the stock value, or sets it. Single graph points are never matched.
struct PresetRule {
    std::string_view pattern;
    bool multiply{true};
    double amount{1};
    bool curves{}; // the rule is for curve and graph multipliers instead of plain values
};
struct BuiltinPreset {
    std::string_view name, note;
    std::vector<PresetRule> rules;
};
// The plain name of a value on the Essentials list, by its lower-case id; empty for the rest.
std::string_view essential_name(std::string_view key, int *rank = nullptr);
// Whether the game's code was found to read the tuning value at this offset of the asset.
// False means "no use found": changing it will probably do nothing.
bool value_used(std::uint16_t offset);
// Presets stack: each applies on top of what is already changed; Stock clears them.
const std::vector<BuiltinPreset> &builtin_presets();
} // namespace dingosdk::trainer
