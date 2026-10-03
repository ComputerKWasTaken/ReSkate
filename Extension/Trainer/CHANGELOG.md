# Changelog

## v0.1.4 - 2026-10-03

- **No comply height and boneless height.** Two new sliders beside the hippy jump's (Tune tab,
  top of Essentials, and the Presets tab). The game's trick scripts launch these jumps; the
  trainer multiplies the launch where the game sets the jump's trajectory, so the skater and
  the board go up together. Measured: no comply 0.50 m stock, 1.70 m at x4; boneless 0.46 m
  stock, 3.39 m at x9. Console: `trainer option nocomply_height|boneless_height <x>`.
- **Top pushing speed works.** The game skips its own push tuning (pushes aim for speeds the
  trick scripts pick), so the value did nothing. Now, holding push carries on past the game's
  9.1 m/s to your number, and a lower number caps pushing. Taps still cruise at the game's
  4 m/s. The Fast and Realistic presets use it.
- **Push strength is hidden:** nothing in this game build reads it while pushes are scripted.

## v0.1.3 - 2026-10-03

- **Hippy jump height.** A slider on the Tune tab (top of Essentials) and the Presets tab. The game
  sets this jump in its trick scripts, not in its tuning, so the trainer recognises a hippy jump
  starting and scales the skater's upward speed.
- **Realistic preset:** lower pop, slower pushing and rotation, earlier speed wobble, easier bails.
- **Spin and flip in the jump read-out:** degrees turned about the vertical (and the peak rate) and
  degrees the body tumbled, in the HUD card, the Map & HUD tab and the log.
- More tuning values are recognised as used: the table now also includes everything the game read
  in traced play sessions (584 of 930 rows, was 527).
- No comply and boneless heights are not adjustable yet: the game drives those jumps along a
  scripted path that a velocity change does not move.

## v0.1.2 - 2026-10-03

- **Tune no longer offers values that do nothing.** A pass over the game's code found which tuning
  values it reads (527 of the 930 rows). The rest, including the Mode ollie heights and the hippy
  jump heights people tried, are hidden unless you tick "Values with no use found", and are marked.
- **Ollie height and body spin speed are plain values again.** The game ignores its own
  `JumpMaxHeight`, `JumpMinHeight` and `MaxSpinSpeed`; the trainer now links them to the graphs the
  game does read, so setting ollie height to twice its stock value doubles the jump graphs.
- The Essentials list only holds values that do something: ollie height, body flip speed, body
  spin speed, pushing speed, grind lock-on and more.
- A preset says when it skipped values you locked.
- Every preset can be switched off again by itself; presets that are still on keep their values.
- Presets no longer contain rules for values the game does not read. Mega Pop and No Speed Wobble
  were rebuilt on the ones it does.
- The Tune tab says that changes apply as you drag and that the box only locks a value.

## v0.1.1 - 2026-10-03

Packaging only (Thunderstore): `Install.bat` and `Uninstall.bat`. The trainer was unchanged.

## v0.1.0 - 2026-10-03

First release, built on ReSkate 1.0.3.

- TRAINER page in the ReSkate menu: Tune, Presets, Practice, Map & HUD.
- Live editing of the game's physics tuning (3,801 values, 89 curves, 80 graphs), named from the
  player's own game data; search, groups, an Essentials list, freeze, reset.
- Quick switches: super high ollie, fast flips (front and back flips), fast spins, never bail.
- Stackable presets, user presets, a preset per map.
- Game speed and pause, five marker slots per map, return after a bail, teleport.
- Speed / air HUD and a read-out after every jump; telemetry recording to CSV.
- `trainer.json` in a mod folder: a map author's spots and recommended preset.
- Every action is a `trainer ...` console command; `trainer selftest` checks a build in game.
