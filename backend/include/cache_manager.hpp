/**
 * Lollypop C++ - CacheManager
 *
 * Shared cache infrastructure for artwork, lyrics, thumbnails, etc.
 *
 * Design (plan.md Section 13.5):
 *
 *   - LRU cache implemented with a hash map + doubly linked list.
 *   - Byte budget rather than a simple item-count limit (a 3000x3000 image
 *     and a 128x128 thumbnail do not count as equal memory usage).
 *   - Disk thumbnail cache keyed by (hash, size) so the same artwork can
 *     have multiple cached sizes.
 *   - Tracks cache metadata, invalidates stale entries, removes expired
 *     files, and cleans orphaned cache entries.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace lollypop {

struct CacheManagerEntry {
    std::string key;
    std::string file_path;      // where the cached data lives on disk
    std::size_t byte_size = 0;  // in-memory / on-disk size for budgeting
    int64_t created_at = 0;     // epoch seconds
    int64_t accessed_at = 0;    // epoch seconds, for LRU ordering
    int64_t ttl_seconds = 0;    // 0 = no expiry
    bool synced = false;        // whether the cached content is timed/synced
    std::string source;         // provenance of the cached content
};

class CacheManager {
public:
    using EvictionCallback = std::function<void(const std::string& key,
                                                 const std::string& file_path)>;

    CacheManager();
    ~CacheManager();

    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // Configure the cache root directory and the total byte budget.
    bool initialize(const std::string& cache_dir, std::size_t byte_budget);
    void shutdown();

    // Look up an entry by key. Returns true if found and not expired.
    // Updates the access time (LRU ordering).
    bool get(const std::string& key, CacheManagerEntry& out);

    // Insert or update an entry. Evicts LRU entries while over budget.
    // Returns false if the entry itself exceeds the byte budget.
    bool put(const std::string& key, const std::string& file_path,
             std::size_t byte_size, int64_t ttl_seconds = 0,
             bool synced = false, const std::string& source = {});

    // Remove an entry by key and delete its backing file.
    bool remove(const std::string& key);

    // Invalidate all entries older than max_age_seconds.
    void invalidate_older_than(int64_t max_age_seconds);

    // Remove cache files on disk that are not tracked by any entry
    // (orphan cleanup).
    void clean_orphans();

    // Accessors
    std::size_t byte_budget() const { return byte_budget_; }
    std::size_t byte_used() const { return byte_used_; }
    std::size_t entry_count() const { return map_.size(); }
    const std::string& cache_dir() const { return cache_dir_; }

    // Returns the on-disk path CacheManager would use for a given key.
    // Useful for callers that want to stage a file before calling put().
    std::string cache_path_for_key(const std::string& key) const;

    void set_eviction_callback(EvictionCallback cb) {
        eviction_cb_ = std::move(cb);
    }

private:
    struct Node {
        std::string key;
        CacheManagerEntry entry;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    mutable std::mutex mutex_;
    std::string cache_dir_;
    std::size_t byte_budget_ = 0;
    std::size_t byte_used_ = 0;

    // Hash map: key -> node.
    std::unordered_map<std::string, Node*> map_;
    // Doubly linked list: head = most recently used, tail = LRU.
    Node* head_ = nullptr;
    Node* tail_ = nullptr;

    EvictionCallback eviction_cb_;

    // List manipulation (caller must hold mutex_).
    void detach(Node* node);
    void attach_head(Node* node);
    // Evict LRU entries until byte_used_ + needed <= byte_budget_.
    // Returns the list of evicted (key, file_path) pairs; the caller invokes
    // the eviction callback outside the lock to avoid deadlocks.
    std::vector<std::pair<std::string, std::string>> evict_to_fit(
        std::size_t needed);
    int64_t now_seconds() const;
};

}  // namespace lollypop
