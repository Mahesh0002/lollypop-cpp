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
    file_paths_.clear();
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

std::string ArtworkCache::make_cache_key(const std::string& key, int bucket) {
    return key + ":" + std::to_string(bucket);
}

std::string ArtworkCache::make_cache_path(const std::string& key,
                                           int bucket) const {
    unsigned long hash = 5381;
    const std::string composite = make_cache_key(key, bucket);
    for (char c : composite) hash = ((hash << 5) + hash) + c;
    char buf[64];
    snprintf(buf, sizeof(buf), "%lx_%d", hash, bucket);
    return cache_dir_ + "/" + std::string(buf) + ".png";
}

std::optional<CacheEntry> ArtworkCache::lookup(const std::string& key,
                                                int bucket) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string composite = make_cache_key(key, bucket);
    auto it = entries_.find(composite);
    if (it == entries_.end()) return std::nullopt;

    lru_order_.erase(it->second.lru_it);
    lru_order_.push_back(composite);
    it->second.lru_it = std::prev(lru_order_.end());

    return it->second;
}

void ArtworkCache::store(const std::string& key, GdkTexture* texture_ref,
                          const std::string& source_file_path, int bucket) {
    if (!texture_ref) return;

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string composite = make_cache_key(key, bucket);
    auto existing = entries_.find(composite);
    if (existing != entries_.end()) {
        if (existing->second.texture) {
            g_object_unref(existing->second.texture);
            // Subtract the actual byte size of the old entry
            memory_usage_ -= existing->second.byte_size;
        }
        lru_order_.erase(existing->second.lru_it);
        entries_.erase(existing);
    }

    g_object_ref(texture_ref);

    CacheEntry entry;
    entry.texture = texture_ref;
    entry.key = key;
    entry.bucket = bucket;
    entry.file_path = source_file_path;  // Use source path for MPRIS artUrl

    gint tw = gdk_texture_get_width(texture_ref);
    gint th = gdk_texture_get_height(texture_ref);
    entry.byte_size = static_cast<size_t>(tw) * th * 4;
    memory_usage_ += entry.byte_size;

    lru_order_.push_back(composite);
    entry.lru_it = std::prev(lru_order_.end());
    entries_[composite] = entry;
    file_paths_[key] = source_file_path;

    evict_lru();
}

std::string ArtworkCache::get_file_path(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = file_paths_.find(key);
    if (it == file_paths_.end()) return "";
    return it->second;
}

void ArtworkCache::evict(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.key != key) {
            ++it;
            continue;
        }
        if (it->second.texture) {
            g_object_unref(it->second.texture);
            // Subtract the actual byte size
            memory_usage_ -= it->second.byte_size;
        }
        lru_order_.erase(it->second.lru_it);
        it = entries_.erase(it);
    }
    file_paths_.erase(key);
}

void ArtworkCache::evict_lru() {
    while (memory_usage_ > max_memory_bytes_ && !lru_order_.empty()) {
        const std::string composite = lru_order_.front();
        lru_order_.pop_front();
        auto it = entries_.find(composite);
        if (it == entries_.end()) continue;

        const std::string base_key = it->second.key;
        if (it->second.texture) {
            g_object_unref(it->second.texture);
            // Subtract the actual byte size
            memory_usage_ -= it->second.byte_size;
        }
        entries_.erase(it);

        bool still_cached = false;
        for (const auto& entry : entries_) {
            if (entry.second.key == base_key) {
                still_cached = true;
                break;
            }
        }
        if (!still_cached) file_paths_.erase(base_key);
    }
}

void ArtworkCache::set_max_memory(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_memory_bytes_ = bytes;
    evict_lru();
}

} // namespace lollypop