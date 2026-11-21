/**
 * Lollypop C++ - ThumbnailGenerator Implementation
 */

#include "data/ThumbnailGenerator.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <iostream>

namespace lollypop {

struct IdleCallbackData {
    GdkTexture* texture = nullptr;
    std::string file_path;
    std::function<void(GdkTexture*, std::string)> callback;
};

static gboolean idle_deliver_callback(gpointer data) {
    auto* cb = static_cast<IdleCallbackData*>(data);
    if (cb->callback) {
        cb->callback(cb->texture, cb->file_path);
    }
    if (cb->texture) g_object_unref(cb->texture);
    delete cb;
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
    ThumbnailJob job{key, source_path, bucket, std::move(callback)};
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        job_queue_.push(std::move(job));
    }
    queue_cv_.notify_one();
}

GdkTexture* ThumbnailGenerator::generate_thumbnail(
    const std::string& source_path, int bucket) const {
    if (!g_file_test(source_path.c_str(), G_FILE_TEST_EXISTS)) return nullptr;

    GFile* file = g_file_new_for_path(source_path.c_str());
    GdkTexture* texture = gdk_texture_new_from_file(file, nullptr);
    g_object_unref(file);
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
            job_queue_.pop();
        }

        // Generate the thumbnail (off main thread).
        GdkTexture* texture = generate_thumbnail(job.source_path, job.bucket);

        // Store in cache (thread-safe).
        std::string file_path;
        if (cache_ && texture) {
            cache_->store(job.key, texture, job.source_path, job.bucket);
            file_path = cache_->get_file_path(job.key);
        }

        // Deliver callback on the GTK main thread.
        if (job.callback) {
            if (texture) g_object_ref(texture);
            auto* cb_data = new IdleCallbackData{
                texture, file_path, std::move(job.callback)
            };
            g_idle_add(idle_deliver_callback, cb_data);
        } else if (texture) {
            g_object_unref(texture);
        }
    }
}

} // namespace lollypop
