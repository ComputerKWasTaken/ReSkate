#include "trainer_presets.h"

#include <algorithm>
#include <array>
#include <utility>

namespace dingosdk::trainer {
std::string_view essential_name(std::string_view key, int *rank) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 26> names{{
        {"physicsmode.jumpmaxheight", "Ollie height (max)"},
        {"physicsmode.jumpminheight", "Ollie height (min)"},
        {"physicsmode.grindjumpcommonmax", "Pop out of grinds (max)"},
        {"physicsjump.hippyjumpmaxheight", "Hippy jump height"},
        {"physicspush.maxpushablespeed", "Top pushing speed (m/s)"},
        {"physicsmode.maxpushdvstart", "Push strength"},
        {"physicsmode.autopushenabled", "Auto push"},
        {"physicsmode.pumpmaxacceleration", "Pump acceleration"},
        {"physicsmode.speedwobblestartspeed", "Speed wobble starts at (m/s)"},
        {"physicsmode.speedwobbleeffectscalar", "Speed wobble strength"},
        {"physicsreckoning.flipscalar", "Flip speed"},
        {"physicsreckoning.flipmaxspeed", "Flip speed limit"},
        {"physicsmode.perfectbodyflips", "Perfect body flips"},
        {"physicsairstates.maxspinspeed", "Spin speed limit"},
        {"physicsmode.maxautobodyspinspeed", "Body spin speed"},
        {"physicsmode.easybodyspins", "Easy body spins"},
        {"physicsmode.grindlockdist", "Grind lock-on distance"},
        {"physicsgrind.commonfrictionscalar", "Grind friction"},
        {"physicsmode.makesurfacessmooth", "Smooth surfaces"},
        {"physicsmode.wipeoutcheckforbadlanding", "Bail on bad landings"},
        {"physicsmode.wipeout_upsidedown", "Bail when upside down"},
        {"physicswipeout.wipeout_airmaxspeedintoground", "Bail: max speed into the ground"},
        {"physicswipeout.maxspeedlandingonboard", "Max landing speed on the board"},
        {"physicstrucks.truckzposfront", "Front truck position (next respawn)"},
        {"physicstrucks.truckzposback", "Back truck position (next respawn)"},
        {"physicswheels.wheeldynamicfriction", "Wheel grip (next respawn)"},
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
        // What the game was seen to read while a player ollied, flipped and spun: jump height
        // comes from the PhysicsJump height graphs (the Mode jump heights are never read),
        // body flips from FlipScalar and FlipMaxSpeed unless PerfectBodyFlips forces exactly
        // one rotation, body spins from the PhysicsBodyspin graphs and MaxAutoBodySpinSpeed.
        {"Super Ollie", "Huge ollies: about three times the height at any speed.",
         {{"physicsjump.maxheightvsspeed", true, 3.0, true}, {"physicsjump.minheightvsspeed", true, 3.0, true},
          {"physicsjump.absoluteminheight", true, 2.0}, {"physicsjump.jumpybonusmax", true, 3.0},
          {"physicsjump.hippyjump", true, 3.0}, {"physicsmode.jumpmaxheight", true, 3.0},
          {"physicsmode.jumpminheight", true, 3.0}, {"physicsmode.grindjump", true, 2.5}}},
        {"Fast Flips", "Front flips and back flips rotate three times as fast.",
         {{"physicsreckoning.flipscalar", true, 3.0}, {"physicsreckoning.flipmaxspeed", true, 3.0},
          {"physicsmode.perfectbodyflips", false, 0.0}}},
        {"Fast Spins", "Body spins rotate three times as fast.",
         {{"physicsbodyspin.propbodyspinvstime", true, 3.0, true}, {"physicsbodyspin.pbsvst_easy", true, 3.0, true},
          {"physicsbodyspin.maxdeltavstime", true, 3.0, true}, {"physicsbodyspin.maxdeltaforautovstime", true, 3.0, true},
          {"physicsreckoning.spinspeedmaxdelta", true, 3.0, true}, {"physicsmode.maxautobodyspinspeed", true, 3.0},
          {"physicsairstates.maxspinspeed", true, 3.0}}},
        {"Mega Pop", "Ollies, nollies and grind pops go about twice as high.",
         {{"physicsmode.jumpmaxheight", true, 2.2}, {"physicsmode.jumpminheight", true, 1.8},
          {"physicsmode.jumpdelta", true, 2.0}, {"physicsmode.grindjump", true, 1.6},
          {"physicsjump.absoluteminheight", true, 1.5}, {"physicsjump.hippyjump", true, 1.8},
          {"physicsjump.jumpybonusmax", true, 2.0}}},
        {"Big Boneless", "Bonelesses and footplants launch higher.",
         {{"physicsboneless.jumpheightvsplanarspeed", true, 2.0, true}, {"physicsboneless.gravityassist", true, 1.5}}},
        {"Fast", "Push to a higher top speed and get there sooner.",
         {{"physicspush.maxpushablespeed !camera", true, 1.8}, {"physicspush.maxspeedforautopush", true, 1.8},
          {"physicsmode.maxpushdv", true, 1.6}, {"physicsmode.pumpmaxacceleration", true, 1.5},
          {"physicsmode.speedwobblestartspeed", true, 3.0}}},
        {"No Speed Wobble", "The board stays steady at any speed.",
         {{"physicsmode.speedwobbleeffectscalar", false, 0.0}, {"physicsmode.speedwobblestartspeed", true, 10.0}}},
        {"Auto Push", "The skater keeps pushing without input.", {{"physicsmode.autopushenabled", false, 1.0}}},
        {"Hard To Bail", "Much larger impacts are needed before a wipeout.",
         {{"physicswipeout.wipeout_ force", true, 3.0}, {"physicswipeout.wipeout_ acceleration", true, 3.0},
          {"physicswipeout.wipeout_ maxspeed", true, 2.5}, {"physicswipeout.wipeout_ maxcontact", true, 3.0},
          {"physicswipeout.wipeout_ maxdisp", true, 3.0}, {"physicswipeout.wipeout_ relativevel", true, 3.0},
          {"physicswipeout.maxspeedlandingonboard", true, 2.5}, {"physicswipeout.maxchangeinspeed", true, 2.5},
          {"wipeout.on forcelimits", true, 3.0}, {"wipeout.runoutwipeoutforcelimits", true, 3.0},
          {"physicsmode.wipeout_ acceleration", true, 3.0}, {"wipeout.groundxzaccelerationthreshold", true, 3.0},
          {"physicsmode.wipeoutcheckforbadlanding", false, 0.0}, {"physicsmode.wipeout_upsidedown", false, 0.0}}},
        {"Sticky Grinds", "Grinds lock on from further away, faster and at steeper angles.",
         {{"physicsmode.grindlockdist", true, 2.0}, {"physicstrajectory.grindmaxspeedsqr", true, 4.0},
          {"physicstrajectory.grindmaxspeeddownontogrind", true, 2.0}, {"physicstrajectory.grindlandingmaxangle", true, 2.0},
          {"physicstrajectory.grindadjustmaxangle", true, 2.0}}},
        {"Slick Grinds", "Grinds and slides keep their speed.",
         {{"physicsgrind.commonfrictionscalar", true, 0.4}, {"physicsgrind.curbfrictionscalar", true, 0.4},
          {"physicsgrind.genericgrindfrictionscalar", true, 0.4}}},
        {"Smooth Surfaces", "Rough ground rides like polished concrete.", {{"physicsmode.makesurfacessmooth", false, 1.0}}},
        {"Long Wheelbase", "Trucks 10 cm further out at both ends. Takes effect on the next respawn.",
         {{"physicstrucks.truckzposfront", false, 0.0143}, {"physicstrucks.truckzposback", false, 0.0143}}},
    };
    return presets;
}
} // namespace dingosdk::trainer
