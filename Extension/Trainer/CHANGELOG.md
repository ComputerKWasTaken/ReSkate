# Changelog

## v0.1.2 - 2026-10-03

- **Tune no longer offers values that do nothing.** A pass over the game's code found which tuning
  values it reads (527 of the 930 rows). The rest, including the Mode ollie heights and the hippy
  jump heights people tried, are hidden unless you tick "Values with no use found", and are marked.
- The Essentials list now only holds values the game reads: ollie height is the jump-height graph
  multiplier, body flip and body spin speed are the ones the quick switches use.
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
