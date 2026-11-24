/**
 * Lollypop C++ - Fetch All Lyrics Tool
 * 
 * Standalone tool to fetch lyrics for all tracks in the library.
 * Usage: ./fetch_lyrics [--all|--missing] [--db-path /path/to/library.db]
 */

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

#include "manager.hpp"
#include "external_providers.hpp"

using namespace lollypop;

int main(int argc, char* argv[]) {
    bool fetch_all = false;
    bool fetch_missing = false;
    std::string db_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all") {
            fetch_all = true;
        } else if (arg == "--missing") {
            fetch_missing = true;
        } else if (arg == "--db-path" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --all          Fetch lyrics for all tracks (overwrites existing)\n"
                      << "  --missing      Fetch lyrics only for tracks without lyrics (default)\n"
                      << "  --db-path PATH Path to SQLite database\n"
                      << "  --help         Show this help\n";
            return 0;
        }
    }

    if (!fetch_all && !fetch_missing) {
        fetch_missing = true;  // Default
    }

    std::cout << "Lollypop C++ - Fetch Lyrics Tool\n";
    std::cout << "==================================\n";

    // Initialize storage
    auto storage = std::make_unique<StorageManager>();
    if (!storage->initialize(db_path.empty() ? ":memory:" : db_path)) {
        std::cerr << "Failed to initialize storage\n";
        return 1;
    }

    // Get all tracks
    auto tracks = storage->get_all_tracks();
    std::cout << "Found " << tracks.size() << " tracks in library\n";

    if (tracks.empty()) {
        std::cout << "No tracks to process.\n";
        return 0;
    }

    // Initialize lyrics manager (we need a cache manager too)
    // For this standalone tool, we'll use the storage directly with a simple approach

    int fetched = 0;
    int failed = 0;
    int skipped = 0;

    // Use the external providers directly (included at top)

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks[i];
        std::cout << "[" << (i + 1) << "/" << tracks.size() << "] " 
                  << track.artist << " - " << track.title << " ... ";

        // Check if lyrics already exist
        auto existing = storage->get_lyrics(track.id);
        if (existing && !existing->text.empty()) {
            if (fetch_missing) {
                std::cout << "SKIP (already has lyrics)\n";
                skipped++;
                continue;
            }
            std::cout << "OVERWRITE ... ";
        }

        // Fetch lyrics from LRCLIB
        std::string lyrics_text = LyricsProvider::fetch_lyrics(track.artist, track.title, track.album, track.duration);
        
        if (!lyrics_text.empty()) {
            bool synced = lyrics_text.find('[') != std::string::npos && lyrics_text.find(']') != std::string::npos;
            if (storage->set_lyrics(track.id, lyrics_text, synced, "lrclib")) {
                std::cout << "OK (" << (synced ? "synced" : "unsynced") << ")\n";
                fetched++;
            } else {
                std::cout << "FAILED (storage error)\n";
                failed++;
            }
        } else {
            std::cout << "NOT FOUND\n";
            failed++;
        }

        // Rate limit
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nDone!\n";
    std::cout << "  Fetched: " << fetched << "\n";
    std::cout << "  Failed:  " << failed << "\n";
    std::cout << "  Skipped: " << skipped << "\n";

    return 0;
}