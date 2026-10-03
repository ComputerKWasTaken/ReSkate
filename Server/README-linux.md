# ReSkate dedicated server on Linux

Native x86_64 build. Same lobby protocol as `ReSkateServer.exe`; needs no game install.
Self-update is disabled on Linux (V1) — update by replacing the binary.

## Requirements

- 64-bit Linux (tested: CachyOS/Arch, Ubuntu 22.04+ should work).
- `cmake >= 3.24`, `g++ >= 12` (C++20), `libssl-dev` (OpenSSL), `libcurl` headers optional (not needed for V1).
- Steamworks SDK redist beside the binary: `libsteam_api.so` + `steamclient.so`
  (Valve proprietary, not in this repo). Anonymous game-server login, same as Windows.

Arch/CachyOS:

```sh
sudo pacman -S base-devel cmake openssl
```

Ubuntu/Debian:

```sh
sudo apt install build-essential cmake libssl-dev
```

## Build

```sh
cmake --preset linux-x64
cmake --build --preset linux-release -j$(nproc)
./build/linux/ReSkateServer
```

Binary: `build/linux/ReSkateServer` (ELF, `OUTPUT_NAME ReSkateServer`).

Portable tests:

```sh
cmake --preset linux-x64 -DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON
cmake --build --preset linux-release -j$(nproc)
ctest --test-dir build/linux --output-on-failure
```

Expected: 8 passed (`multiplayer_parties`, `server_activity`, `server_speed_check`,
`server_config`, `word_filter`, `multiplayer_bandwidth`, `multiplayer_lanes`,
`multiplayer_prediction`). Windows-only tests (voice, lobbies, UI) stay `WIN32`-gated.

## Run

1. Put `ReSkateServer` in its own folder with `libsteam_api.so`, `steamclient.so`.
2. First run writes `ReSkateServer.json`; edit `name` + `admins`, restart.
3. Optional: `world-layers.json` for time-of-day / world layers.
   Normal server runs only *read* this file (`world_layer_scan::read`, JSON only)
   and work on Linux — verified. Get it by either:
   - Windows export once: `ReSkateServer.exe --export-world-layers "<Skate folder>" world-layers.json`,
     then copy to the Linux folder; or
   - copy from a Windows player's cache `%LOCALAPPDATA%\ReSkate\cache\<build>\world-layers.json`
     (same game build; the server tells admins this path in `layer-sync` errors).
   Without it every player keeps their own layers; with it + `world_layer_sync` the server forces them.
4. No ports need opening (Steam relay). Optional UDP `27015-27016` (`port`,
   `query_port`) for browser ping + faster joins.

Without `libsteam_api.so` the server exits 1 with:
`Cannot load .../libsteam_api.so. Put libsteam_api.so (Steamworks SDK) next to the server.`

Full command list: `Server/README.txt` (same on Linux; binary name differs).

## systemd

See `contrib/reskate-server.service`. Install:

```sh
sudo cp contrib/reskate-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now reskate-server
```

## Notes / limits (V1)

- `auto_update` / `update` command: `updates_enabled()==false` on Linux.
  `check_for_update` reports “self-update is not supported on Linux”.
- `--export-world-layers` on Linux: the reader works, the scanner needs the
  Windows game (`Data/layout.toc` + CAS) and `oo2core_9_win64.dll` (Oodle).
  Oodle blocks throw `Oodle CAS data is only supported on Windows` (caught,
  exit 1, no crash). Export on Windows, copy the JSON over — no game needed
  on the server itself.
- `content_cache` dir: `%LOCALAPPDATA%` on Windows, `$XDG_CACHE_HOME`/`~/.cache` on Linux.
- Crypto interop verified: OpenSSL `PKCS5_PBKDF2_HMAC(SHA256, 100k)` +
  `HMAC-SHA256` matches Windows `BCrypt` (checked against Python `hashlib`).
