/**
 * Lollypop C++ - Storage Manager Implementation
 * SQLite database for local music library persistence
 *
 * Referenced files (kept as comments so other models can follow the wiring):
 *   storage/include/manager.hpp
 *   backend/src/controller.cpp
 *   backend/src/library_manager.cpp
 *   backend/src/library_scanner.cpp
 *   frontend/src/ui/pages/AlbumDetailPage.cpp
 */

#include "manager.hpp"

#include <algorithm>
#include <mutex>
#include <sqlite3.h>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <random>
#include <unordered_map>

namespace lollypop {

// Helper functions at namespace scope (not in anonymous namespace)
constexpr const char* kTrackColumns =
    "id, title, artist, album, album_artist, genre, track_number, disc_number, "
    "year, duration, file_path, artwork_path, liked, play_count, last_played, "
    "date_added";

constexpr const char* kTrackColumnsQualified =
    "tracks.id, tracks.title, tracks.artist, tracks.album, "
    "tracks.album_artist, tracks.genre, tracks.track_number, "
    "tracks.disc_number, tracks.year, tracks.duration, tracks.file_path, "
    "tracks.artwork_path, tracks.embedded_lyrics, tracks.liked, tracks.play_count, tracks.last_played, "
    "tracks.date_added";

// sqlite3_column_text() returns nullptr for SQL NULL columns. Constructing a
// std::string from nullptr is undefined behaviour, so always go through this.
std::string column_text(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string build_fts_query(const std::string& input) {
    std::string result;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        std::string escaped;
        escaped.reserve(token.size());
        for (char c : token) {
            if (c == '"') escaped += "\"\"";
            else escaped += c;
        }
        if (!result.empty()) result += ' ';
        result += '"' + escaped + "\"*";
        token.clear();
    };

    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
        } else {
            token += c;
        }
    }
    flush();
    return result;
}

} // namespace lollypop

namespace lollypop {

struct StorageManager::Impl {
    sqlite3* db = nullptr;
    std::string db_path;
    int schema_version = 6;

    std::mutex ro_mutex;
    std::vector<sqlite3*> read_only_connections;

    std::unordered_map<std::string, std::string> artist_name_to_id;
    std::unordered_map<std::string, std::string> album_key_to_id;

    Impl() = default;
    ~Impl() {
        std::lock_guard<std::mutex> lock(ro_mutex);
        for (sqlite3* ro_db : read_only_connections) {
            if (ro_db) sqlite3_close(ro_db);
        }
        read_only_connections.clear();
        if (db) {
            sqlite3_close(db);
        }
    }
};

StorageManager::StorageManager() : impl_(std::make_unique<Impl>()) {}

StorageManager::~StorageManager() {
    shutdown();
}

bool StorageManager::initialize(const std::string& db_path) {
    if (db_path.empty()) {
        const char* home = getenv("HOME");
        std::string base = home ? home : ".";
        impl_->db_path = base + "/.local/share/lollypop-cpp/library.db";
    } else {
        impl_->db_path = db_path;
    }

    if (impl_->db_path != ":memory:") {
        std::filesystem::path dir = std::filesystem::path(impl_->db_path).parent_path();
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(impl_->db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(impl_->db) << std::endl;
        return false;
    }

    sqlite3_exec(impl_->db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(impl_->db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);

    sqlite3_stmt* version_stmt = nullptr;
    int current_version = 0;
    if (sqlite3_prepare_v2(impl_->db, "PRAGMA user_version;", -1,
                           &version_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(version_stmt) == SQLITE_ROW) {
            current_version = sqlite3_column_int(version_stmt, 0);
        }
        sqlite3_finalize(version_stmt);
    }
    const bool schema_changed = current_version != impl_->schema_version;
    if (!migrate_schema(current_version, impl_->schema_version)) return false;
    if (!create_schema()) return false;
    if (!create_fts5_index()) return false;
    if (!create_triggers()) return false;

    // Rebuilding the FTS index is only needed after a schema change; doing it
    // on every launch makes startup scale with library size.
    if (schema_changed) {
        backfill_library_index(impl_->db);
    }

    return true;
}

void StorageManager::shutdown() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
    std::lock_guard<std::mutex> lock(impl_->ro_mutex);
    for (sqlite3* ro_db : impl_->read_only_connections) {
        if (ro_db) sqlite3_close(ro_db);
    }
    impl_->read_only_connections.clear();
}

bool StorageManager::migrate_schema(int from_version, int to_version) {
    if (from_version >= to_version) return true;

    if (from_version < 1) {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS tracks (
                id TEXT PRIMARY KEY,
                title TEXT NOT NULL,
                artist TEXT NOT NULL,
                album TEXT,
                album_artist TEXT,
                genre TEXT,
                track_number INTEGER,
                disc_number INTEGER,
                year INTEGER,
                duration REAL,
                file_path TEXT UNIQUE NOT NULL,
                artwork_path TEXT,
                liked INTEGER DEFAULT 0,
                play_count INTEGER DEFAULT 0,
                last_played INTEGER DEFAULT 0,
                date_added INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS albums (
                id TEXT PRIMARY KEY,
                title TEXT NOT NULL,
                artist TEXT NOT NULL,
                year INTEGER,
                artwork_path TEXT,
                date_added INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS artists (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL UNIQUE,
                artwork_path TEXT,
                date_added INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS playlists (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                date_created INTEGER DEFAULT 0,
                date_modified INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS playlist_tracks (
                playlist_id TEXT NOT NULL,
                track_id TEXT NOT NULL,
                position INTEGER NOT NULL,
                PRIMARY KEY (playlist_id, track_id),
                FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
                FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS album_tracks (
                album_id TEXT NOT NULL,
                track_id TEXT NOT NULL,
                PRIMARY KEY (album_id, track_id),
                FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE,
                FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS artist_albums (
                artist_id TEXT NOT NULL,
                album_id TEXT NOT NULL,
                PRIMARY KEY (artist_id, album_id),
                FOREIGN KEY (artist_id) REFERENCES artists(id) ON DELETE CASCADE,
                FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS track_genres (
                track_id TEXT NOT NULL,
                genre TEXT NOT NULL,
                PRIMARY KEY (track_id, genre),
                FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS genre_edges (
                parent TEXT NOT NULL,
                child TEXT NOT NULL,
                PRIMARY KEY (parent, child)
            );
            CREATE TABLE IF NOT EXISTS lyrics (
                track_id TEXT PRIMARY KEY,
                text TEXT NOT NULL,
                synced INTEGER DEFAULT 0,
                FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
            );
            CREATE TABLE IF NOT EXISTS preferences (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);
            CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album);
            CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);
            CREATE INDEX IF NOT EXISTS idx_tracks_date_added ON tracks(date_added);
            CREATE INDEX IF NOT EXISTS idx_tracks_liked ON tracks(liked);
            CREATE INDEX IF NOT EXISTS idx_tracks_album_artist ON tracks(album_artist);
            CREATE INDEX IF NOT EXISTS idx_playlist_tracks_position ON playlist_tracks(playlist_id, position);
        )";
        char* error = nullptr;
        if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "Schema migration failed: " << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
            return false;
        }
    }

    if (from_version < 2) {
        const char* sql = R"(
            CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);
            CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album);
            CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);
            CREATE INDEX IF NOT EXISTS idx_tracks_date_added ON tracks(date_added);
            CREATE INDEX IF NOT EXISTS idx_tracks_liked ON tracks(liked);
            CREATE INDEX IF NOT EXISTS idx_tracks_album_artist ON tracks(album_artist);
            CREATE INDEX IF NOT EXISTS idx_playlist_tracks_position ON playlist_tracks(playlist_id, position);
        )";
        char* error = nullptr;
        if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "Schema migration v2 failed: " << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
            return false;
        }
    }

    if (from_version < 3) {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS genre_edges (
                parent TEXT NOT NULL,
                child TEXT NOT NULL,
                PRIMARY KEY (parent, child)
            );
            CREATE INDEX IF NOT EXISTS idx_genre_edges_parent ON genre_edges(parent);
            CREATE INDEX IF NOT EXISTS idx_genre_edges_child ON genre_edges(child);
        )";
        char* error = nullptr;
        if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "Schema migration v3 failed: " << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
            return false;
        }
    }

    if (from_version < 4) {
        const char* sql = R"(
            ALTER TABLE tracks ADD COLUMN embedded_lyrics TEXT;
        )";
        char* error = nullptr;
        if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "Schema migration v4 failed: " << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
            return false;
        }
    }

    if (from_version < 5) {
        const char* sql = R"(
            ALTER TABLE albums ADD COLUMN favorite INTEGER DEFAULT 0;
            ALTER TABLE playlists ADD COLUMN is_smart INTEGER DEFAULT 0;
            ALTER TABLE playlists ADD COLUMN smart_criteria TEXT DEFAULT '';
        )";
        char* error = nullptr;
        if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "Schema migration v5 failed: " << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
            return false;
        }
    }

    if (from_version < 6) {
        // v1 already creates artists.artwork_path. Adding it unconditionally
        // breaks fresh databases, so only migrate older schemas that need it.
        auto has_column = [](sqlite3* db, const char* table, const char* column) {
            const std::string pragma = std::string("PRAGMA table_info(") + table + ");";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return false;
            }
            bool found = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name && std::string(reinterpret_cast<const char*>(name)) == column) {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
            return found;
        };

        if (!has_column(impl_->db, "artists", "artwork_path")) {
            const char* sql = R"(
                ALTER TABLE artists ADD COLUMN artwork_path TEXT;
            )";
            char* error = nullptr;
            if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
                std::cerr << "Schema migration v6 failed: "
                          << (error ? error : "unknown") << std::endl;
                sqlite3_free(error);
                return false;
            }
        }
    }

    if (sqlite3_exec(impl_->db,
                     ("PRAGMA user_version = " + std::to_string(to_version) + ";").c_str(),
                     nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }
    impl_->schema_version = to_version;
    return true;
}

bool StorageManager::create_schema() {
    return true;
}

bool StorageManager::create_fts5_index() {
    // FTS5 tokenizer configuration - use default tokenizer with prefix support.
    // unicode61/porter may not be available on all SQLite builds.
    const char* sql = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
            title, artist, album, album_artist, genre,
            content='tracks', content_rowid='rowid',
            prefix='2 3'
        );
    )";
    char* error = nullptr;
    if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::cerr << "FTS5 index creation failed: " << (error ? error : "unknown") << std::endl;
        sqlite3_free(error);
        return false;
    }
    return true;
}

bool StorageManager::create_triggers() {
    const char* sql = R"(
        CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
            INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre)
            VALUES (new.rowid, new.title, new.artist, new.album, new.album_artist, new.genre);
        END;
        CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre)
            VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.album_artist, old.genre);
        END;
        CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre)
            VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.album_artist, old.genre);
            INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre)
            VALUES (new.rowid, new.title, new.artist, new.album, new.album_artist, new.genre);
        END;
    )";
    char* error = nullptr;
    if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::cerr << "Trigger creation failed: " << (error ? error : "unknown") << std::endl;
        sqlite3_free(error);
        return false;
    }
    return true;
}

Track StorageManager::row_to_track(sqlite3_stmt* stmt) {
    Track track;
    track.id = column_text(stmt, 0);
    track.title = column_text(stmt, 1);
    track.artist = column_text(stmt, 2);
    track.album = column_text(stmt, 3);
    track.album_artist = column_text(stmt, 4);
    track.genre = column_text(stmt, 5);
    track.track_number = sqlite3_column_int(stmt, 6);
    track.disc_number = sqlite3_column_int(stmt, 7);
    track.year = sqlite3_column_int(stmt, 8);
    track.duration = sqlite3_column_double(stmt, 9);
    track.file_path = column_text(stmt, 10);
    track.artwork_path = column_text(stmt, 11);
    track.embedded_lyrics = column_text(stmt, 12);
    track.liked = sqlite3_column_int(stmt, 13) != 0;
    track.play_count = sqlite3_column_int64(stmt, 14);
    track.last_played = sqlite3_column_int64(stmt, 15);
    track.date_added = sqlite3_column_int64(stmt, 16);
    return track;
}

void StorageManager::backfill_library_index(sqlite3* db) {
    auto run = [db](const char* sql) {
        char* error = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::cerr << "StorageManager: backfill failed: "
                      << (error ? error : "unknown") << std::endl;
            sqlite3_free(error);
        }
    };

    run(R"(
        INSERT OR IGNORE INTO artists (id, name, date_added)
        SELECT lower(hex(randomblob(16))), artist, strftime('%s','now')
        FROM tracks WHERE artist IS NOT NULL AND artist != '';
    )");
    run(R"(
        INSERT OR IGNORE INTO artists (id, name, date_added)
        SELECT lower(hex(randomblob(16))), album_artist, strftime('%s','now')
        FROM tracks WHERE album_artist IS NOT NULL AND album_artist != '';
    )");
    run(R"(
        INSERT INTO albums (id, title, artist, year, artwork_path, date_added)
        SELECT lower(hex(randomblob(16))), t.album,
               COALESCE(NULLIF(t.album_artist, ''), t.artist),
               MAX(t.year), MAX(t.artwork_path), strftime('%s','now')
        FROM tracks t
        WHERE t.album IS NOT NULL AND t.album != ''
          AND NOT EXISTS (
              SELECT 1 FROM albums a
              WHERE a.title = t.album
                AND a.artist = COALESCE(NULLIF(t.album_artist, ''), t.artist))
        GROUP BY t.album, COALESCE(NULLIF(t.album_artist, ''), t.artist);
    )");
    run(R"(
        INSERT OR IGNORE INTO album_tracks (album_id, track_id)
        SELECT a.id, t.id FROM tracks t
        JOIN albums a
          ON a.title = t.album
         AND a.artist = COALESCE(NULLIF(t.album_artist, ''), t.artist)
        WHERE t.album IS NOT NULL AND t.album != '';
    )");
    run(R"(
        INSERT OR IGNORE INTO artist_albums (artist_id, album_id)
        SELECT ar.id, a.id FROM albums a
        JOIN artists ar ON ar.name = a.artist;
    )");
}

// Helper to ensure artist, album, and junction tables are updated for a track
// Returns the album ID (existing or newly created)
std::string StorageManager::ensure_album_artist_relationships(const Track& track) {
    if (track.album.empty()) return "";

    // Determine album artist: use album_artist if set, otherwise track artist
    std::string album_artist = track.album_artist.empty() ? track.artist : track.album_artist;
    if (album_artist.empty()) return "";

    sqlite3* db = impl_->db;
    std::string album_id;
    int64_t now = now_epoch_seconds();

    // 1. Ensure artist exists
    std::string artist_id;
    {
        const char* sql = "SELECT id FROM artists WHERE name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, album_artist.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                artist_id = column_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (artist_id.empty()) {
            // Create new artist
            artist_id = "art_" + std::to_string(now) + "_" + std::to_string(rand());
            const char* sql = "INSERT INTO artists (id, name, date_added) VALUES (?, ?, ?);";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, album_artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 3, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // 2. Ensure album exists (match by title + artist)
    {
        const char* sql = "SELECT id FROM albums WHERE title = ? AND artist = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, track.album.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, album_artist.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                album_id = column_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (album_id.empty()) {
            // Create new album
            album_id = "alb_" + std::to_string(now) + "_" + std::to_string(rand());
            const char* sql = "INSERT INTO albums (id, title, artist, year, artwork_path, date_added) VALUES (?, ?, ?, ?, ?, ?);";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, track.album.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, album_artist.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 4, track.year);
                sqlite3_bind_text(stmt, 5, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 6, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    if (album_id.empty()) return "";

    // 3. Link track to album in album_tracks
    {
        const char* sql = "INSERT OR IGNORE INTO album_tracks (album_id, track_id) VALUES (?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, track.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // 4. Link album to artist in artist_albums
    if (!artist_id.empty()) {
        const char* sql = "INSERT OR IGNORE INTO artist_albums (artist_id, album_id) VALUES (?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, album_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return album_id;
}

bool StorageManager::insert_track(const Track& track) {
    const char* sql = R"(
        INSERT INTO tracks (id, title, artist, album, album_artist, genre,
                            track_number, disc_number, year, duration,
                            file_path, artwork_path, embedded_lyrics, liked, play_count,
                            last_played, date_added)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(file_path) DO UPDATE SET
            title=excluded.title,
            artist=excluded.artist,
            album=excluded.album,
            album_artist=excluded.album_artist,
            genre=excluded.genre,
            track_number=excluded.track_number,
            disc_number=excluded.disc_number,
            year=excluded.year,
            duration=excluded.duration,
            artwork_path=excluded.artwork_path,
            embedded_lyrics=excluded.embedded_lyrics
        -- Do NOT update: id, liked, play_count, last_played, date_added
        ;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, track.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, track.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, track.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, track.track_number);
    sqlite3_bind_int(stmt, 8, track.disc_number);
    sqlite3_bind_int(stmt, 9, track.year);
    sqlite3_bind_double(stmt, 10, track.duration);
    sqlite3_bind_text(stmt, 11, track.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, track.embedded_lyrics.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, track.liked ? 1 : 0);
    sqlite3_bind_int64(stmt, 15, track.play_count);
    sqlite3_bind_int64(stmt, 16, track.last_played);
    sqlite3_bind_int64(stmt, 17, track.date_added);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        ensure_album_artist_relationships(track);
    }
    return rc == SQLITE_DONE;
}

bool StorageManager::insert_tracks_batch(const std::vector<Track>& tracks) {
    if (tracks.empty()) return true;

    const char* sql = R"(
        INSERT INTO tracks (id, title, artist, album, album_artist, genre,
                            track_number, disc_number, year, duration,
                            file_path, artwork_path, embedded_lyrics, liked, play_count,
                            last_played, date_added)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(file_path) DO UPDATE SET
            title=excluded.title,
            artist=excluded.artist,
            album=excluded.album,
            album_artist=excluded.album_artist,
            genre=excluded.genre,
            track_number=excluded.track_number,
            disc_number=excluded.disc_number,
            year=excluded.year,
            duration=excluded.duration,
            artwork_path=excluded.artwork_path,
            embedded_lyrics=excluded.embedded_lyrics
        -- Do NOT update: id, liked, play_count, last_played, date_added
        ;
    )";

    sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& track : tracks) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(stmt, 1, track.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, track.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, track.genre.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, track.track_number);
        sqlite3_bind_int(stmt, 8, track.disc_number);
        sqlite3_bind_int(stmt, 9, track.year);
        sqlite3_bind_double(stmt, 10, track.duration);
        sqlite3_bind_text(stmt, 11, track.file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 13, track.embedded_lyrics.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 14, track.liked ? 1 : 0);
        sqlite3_bind_int64(stmt, 15, track.play_count);
        sqlite3_bind_int64(stmt, 16, track.last_played);
        sqlite3_bind_int64(stmt, 17, track.date_added);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(impl_->db, success ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    if (success) {
        for (const auto& track : tracks) {
            ensure_album_artist_relationships(track);
        }
    }
    return success;
}

bool StorageManager::delete_track(const std::string& track_id) {
    const char* sql = "DELETE FROM tracks WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Track> StorageManager::get_all_tracks() {
    std::vector<Track> tracks;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks ORDER BY date_added DESC;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_liked_tracks() {
    std::vector<Track> tracks;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE liked = 1 ORDER BY date_added DESC;";
    const char* sql = sql_str.c_str();
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
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks ORDER BY date_added DESC LIMIT ?;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_recently_played(int limit) {
    std::vector<Track> tracks;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT ?;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

Track StorageManager::get_track(const std::string& track_id) {
    Track track;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE id = ?;";
    const char* sql = sql_str.c_str();
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

Track StorageManager::get_track_by_path(const std::string& file_path) {
    Track track;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE file_path = ?;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            track = row_to_track(stmt);
        }
        sqlite3_finalize(stmt);
    }
    return track;
}

std::vector<Track> StorageManager::get_tracks_by_artist(const std::string& artist_name) {
    std::vector<Track> tracks;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE artist = ? ORDER BY album, track_number;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_tracks_by_genre(const std::string& genre) {
    std::vector<Track> tracks;
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks t JOIN track_genres tg ON t.id = tg.track_id WHERE tg.genre = ? ORDER BY t.artist, t.album, t.track_number;";
    const char* sql = sql_str.c_str();
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

std::vector<Track> StorageManager::get_smart_playlist_tracks(const std::string& playlist_id) {
    std::vector<Track> tracks;
    // Join playlist_tracks and ORDER BY playlist_tracks.position
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified +
        " FROM tracks t JOIN playlist_tracks pt ON t.id = pt.track_id "
        "WHERE pt.playlist_id = ? ORDER BY pt.position;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::search_tracks_fts(const std::string& query) {
    std::vector<Track> tracks;
    std::string fts_query = build_fts_query(query);
    if (fts_query.empty()) return tracks;

    // Join FTS table explicitly: tracks.rowid = tracks_fts.rowid
    // Use alias for MATCH and bm25() to avoid "no such column" error
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified +
        " FROM tracks t JOIN tracks_fts f ON t.rowid = f.rowid "
        "WHERE f MATCH ? ORDER BY bm25(f);";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_tracks_by_ids(const std::vector<std::string>& track_ids) {
    std::vector<Track> tracks;
    if (track_ids.empty()) return tracks;

    // Stay well below the smallest historical SQLite bind limit (999). The
    // effective limit is queried at runtime so unusual builds are safe too.
    const int variable_limit = sqlite3_limit(impl_->db, SQLITE_LIMIT_VARIABLE_NUMBER, -1);
    const size_t chunk_size = static_cast<size_t>(
        std::max(1, std::min(variable_limit, 500)));

    // Query in chunks, then reassemble in input order. Duplicate and missing
    // IDs behave exactly as before: duplicates repeat, missing IDs are skipped.
    std::unordered_map<std::string, Track> by_id;
    for (size_t offset = 0; offset < track_ids.size(); offset += chunk_size) {
        const size_t end = std::min(offset + chunk_size, track_ids.size());

        std::string sql = "WITH input_ids(id, ord) AS (VALUES ";
        for (size_t i = offset; i < end; ++i) {
            if (i > offset) sql += ", ";
            sql += "(?, " + std::to_string(i - offset) + ")";
        }
        // kTrackColumnsQualified prefixes columns with `tracks`, so do not
        // alias the table here.
        sql += ") SELECT " + std::string(kTrackColumnsQualified) +
               " FROM tracks JOIN input_ids i ON tracks.id = i.id ORDER BY i.ord;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "StorageManager: get_tracks_by_ids() prepare failed for "
                      << (end - offset) << " ids: "
                      << sqlite3_errmsg(impl_->db) << std::endl;
            continue;
        }

        int rc = SQLITE_OK;
        for (size_t i = offset; i < end && rc == SQLITE_OK; ++i) {
            rc = sqlite3_bind_text(stmt, static_cast<int>(i - offset + 1),
                                   track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK) {
            std::cerr << "StorageManager: get_tracks_by_ids() bind failed: "
                      << sqlite3_errmsg(impl_->db) << std::endl;
        } else {
            while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                Track track = row_to_track(stmt);
                const std::string id = track.id;
                by_id[id] = std::move(track);
            }
            if (rc != SQLITE_DONE) {
                std::cerr << "StorageManager: get_tracks_by_ids() step failed: "
                          << sqlite3_errmsg(impl_->db) << std::endl;
            }
        }
        sqlite3_finalize(stmt);
    }

    tracks.reserve(track_ids.size());
    for (const auto& id : track_ids) {
        auto it = by_id.find(id);
        if (it != by_id.end()) tracks.push_back(it->second);
    }
    return tracks;
}

std::vector<Album> StorageManager::get_all_albums() {
    std::vector<Album> albums;

    // Album membership is persisted in album_tracks when tracks are scanned.
    // Load it in one query (same join get_album() uses) instead of grouping by
    // album title: title-only grouping merges same-named albums from different
    // artists and disagrees with the album detail page.
    std::unordered_map<std::string, std::vector<std::string>> tracks_by_album_id;
    const char* track_sql =
        "SELECT at.album_id, at.track_id FROM album_tracks at "
        "JOIN tracks t ON t.id = at.track_id "
        "ORDER BY at.album_id, t.track_number;";
    sqlite3_stmt* track_stmt;
    if (sqlite3_prepare_v2(impl_->db, track_sql, -1, &track_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(track_stmt) == SQLITE_ROW) {
            tracks_by_album_id[column_text(track_stmt, 0)].push_back(
                column_text(track_stmt, 1));
        }
        sqlite3_finalize(track_stmt);
    }

    // Load all albums.
    const char* sql = "SELECT id, title, artist, year, artwork_path, date_added, favorite FROM albums ORDER BY title;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Album album;
            album.id = column_text(stmt, 0);
            album.title = column_text(stmt, 1);
            album.artist = column_text(stmt, 2);
            album.year = sqlite3_column_int(stmt, 3);
            album.artwork_path = column_text(stmt, 4);
            album.date_added = sqlite3_column_int64(stmt, 5);
            album.favorite = sqlite3_column_int(stmt, 6) != 0;

            auto it = tracks_by_album_id.find(album.id);
            if (it != tracks_by_album_id.end()) {
                album.track_ids = it->second;
            }
            albums.push_back(std::move(album));
        }
        sqlite3_finalize(stmt);
    }
    return albums;
}

std::vector<Album> StorageManager::get_albums_by_artist(const std::string& artist_id) {
    std::vector<Album> albums;
    // Resolve artist id -> name first, then join artist_albums
    std::string artist_name;
    {
        const char* sql = "SELECT name FROM artists WHERE id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                artist_name = column_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    if (artist_name.empty()) return albums;

    // Get albums via artist_albums join
    const char* sql = "SELECT a.id, a.title, a.artist, a.year, a.artwork_path, a.date_added "
                      "FROM albums a JOIN artist_albums aa ON a.id = aa.album_id "
                      "WHERE aa.artist_id = ? ORDER BY a.year, a.title;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Album album;
            album.id = column_text(stmt, 0);
            album.title = column_text(stmt, 1);
            album.artist = column_text(stmt, 2);
            album.year = sqlite3_column_int(stmt, 3);
            album.artwork_path = column_text(stmt, 4);
            album.date_added = sqlite3_column_int64(stmt, 5);
            auto tracks = get_tracks_by_album(album.id);
            for (const auto& t : tracks) album.track_ids.push_back(t.id);
            albums.push_back(std::move(album));
        }
        sqlite3_finalize(stmt);
    }
    return albums;
}

Album StorageManager::get_album(const std::string& album_id) {
    Album album;
    const char* sql = "SELECT id, title, artist, year, artwork_path, date_added FROM albums WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            album.id = column_text(stmt, 0);
            album.title = column_text(stmt, 1);
            album.artist = column_text(stmt, 2);
            album.year = sqlite3_column_int(stmt, 3);
            album.artwork_path = column_text(stmt, 4);
            album.date_added = sqlite3_column_int64(stmt, 5);
            auto tracks = get_tracks_by_album(album_id);
            for (const auto& t : tracks) album.track_ids.push_back(t.id);
        }
        sqlite3_finalize(stmt);
    }
    return album;
}

std::vector<Album> StorageManager::get_favourite_albums() {
    std::vector<Album> albums;
    const char* sql = "SELECT a.id, a.title, a.artist, a.year, a.artwork_path, a.date_added FROM albums a WHERE a.favorite = 1 ORDER BY a.title;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Album album;
            album.id = column_text(stmt, 0);
            album.title = column_text(stmt, 1);
            album.artist = column_text(stmt, 2);
            album.year = sqlite3_column_int(stmt, 3);
            album.artwork_path = column_text(stmt, 4);
            album.date_added = sqlite3_column_int64(stmt, 5);
            album.favorite = true;
            auto tracks = get_tracks_by_album(album.id);
            for (const auto& t : tracks) album.track_ids.push_back(t.id);
            albums.push_back(std::move(album));
        }
        sqlite3_finalize(stmt);
    }
    return albums;
}

std::vector<Track> StorageManager::get_tracks_by_album(const std::string& album_id) {
    std::vector<Track> tracks;
    // Join album_tracks to get tracks for this album_id (not just by title)
    std::string sql_str = std::string("SELECT ") + kTrackColumnsQualified +
        " FROM tracks t JOIN album_tracks at ON t.id = at.track_id "
        "WHERE at.album_id = ? ORDER BY t.track_number;";
    const char* sql = sql_str.c_str();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Artist> StorageManager::get_all_artists() {
    std::vector<Artist> artists;

    // Load all albums once and group by artist name to avoid N+1 queries.
    // Previously we called get_albums_by_artist() per artist (N extra queries),
    // which itself called get_tracks_by_album() per album (N*N extra queries).
    std::unordered_map<std::string, std::vector<std::string>> albums_by_artist_name;
    const char* album_sql = "SELECT id, title, artist FROM albums ORDER BY title;";
    sqlite3_stmt* album_stmt;
    if (sqlite3_prepare_v2(impl_->db, album_sql, -1, &album_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(album_stmt) == SQLITE_ROW) {
            std::string album_id = column_text(album_stmt, 0);
            std::string artist_name = column_text(album_stmt, 2);
            albums_by_artist_name[artist_name].push_back(album_id);
        }
        sqlite3_finalize(album_stmt);
    }

    // Load all artists.
    const char* sql = "SELECT id, name, artwork_path, date_added FROM artists ORDER BY name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Artist artist;
            artist.id = column_text(stmt, 0);
            artist.name = column_text(stmt, 1);
            artist.artwork_path = column_text(stmt, 2);
            artist.date_added = sqlite3_column_int64(stmt, 3);

            // Look up albums already loaded in memory (matched by artist name).
            auto it = albums_by_artist_name.find(artist.name);
            if (it != albums_by_artist_name.end()) {
                for (const auto& aid : it->second) {
                    artist.album_ids.push_back(aid);
                }
            }
            artists.push_back(std::move(artist));
        }
        sqlite3_finalize(stmt);
    }
    return artists;
}

Artist StorageManager::get_artist(const std::string& artist_id) {
    Artist artist;
    const char* sql = "SELECT id, name, artwork_path, date_added FROM artists WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, artist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            artist.id = column_text(stmt, 0);
            artist.name = column_text(stmt, 1);
            artist.artwork_path = column_text(stmt, 2);
            artist.date_added = sqlite3_column_int64(stmt, 3);
            auto albums = get_albums_by_artist(artist.id);
            for (const auto& a : albums) artist.album_ids.push_back(a.id);
        }
        sqlite3_finalize(stmt);
    }
    return artist;
}

bool StorageManager::update_artist_artwork(const std::string& artist_id, const std::string& artwork_path) {
    const char* sql = "UPDATE artists SET artwork_path = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_track_artwork(const std::string& track_id,
                                          const std::string& artwork_path) {
    const char* sql = "UPDATE tracks SET artwork_path = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_album_artwork(const std::string& album_id,
                                          const std::string& artwork_path) {
    const char* sql = "UPDATE albums SET artwork_path = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, album_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Playlist> StorageManager::get_all_playlists() {
    std::vector<Playlist> playlists;
    const char* sql = "SELECT id, name, date_created, date_modified FROM playlists ORDER BY name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Playlist pl;
            pl.id = column_text(stmt, 0);
            pl.name = column_text(stmt, 1);
            pl.date_created = sqlite3_column_int64(stmt, 2);
            pl.date_modified = sqlite3_column_int64(stmt, 3);
            pl.track_ids.clear();
            auto tracks = get_smart_playlist_tracks(pl.id);
            for (const auto& t : tracks) pl.track_ids.push_back(t.id);
            playlists.push_back(std::move(pl));
        }
        sqlite3_finalize(stmt);
    }
    return playlists;
}

Playlist StorageManager::get_playlist(const std::string& playlist_id) {
    Playlist pl;
    const char* sql = "SELECT id, name, date_created, date_modified FROM playlists WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pl.id = column_text(stmt, 0);
            pl.name = column_text(stmt, 1);
            pl.date_created = sqlite3_column_int64(stmt, 2);
            pl.date_modified = sqlite3_column_int64(stmt, 3);
            pl.track_ids.clear();
            auto tracks = get_smart_playlist_tracks(pl.id);
            for (const auto& t : tracks) pl.track_ids.push_back(t.id);
        }
        sqlite3_finalize(stmt);
    }
    return pl;
}

std::vector<std::string> StorageManager::get_all_genres() {
    std::vector<std::string> genres;
    const char* sql = "SELECT DISTINCT genre FROM track_genres ORDER BY genre;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            genres.push_back(column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return genres;
}

std::vector<std::string> StorageManager::get_genre_descendants(const std::string& genre) {
    std::vector<std::string> descendants;
    const char* sql = "SELECT child FROM genre_edges WHERE parent = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, genre.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            descendants.push_back(column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return descendants;
}

std::vector<Track> StorageManager::get_tracks_paginated(int limit,
                                                         const std::string& sort_key,
                                                         const std::string& last_sort_value,
                                                         const std::string& last_id,
                                                         const std::string& genre_filter) {
    static const std::unordered_map<std::string, std::string> valid_sort_keys = {
        {"date_added", "date_added"},
        {"title", "title"},
        {"artist", "artist"},
        {"album", "album"},
        {"duration", "duration"},
        {"play_count", "play_count"},
        {"last_played", "last_played"},
        {"year", "year"},
        {"track_number", "track_number"},
        {"genre", "genre"},
        {"liked", "liked"},
    };

    auto it = valid_sort_keys.find(sort_key);
    if (it == valid_sort_keys.end()) {
        it = valid_sort_keys.find("date_added");
    }
    const std::string sort_column = it->second;
    const bool is_numeric =
        (sort_column == "date_added" || sort_column == "duration" ||
         sort_column == "play_count" || sort_column == "last_played" ||
         sort_column == "year" || sort_column == "track_number" ||
         sort_column == "liked");
    // Qualify the column so a genre-filtered join cannot make "genre"
    // ambiguous between tracks and track_genres.
    const std::string column = "tracks." + sort_column;

    std::vector<Track> tracks;
    // limit == 0 returns nothing; a negative limit maps to SQLite's LIMIT -1
    // (unbounded), which callers use for full-library sorted ID lists.
    if (limit == 0) return tracks;

    const std::string cols = kTrackColumnsQualified;
    std::string sql;

    std::string from_clause = "FROM tracks";
    if (!genre_filter.empty()) {
        from_clause += " JOIN track_genres tg ON tracks.id = tg.track_id";
    }

    if (last_id.empty()) {
        sql = "SELECT " + cols + " " + from_clause;
        if (!genre_filter.empty()) {
            sql += " WHERE tg.genre = ?";
        }
        sql += " ORDER BY " + column + ", id LIMIT ?;";
    } else {
        std::string where_clause;
        if (!genre_filter.empty()) {
            where_clause = "WHERE tg.genre = ? AND (";
        } else {
            where_clause = "WHERE (";
        }
        where_clause += column + " > ? OR (" + column + " = ? AND id > ?))";

        sql = "SELECT " + cols + " FROM tracks";
        if (!genre_filter.empty()) {
            sql += " JOIN track_genres tg ON tracks.id = tg.track_id";
        }
        sql += " " + where_clause + " ORDER BY " + column + ", id LIMIT ?;";
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "StorageManager: paginated query failed: "
                  << sqlite3_errmsg(impl_->db) << std::endl;
        return tracks;
    }

    int bind_idx = 1;
    if (!genre_filter.empty()) {
        sqlite3_bind_text(stmt, bind_idx++, genre_filter.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!last_id.empty()) {
        if (is_numeric) {
            long long sort_val = 0;
            try {
                sort_val = std::stoll(last_sort_value);
            } catch (...) {
                sort_val = 0;
            }
            sqlite3_bind_int64(stmt, bind_idx++, sort_val);
            sqlite3_bind_int64(stmt, bind_idx++, sort_val);
        } else {
            sqlite3_bind_text(stmt, bind_idx++, last_sort_value.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bind_idx++, last_sort_value.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, bind_idx++, last_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bind_idx, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(row_to_track(stmt));
    }
    sqlite3_finalize(stmt);
    return tracks;
}

bool StorageManager::toggle_like(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET liked = NOT liked WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::is_track_liked(const std::string& track_id) {
    const char* sql = "SELECT liked FROM tracks WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    bool liked = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        liked = sqlite3_column_int(stmt, 0) != 0;
    }
    sqlite3_finalize(stmt);
    return liked;
}

bool StorageManager::toggle_favorite_album(const std::string& album_id) {
    const char* sql = "UPDATE albums SET favorite = NOT favorite WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::is_album_favorited(const std::string& album_id) {
    const char* sql = "SELECT favorite FROM albums WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
    bool favorite = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        favorite = sqlite3_column_int(stmt, 0) != 0;
    }
    sqlite3_finalize(stmt);
    return favorite;
}

bool StorageManager::add_genre_edge(const std::string& parent, const std::string& child) {
    if (parent == child) return false;
    const char* sql = "INSERT OR IGNORE INTO genre_edges (parent, child) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, parent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, child.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

namespace {

int count_table_rows(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

}  // namespace

int StorageManager::get_track_count() const {
    return count_table_rows(impl_->db, "SELECT COUNT(*) FROM tracks;");
}

int StorageManager::get_album_count() const {
    return count_table_rows(impl_->db, "SELECT COUNT(*) FROM albums;");
}

int StorageManager::get_artist_count() const {
    return count_table_rows(impl_->db, "SELECT COUNT(*) FROM artists;");
}

std::vector<std::string> StorageManager::get_library_roots() const {
    std::vector<std::string> roots;
    // First, try to get stored library roots from preferences
    const char* sql = "SELECT value FROM preferences WHERE key LIKE 'library_root_%' ORDER BY key;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string path = column_text(stmt, 0);
            if (!path.empty() &&
                std::find(roots.begin(), roots.end(), path) == roots.end()) {
                roots.push_back(path);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Fallback: derive from tracks if no stored roots
    if (roots.empty()) {
        const char* fallback_sql = "SELECT DISTINCT file_path FROM tracks;";
        if (sqlite3_prepare_v2(impl_->db, fallback_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string path = column_text(stmt, 0);
                auto pos = path.find_last_of('/');
                if (pos != std::string::npos) {
                    path = path.substr(0, pos);
                }
                if (!path.empty() &&
                    std::find(roots.begin(), roots.end(), path) == roots.end()) {
                    roots.push_back(path);
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    return roots;
}

bool StorageManager::set_preference(const std::string& key, const std::string& value) {
    const char* sql = "INSERT OR REPLACE INTO preferences (key, value) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<std::string> StorageManager::get_preference(const std::string& key) const {
    const char* sql = "SELECT value FROM preferences WHERE key = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = column_text(stmt, 0);
        if (!value.empty()) result = value;
    }
    sqlite3_finalize(stmt);
    return result;
}

sqlite3* StorageManager::open_read_only() {
    sqlite3* ro_db = nullptr;
    int rc = sqlite3_open_v2(impl_->db_path.c_str(), &ro_db,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    if (rc != SQLITE_OK) {
        if (ro_db) sqlite3_close(ro_db);
        return nullptr;
    }
    sqlite3_exec(ro_db, "PRAGMA query_only = ON;", nullptr, nullptr, nullptr);
    std::lock_guard<std::mutex> lock(impl_->ro_mutex);
    impl_->read_only_connections.push_back(ro_db);
    return ro_db;
}

void StorageManager::close_read_only(sqlite3* ro_db) {
    if (!ro_db) return;
    std::lock_guard<std::mutex> lock(impl_->ro_mutex);
    auto it = std::find(impl_->read_only_connections.begin(), impl_->read_only_connections.end(), ro_db);
    if (it != impl_->read_only_connections.end()) {
        impl_->read_only_connections.erase(it);
        sqlite3_close(ro_db);
    }
}

StorageManager::ReadOnlyConnection StorageManager::open_read_only_connection() {
    return ReadOnlyConnection(this, open_read_only());
}

bool StorageManager::integrity_check() {
    const char* sql = "PRAGMA integrity_check;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        ok = result && std::string(result) == "ok";
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool StorageManager::quick_check() {
    const char* sql = "PRAGMA quick_check;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        ok = result && std::string(result) == "ok";
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool StorageManager::backup_database(const std::string& backup_path) {
    sqlite3* dest = nullptr;
    int rc = sqlite3_open(backup_path.c_str(), &dest);
    if (rc != SQLITE_OK) return false;
    sqlite3_backup* backup = sqlite3_backup_init(dest, "main", impl_->db, "main");
    if (!backup) {
        sqlite3_close(dest);
        return false;
    }
    sqlite3_backup_step(backup, -1);
    rc = sqlite3_backup_finish(backup);
    sqlite3_close(dest);
    return rc == SQLITE_OK;
}

bool StorageManager::vacuum() {
    const char* sql = "VACUUM;";
    char* error = nullptr;
    int rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::cerr << "VACUUM failed: " << (error ? error : "unknown") << std::endl;
        sqlite3_free(error);
        return false;
    }
    return true;
}

bool StorageManager::set_lyrics(const std::string& track_id, const std::string& text, bool synced, const std::string& source) {
    const char* sql = "INSERT OR REPLACE INTO lyrics (track_id, text, synced) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, synced ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<Lyrics> StorageManager::get_lyrics(const std::string& track_id) const {
    const char* sql = "SELECT text, synced FROM lyrics WHERE track_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Lyrics> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Lyrics lyrics;
        lyrics.text = column_text(stmt, 0);
        lyrics.synced = sqlite3_column_int(stmt, 1) != 0;
        result = lyrics;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool StorageManager::delete_lyrics(const std::string& track_id) {
    const char* sql = "DELETE FROM lyrics WHERE track_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// --- Missing methods for BackendController compatibility ---

bool StorageManager::add_track(const Track& track) {
    return insert_track(track);
}

bool StorageManager::add_library_root(const std::string& path) {
    // Store library root in preferences
    std::string key = "library_root_" + path;
    return set_preference(key, path);
}

bool StorageManager::remove_library_root(const std::string& path) {
    std::string key = "library_root_" + path;
    const char* sql = "DELETE FROM preferences WHERE key = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::is_library_root(const std::string& path) const {
    std::string key = "library_root_" + path;
    const char* sql = "SELECT 1 FROM preferences WHERE key = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool StorageManager::delete_tracks_under_path(const std::string& directory_path) {
    // Normalize path: ensure it ends with a separator
    std::string normalized = directory_path;
    if (!normalized.empty() && normalized.back() != '/') {
        normalized += '/';
    }
    
    // Escape special LIKE characters (% and _)
    std::string pattern;
    pattern.reserve(normalized.size() + 2);
    for (char c : normalized) {
        if (c == '%' || c == '_' || c == '\\') {
            pattern += '\\';
        }
        pattern += c;
    }
    pattern += '%';

    const char* sql = "DELETE FROM tracks WHERE file_path LIKE ? ESCAPE '\\';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::delete_track_by_path(const std::string& file_path) {
    const char* sql = "DELETE FROM tracks WHERE file_path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Track> StorageManager::search_tracks(const std::string& query) {
    return search_tracks_fts(query);
}

std::vector<Track> StorageManager::get_tracks_by_genre_hierarchy(const std::string& genre) {
    std::vector<std::string> genres = get_genre_descendants(genre);
    genres.push_back(genre);
    
    std::vector<Track> all_tracks;
    for (const auto& g : genres) {
        auto tracks = get_tracks_by_genre(g);
        all_tracks.insert(all_tracks.end(), tracks.begin(), tracks.end());
    }
    return all_tracks;
}

bool StorageManager::create_playlist(const std::string& name) {
    const char* sql = "INSERT INTO playlists (id, name, date_created, date_modified) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    std::string id = lower_hex_randomblob(16);
    int64_t now = now_epoch_seconds();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::rename_playlist(const std::string& playlist_id,
                                     const std::string& name) {
    if (name.empty()) return false;
    const char* sql =
        "UPDATE playlists SET name = ?, date_modified = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now_epoch_seconds());
    sqlite3_bind_text(stmt, 3, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(impl_->db) > 0;
}

bool StorageManager::delete_playlist(const std::string& playlist_id) {
    const char* sql = "DELETE FROM playlists WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::add_track_to_playlist(const std::string& playlist_id, const std::string& track_id) {
    const char* sql = "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position) VALUES (?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_tracks WHERE playlist_id = ?));";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id) {
    const char* sql = "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::create_smart_playlist(const std::string& name, const std::string& criteria) {
    const char* sql = "INSERT INTO playlists (id, name, is_smart, smart_criteria, date_created, date_modified) VALUES (?, ?, 1, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    std::string id = lower_hex_randomblob(16);
    int64_t now = now_epoch_seconds();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, criteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int64(stmt, 5, now);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_smart_playlist(const std::string& playlist_id, const std::string& name, const std::string& criteria) {
    const char* sql = "UPDATE playlists SET name = ?, smart_criteria = ?, date_modified = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    int64_t now = now_epoch_seconds();
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, criteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_text(stmt, 4, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::rebuild_smart_playlists() {
    auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string{};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    };

    struct Rule {
        std::string sql;
        std::string value;
        bool bind = true;
    };

    std::vector<std::pair<std::string, std::string>> smart_playlists;
    sqlite3_stmt* list_stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db,
                           "SELECT id, smart_criteria FROM playlists "
                           "WHERE is_smart = 1;",
                           -1, &list_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(list_stmt) == SQLITE_ROW) {
            smart_playlists.emplace_back(column_text(list_stmt, 0),
                                          column_text(list_stmt, 1));
        }
        sqlite3_finalize(list_stmt);
    }
    if (smart_playlists.empty()) return true;

    if (sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr,
                     nullptr) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    for (const auto& [playlist_id, criteria] : smart_playlists) {
        std::vector<Rule> rules;
        int limit = 100000;

        std::stringstream criteria_stream(criteria);
        std::string item;
        while (std::getline(criteria_stream, item, ';')) {
            const auto equals = item.find('=');
            if (equals == std::string::npos) continue;
            const std::string key = lower(trim(item.substr(0, equals)));
            const std::string value = trim(item.substr(equals + 1));
            if (value.empty()) continue;

            if (key == "genre") {
                rules.push_back({"tracks.genre = ?", value, true});
            } else if (key == "artist") {
                rules.push_back({"tracks.artist = ?", value, true});
            } else if (key == "album") {
                rules.push_back({"tracks.album = ?", value, true});
            } else if (key == "title") {
                rules.push_back({"tracks.title LIKE ?", "%" + value + "%", true});
            } else if (key == "liked") {
                rules.push_back({lower(value) == "true" || value == "1"
                                     ? "tracks.liked = 1"
                                     : "tracks.liked = 0",
                                     {}, false});
            } else if (key == "min_year") {
                try {
                    rules.push_back({"tracks.year >= ?", value, true});
                } catch (...) {}
            } else if (key == "max_year") {
                rules.push_back({"tracks.year <= ?", value, true});
            } else if (key == "min_play_count") {
                rules.push_back({"tracks.play_count >= ?", value, true});
            } else if (key == "limit") {
                try {
                    limit = std::clamp(std::stoi(value), 1, 100000);
                } catch (...) {}
            }
        }

        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(impl_->db,
                                   "DELETE FROM playlist_tracks WHERE playlist_id = ?;",
                                   -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1,
                                  SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) != SQLITE_DONE) success = false;
                sqlite3_finalize(stmt);
            } else {
                success = false;
            }
        }
        if (!success) break;

        std::string select_sql = "SELECT t.id FROM tracks t";
        if (!rules.empty()) {
            select_sql += " WHERE ";
            for (size_t i = 0; i < rules.size(); ++i) {
                if (i > 0) select_sql += " AND ";
                select_sql += rules[i].sql;
            }
        }
        select_sql += " ORDER BY t.date_added DESC LIMIT " +
                      std::to_string(limit) + ";";

        std::vector<std::string> track_ids;
        sqlite3_stmt* select_stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, select_sql.c_str(), -1, &select_stmt,
                               nullptr) == SQLITE_OK) {
            int bind_index = 1;
            for (const auto& rule : rules) {
                if (!rule.bind) continue;
                sqlite3_bind_text(select_stmt, bind_index++, rule.value.c_str(),
                                  -1, SQLITE_TRANSIENT);
            }
            while (sqlite3_step(select_stmt) == SQLITE_ROW) {
                track_ids.push_back(column_text(select_stmt, 0));
            }
            sqlite3_finalize(select_stmt);
        } else {
            success = false;
        }
        if (!success) break;

        for (size_t position = 0; position < track_ids.size(); ++position) {
            sqlite3_stmt* insert_stmt = nullptr;
            if (sqlite3_prepare_v2(
                    impl_->db,
                    "INSERT INTO playlist_tracks (playlist_id, track_id, position) "
                    "VALUES (?, ?, ?);",
                    -1, &insert_stmt, nullptr) != SQLITE_OK) {
                success = false;
                break;
            }
            sqlite3_bind_text(insert_stmt, 1, playlist_id.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(insert_stmt, 2, track_ids[position].c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert_stmt, 3,
                               static_cast<int64_t>(position));
            if (sqlite3_step(insert_stmt) != SQLITE_DONE) success = false;
            sqlite3_finalize(insert_stmt);
            if (!success) break;
        }
        if (!success) break;
    }

    sqlite3_exec(impl_->db, success ? "COMMIT;" : "ROLLBACK;", nullptr,
                 nullptr, nullptr);
    return success;
}

void StorageManager::update_play_count(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET play_count = play_count + 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void StorageManager::update_last_played(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET last_played = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    int64_t now = now_epoch_seconds();
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool StorageManager::begin_transaction() {
    return sqlite3_exec(impl_->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool StorageManager::commit_transaction() {
    return sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool StorageManager::rollback_transaction() {
    return sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

// Helper for generating random IDs
std::string StorageManager::lower_hex_randomblob(int bytes) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, "SELECT lower(hex(randomblob(?)))", -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_int(stmt, 1, bytes);
    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = column_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace lollypop