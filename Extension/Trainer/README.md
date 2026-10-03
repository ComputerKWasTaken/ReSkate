# ReSkate Trainer

A TRAINER page in the ReSkate menu (Insert): live physics tuning, presets, practice markers and a
telemetry HUD. It ships no game data: the list of values is built at run time from the player's own
`Gameplay/SkatePhysicsTuning`.

## What it does

| Tab | What you get |
|---|---|
| **TUNE** | An Essentials list of 26 plainly named values (ollie height, top speed, flip and spin speed, grind lock-on, bail limits...), then every plain value of the game's physics tuning (about 760), plus one multiplier for each of its 169 curves and graphs. Search, filter by group, "only what I changed", freeze, reset. Changes apply while you skate. |
| **PRESETS** | Quick switches for super high ollie, fast flips, fast spins and never bail. Built-in presets that stack (Super Ollie, Fast Flips, Fast Spins, Mega Pop, Fast, Hard To Bail, Sticky Grinds, ...), your own saved presets, and a preset a map applies every time it loads. |
| **PRACTICE** | Game speed and pause, five marker slots per map (save / go / clear), return to the marker after a bail, teleport to coordinates, copy your position (game or Blender axes). |
| **MAP & HUD** | Speed and air-time HUD, a read-out after every jump (takeoff speed and angle, height, distance, drop, landing speed), telemetry recording to CSV, and whatever the map's author ships for the trainer. |

Controller: hold **LB + RB**, then D-pad **up** saves the marker, **down** goes to it, **left / right**
pick the slot.

Everything is also a console command (`~`): `trainer open [tune|presets|practice|map]`, `trainer status`, `trainer set <id> <value>`,
`trainer find <words>`, `trainer preset apply|remove <name>`, `trainer marker save|go|clear [slot]`,
`trainer tp <x> <y> <z>`, `trainer where`, `trainer jumps`, `trainer dump`, `trainer selftest`.

## For map makers: `trainer.json`

Put a `trainer.json` in your mod folder (beside `manifest.json`). Stock ReSkate ignores it; with the
trainer, players get your spots and your recommended tuning on the MAP & HUD tab.

```json
{
  "schema": 1,
  "note": "Built for about 60 km/h off the first lip.",
  "preset": {
    "name": "Gravy Train",
    "values": { "PhysicsPush.MaxPushableSpeed": 12.0 }
  },
  "spots": [
    { "name": "Start deck", "position": [0.0, 790.0, 0.0] },
    { "name": "Jump 3", "position": [0.0, 700.0, 310.0] }
  ]
}
```

`"level"` (a level asset) is optional: without it the file stands for every level the mod's
`reskate-levels.json` adds. Value ids are the ones `trainer dump` lists.

## Multiplayer

The trainer goes through ReSkate's own session rules instead of around them:

- A guest whose host sets the physics tuning cannot edit it; the page says so.
- Teleports and markers follow the host's noclip / teleport permission.
- Game speed is ReSkate's `SimulationTime.TimeScale` setting, which ReSkate locks in a session.
- A host's tuning changes reach guests through ReSkate's existing host-tuning sync, and servers'
  `score_check` / `enforce_tuning` see them like any other tuning mod.

It unlocks no cosmetics or entitlements.

## How it works

- `physics_tuning_model` keeps the field names the game's EBX carries, so `trainer.cpp` can build its
  table from `read_game_tuning()`.
- Edits go into a target copy of the asset; `physics_tuning::write_live()` writes the differences and
  refreshes the local skater's cached block. Values are re-applied after a level load.
- The game thread owns all state. The menu only queues `trainer ...` console commands and reads two
  snapshots (`trainer_view.cpp`).
- Jumps are measured from the skater's position each client tick: the game's own physics state says when the skater is in the air.
  Wipeouts come from the same state, which the no-bail hook already sees.
- Settings, presets, markers: `%LOCALAPPDATA%\ReSkate\trainer\trainer.json`.

## Checking a build

```
RESKATE_STARTUP_COMMANDS="load <level asset>;wait 25;trainer selftest"
ReSkateLauncher.exe --no-gui --no-update
```

then read the `trainer selftest:` lines in `logs\ReSkate.log`. `dingosdk_trainer_tuning_dump <Skate
folder>` (CMake option `DINGOSDK_BUILD_TRAINER_TESTS`) lists the tuning values without the game.

## Known limits

- The jump read-out uses real time, so it reads low while game speed is not 1x.
- Masses and collision sizes (deck, trucks, wheels) only change on the next respawn.
- Curve and graph multipliers scale outputs only; a curve whose point count a mod changed keeps the
  mod's points.
- Not every tuning value is used by the game. The quick switches were built from what the game was
  seen to read during tricks: ollie height comes from the `PhysicsJump` height graphs (the
  `PhysicsMode` jump heights are never read), body flips from `PhysicsReckoning.FlipScalar` and
  `FlipMaxSpeed` (`PerfectBodyFlips` forces exactly one rotation and ignores them), body spins from
  the `PhysicsBodyspin` graphs.
- Built for one game build (the one ReSkate 1.0.3 supports). A game update needs a new build.
