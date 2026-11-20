/**
 * Lollypop C++ - Common Types
 * Shared data structures used across frontend, backend, and storage
 */

#pragma once

#include <string>
#include <vector>
#include <optional>

namespace lollypop {

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string genre;
    int track_number = 0;
    int disc_number = 0;
    int year = 0;
    int duration = 0;  // seconds
    std::string file_path;
    std::string artwork_path;
    bool liked = false;
    int play_count = 0;
    int64_t last_played = 0;
    int64_t date_added = 0;
};

struct Album {
    std::string id;
    std::string title;
    std::string artist;
    int year = 0;
    std::string artwork_path;
    bool favorite = false;
    int64_t date_added = 0;
    std::vector<std::string> track_ids;
};

struct Artist {
    std::string id;
    std::string name;
    std::string artwork_path;
    int64_t date_added = 0;
    std::vector<std::string> album_ids;
};

struct Playlist {
    std::string id;
    std::string name;
    bool is_smart = false;
    std::string smart_criteria;
    int64_t date_created = 0;
    int64_t date_modified = 0;
    std::vector<std::string> track_ids;
};

struct Lyrics {
    std::string track_id;
    std::string text;
    bool synced = false;
    std::string source;
    int64_t updated_at = 0;
};

} // namespace lollypop