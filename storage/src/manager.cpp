/**
 * Lollypop C++ - Storage Manager Implementation
 * SQLite database for local music library persistence
 */

#include "manager.hpp"

#include <sqlite3.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>

namespace lollypop {

struct StorageManager::Impl {
    sqlite3* db = nullptr;
    std::string db_path;
    int schema_version = 1;

    Impl() = default;
    ~Impl() {
        if (db) {
            sqlite3_close(db);
        }
    }

    bool execute(const std::string& sql) {
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << (err_msg ? err_msg : "unknown") << std::endl;
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    std::string generate_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        for (int i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                ss << '-';
            } else {
                ss << "0123456789abcdef"[dis(gen)];
            }
        }
        return ss.str();
    }

    int64_t get_current_timestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

StorageManager::StorageManager() : impl_(std::make_unique<Impl>()) {}

StorageManager::~StorageManager() {
    shutdown();
}

bool StorageManager::initialize(const std::string& db_path) {
    if (db_path.empty()) {
        // Default to ~/.local/share/lollypop-cpp/library.db
        const char* home = getenv("HOME");
        std::string base = home ? home : ".";
        impl_->db_path = base + "/.local/share/lollypop-cpp/library.db";
    } else {
        impl_->db_path = db_path;
    }

    // Create directory if it doesn't exist (skip for :memory: database)
    if (impl_->db_path != ":memory:") {
        std::filesystem::path dir = std::filesystem::path(impl_->db_path).parent_path();
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(impl_->db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(impl_->db) << std::endl;
        return false;
    }

    // Enable foreign keys
    sqlite3_exec(impl_->db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    // Enable WAL mode for better concurrency
    sqlite3_exec(impl_->db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);

    // Create schema
    if (!create_schema()) {
        return false;
    }

    std::cout << "StorageManager: Initialized at " << impl_->db_path << std::endl;
    return true;
}

void StorageManager::shutdown() {
    impl_.reset();
}

bool StorageManager::create_schema() {
    // Tracks table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS tracks (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            artist TEXT NOT NULL,
            album TEXT,
            album_artist TEXT,
            genre TEXT,
            track_number INTEGER DEFAULT 0,
            disc_number INTEGER DEFAULT 0,
            year INTEGER DEFAULT 0,
            duration INTEGER DEFAULT 0,
            file_path TEXT NOT NULL UNIQUE,
            artwork_path TEXT,
            liked INTEGER DEFAULT 0,
            play_count INTEGER DEFAULT 0,
            last_played INTEGER DEFAULT 0,
            date_added INTEGER DEFAULT 0,
            date_modified INTEGER DEFAULT 0
        );
    )")) return false;

    // Albums table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS albums (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            artist TEXT NOT NULL,
            year INTEGER DEFAULT 0,
            artwork_path TEXT,
            favorite INTEGER DEFAULT 0,
            date_added INTEGER DEFAULT 0
        );
    )")) return false;

    // Artists table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS artists (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            artwork_path TEXT,
            date_added INTEGER DEFAULT 0
        );
    )")) return false;

    // Playlists table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS playlists (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            is_smart INTEGER DEFAULT 0,
            smart_criteria TEXT,
            date_created INTEGER DEFAULT 0,
            date_modified INTEGER DEFAULT 0
        );
    )")) return false;

    // Playlist tracks (many-to-many)
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS playlist_tracks (
            playlist_id TEXT NOT NULL,
            track_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            date_added INTEGER DEFAULT 0,
            PRIMARY KEY (playlist_id, track_id),
            FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
        );
    )")) return false;

    // Album tracks (many-to-many)
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS album_tracks (
            album_id TEXT NOT NULL,
            track_id TEXT NOT NULL,
            PRIMARY KEY (album_id, track_id),
            FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE,
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
        );
    )")) return false;

    // Artist albums (many-to-many)
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS artist_albums (
            artist_id TEXT NOT NULL,
            album_id TEXT NOT NULL,
            PRIMARY KEY (artist_id, album_id),
            FOREIGN KEY (artist_id) REFERENCES artists(id) ON DELETE CASCADE,
            FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE
        );
    )")) return false;

    // Genres table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS genres (
            name TEXT PRIMARY KEY
        );
    )")) return false;

    // Track genres (many-to-many)
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS track_genres (
            track_id TEXT NOT NULL,
            genre TEXT NOT NULL,
            PRIMARY KEY (track_id, genre),
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE,
            FOREIGN KEY (genre) REFERENCES genres(name) ON DELETE CASCADE
        );
    )")) return false;

    // Preferences table
    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS preferences (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )")) return false;

    // Indexes for performance
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_liked ON tracks(liked);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_date_added ON tracks(date_added);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_last_played ON tracks(last_played);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_albums_artist ON albums(artist);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_albums_favorite ON albums(favorite);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_playlist_tracks_position ON playlist_tracks(playlist_id, position);");

    // FTS5 virtual table for full-text search
    if (!impl_->execute(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
            title, artist, album, album_artist, genre,
            content='tracks', content_rowid='rowid'
        );
    )")) return false;

    // Triggers to keep FTS in sync
    impl_->execute(R"(
        CREATE TRIGGER IF NOT EXISTS tracks_fts_insert AFTER INSERT ON tracks BEGIN
            INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre)
            VALUES (new.rowid, new.title, new.artist, new.album, new.album_artist, new.genre);
        END;
    )");
    impl_->execute(R"(
        CREATE TRIGGER IF NOT EXISTS tracks_fts_delete AFTER DELETE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre)
            VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.album_artist, old.genre);
        END;
    )");
    impl_->execute(R"(
        CREATE TRIGGER IF NOT EXISTS tracks_fts_update AFTER UPDATE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre)
            VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.album_artist, old.genre);
            INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre)
            VALUES (new.rowid, new.title, new.artist, new.album, new.album_artist, new.genre);
        END;
    )");

    return true;
}

bool StorageManager::migrate_schema(int from_version, int to_version) {
    // TODO: Implement schema migrations
    return true;
}

// Helper to convert SQLite row to Track
Track StorageManager::row_to_track(sqlite3_stmt* stmt) {
    Track track;
    track.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    track.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    track.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    track.album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    track.album_artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    track.genre = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    track.track_number = sqlite3_column_int(stmt, 6);
    track.disc_number = sqlite3_column_int(stmt, 7);
    track.year = sqlite3_column_int(stmt, 8);
    track.duration = sqlite3_column_int(stmt, 9);
    track.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    track.artwork_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    track.liked = sqlite3_column_int(stmt, 12) != 0;
    return track;
}

Album StorageManager::row_to_album(sqlite3_stmt* stmt) {
    Album album;
    album.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    album.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    album.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    album.year = sqlite3_column_int(stmt, 3);
    album.artwork_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    return album;
}

Artist StorageManager::row_to_artist(sqlite3_stmt* stmt) {
    Artist artist;
    artist.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    artist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    artist.artwork_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    return artist;
}

Playlist StorageManager::row_to_playlist(sqlite3_stmt* stmt) {
    Playlist playlist;
    playlist.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    playlist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    playlist.is_smart = sqlite3_column_int(stmt, 2) != 0;
    return playlist;
}

std::vector<Track> StorageManager::get_all_tracks() {
    std::vector<Track> tracks;
    const char* sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks ORDER BY artist, album, track_number;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_recently_added(int limit) {
    std::vector<Track> tracks;
    std::string sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks ORDER BY date_added DESC LIMIT " + std::to_string(limit) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_recently_played(int limit) {
    std::vector<Track> tracks;
    std::string sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT " + std::to_string(limit) + ";";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_liked_tracks() {
    std::vector<Track> tracks;
    const char* sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks WHERE liked = 1 ORDER BY artist, album, track_number;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::search_tracks(const std::string& query) {
    std::vector<Track> tracks;
    std::string sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks WHERE tracks_fts MATCH ? ORDER BY rank;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

Track StorageManager::get_track(const std::string& track_id) {
    Track track;
    const char* sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            track = row_to_track(stmt);
        }
        sqlite3_finalize(stmt);
    }
    return track;
}

bool StorageManager::add_track(const Track& track) {
    const char* sql = "INSERT INTO tracks (id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked, date_added, date_modified) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, track.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, track.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, track.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, track.track_number);
    sqlite3_bind_int(stmt, 8, track.disc_number);
    sqlite3_bind_int(stmt, 9, track.year);
    sqlite3_bind_int(stmt, 10, track.duration);
    sqlite3_bind_text(stmt, 11, track.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, track.liked ? 1 : 0);
    sqlite3_bind_int64(stmt, 14, impl_->get_current_timestamp());
    sqlite3_bind_int64(stmt, 15, impl_->get_current_timestamp());

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::update_track(const Track& track) {
    const char* sql = "UPDATE tracks SET title=?, artist=?, album=?, album_artist=?, genre=?, track_number=?, disc_number=?, year=?, duration=?, file_path=?, artwork_path=?, liked=?, date_modified=? WHERE id=?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, track.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, track.track_number);
    sqlite3_bind_int(stmt, 7, track.disc_number);
    sqlite3_bind_int(stmt, 8, track.year);
    sqlite3_bind_int(stmt, 9, track.duration);
    sqlite3_bind_text(stmt, 10, track.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, track.liked ? 1 : 0);
    sqlite3_bind_int64(stmt, 13, impl_->get_current_timestamp());
    sqlite3_bind_text(stmt, 14, track.id.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::delete_track(const std::string& track_id) {
    const char* sql = "DELETE FROM tracks WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::delete_track_by_path(const std::string& file_path) {
    const char* sql = "DELETE FROM tracks WHERE file_path = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::toggle_like(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET liked = NOT liked WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

void StorageManager::update_play_count(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET play_count = play_count + 1 WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void StorageManager::update_last_played(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET last_played = ? WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, impl_->get_current_timestamp());
        sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<Album> StorageManager::get_all_albums() {
    std::vector<Album> albums;
    const char* sql = "SELECT id, title, artist, year, artwork_path FROM albums ORDER BY artist, title;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            albums.push_back(row_to_album(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return albums;
}

std::vector<Album> StorageManager::get_albums_by_artist(const std::string& artist_id) {
    std::vector<Album> albums;
    const char* sql = "SELECT a.id, a.title, a.artist, a.year, a.artwork_path FROM albums a JOIN artist_albums aa ON a.id = aa.album_id WHERE aa.artist_id = ? ORDER BY a.year, a.title;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            albums.push_back(row_to_album(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return albums;
}

Album StorageManager::get_album(const std::string& album_id) {
    Album album;
    const char* sql = "SELECT id, title, artist, year, artwork_path FROM albums WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            album = row_to_album(stmt);
        }
        sqlite3_finalize(stmt);
    }
    return album;
}

bool StorageManager::add_album(const Album& album) {
    const char* sql = "INSERT INTO albums (id, title, artist, year, artwork_path, date_added) VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, album.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, album.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, album.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, album.year);
    sqlite3_bind_text(stmt, 5, album.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, impl_->get_current_timestamp());

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::update_album(const Album& album) {
    const char* sql = "UPDATE albums SET title=?, artist=?, year=?, artwork_path=? WHERE id=?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, album.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, album.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, album.year);
    sqlite3_bind_text(stmt, 4, album.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, album.id.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::toggle_favorite_album(const std::string& album_id) {
    const char* sql = "UPDATE albums SET favorite = NOT favorite WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<Artist> StorageManager::get_all_artists() {
    std::vector<Artist> artists;
    const char* sql = "SELECT id, name, artwork_path FROM artists ORDER BY name;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            artists.push_back(row_to_artist(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return artists;
}

Artist StorageManager::get_artist(const std::string& artist_id) {
    Artist artist;
    const char* sql = "SELECT id, name, artwork_path FROM artists WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            artist = row_to_artist(stmt);
        }
        sqlite3_finalize(stmt);
    }
    return artist;
}

bool StorageManager::add_artist(const Artist& artist) {
    const char* sql = "INSERT INTO artists (id, name, artwork_path, date_added) VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, artist.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, artist.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, impl_->get_current_timestamp());

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::update_artist(const Artist& artist) {
    const char* sql = "UPDATE artists SET name=?, artwork_path=? WHERE id=?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, artist.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, artist.id.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<Playlist> StorageManager::get_all_playlists() {
    std::vector<Playlist> playlists;
    const char* sql = "SELECT id, name, is_smart FROM playlists ORDER BY name;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            playlists.push_back(row_to_playlist(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return playlists;
}

Playlist StorageManager::get_playlist(const std::string& playlist_id) {
    Playlist playlist;
    const char* sql = "SELECT id, name, is_smart FROM playlists WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            playlist = row_to_playlist(stmt);
        }
        sqlite3_finalize(stmt);
    }

    // Load tracks
    const char* tracks_sql = "SELECT t.id, t.title, t.artist, t.album, t.album_artist, t.genre, t.track_number, t.disc_number, t.year, t.duration, t.file_path, t.artwork_path, t.liked FROM tracks t JOIN playlist_tracks pt ON t.id = pt.track_id WHERE pt.playlist_id = ? ORDER BY pt.position;";
    if (sqlite3_prepare_v2(impl_->db, tracks_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            playlist.track_ids.push_back(row_to_track(stmt).id);
        }
        sqlite3_finalize(stmt);
    }

    return playlist;
}

bool StorageManager::create_playlist(const std::string& name) {
    std::string id = impl_->generate_id();
    const char* sql = "INSERT INTO playlists (id, name, date_created, date_modified) VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, impl_->get_current_timestamp());
    sqlite3_bind_int64(stmt, 4, impl_->get_current_timestamp());

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::delete_playlist(const std::string& playlist_id) {
    const char* sql = "DELETE FROM playlists WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::add_track_to_playlist(const std::string& playlist_id, const std::string& track_id) {
    // Get max position
    int max_pos = 0;
    const char* max_sql = "SELECT COALESCE(MAX(position), -1) FROM playlist_tracks WHERE playlist_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, max_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            max_pos = sqlite3_column_int(stmt, 0) + 1;
        }
        sqlite3_finalize(stmt);
    }

    const char* sql = "INSERT INTO playlist_tracks (playlist_id, track_id, position, date_added) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, max_pos);
    sqlite3_bind_int64(stmt, 4, impl_->get_current_timestamp());

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id) {
    const char* sql = "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool StorageManager::reorder_playlist_tracks(const std::string& playlist_id, const std::vector<std::string>& track_ids) {
    const char* sql = "UPDATE playlist_tracks SET position = ? WHERE playlist_id = ? AND track_id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    for (size_t i = 0; i < track_ids.size(); ++i) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(i));
        sqlite3_bind_text(stmt, 2, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

std::vector<std::string> StorageManager::get_all_genres() {
    std::vector<std::string> genres;
    const char* sql = "SELECT DISTINCT genre FROM tracks WHERE genre IS NOT NULL AND genre != '' ORDER BY genre;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            genres.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return genres;
}

std::vector<Track> StorageManager::get_tracks_by_genre(const std::string& genre) {
    std::vector<Track> tracks;
    const char* sql = "SELECT id, title, artist, album, album_artist, genre, track_number, disc_number, year, duration, file_path, artwork_path, liked FROM tracks WHERE genre = ? ORDER BY artist, album, track_number;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, genre.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

bool StorageManager::set_preference(const std::string& key, const std::string& value) {
    const char* sql = "INSERT OR REPLACE INTO preferences (key, value) VALUES (?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::optional<std::string> StorageManager::get_preference(const std::string& key) const {
    const char* sql = "SELECT value FROM preferences WHERE key = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            sqlite3_finalize(stmt);
            return value;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

int StorageManager::get_track_count() const {
    const char* sql = "SELECT COUNT(*) FROM tracks;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int StorageManager::get_album_count() const {
    const char* sql = "SELECT COUNT(*) FROM albums;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int StorageManager::get_artist_count() const {
    const char* sql = "SELECT COUNT(*) FROM artists;";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

} // namespace lollypop