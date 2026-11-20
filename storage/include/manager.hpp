/**
 * Lollypop C++ - Storage Manager
 * SQLite persistence layer for library metadata and user data
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

#include <sqlite3.h>

#include "types.hpp"

namespace lollypop {

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool initialize(const std::string& db_path = "");
    void shutdown();

    // Schema management
    bool create_schema();
    bool migrate_schema(int from_version, int to_version);
    int get_schema_version() const;

    // Bulk updates (e.g. a full library scan)
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    // Track operations
    std::vector<Track> get_all_tracks();
    std::vector<Track> get_recently_added(int limit);
    std::vector<Track> get_recently_played(int limit);
    std::vector<Track> get_liked_tracks();
    std::vector<Track> search_tracks(const std::string& query);
    Track get_track(const std::string& track_id);
    Track get_track_by_path(const std::string& file_path);
    bool add_track(const Track& track);
    bool update_track(const Track& track);
    bool delete_track(const std::string& track_id);
    bool delete_track_by_path(const std::string& file_path);
    bool delete_tracks_under_path(const std::string& directory_path);
    std::vector<std::string> prune_orphaned_entities();
    bool toggle_like(const std::string& track_id);
    void update_play_count(const std::string& track_id);
    void update_last_played(const std::string& track_id);

    // Album operations
    std::vector<Album> get_all_albums();
    std::vector<Album> get_albums_by_artist(const std::string& artist_id);
    Album get_album(const std::string& album_id);
    bool add_album(const Album& album);
    bool update_album(const Album& album);
    bool toggle_favorite_album(const std::string& album_id);

    // Artist operations
    std::vector<Artist> get_all_artists();
    Artist get_artist(const std::string& artist_id);
    std::vector<Track> get_tracks_by_artist(const std::string& artist_name);
    bool add_artist(const Artist& artist);
    bool update_artist(const Artist& artist);

    // Playlist operations
    std::vector<Playlist> get_all_playlists();
    Playlist get_playlist(const std::string& playlist_id);
    std::vector<Track> get_smart_playlist_tracks(const std::string& playlist_id);
    bool create_playlist(const std::string& name);
    bool create_smart_playlist(const std::string& name, const std::string& criteria);
    bool update_smart_playlist(const std::string& playlist_id,
                               const std::string& name,
                               const std::string& criteria);
    bool delete_playlist(const std::string& playlist_id);
    bool add_track_to_playlist(const std::string& playlist_id, const std::string& track_id);
    bool remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id);
    bool reorder_playlist_tracks(const std::string& playlist_id, const std::vector<std::string>& track_ids);

    // Lyrics are always stored locally; a caller may mark text as synchronized.
    bool set_lyrics(const std::string& track_id, const std::string& text,
                    bool synced = false, const std::string& source = {});
    std::optional<Lyrics> get_lyrics(const std::string& track_id) const;
    bool delete_lyrics(const std::string& track_id);

    // Genre operations
    std::vector<std::string> get_all_genres();
    std::vector<Track> get_tracks_by_genre(const std::string& genre);

    // User preferences
    bool set_preference(const std::string& key, const std::string& value);
    std::optional<std::string> get_preference(const std::string& key) const;

    // Statistics
    int get_track_count() const;
    int get_album_count() const;
    int get_artist_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Helper methods
    Track row_to_track(sqlite3_stmt* stmt);
    Album row_to_album(sqlite3_stmt* stmt);
    Artist row_to_artist(sqlite3_stmt* stmt);
    Playlist row_to_playlist(sqlite3_stmt* stmt);
};

} // namespace lollypop