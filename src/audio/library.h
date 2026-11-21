// The player's own music (0.68.0): the "My Music" folder next to the game. See library.cpp.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace audio {

struct Song {
    std::string path;    // to open it
    std::string file;    // its name inside My Music ("Album/03 Song.mp3"): how the settings remember it
    std::string title;   // its tag, else from the file name
    std::string artist;  // its tag, else from "Artist - Title.mp3" ("" when unknown)
};

// My Music's MP3 files, folders inside it first, each folder in name order (otherAudio: how many FLAC / M4A / ... files
// it skipped)
std::vector<Song> findSongs(int* otherAudio = nullptr);
std::string myMusicFolder(bool make = true);  // its full path; made when it isn't there (make)
std::FILE* openFile(const std::string& path, const char* mode);  // UTF-8 paths on Windows too
bool canShowFolder();                           // Windows: a folder can be opened in File Explorer
bool showFolder(const std::string& path);        // opens it there (false: couldn't)

}  // namespace audio
