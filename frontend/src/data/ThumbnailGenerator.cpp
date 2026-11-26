/**
 * Lollypop C++ - ThumbnailGenerator Implementation
 */

#include "data/ThumbnailGenerator.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace lollypop {

namespace {

// Deterministic on-disk name for a source image + size bucket. Kept separate
// from the in-memory cache key so a changed artwork path never reuses a stale
// thumbnail.
std::string thumbnail_cache_path(const std::string& cache_dir,
                                 const std::string& source_path, int bucket) {
    const std::string composite =
        source_path + ":" + std::to_string(bucket);
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a
    for (char ch : composite) {
        const auto c = static_cast<unsigned char>(ch);
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%016llx_%d.png",
                  static_cast<unsigned long long>(hash), bucket);
    return (std::filesystem::path(cache_dir) / name).string();
}

std::filesystem::file_time_type safe_mtime(const std::string& path) {
    std::error_code ec;
    return std::filesystem::last_write_time(path, ec);
}

}  // namespace

struct IdleCallbackData {
    GdkTexture* texture = nullptr;
    std::string file_path;
    std::vector<std::function<void(GdkTexture*, std::string)>> callbacks;
};

static gboolean idle_deliver_callback(gpointer data) {
    auto* cb = static_cast<IdleCallbackData*>(data);
    for (auto& callback : cb->callbacks) {
        if (callback) callback(cb->texture, cb->file_path);
    }
    if (cb->texture) g_object_unref(cb->texture);
    delete cb;
    return G_SOURCE_REMOVE;
}

// Queued jobs dropped by the cap still need to fail on the main thread.
static gboolean idle_deliver_empty_callbacks(gpointer data) {
    auto* callbacks =
        static_cast<std::vector<std::function<void(GdkTexture*, std::string)>>*>(data);
    for (auto& callback : *callbacks) {
        if (callback) callback(nullptr, {});
    }
    delete callbacks;
    return G_SOURCE_REMOVE;
}

ThumbnailGenerator::ThumbnailGenerator() = default;

ThumbnailGenerator::~ThumbnailGenerator() {
    shutdown();
}

bool ThumbnailGenerator::initialize(const std::string& cache_dir,
                                     ArtworkCache* cache) {
    cache_ = cache;
    cache_dir_ = cache_dir;
    if (!cache_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir_, ec);
    }
    running_ = true;
    worker_ = std::make_unique<std::thread>(&ThumbnailGenerator::worker_loop, this);
    return true;
}

void ThumbnailGenerator::shutdown() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
    }
    queue_cv_.notify_all();
    if (worker_ && worker_->joinable()) worker_->join();
}

void ThumbnailGenerator::queue_job(
    const std::string& key, const std::string& source_path, int bucket,
    std::function<void(GdkTexture*, std::string)> callback) {
    std::vector<ThumbnailJob> dropped;
    bool appended_to_queued_job = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!running_) return;

        auto existing = std::find_if(job_queue_.begin(), job_queue_.end(),
                                     [&key, bucket](const ThumbnailJob& job) {
                                         return job.key == key && job.bucket == bucket;
                                     });
        if (existing != job_queue_.end()) {
            existing->callbacks.push_back(std::move(callback));
            appended_to_queued_job = true;
        } else {
            while (job_queue_.size() >= kMaxQueuedJobs) {
                dropped.push_back(std::move(job_queue_.front()));
                job_queue_.pop_front();
            }
            ThumbnailJob job{key, source_path, bucket, {}};
            job.callbacks.push_back(std::move(callback));
            job_queue_.push_back(std::move(job));
        }
    }

    for (auto& job : dropped) {
        auto* callbacks = new std::vector<std::function<void(GdkTexture*, std::string)>>(
            std::move(job.callbacks));
        g_idle_add(idle_deliver_empty_callbacks, callbacks);
    }
    if (!appended_to_queued_job) queue_cv_.notify_one();
}

GdkTexture* ThumbnailGenerator::generate_thumbnail(
    const std::string& source_path, int bucket) const {
    if (source_path.empty() ||
        !g_file_test(source_path.c_str(), G_FILE_TEST_EXISTS)) {
        return nullptr;
    }

    const std::string cache_path =
        cache_dir_.empty() ? std::string{}
                           : thumbnail_cache_path(cache_dir_, source_path, bucket);

    // Reuse a persisted thumbnail when it is at least as new as the source.
    if (!cache_path.empty() &&
        g_file_test(cache_path.c_str(), G_FILE_TEST_EXISTS)) {
        const auto cached_mtime = safe_mtime(cache_path);
        const auto source_mtime = safe_mtime(source_path);
        if (cached_mtime >= source_mtime) {
            GError* error = nullptr;
            GdkPixbuf* cached = gdk_pixbuf_new_from_file(cache_path.c_str(), &error);
            if (error) {
                g_error_free(error);
            } else if (cached) {
                GdkTexture* texture = gdk_texture_new_for_pixbuf(cached);
                g_object_unref(cached);
                return texture;
            }
        }
    }

    GError* error = nullptr;
    GdkPixbuf* source = gdk_pixbuf_new_from_file(source_path.c_str(), &error);
    if (error) {
        g_error_free(error);
        return nullptr;
    }
    if (!source) return nullptr;

    const int width = gdk_pixbuf_get_width(source);
    const int height = gdk_pixbuf_get_height(source);
    const int side = std::min(width, height);
    if (side <= 0) {
        g_object_unref(source);
        return nullptr;
    }

    GdkPixbuf* square = gdk_pixbuf_new_subpixbuf(
        source, (width - side) / 2, (height - side) / 2, side, side);
    const int target = std::min(side, std::max(1, bucket));
    GdkPixbuf* scaled = (target == side)
                            ? static_cast<GdkPixbuf*>(g_object_ref(square))
                            : gdk_pixbuf_scale_simple(square, target, target,
                                                      GDK_INTERP_BILINEAR);

    // Write the resized image atomically so a concurrent reader never sees a
    // partially written PNG. The worker is single-threaded; the PID keeps
    // separate application instances from clobbering each other's temp file.
    if (!cache_path.empty() && scaled) {
        const std::string temp_path =
            cache_path + ".tmp-" + std::to_string(static_cast<long>(getpid()));
        GError* save_error = nullptr;
        if (gdk_pixbuf_save(scaled, temp_path.c_str(), "png", &save_error,
                            nullptr)) {
            std::error_code ec;
            std::filesystem::rename(temp_path, cache_path, ec);
            if (ec) std::filesystem::remove(temp_path, ec);
        } else {
            if (save_error) g_error_free(save_error);
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
        }
    }

    GdkTexture* texture = gdk_texture_new_for_pixbuf(scaled);

    g_object_unref(scaled);
    g_object_unref(square);
    g_object_unref(source);
    return texture;
}

void ThumbnailGenerator::worker_loop() {
    while (running_) {
        ThumbnailJob job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !job_queue_.empty();
            });
            if (!running_ && job_queue_.empty()) return;
            if (job_queue_.empty()) continue;
            job = std::move(job_queue_.front());
            job_queue_.pop_front();
        }

        // Generate the thumbnail (off main thread).
        GdkTexture* texture = generate_thumbnail(job.source_path, job.bucket);

        // Store in cache (thread-safe).
        std::string file_path;
        if (cache_ && texture) {
            cache_->store(job.key, texture, job.source_path, job.bucket);
            file_path = cache_->get_file_path(job.key);
        }

        // Deliver callbacks on the GTK main thread. cache_->store() holds its
        // own reference, so release the generator's reference here.
        if (texture && !job.callbacks.empty()) g_object_ref(texture);
        if (!job.callbacks.empty()) {
            auto* cb_data = new IdleCallbackData{
                texture, file_path, std::move(job.callbacks)
            };
            g_idle_add(idle_deliver_callback, cb_data);
        }
        if (texture) g_object_unref(texture);
    }
}

} // namespace lollypop
