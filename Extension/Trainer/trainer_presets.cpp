#include "trainer_presets.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace dingosdk::trainer {
bool value_used(std::uint16_t offset) {
    static constexpr std::uint16_t used[]{
#include "trainer_used.inc"
    };
    return std::binary_search(std::begin(used), std::end(used), offset);
}
// Only values the game was found to read: a slider that does nothing is worse than none.
std::string_view essential_name(std::string_view key, int *rank) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 22> names{{
        {"physicsjump.maxheightvsspeed x", "Ollie height (x)"},
        {"physicsjump.minheightvsspeed x", "Ollie height, light pop (x)"},
        {"physicsmode.grindjumpcommonmax", "Pop out of grinds (max)"},
        {"physicsjump.jumpybonusmax", "Jump bonus (max)"},
        {"physicspush.maxpushablespeed", "Top pushing speed (m/s)"},
        {"physicsmode.maxpushdvstart", "Push strength"},
        {"physicsmode.autopushenabled", "Auto push"},
        {"physicsmode.speedwobblestartspeed", "Speed wobble starts at (m/s)"},
        {"physicsreckoning.flipscalar", "Body flip speed"},
        {"physicsreckoning.flipmaxspeed", "Body flip speed limit"},
        {"physicsmode.perfectbodyflips", "Perfect body flips (exactly one rotation)"},
        {"physicsbodyspin.propbodyspinvstime x", "Body spin speed (x)"},
        {"physicsbodyspin.maxdeltavstime x", "Body spin acceleration (x)"},
        {"physicsmode.maxautobodyspinspeed", "Auto body spin speed"},
        {"physicsmode.easybodyspins", "Easy body spins"},
        {"physicsmode.grindlockdist", "Grind lock-on distance"},
        {"physicsgrind.commonfrictionscalar", "Grind friction"},
        {"physicsmode.makesurfacessmooth", "Smooth surfaces"},
        {"physicsmode.wipeoutcheckforbadlanding", "Bail on bad landings"},
        {"physicsmode.wipeout_groundxzacceleration", "Bail: sideways hit limit"},
        {"physicstrucks.truckzposfront", "Front truck position (next respawn)"},
        {"physicstrucks.truckzposback", "Back truck position (next respawn)"},
    }};
    const auto found = std::ranges::find(names, key, &std::pair<std::string_view, std::string_view>::first);
    if (rank) *rank = found == names.end() ? 0 : static_cast<int>(found - names.begin()) + 1;
    return found == names.end() ? std::string_view{} : found->second;
}

// Patterns are matched against the lower-case ids the game's own data gives its tuning
// (run `trainer dump` for the list), so a preset reaches every value a pattern names in
// whatever build is running and simply skips the ones that build lacks.
const std::vector<BuiltinPreset> &builtin_presets() {
    static const std::vector<BuiltinPreset> presets{
        // Rules name values the game was found to read (trainer_used.inc). Jump height comes
        // from the PhysicsJump height graphs (the Mode jump heights are never read); body flips
        // from FlipScalar and FlipMaxSpeed unless PerfectBodyFlips forces exactly one rotation;
        // body spins from the PhysicsBodyspin graphs and MaxAutoBodySpinSpeed.
        {"Super Ollie", "Huge ollies: about three times the height at any speed.",
         {{"physicsjump.maxheightvsspeed", true, 3.0, true}, {"physicsjump.minheightvsspeed", true, 3.0, true},
          {"physicsjump.absoluteminheight", true, 2.0}, {"physicsjump.jumpybonusmax", true, 3.0},
          {"physicsmode.grindjump", true, 2.5}}},
        {"Fast Flips", "Front flips and back flips rotate three times as fast.",
         {{"physicsreckoning.flipscalar", true, 3.0}, {"physicsreckoning.flipmaxspeed", true, 3.0},
          {"physicsmode.perfectbodyflips", false, 0.0}}},
        {"Fast Spins", "Body spins rotate three times as fast.",
         {{"physicsbodyspin.propbodyspinvstime", true, 3.0, true}, {"physicsbodyspin.pbsvst_easy", true, 3.0, true},
          {"physicsbodyspin.maxdeltavstime", true, 3.0, true}, {"physicsmode.maxautobodyspinspeed", true, 3.0}}},
        {"Mega Pop", "Ollies and grind pops go about twice as high.",
         {{"physicsjump.maxheightvsspeed", true, 2.0, true}, {"physicsjump.minheightvsspeed", true, 1.6, true},
          {"physicsjump.jumpybonusmax", true, 2.0}, {"physicsmode.grindjump", true, 1.6}}},
        {"Fast", "Push to a higher top speed and get there sooner.",
         {{"physicspush.maxpushablespeed !camera", true, 1.8}, {"physicspush.maxspeedforautopush", true, 1.8},
          {"physicsmode.maxpushdv", true, 1.6}, {"physicsmode.speedwobblestartspeed", true, 3.0}}},
        {"No Speed Wobble", "The board stays steady at any speed.", {{"physicsmode.speedwobblestartspeed", true, 20.0}}},
        {"Auto Push", "The skater keeps pushing without input.", {{"physicsmode.autopushenabled", false, 1.0}}},
        {"Hard To Bail", "Much larger impacts are needed before a wipeout.",
         {{"physicswipeout.wipeout_ force", true, 3.0}, {"physicswipeout.wipeout_ acceleration", true, 3.0},
          {"physicswipeout.wipeout_ maxspeed", true, 2.5}, {"physicswipeout.wipeout_ maxdisp", true, 3.0}, {"physicswipeout.wipeout_ relativevel", true, 3.0},
          {"wipeout.on forcelimits", true, 3.0}, {"wipeout.runoutwipeoutforcelimits", true, 3.0},
          {"physicsmode.wipeout_ acceleration", true, 3.0}, {"physicsmode.wipeoutcheckforbadlanding", false, 0.0}}},
        {"Sticky Grinds", "Grinds lock on from further away and at higher speeds.",
         {{"physicsmode.grindlockdist", true, 2.0}, {"physicstrajectory.grindmaxspeedsqr", true, 4.0},
          {"physicstrajectory.grindmaxspeeddownontogrind", true, 2.0}, {"physicstrajectory.grindlandingmaxangle", true, 2.0}}},
        {"Slick Grinds", "Grinds and slides keep their speed.",
         {{"physicsgrind.commonfrictionscalar", true, 0.4}, {"physicsgrind.curbfrictionscalar", true, 0.4}}},
        {"Smooth Surfaces", "Rough ground rides like polished concrete.", {{"physicsmode.makesurfacessmooth", false, 1.0}}},
        {"Long Wheelbase", "Trucks 10 cm further out at both ends. Takes effect on the next respawn.",
         {{"physicstrucks.truckzposfront", false, 0.0143}, {"physicstrucks.truckzposback", false, 0.0143}}},
    };
    return presets;
}
} // namespace dingosdk::trainer
