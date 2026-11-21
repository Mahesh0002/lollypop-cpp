/**
 * Lollypop C++ - ArtworkManager
 *
 * Artwork extraction, caching, and retrieval.
 *
 * Design (plan.md Section 13):
 *
 *   - Priority queue: now-playing > next-track > background library work.
 *   - In-flight key set: the same cover is never extracted/fetched twice.
 *   - Stable cache key per (album_artist, album) or per file path.
 *   - Built on CacheManager: byte-budgeted LRU + disk thumbnails keyed by
 *     (hash, size).
 *   - Avoids duplicate extraction; invalidates stale artwork.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cache_manager.hpp"

namespace lollypop {

struct ArtworkRequest {
    enum class Priority {
        Background = 0,
        NextTrack = 1,
        NowPlaying = 2,
    };

    std::string key;            // stable cache key (e.g. "artist|album")
    std::string file_path;      // source file to extract from (if any)
    std::string album_artist;
    std::string album;
    Priority priority = Priority::Background;
    int max_size = 512;         // requested thumbnail size in pixels

    // Ordering for the priority queue: highest priority first.
    bool operator<(const ArtworkRequest& other) const {
        return static_cast<int>(priority) <
               static_cast<int>(other.priority);
    }
};

using ArtworkReadyCallback = std::function<void(const std::string& key,
                                                 const std::string& artwork_path)>;

class ArtworkManager {
public:
    ArtworkManager();
    ~ArtworkManager();

    ArtworkManager(const ArtworkManager&) = delete;
    ArtworkManager& operator=(const ArtworkManager&) = delete;

    // Configure cache dir and byte budget; starts the worker thread.
    bool initialize(const std::string& cache_dir,
                    std::size_t byte_budget);
    void shutdown();

    // Request artwork. Returns true if the request was queued (or is already
    // in flight / cached). The callback fires on the worker thread; the
    // caller is responsible for dispatching to the GTK main context if it
    // touches UI.
    bool request_artwork(const ArtworkRequest& request,
                          ArtworkReadyCallback cb);

    // Look up cached artwork by key without queuing extraction.
    bool get_cached_artwork(const std::string& key,
                             std::string& artwork_path) const;

    // Invalidate artwork for a key (removes from cache and disk).
    void invalidate(const std::string& key);

    // Generate a stable cache key from album identity.
    static std::string make_key(const std::string& album_artist,
                                 const std::string& album);

    // Set the extraction function. If unset, ArtworkManager falls back to
    // checking for folder artwork (cover.jpg, etc.) next to file_path.
    using ExtractFn = std::function<std::string(
        const std::string& file_path, const std::string& key,
        int max_size)>;
    void set_extract_fn(ExtractFn fn);

private:
    struct QueuedRequest {
        ArtworkRequest request;
        ArtworkReadyCallback callback;

        // Highest priority first for the priority queue.
        bool operator<(const QueuedRequest& other) const {
            return static_cast<int>(request.priority) <
                   static_cast<int>(other.request.priority);
        }
    };

    // Priority queue: higher priority extracted first.
    std::priority_queue<QueuedRequest> queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // In-flight keys: prevents duplicate extraction of the same cover.
    // pending_callbacks_ holds all callbacks for a key currently in flight,
    // so every requester is notified when extraction completes.
    std::unordered_set<std::string> in_flight_;
    std::unordered_map<std::string, std::vector<ArtworkReadyCallback>> pending_callbacks_;
    std::mutex in_flight_mutex_;

    std::unique_ptr<CacheManager> cache_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    ExtractFn extract_fn_;

    void worker_loop();
    void process_request(const QueuedRequest& queued);
    std::string extract_folder_artwork(const std::string& file_path,
                                        int max_size) const;
    void fire_callback(ArtworkReadyCallback cb, const std::string& key,
                       const std::string& artwork_path);
};

}  // namespace lollypop
