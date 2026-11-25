/**
 * Lollypop C++ - ArtworkManager Implementation
 */

#include "artwork_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lollypop {

ArtworkManager::ArtworkManager() = default;

ArtworkManager::~ArtworkManager() {
    shutdown();
}

std::string ArtworkManager::make_key(const std::string& album_artist,
                                      const std::string& album,
                                      const std::string& file_path) {
    // Stable key from album identity. Incomplete metadata falls back to the
    // source path, then to a unique id so unknown albums never share a key.
    if (!album_artist.empty() && !album.empty()) {
        return "album|" + album_artist + "|" + album;
    }
    if (!album.empty()) return "album|" + album;
    if (!album_artist.empty()) return "artist|" + album_artist;
    if (!file_path.empty()) return "path|" + file_path;
    static std::atomic<uint64_t> next_unknown{0};
    return "unknown|" + std::to_string(next_unknown.fetch_add(1));
}

bool ArtworkManager::initialize(const std::string& cache_dir,
                                 std::size_t byte_budget) {
    cache_ = std::make_unique<CacheManager>();
    if (!cache_->initialize(cache_dir, byte_budget)) {
        cache_.reset();
        return false;
    }

    running_ = true;
    worker_thread_ = std::thread(&ArtworkManager::worker_loop, this);
    return true;
}

void ArtworkManager::shutdown() {
    if (running_.exchange(false)) {
        queue_cv_.notify_all();
        if (worker_thread_.joinable()) worker_thread_.join();
    }

    // Drop queued work and callbacks that may capture objects which are being
    // torn down; no worker can be running at this point.
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::priority_queue<QueuedRequest> empty;
        std::swap(queue_, empty);
        extract_fn_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        in_flight_.clear();
        pending_callbacks_.clear();
    }

    if (cache_) cache_->shutdown();
}

void ArtworkManager::set_extract_fn(ExtractFn fn) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    extract_fn_ = std::move(fn);
}

bool ArtworkManager::request_artwork(const ArtworkRequest& request,
                                      ArtworkReadyCallback cb) {
    // If already cached, deliver immediately (no extraction needed).
    {
        CacheManagerEntry entry;
        if (cache_ && cache_->get(request.key, entry)) {
            if (cb) cb(request.key, entry.file_path);
            return true;
        }
    }

    // Lock ordering: always take in_flight_mutex_ before queue_mutex_.
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_.find(request.key);
        if (it != in_flight_.end()) {
            // Already in flight: remember this callback too so every
            // requester is notified when extraction completes.
            pending_callbacks_[request.key].push_back(std::move(cb));
            return true;
        }
        // Mark in flight and store the first callback.
        in_flight_.insert(request.key);
        pending_callbacks_[request.key].push_back(std::move(cb));
    }

    QueuedRequest queued;
    queued.request = request;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(std::move(queued));
    }
    queue_cv_.notify_one();
    return true;
}

bool ArtworkManager::get_cached_artwork(const std::string& key,
                                         std::string& artwork_path) const {
    if (!cache_) return false;
    CacheManagerEntry entry;
    if (!cache_->get(key, entry)) return false;
    artwork_path = entry.file_path;
    return true;
}

void ArtworkManager::invalidate(const std::string& key) {
    if (cache_) cache_->remove(key);
}

void ArtworkManager::fire_callback(ArtworkReadyCallback cb,
                                    const std::string& key,
                                    const std::string& artwork_path) {
    if (!cb) return;
    try {
        cb(key, artwork_path);
    } catch (const std::exception& e) {
        std::cerr << "ArtworkManager: callback threw: " << e.what()
                  << std::endl;
    } catch (...) {
        std::cerr << "ArtworkManager: callback threw unknown exception"
                  << std::endl;
    }
}

std::string ArtworkManager::extract_folder_artwork(
    const std::string& file_path, int /*max_size*/) const {
    std::filesystem::path p(file_path);
    if (!p.has_parent_path()) return {};

    const std::filesystem::path dir = p.parent_path();
    // Common folder artwork names, in preference order.
    const std::vector<std::string> candidates = {
        "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg",
        "front.jpg", "front.png", "album.jpg", "album.png",
    };

    std::error_code ec;
    for (const auto& name : candidates) {
        std::filesystem::path candidate = dir / name;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate.string();
        }
    }
    return {};
}

void ArtworkManager::process_request(const QueuedRequest& queued) {
    const ArtworkRequest& req = queued.request;

    std::string artwork_path;

    // 1. Check cache first.
    CacheManagerEntry entry;
    if (cache_ && cache_->get(req.key, entry)) {
        artwork_path = entry.file_path;
    }

    // 2. Otherwise extract.
    if (artwork_path.empty()) {
        // Read extract_fn_ under the queue mutex so a concurrent
        // set_extract_fn() cannot race with the worker.
        ExtractFn extract_fn;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            extract_fn = extract_fn_;
        }

        std::string extracted;
        if (extract_fn) {
            extracted = extract_fn_(req.file_path, req.key, req.max_size);
        } else if (!req.file_path.empty()) {
            extracted = extract_folder_artwork(req.file_path, req.max_size);
        }

        if (!extracted.empty()) {
            // Read the extracted file size for budgeting; on error treat as 0
            // rather than casting uintmax_t(-1) to a huge size.
            std::size_t byte_size = 0;
            std::error_code ec;
            std::uintmax_t fsize = std::filesystem::file_size(extracted, ec);
            if (!ec) {
                byte_size = static_cast<std::size_t>(fsize);
            } else {
                std::cerr << "ArtworkManager: failed to get size of "
                          << extracted << ": " << ec.message() << std::endl;
            }

            // Store a copy in the cache under the stable key.
            std::string cached_path = cache_->cache_path_for_key(req.key);

            // Copy extracted file -> cache path, checking the streams.
            std::ifstream src(extracted, std::ios::binary);
            std::ofstream dst(cached_path, std::ios::binary);
            bool copy_ok = static_cast<bool>(src) && static_cast<bool>(dst);
            if (copy_ok) {
                dst << src.rdbuf();
                copy_ok = static_cast<bool>(dst);
            }
            if (!copy_ok) {
                std::cerr << "ArtworkManager: failed to copy extracted artwork "
                          << "to cache path " << cached_path << std::endl;
            } else if (cache_->put(req.key, cached_path, byte_size,
                                   /*ttl_seconds=*/0)) {
                artwork_path = cached_path;
            } else {
                std::cerr << "ArtworkManager: cache rejected entry for "
                          << req.key << std::endl;
            }
        }
    }

    // Collect all pending callbacks for this key and clear in-flight state,
    // so every requester is notified.
    std::vector<ArtworkReadyCallback> cbs;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        in_flight_.erase(req.key);
        auto it = pending_callbacks_.find(req.key);
        if (it != pending_callbacks_.end()) {
            cbs = std::move(it->second);
            pending_callbacks_.erase(it);
        }
    }

    for (auto& cb : cbs) {
        fire_callback(cb, req.key, artwork_path);
    }
}

void ArtworkManager::worker_loop() {
    while (running_) {
        QueuedRequest queued;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !queue_.empty();
            });
            if (!running_ && queue_.empty()) return;
            if (queue_.empty()) continue;
            queued = std::move(queue_.top());
            queue_.pop();
        }
        process_request(queued);
    }
}

}  // namespace lollypop
