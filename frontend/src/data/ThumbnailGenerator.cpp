/**
 * Lollypop C++ - ThumbnailGenerator Implementation
 */

#include "data/ThumbnailGenerator.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <algorithm>
#include <iostream>
#include <utility>

namespace lollypop {

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
    if (!g_file_test(source_path.c_str(), G_FILE_TEST_EXISTS)) return nullptr;

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
