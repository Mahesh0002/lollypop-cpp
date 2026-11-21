/**
 * Lollypop C++ - Storage Manager Implementation
 * SQLite database for local music library persistence
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
    "tracks.artwork_path, tracks.liked, tracks.play_count, tracks.last_played, "
    "tracks.date_added";

sqlite3_column_text() returns nullptr for SQL NULL columns. Constructing a
std::string from nullptr is undefined behaviour, so always go through this.
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

// Bring helper functions into global scope for StorageManager method definitions
using lollypop::column_text;
using lollypop::build_fts_query;
using lollypop::now_epoch_seconds;
using lollypop::backfill_library_index;

void backfill_library_index(sqlite3* db) {
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

struct StorageManager::Impl {
    sqlite3* db = nullptr;
    std::string db_path;
    int schema_version = 3;

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

    sqlite3* open_read_only() {
        if (db_path.empty()) return nullptr;

        sqlite3* ro_db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &ro_db,
                                 SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (ro_db) sqlite3_close(ro_db);
            std::cerr << "StorageManager: cannot open read-only connection: "
                      << sqlite3_errmsg(ro_db) << std::endl;
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(ro_mutex);
        read_only_connections.push_back(ro_db);
        return ro_db;
    }

    void close_read_only(sqlite3* ro_db) {
        if (!ro_db) return;
        std::lock_guard<std::mutex> lock(ro_mutex);
        auto it = std::find(read_only_connections.begin(),
                            read_only_connections.end(), ro_db);
        if (it != read_only_connections.end()) {
            read_only_connections.erase(it);
        }
        sqlite3_close(ro_db);
    }

    bool table_exists(const std::string& name) {
        sqlite3_stmt* stmt = nullptr;
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?;";
        bool exists = false;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
            sqlite3_finalize(stmt);
        }
        return exists;
    }

    std::string get_or_create_artist(const std::string& name) {
        if (name.empty()) return {};
        auto it = artist_name_to_id.find(name);
        if (it != artist_name_to_id.end()) return it->second;

        sqlite3_stmt* stmt = nullptr;
        std::string id;
        if (sqlite3_prepare_v2(db,
                               "SELECT id FROM artists WHERE name = ? LIMIT 1;",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                id = column_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (!id.empty()) {
            artist_name_to_id[name] = id;
            return id;
        }

        id = generate_id();
        if (sqlite3_prepare_v2(db,
                               "INSERT OR IGNORE INTO artists (id, name, date_added) "
                               "VALUES (?, ?, ?);",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, get_current_timestamp());
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        artist_name_to_id[name] = id;
        return id;
    }

    std::string get_or_create_album(const std::string& title,
                                     const std::string& artist,
                                     const std::string& artwork) {
        if (title.empty()) return {};
        std::string cache_key = title + "|" + artist;
        auto it = album_key_to_id.find(cache_key);
        if (it != album_key_to_id.end()) return it->second;

        sqlite3_stmt* stmt = nullptr;
        std::string id;
        if (sqlite3_prepare_v2(db,
                               "SELECT id FROM albums WHERE title = ? AND "
                               "artist = ? ORDER BY id LIMIT 1;",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, artist.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                id = column_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (!id.empty()) {
            if (!artwork.empty()) {
                if (sqlite3_prepare_v2(db,
                                       "UPDATE albums SET artwork_path = ? "
                                       "WHERE id = ? AND (artwork_path IS NULL "
                                       "OR artwork_path = '');",
                                       -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, artwork.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            album_key_to_id[cache_key] = id;
            return id;
        }

        id = generate_id();
        if (sqlite3_prepare_v2(db,
                               "INSERT INTO albums (id, title, artist, year, "
                               "artwork_path, date_added) VALUES (?, ?, ?, ?, "
                               "?, ?);",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, artist.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, 0);
            sqlite3_bind_text(stmt, 4, artwork.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, get_current_timestamp());
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        album_key_to_id[cache_key] = id;
        return id;
    }

    void clear_interning_caches() {
        artist_name_to_id.clear();
        album_key_to_id.clear();
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
    if (!migrate_schema(current_version, 3)) return false;
    if (!create_schema()) return false;

    std::cout << "StorageManager: Initialized at " << impl_->db_path << std::endl;
    return true;
}

void StorageManager::shutdown() {
    impl_.reset();
}

bool StorageManager::create_schema() {
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

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS artists (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            artwork_path TEXT,
            date_added INTEGER DEFAULT 0
        );
    )")) return false;

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

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS album_tracks (
            album_id TEXT NOT NULL,
            track_id TEXT NOT NULL,
            PRIMARY KEY (album_id, track_id),
            FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE,
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS artist_albums (
            artist_id TEXT NOT NULL,
            album_id TEXT NOT NULL,
            PRIMARY KEY (artist_id, album_id),
            FOREIGN KEY (artist_id) REFERENCES artists(id) ON DELETE CASCADE,
            FOREIGN KEY (album_id) REFERENCES albums(id) ON DELETE CASCADE
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS genres (
            name TEXT PRIMARY KEY
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS track_genres (
            track_id TEXT NOT NULL,
            genre TEXT NOT NULL,
            PRIMARY KEY (track_id, genre),
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE,
            FOREIGN KEY (genre) REFERENCES genres(name) ON DELETE CASCADE
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS genre_edges (
            parent TEXT NOT NULL,
            child TEXT NOT NULL,
            PRIMARY KEY (parent, child),
            FOREIGN KEY (parent) REFERENCES genres(name) ON DELETE CASCADE,
            FOREIGN KEY (child) REFERENCES genres(name) ON DELETE CASCADE
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS library_roots (
            path TEXT PRIMARY KEY
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS lyrics (
            track_id TEXT PRIMARY KEY,
            text TEXT NOT NULL,
            synced INTEGER NOT NULL DEFAULT 0,
            source TEXT NOT NULL DEFAULT '',
            updated_at INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
        );
    )")) return false;

    if (!impl_->execute(R"(
        CREATE TABLE IF NOT EXISTS preferences (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )")) return false;

    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_liked ON tracks(liked);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_date_added ON tracks(date_added);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_tracks_last_played ON tracks(last_played);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_albums_artist ON albums(artist);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_albums_favorite ON albums(favorite);");
    impl_->execute("CREATE INDEX IF NOT EXISTS idx_playlist_tracks_position ON playlist_tracks(playlist_id, position);");

    if (!impl_->execute(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
            title, artist, album, album_artist, genre,
            content='tracks', content_rowid='rowid',
            tokenize='unicode61 remove_diacritics 2',
            prefix='2 3'
        );
    )")) return false;

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

    impl_->execute("PRAGMA user_version = 3;");

    backfill_library_index(impl_->db);

    return true;
}

bool StorageManager::migrate_schema(int from_version, int to_version) {
    if (from_version < 0 || from_version > to_version) return false;
    if (from_version == to_version) return true;

    if (!begin_transaction()) return false;

    bool success = true;

    if (from_version < 2 && impl_->table_exists("playlists")) {
        auto has_column = [this](const char* table, const char* column) {
            sqlite3_stmt* stmt = nullptr;
            const std::string sql = std::string("PRAGMA table_info(") + table + ");";
            bool found = false;
            if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    if (column_text(stmt, 1) == column) { found = true; break; }
                }
                sqlite3_finalize(stmt);
            }
            return found;
        };
        if (!has_column("playlists", "is_smart") &&
            !impl_->execute("ALTER TABLE playlists ADD COLUMN is_smart INTEGER NOT NULL DEFAULT 0;")) {
            success = false;
        }
        if (success && !has_column("playlists", "smart_criteria") &&
            !impl_->execute("ALTER TABLE playlists ADD COLUMN smart_criteria TEXT NOT NULL DEFAULT '';")) {
            success = false;
        }
    }

    if (success && from_version < 3 && impl_->table_exists("tracks_fts")) {
        if (!impl_->execute("DROP TABLE tracks_fts;")) {
            success = false;
        }
        if (success && !impl_->execute(R"(
            CREATE VIRTUAL TABLE tracks_fts USING fts5(
                title, artist, album, album_artist, genre,
                content='tracks', content_rowid='rowid',
                tokenize='unicode61 remove_diacritics 2',
                prefix='2 3'
            );
        )")) {
            success = false;
        }
        if (success) {
            impl_->execute(R"(
                INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre)
                SELECT rowid, title, artist, album, album_artist, genre FROM tracks;
            )");
        }
    }

    if (success) {
        success = impl_->execute("PRAGMA user_version = 3;");
    }

    if (success) {
        success = commit_transaction();
    } else {
        rollback_transaction();
    }
    return success;
}

int StorageManager::get_schema_version() const {
    sqlite3_stmt* stmt = nullptr;
    int version = 0;
    if (sqlite3_prepare_v2(impl_->db, "PRAGMA user_version;", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return version;
}

bool StorageManager::begin_transaction() {
    return impl_->execute("BEGIN IMMEDIATE TRANSACTION;");
}

bool StorageManager::commit_transaction() {
    return impl_->execute("COMMIT;");
}

bool StorageManager::rollback_transaction() {
    return impl_->execute("ROLLBACK;");
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
    track.duration = sqlite3_column_int(stmt, 9);
    track.file_path = column_text(stmt, 10);
    track.artwork_path = column_text(stmt, 11);
    track.liked = sqlite3_column_int(stmt, 12) != 0;
    track.play_count = sqlite3_column_int(stmt, 13);
    track.last_played = sqlite3_column_int64(stmt, 14);
    track.date_added = sqlite3_column_int64(stmt, 15);
    return track;
}

Album StorageManager::row_to_album(sqlite3_stmt* stmt) {
    Album album;
    album.id = column_text(stmt, 0);
    album.title = column_text(stmt, 1);
    album.artist = column_text(stmt, 2);
    album.year = sqlite3_column_int(stmt, 3);
    album.artwork_path = column_text(stmt, 4);
    album.favorite = sqlite3_column_int(stmt, 5) != 0;
    album.date_added = sqlite3_column_int64(stmt, 6);
    return album;
}

Artist StorageManager::row_to_artist(sqlite3_stmt* stmt) {
    Artist artist;
    artist.id = column_text(stmt, 0);
    artist.name = column_text(stmt, 1);
    artist.artwork_path = column_text(stmt, 2);
    artist.date_added = sqlite3_column_int64(stmt, 3);
    return artist;
}

Playlist StorageManager::row_to_playlist(sqlite3_stmt* stmt) {
    Playlist playlist;
    playlist.id = column_text(stmt, 0);
    playlist.name = column_text(stmt, 1);
    playlist.is_smart = sqlite3_column_int(stmt, 2) != 0;
    playlist.smart_criteria = column_text(stmt, 3);
    playlist.date_created = sqlite3_column_int64(stmt, 4);
    playlist.date_modified = sqlite3_column_int64(stmt, 5);
    return playlist;
}

std::vector<Track> StorageManager::get_all_tracks() {
    std::vector<Track> tracks;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks ORDER BY date_added DESC;").c_str();
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
    std::string sql = "SELECT " + std::string(kTrackColumnsQualified) + " FROM tracks ORDER BY date_added DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
    std::string sql = "SELECT " + std::string(kTrackColumnsQualified) + " FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

std::vector<Track> StorageManager::get_liked_tracks() {
    std::vector<Track> tracks;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE liked = 1 ORDER BY date_added DESC;").c_str();
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
    const std::string fts_query = build_fts_query(query);
    if (fts_query.empty()) return tracks;

    const std::string sql =
        std::string("SELECT ") + kTrackColumnsQualified +
        " FROM tracks JOIN tracks_fts ON tracks.rowid = tracks_fts.rowid"
        " WHERE tracks_fts MATCH ? ORDER BY tracks_fts.rank;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tracks.push_back(row_to_track(stmt));
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "StorageManager: search query failed: "
                  << sqlite3_errmsg(impl_->db) << std::endl;
    }
    return tracks;
}

Track StorageManager::get_track(const std::string& track_id) {
    Track track;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE id = ?;").c_str();

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
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE file_path = ?;").c_str();

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

bool StorageManager::add_track(const Track& track) {
    if (track.file_path.empty()) return false;

    const char* sql =
        "INSERT INTO tracks (id, title, artist, album, album_artist, genre, "
        "track_number, disc_number, year, duration, file_path, artwork_path, "
        "liked, play_count, last_played, date_added, date_modified) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(file_path) DO UPDATE SET "
        "title=excluded.title, artist=excluded.artist, album=excluded.album, "
        "album_artist=excluded.album_artist, genre=excluded.genre, "
        "track_number=excluded.track_number, disc_number=excluded.disc_number, "
        "year=excluded.year, duration=excluded.duration, "
        "artwork_path=CASE WHEN excluded.artwork_path != '' "
        "THEN excluded.artwork_path ELSE tracks.artwork_path END, "
        "date_modified=excluded.date_modified;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    int idx = 1;
    sqlite3_bind_text(stmt, idx++, track.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, track.track_number);
    sqlite3_bind_int(stmt, idx++, track.disc_number);
    sqlite3_bind_int(stmt, idx++, track.year);
    sqlite3_bind_int(stmt, idx++, track.duration);
    sqlite3_bind_text(stmt, idx++, track.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, track.liked ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, track.play_count);
    sqlite3_bind_int64(stmt, idx++, track.last_played);
    sqlite3_bind_int64(stmt, idx++, track.date_added);
    sqlite3_bind_int64(stmt, idx++, impl_->get_current_timestamp());

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_track(const Track& track) {
    if (track.id.empty() || track.file_path.empty()) return false;

    const char* sql =
        "UPDATE tracks SET title=?, artist=?, album=?, album_artist=?, genre=?, "
        "track_number=?, disc_number=?, year=?, duration=?, artwork_path=?, "
        "liked=?, play_count=?, last_played=?, date_modified=? "
        "WHERE id=?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    int idx = 1;
    sqlite3_bind_text(stmt, idx++, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.album.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, track.track_number);
    sqlite3_bind_int(stmt, idx++, track.disc_number);
    sqlite3_bind_int(stmt, idx++, track.year);
    sqlite3_bind_int(stmt, idx++, track.duration);
    sqlite3_bind_text(stmt, idx++, track.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, track.liked ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, track.play_count);
    sqlite3_bind_int64(stmt, idx++, track.last_played);
    sqlite3_bind_int64(stmt, idx++, impl_->get_current_timestamp());
    sqlite3_bind_text(stmt, idx++, track.id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::delete_track(const std::string& track_id) {
    const char* sql = "DELETE FROM tracks WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::delete_track_by_path(const std::string& file_path) {
    const char* sql = "DELETE FROM tracks WHERE file_path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::delete_tracks_under_path(const std::string& directory_path) {
    std::string prefix = directory_path;
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    const char* sql = "DELETE FROM tracks WHERE file_path = ? OR file_path LIKE ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, directory_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, (prefix + "%").c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<std::string> StorageManager::prune_orphaned_entities() {
    std::vector<std::string> removed;
    // Albums with no tracks
    const char* sql_albums =
        "DELETE FROM albums WHERE id NOT IN (SELECT DISTINCT album_id FROM album_tracks);";
    if (impl_->execute(sql_albums)) {
        // Get count of affected rows
        removed.push_back("albums");
    }
    // Artists with no albums
    const char* sql_artists =
        "DELETE FROM artists WHERE id NOT IN (SELECT DISTINCT artist_id FROM artist_albums);";
    if (impl_->execute(sql_artists)) {
        removed.push_back("artists");
    }
    // Genres with no tracks
    const char* sql_genres =
        "DELETE FROM genres WHERE name NOT IN (SELECT DISTINCT genre FROM track_genres);";
    if (impl_->execute(sql_genres)) {
        removed.push_back("genres");
    }
    return removed;
}

bool StorageManager::toggle_like(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET liked = NOT liked WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

void StorageManager::update_play_count(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET play_count = play_count + 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void StorageManager::update_last_played(const std::string& track_id) {
    const char* sql = "UPDATE tracks SET last_played = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    int64_t now = impl_->get_current_timestamp();
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<Album> StorageManager::get_all_albums() {
    std::vector<Album> albums;
    const char* sql = "SELECT id, title, artist, year, artwork_path, favorite, date_added FROM albums ORDER BY title;";
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
    const char* sql = "SELECT a.id, a.title, a.artist, a.year, a.artwork_path, a.favorite, a.date_added "
                      "FROM albums a JOIN artist_albums aa ON a.id = aa.album_id "
                      "WHERE aa.artist_id = ? ORDER BY a.year, a.title;";
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
    const char* sql = "SELECT id, title, artist, year, artwork_path, favorite, date_added FROM albums WHERE id = ?;";
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
    if (album.id.empty() || album.title.empty()) return false;
    const char* sql = "INSERT OR REPLACE INTO albums (id, title, artist, year, artwork_path, favorite, date_added) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, album.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, album.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, album.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, album.year);
    sqlite3_bind_text(stmt, idx++, album.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, album.favorite ? 1 : 0);
    sqlite3_bind_int64(stmt, idx++, album.date_added);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_album(const Album& album) {
    if (album.id.empty()) return false;
    const char* sql = "UPDATE albums SET title=?, artist=?, year=?, artwork_path=?, favorite=?, date_added=? WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, album.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, album.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, album.year);
    sqlite3_bind_text(stmt, idx++, album.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, album.favorite ? 1 : 0);
    sqlite3_bind_int64(stmt, idx++, album.date_added);
    sqlite3_bind_text(stmt, idx++, album.id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::toggle_favorite_album(const std::string& album_id) {
    const char* sql = "UPDATE albums SET favorite = NOT favorite WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, album_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Artist> StorageManager::get_all_artists() {
    std::vector<Artist> artists;
    const char* sql = "SELECT id, name, artwork_path, date_added FROM artists ORDER BY name;";
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
    const char* sql = "SELECT id, name, artwork_path, date_added FROM artists WHERE id = ?;";
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

std::vector<Track> StorageManager::get_tracks_by_artist(const std::string& artist_name) {
    std::vector<Track> tracks;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE artist = ? ORDER BY album, track_number;").c_str();
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

bool StorageManager::add_artist(const Artist& artist) {
    if (artist.id.empty() || artist.name.empty()) return false;
    const char* sql = "INSERT OR REPLACE INTO artists (id, name, artwork_path, date_added) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, artist.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, artist.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, artist.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, artist.date_added);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_artist(const Artist& artist) {
    if (artist.id.empty()) return false;
    const char* sql = "UPDATE artists SET name=?, artwork_path=?, date_added=? WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, artist.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, artist.artwork_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, artist.date_added);
    sqlite3_bind_text(stmt, idx++, artist.id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Playlist> StorageManager::get_all_playlists() {
    std::vector<Playlist> playlists;
    const char* sql = "SELECT id, name, is_smart, smart_criteria, date_created, date_modified FROM playlists ORDER BY name;";
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
    const char* sql = "SELECT id, name, is_smart, smart_criteria, date_created, date_modified FROM playlists WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            playlist = row_to_playlist(stmt);
        }
        sqlite3_finalize(stmt);
    }
    return playlist;
}

std::vector<Track> StorageManager::get_smart_playlist_tracks(const std::string& playlist_id) {
    std::vector<Track> tracks;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks WHERE id IN (SELECT track_id FROM playlist_tracks WHERE playlist_id = ? ORDER BY position);").c_str();
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

bool StorageManager::create_playlist(const std::string& name) {
    if (name.empty()) return false;
    const char* sql = "INSERT INTO playlists (id, name, is_smart, smart_criteria, date_created, date_modified) VALUES (?, ?, 0, '', ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    std::string id = generate_id();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::create_smart_playlist(const std::string& name, const std::string& criteria) {
    if (name.empty()) return false;
    const char* sql = "INSERT INTO playlists (id, name, is_smart, smart_criteria, date_created, date_modified) VALUES (?, ?, 1, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    std::string id = generate_id();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, criteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::update_smart_playlist(const std::string& id, const std::string& name, const std::string& criteria) {
    if (id.empty() || name.empty()) return false;
    const char* sql = "UPDATE playlists SET name=?, smart_criteria=?, date_modified=? WHERE id=? AND is_smart=1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, criteria.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
    sqlite3_bind_text(stmt, idx++, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
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
    const char* sql = "INSERT OR REPLACE INTO playlist_tracks (playlist_id, track_id, position, date_added) VALUES (?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_tracks WHERE playlist_id = ?), ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, get_current_timestamp());
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

bool StorageManager::reorder_playlist_tracks(const std::string& playlist_id, const std::vector<std::string>& track_ids) {
    const char* sql_delete = "DELETE FROM playlist_tracks WHERE playlist_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql_delete, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    const char* sql_insert = "INSERT INTO playlist_tracks (playlist_id, track_id, position, date_added) VALUES (?, ?, ?, ?);";
    for (size_t i = 0; i < track_ids.size(); ++i) {
        if (sqlite3_prepare_v2(impl_->db, sql_insert, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, playlist_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, static_cast<int>(i));
        sqlite3_bind_int64(stmt, 4, get_current_timestamp());
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
    }
    return true;
}

std::vector<std::string> StorageManager::get_all_genres() {
    std::vector<std::string> genres;
    const char* sql = "SELECT name FROM genres ORDER BY name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            genres.push_back(column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return genres;
}

std::vector<Track> StorageManager::get_tracks_by_genre(const std::string& genre) {
    std::vector<Track> tracks;
    const char* sql = (std::string("SELECT ") + kTrackColumnsQualified + " FROM tracks t JOIN track_genres tg ON t.id = tg.track_id WHERE tg.genre = ? ORDER BY t.artist, t.album, t.track_number;").c_str();
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

bool StorageManager::add_genre_edge(const std::string& parent, const std::string& child) {
    if (parent == child) return false;
    const char* sql = "INSERT OR IGNORE INTO genre_edges (parent, child) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, parent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, child.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

std::vector<std::string> StorageManager::get_genre_descendants(const std::string& genre) {
    std::vector<std::string> descendants;
    const char* sql = "WITH RECURSIVE genre_tree(child) AS (SELECT ? UNION SELECT ge.child FROM genre_edges ge JOIN genre_tree gt ON ge.parent = gt.child) SELECT child FROM genre_tree;";
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

std::vector<Track> StorageManager::get_tracks_by_genre_hierarchy(const std::string& genre) {
    std::vector<std::string> genres = get_genre_descendants(genre);
    std::vector<Track> tracks;
    for (const auto& g : genres) {
        auto genre_tracks = get_tracks_by_genre(g);
        tracks.insert(tracks.end(), genre_tracks.begin(), genre_tracks.end());
    }
    return tracks;
}

bool StorageManager::add_library_root(const std::string& path) {
    const char* sql = "INSERT OR IGNORE INTO library_roots (path) VALUES (?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool StorageManager::remove_library_root(const std::string& path) {
    const char* sql = "DELETE FROM library_roots WHERE path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<std::string> StorageManager::get_library_roots() const {
    std::vector<std::string> roots;
    const char* sql = "SELECT path FROM library_roots ORDER BY path;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            roots.push_back(column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return roots;
}

bool StorageManager::is_library_root(const std::string& path) const {
    const char* sql = "SELECT 1 FROM library_roots WHERE path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return exists;
    }
    return false;
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
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string value = column_text(stmt, 0);
            sqlite3_finalize(stmt);
            return value;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

int StorageManager::get_track_count() const {
    int count = 0;
    const char* sql = "SELECT COUNT(*) FROM tracks;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int StorageManager::get_album_count() const {
    int count = 0;
    const char* sql = "SELECT COUNT(*) FROM albums;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int StorageManager::get_artist_count() const {
    int count = 0;
    const char* sql = "SELECT COUNT(*) FROM artists;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

sqlite3* StorageManager::open_read_only() {
    return impl_->open_read_only();
}

void StorageManager::close_read_only(sqlite3* ro_db) {
    impl_->close_read_only(ro_db);
}

bool StorageManager::integrity_check() {
    const char* sql = "PRAGMA integrity_check;";
    char* err_msg = nullptr;
    int rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "Integrity check: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return rc == SQLITE_OK;
}

bool StorageManager::quick_check() {
    const char* sql = "PRAGMA quick_check;";
    char* err_msg = nullptr;
    int rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "Quick check: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return rc == SQLITE_OK;
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
    char* err_msg = nullptr;
    int rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "Vacuum: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return rc == SQLITE_OK;
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
    };

    auto it = valid_sort_keys.find(sort_key);
    if (it == valid_sort_keys.end()) {
        it = valid_sort_keys.find("date_added");
    }
    const std::string& column = it->second;
    const bool is_numeric =
        (column == "date_added" || column == "duration" ||
         column == "play_count" || column == "last_played" ||
         column == "year" || column == "track_number");

    std::vector<Track> tracks;
    if (limit <= 0) return tracks;

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
    sqlite3_bind_int(stmt, bind_idx++, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(row_to_track(stmt));
    }
    sqlite3_finalize(stmt);
    return tracks;
}

bool StorageManager::set_lyrics(const std::string& track_id, const std::string& text,
                                bool synced, const std::string& source) {
    const char* sql = "INSERT OR REPLACE INTO lyrics (track_id, text, synced, source, updated_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, synced ? 1 : 0);
    sqlite3_bind_text(stmt, idx++, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, impl_->get_current_timestamp());
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<Lyrics> StorageManager::get_lyrics(const std::string& track_id) const {
    const char* sql = "SELECT track_id, text, synced, source, updated_at FROM lyrics WHERE track_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            Lyrics lyrics;
            lyrics.track_id = column_text(stmt, 0);
            lyrics.text = column_text(stmt, 1);
            lyrics.synced = sqlite3_column_int(stmt, 2) != 0;
            lyrics.source = column_text(stmt, 3);
            lyrics.updated_at = sqlite3_column_int64(stmt, 4);
            sqlite3_finalize(stmt);
            return lyrics;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
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

} // namespace lollypop