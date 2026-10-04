#pragma once

#include <string>
#include <vector>

namespace dingosdk::mods {

// A playlist a mod declares for the game's music screen. `songs` are song ids, the
// "artist - title" the game registers a song under.
struct MusicPlaylist {
    std::string id;   // "mod:<mod name>:<playlist name>"
    std::string name; // shown in the music screen
    std::vector<std::string> songs;
};

// Reads a mod's reskate-music.json:
//   {"schema":1,"playlists":[{"name":"...","songs":["artist - title", ...]}]}
// Text must be non-empty, at most 255 bytes and free of control characters; a song entry that is
// not such text is dropped, as are playlists beyond 64 and songs beyond 1024 per playlist.
// Throws std::exception on a document that is not this shape, so the caller can skip the file.
std::vector<MusicPlaylist> parse_music_playlists(const std::string& mod, const std::string& json);

} // namespace dingosdk::mods
