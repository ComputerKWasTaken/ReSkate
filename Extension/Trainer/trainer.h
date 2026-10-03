#pragma once
// The ReSkate Trainer: live physics tuning, presets, practice markers and a telemetry HUD.
//
// The game thread owns the trainer's state (trainer.cpp). Every change is a `trainer ...`
// console command, so the menu page (presentation thread) only queues commands and reads
// the two snapshots below; nothing here touches the game from the UI.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::trainer {
// curve: a FloatCurve the asset points at; graph: a table of points stored in the asset
// itself (X0..X15, Y0..Y15). Both are edited as one multiplier on their outputs.
enum class Kind : std::uint8_t { real, integer, flag, curve, graph };

// One editable value of Gameplay/SkatePhysicsTuning. `id` is the name the game's data gives
// it ("PhysicsMode.JumpMaxHeight").
struct Row {
    std::string id, label, group;
    Kind kind{};
    double value{}, stock{};
    bool touched{}, frozen{};
    bool detail{}; // a single point or bound of a graph: hidden unless asked for
    std::string friendly; // a plain name, for the handful of values on the Essentials list
    int rank{};           // its place on that list, from 1; 0: not on it
};
struct PresetRow {
    std::string name, note;
    bool builtin{}, active{};
};
struct Marker {
    bool set{};
    std::array<float, 3> position{};
};
struct Spot {
    std::string name;
    std::array<float, 3> position{};
};
inline constexpr std::size_t marker_slots = 5;

// Changes rarely: rebuilt when a command or a level load changes something.
struct View {
    std::uint64_t revision{};
    bool ready{};        // the game's tuning is read and the running game's copy was found
    std::string status;  // why not
    std::string last;    // what the last command answered
    bool editable{};     // false while a session's host tuning is enforced
    std::string blocked; // the reason shown on locked rows
    std::vector<Row> rows;
    std::vector<std::string> groups;
    std::vector<PresetRow> presets;
    std::size_t touched{};
    // Practice
    int slot{};
    std::array<Marker, marker_slots> markers{};
    bool auto_return{};
    float return_delay{1.5f};
    bool pad_shortcuts{true};
    // HUD
    bool hud{}, hud_jump{true}, logging{};
    // The loaded map and what its author ships for the trainer (Mods/<mod>/trainer.json).
    std::string map, map_note, map_preset, profile_preset;
    std::vector<Spot> spots;
    // `trainer open <tab>`: each new serial opens the menu on the trainer page at that tab.
    std::uint64_t open_serial{};
    int open_tab{};
};

struct Jump {
    std::uint64_t serial{}; // 0: none yet
    float takeoff_speed{}, takeoff_angle{}, air_time{}, height{}, distance{}, drop{}, landing_speed{};
    std::array<float, 3> takeoff{}, landing{};
};
// Changes every client tick.
struct Telemetry {
    bool skater{};
    std::array<float, 3> position{};
    float heading{};           // degrees, 0 = +Z, clockwise seen from above
    float speed{}, vertical{}; // metres per second: over the ground, and upward
    bool airborne{};
    float air_time{}, height{}; // of the jump in progress
    std::uint32_t physics_state{};
    Jump last, best;
    float top_speed{};
};

// Thread-safe snapshots (trainer_view.cpp, part of the overlay).
std::shared_ptr<const View> view() noexcept;
Telemetry telemetry() noexcept;
void publish(std::shared_ptr<const View>) noexcept;
void publish(const Telemetry &) noexcept;

// Game thread (trainer.cpp).
// Each client tick; `playing` while a local skater can exist, `level` the loaded level asset.
void tick(std::uintptr_t base, std::uintptr_t client, bool playing, const std::string &level) noexcept;
// Runs one `trainer` command and returns the line to print. Only from the console dispatcher.
std::string command(std::string_view verb, const std::vector<std::string> &arguments);
} // namespace dingosdk::trainer
