/**
 * Lollypop C++ - ArtworkCache
 *
 * File-backed artwork cache with size-bucketed GdkTexture LRU cache.
 * Used for MPRIS artUrl (must point to a real accessible file) and
 * in-app artwork display.
 *
 * Plan Section 11.5 / 10.10 / 21.4:
 *   - Stable cache location, file-backed representation for MPRIS
 *   - Size-bucketed GdkTexture LRU cache (64/160/512/1024 px buckets)
 *   - Bucket selection by physical pixel = logical × scale_factor
 *   - Background thumbnail generation
 *   - artUrl points to an existing file (never deleted temp file)
 */

#pragma once

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <list>
#include <optional>

namespace lollypop {

/// Artwork thumbnail size buckets (logical pixels).
enum class ArtworkBucket {
    Small64 = 64,    ///< Small thumbnails (queue items, sidebar)
    Medium160 = 160, ///< Medium cards and rows
    Large512 = 512,  ///< Large artwork (right panel normal mode)
    XLarge1024 = 1024,///< 2× scale or high-DPI display
};

struct CacheEntry {
    GdkTexture* texture = nullptr;   ///< Owned reference (ref-counted)
    std::string key;                 ///< Base artwork key (without bucket)
    std::string file_path;           ///< Real file on disk (for MPRIS artUrl)
    int bucket = 0;                  ///< Which bucket this was generated for
    size_t byte_size = 0;            ///< Actual memory size of this texture (width * height * 4)
    std::list<std::string>::iterator lru_it;
};

class ArtworkCache {
public:
    ArtworkCache();
    ~ArtworkCache();

    ArtworkCache(const ArtworkCache&) = delete;
    ArtworkCache& operator=(const ArtworkCache&) = delete;

    /// Initialize the cache with a directory and max memory budget (bytes).
    bool initialize(const std::string& cache_dir, size_t max_memory_bytes = 64ull * 1024 * 1024);

    /// Look up artwork for a track/album/artist key at the given bucket.
    /// Returns texture (ref-counted, caller should not unref) and file path.
    std::optional<CacheEntry> lookup(const std::string& key, int bucket);

    /// Store artwork in the cache. Takes ownership of texture_ref (ref it once).
    /// Writes a file-backed copy to cache_dir for MPRIS artUrl.
    void store(const std::string& key, GdkTexture* texture_ref,
               const std::string& source_file_path, int bucket);

    /// Select the appropriate bucket for a display size at a given scale factor.
    /// physical_size = logical_display_size × scale_factor.
    static int select_bucket(int logical_display_size, double scale_factor = 1.0);

    /// Get the file path for a key (for MPRIS artUrl). Returns empty if not cached.
    std::string get_file_path(const std::string& key) const;

    /// Evict a specific key from the cache.
    void evict(const std::string& key);

    /// Get current memory usage estimate (bytes).
    size_t memory_usage() const { return memory_usage_; }

    /// Set max memory budget and evict if over.
    void set_max_memory(size_t bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex mutex_;
    size_t max_memory_bytes_ = 64ull * 1024 * 1024;
    size_t memory_usage_ = 0;

    // LRU order: most recently used at back. Entries are keyed by
    // "<artwork key>:<bucket>" so the same image can be cached at several sizes.
    std::list<std::string> lru_order_;
    std::unordered_map<std::string, CacheEntry> entries_;
    std::unordered_map<std::string, std::string> file_paths_;

    std::string cache_dir_;

    static std::string make_cache_key(const std::string& key, int bucket);
    void evict_lru();
    std::string make_cache_path(const std::string& key, int bucket) const;
    GdkTexture* load_texture_from_file(const std::string& path, int max_dim) const;
};

} // namespace lollypop
