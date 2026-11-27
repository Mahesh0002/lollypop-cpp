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
    bool create_fts5_index();
    bool create_triggers();
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
    std::vector<Track> search_tracks_fts(const std::string& query);
    Track get_track(const std::string& track_id);
    Track get_track_by_path(const std::string& file_path);
    bool add_track(const Track& track);
    bool update_track(const Track& track);
    bool delete_track(const std::string& track_id);
    bool delete_track_by_path(const std::string& file_path);
    bool delete_tracks_under_path(const std::string& directory_path);
    std::vector<std::string> prune_orphaned_entities();
    bool toggle_like(const std::string& track_id);
    bool is_track_liked(const std::string& track_id);
    void update_play_count(const std::string& track_id);
    void update_last_played(const std::string& track_id);
    bool insert_tracks_batch(const std::vector<Track>& tracks);
    std::vector<Track> get_tracks_by_ids(const std::vector<std::string>& track_ids);
    std::vector<Track> get_tracks_by_album(const std::string& album_id);

    // Album operations
    std::vector<Album> get_all_albums();
    std::vector<Album> get_albums_by_artist(const std::string& artist_id);
    Album get_album(const std::string& album_id);
    std::vector<Album> get_favourite_albums();
    bool add_album(const Album& album);
    bool update_album(const Album& album);
    bool toggle_favorite_album(const std::string& album_id);
    bool is_album_favorited(const std::string& album_id);

    // Artist operations
    std::vector<Artist> get_all_artists();
    Artist get_artist(const std::string& artist_id);
    std::vector<Track> get_tracks_by_artist(const std::string& artist_name);
    bool add_artist(const Artist& artist);
    bool update_artist(const Artist& artist);
    bool update_artist_artwork(const std::string& artist_id, const std::string& artwork_path);
    bool update_track_artwork(const std::string& track_id, const std::string& artwork_path);
    bool update_album_artwork(const std::string& album_id, const std::string& artwork_path);

    // Playlist operations
    std::vector<Playlist> get_all_playlists();
    Playlist get_playlist(const std::string& playlist_id);
    std::vector<Track> get_smart_playlist_tracks(const std::string& playlist_id);
    bool create_playlist(const std::string& name);
    bool rename_playlist(const std::string& playlist_id, const std::string& name);
    bool create_smart_playlist(const std::string& name, const std::string& criteria);
    bool update_smart_playlist(const std::string& playlist_id,
                               const std::string& name,
                               const std::string& criteria);
    // Recompute persisted membership for every smart playlist. Call after a
    // scan completes or when smart criteria change; reads stay O(1) afterwards.
    bool rebuild_smart_playlists();
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

    // Genre hierarchy (DAG)
    // add_genre_edge returns false if the edge would create a cycle.
    bool add_genre_edge(const std::string& parent, const std::string& child);
    // Returns genre and all descendants (recursive, cycle-safe via UNION).
    std::vector<std::string> get_genre_descendants(const std::string& genre);
    // Returns all tracks whose genre is the given genre or any descendant.
    std::vector<Track> get_tracks_by_genre_hierarchy(const std::string& genre);

    // Library root tracking (plan.md §16.7).
    // Sorted vector + binary search is sufficient for a small number of roots.
    bool add_library_root(const std::string& path);
    bool remove_library_root(const std::string& path);
    std::vector<std::string> get_library_roots() const;
    bool is_library_root(const std::string& path) const;

    // User preferences
    bool set_preference(const std::string& key, const std::string& value);
    std::optional<std::string> get_preference(const std::string& key) const;

    // Statistics
    int get_track_count() const;
    int get_album_count() const;
    int get_artist_count() const;

    // Read-only connections for worker threads (plan.md §16.6).
    // Prefer open_read_only_connection() so the connection is released even on
    // early returns. The raw API remains for existing callers.
    class ReadOnlyConnection;
    sqlite3* open_read_only();
    void close_read_only(sqlite3* ro_db);
    ReadOnlyConnection open_read_only_connection();

    // RAII owner for a read-only connection. The StorageManager must outlive
    // this object; moving transfers ownership and closes any prior connection.
    class ReadOnlyConnection {
    public:
        ReadOnlyConnection() = default;
        ReadOnlyConnection(StorageManager* manager, sqlite3* db)
            : manager_(manager), db_(db) {}
        ~ReadOnlyConnection() { reset(); }

        ReadOnlyConnection(const ReadOnlyConnection&) = delete;
        ReadOnlyConnection& operator=(const ReadOnlyConnection&) = delete;

        ReadOnlyConnection(ReadOnlyConnection&& other) noexcept
            : manager_(other.manager_), db_(other.db_) {
            other.manager_ = nullptr;
            other.db_ = nullptr;
        }

        ReadOnlyConnection& operator=(ReadOnlyConnection&& other) noexcept {
            if (this != &other) {
                reset();
                manager_ = other.manager_;
                db_ = other.db_;
                other.manager_ = nullptr;
                other.db_ = nullptr;
            }
            return *this;
        }

        sqlite3* get() const { return db_; }
        explicit operator bool() const { return db_ != nullptr; }

        sqlite3* release() {
            sqlite3* db = db_;
            db_ = nullptr;
            manager_ = nullptr;
            return db;
        }

        void reset() {
            if (db_ && manager_) manager_->close_read_only(db_);
            db_ = nullptr;
            manager_ = nullptr;
        }

    private:
        StorageManager* manager_ = nullptr;
        sqlite3* db_ = nullptr;
    };

    // Database recovery (plan.md §16.7).
    bool integrity_check();
    bool quick_check();
    bool backup_database(const std::string& backup_path);
    bool vacuum();

    // Keyset pagination (avoids OFFSET overhead on large libraries)
    // sort_key must be one of: date_added, title, artist, album, duration,
    // play_count, last_played, year, track_number, genre, liked.
    // Optional genre_filter restricts results to tracks with the given genre.
    // A negative limit means unbounded (SQLite LIMIT -1).
    std::vector<Track> get_tracks_paginated(int limit,
                                             const std::string& sort_key = "date_added",
                                             const std::string& last_sort_value = "",
                                             const std::string& last_id = "",
                                             const std::string& genre_filter = "");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Helper methods
    Track row_to_track(sqlite3_stmt* stmt);
    Album row_to_album(sqlite3_stmt* stmt);
    Artist row_to_artist(sqlite3_stmt* stmt);
    Playlist row_to_playlist(sqlite3_stmt* stmt);
    void backfill_library_index(sqlite3* db);
    bool insert_track(const Track& track);
    std::string ensure_album_artist_relationships(const Track& track);
    std::string lower_hex_randomblob(int bytes);
};

} // namespace lollypop