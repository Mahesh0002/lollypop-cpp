/**
 * Lollypop C++ - ThumbnailGenerator
 *
 * Background thumbnail generation worker. Runs on a dedicated thread,
 * decodes artwork files, generates size-bucketed thumbnails, and stores
 * them in the ArtworkCache.
 *
 * Plan Section 10.8 / 21.4:
 *   - Generate thumbnails in the background (never on GTK main thread)
 *   - Size-bucketed GdkTexture LRU cache
 *   - Bucket selection by physical pixel requirements
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <atomic>
#include <condition_variable>

#include <gdk/gdk.h>

#include "data/ArtworkCache.hpp"

namespace lollypop {

struct ThumbnailJob {
    std::string key;           ///< Cache key (track/album/artist id)
    std::string source_path;   ///< Source artwork file path
    int bucket;                ///< Target bucket size
    std::function<void(GdkTexture*, std::string)> callback;  ///< Delivered on main thread
};

class ThumbnailGenerator {
public:
    ThumbnailGenerator();
    ~ThumbnailGenerator();

    ThumbnailGenerator(const ThumbnailGenerator&) = delete;
    ThumbnailGenerator& operator=(const ThumbnailGenerator&) = delete;

    /// Initialize with a cache directory and ArtworkCache reference.
    bool initialize(const std::string& cache_dir, ArtworkCache* cache);

    /// Queue a thumbnail generation job. Callback is invoked on the GTK
    /// main thread with the generated texture (or nullptr on failure).
    void queue_job(const std::string& key, const std::string& source_path,
                   int bucket,
                   std::function<void(GdkTexture*, std::string)> callback);

    /// Shut down the worker thread.
    void shutdown();

private:
    std::unique_ptr<std::thread> worker_;
    std::atomic<bool> running_{false};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<ThumbnailJob> job_queue_;

    ArtworkCache* cache_ = nullptr;
    std::string cache_dir_;

    void worker_loop();
    GdkTexture* generate_thumbnail(const std::string& source_path, int bucket) const;
};

} // namespace lollypop
