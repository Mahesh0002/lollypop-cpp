/**
 * Lollypop C++ - ArtworkCache Implementation
 */

#include "data/ArtworkCache.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <iostream>
#include <filesystem>

namespace lollypop {

struct ArtworkCache::Impl {
    std::string cache_dir;
};

ArtworkCache::ArtworkCache() : impl_(std::make_unique<Impl>()) {}

ArtworkCache::~ArtworkCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, entry] : entries_) {
        if (entry.texture) g_object_unref(entry.texture);
    }
    entries_.clear();
    lru_order_.clear();
}

bool ArtworkCache::initialize(const std::string& cache_dir,
                               size_t max_memory_bytes) {
    cache_dir_ = cache_dir;
    max_memory_bytes_ = max_memory_bytes;

    try {
        std::filesystem::create_directories(cache_dir_);
    } catch (const std::exception& e) {
        std::cerr << "ArtworkCache: cannot create cache dir: " << e.what()
                  << std::endl;
        return false;
    }
    return true;
}

int ArtworkCache::select_bucket(int logical_display_size, double scale_factor) {
    int physical = static_cast<int>(logical_display_size * scale_factor);
    if (physical <= 64) return static_cast<int>(ArtworkBucket::Small64);
    if (physical <= 160) return static_cast<int>(ArtworkBucket::Medium160);
    if (physical <= 512) return static_cast<int>(ArtworkBucket::Large512);
    return static_cast<int>(ArtworkBucket::XLarge1024);
}

std::string ArtworkCache::make_cache_path(const std::string& key,
                                           int bucket) const {
    unsigned long hash = 5381;
    for (char c : key) hash = ((hash << 5) + hash) + c;
    char buf[64];
    snprintf(buf, sizeof(buf), "%lx_%d", hash, bucket);
    return cache_dir_ + "/" + std::string(buf) + ".png";
}

std::optional<CacheEntry> ArtworkCache::lookup(const std::string& key,
                                                int bucket) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return std::nullopt;

    lru_order_.erase(it->second.lru_it);
    lru_order_.push_back(key);
    it->second.lru_it = std::prev(lru_order_.end());

    return it->second;
}

void ArtworkCache::store(const std::string& key, GdkTexture* texture_ref,
                          const std::string& source_file_path, int bucket) {
    if (!texture_ref) return;

    std::lock_guard<std::mutex> lock(mutex_);

    auto existing = entries_.find(key);
    if (existing != entries_.end()) {
        if (existing->second.texture) {
            g_object_unref(existing->second.texture);
            memory_usage_ -= 64 * 64 * 4;
        }
        lru_order_.erase(existing->second.lru_it);
        entries_.erase(existing);
    }

    g_object_ref(texture_ref);

    CacheEntry entry;
    entry.texture = texture_ref;
    entry.bucket = bucket;
    entry.file_path = source_file_path;  // Use source path for MPRIS artUrl

    gint tw = gdk_texture_get_width(texture_ref);
    gint th = gdk_texture_get_height(texture_ref);
    memory_usage_ += static_cast<size_t>(tw) * th * 4;

    lru_order_.push_back(key);
    entry.lru_it = std::prev(lru_order_.end());
    entries_[key] = entry;

    evict_lru();
}

std::string ArtworkCache::get_file_path(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return "";
    return it->second.file_path;
}

void ArtworkCache::evict(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return;
    if (it->second.texture) {
        g_object_unref(it->second.texture);
        memory_usage_ -= 64 * 64 * 4;
    }
    lru_order_.erase(it->second.lru_it);
    entries_.erase(it);
}

void ArtworkCache::evict_lru() {
    while (memory_usage_ > max_memory_bytes_ && !lru_order_.empty()) {
        const std::string& key = lru_order_.front();
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            if (it->second.texture) {
                g_object_unref(it->second.texture);
                memory_usage_ -= 64 * 64 * 4;
            }
            lru_order_.pop_front();
            entries_.erase(it);
        } else {
            lru_order_.pop_front();
        }
    }
}

void ArtworkCache::set_max_memory(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_memory_bytes_ = bytes;
    evict_lru();
}

} // namespace lollypop
