/**
 * Lollypop C++ - CacheManager Implementation
 */

#include "cache_manager.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lollypop {

CacheManager::CacheManager() = default;

CacheManager::~CacheManager() {
    shutdown();
}

bool CacheManager::initialize(const std::string& cache_dir,
                               std::size_t byte_budget) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_dir_ = cache_dir;
    byte_budget_ = byte_budget;
    byte_used_ = 0;

    // Clear any existing state so re-initializing cannot corrupt accounting.
    while (tail_) {
        Node* lru = tail_;
        detach(lru);
        if (!lru->entry.file_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(lru->entry.file_path, ec);
        }
        map_.erase(lru->key);
        delete lru;
    }
    head_ = nullptr;
    tail_ = nullptr;
    map_.clear();

    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    if (ec) {
        std::cerr << "CacheManager: cannot create cache directory "
                  << cache_dir_ << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

void CacheManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Drop all entries and their backing files.
    while (tail_) {
        Node* lru = tail_;
        detach(lru);
        if (!lru->entry.file_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(lru->entry.file_path, ec);
        }
        map_.erase(lru->key);
        byte_used_ -= lru->entry.byte_size;
        delete lru;
    }
    head_ = nullptr;
    tail_ = nullptr;
    map_.clear();
    byte_used_ = 0;
}

int64_t CacheManager::now_seconds() const {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string CacheManager::cache_path_for_key(const std::string& key) const {
    // Derive a safe filename from the key (hash-like). Keep it simple and
    // collision-resistant enough for cache usage.
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), '/', '_');
    return cache_dir_ + "/" + safe + ".cache";
}

bool CacheManager::get(const std::string& key, CacheManagerEntry& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) return false;

    Node* node = it->second;
    const int64_t now = now_seconds();

    // Expiry check.
    if (node->entry.ttl_seconds > 0 &&
        now - node->entry.created_at > node->entry.ttl_seconds) {
        // Expired: remove it.
        detach(node);
        if (!node->entry.file_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(node->entry.file_path, ec);
        }
        byte_used_ -= node->entry.byte_size;
        map_.erase(it);
        delete node;
        return false;
    }

    // Update access time and promote to head (most recently used).
    node->entry.accessed_at = now;
    detach(node);
    attach_head(node);

    out = node->entry;
    return true;
}

bool CacheManager::put(const std::string& key, const std::string& file_path,
                        std::size_t byte_size, int64_t ttl_seconds,
                        bool synced, const std::string& source) {
    if (byte_size == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    const int64_t now = now_seconds();

    // If the entry already exists, replace it.
    auto it = map_.find(key);
    if (it != map_.end()) {
        Node* node = it->second;
        // Remove old backing file if the path changed.
        if (node->entry.file_path != file_path &&
            !node->entry.file_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(node->entry.file_path, ec);
        }
        // Take the node out of the LRU list so it cannot be evicted while we
        // make room for the new size.
        byte_used_ -= node->entry.byte_size;
        detach(node);
        // Evict other LRU entries to make room for the new size.
        auto evicted = evict_to_fit(byte_size);
        node->entry.file_path = file_path;
        node->entry.byte_size = byte_size;
        node->entry.created_at = now;
        node->entry.accessed_at = now;
        node->entry.ttl_seconds = ttl_seconds;
        node->entry.synced = synced;
        node->entry.source = source;
        attach_head(node);
        byte_used_ += byte_size;
        // Invoke eviction callbacks outside the lock.
        for (auto& [evicted_key, evicted_path] : evicted) {
            if (eviction_cb_) eviction_cb_(evicted_key, evicted_path);
        }
        return true;
    }

    // An entry larger than the whole budget can never be cached.
    if (byte_size > byte_budget_) return false;

    Node* node = new Node;
    node->key = key;
    node->entry.key = key;
    node->entry.file_path = file_path;
    node->entry.byte_size = byte_size;
    node->entry.created_at = now;
    node->entry.accessed_at = now;
    node->entry.ttl_seconds = ttl_seconds;
    node->entry.synced = synced;
    node->entry.source = source;

    byte_used_ += byte_size;
    auto evicted = evict_to_fit(0);
    for (auto& [evicted_key, evicted_path] : evicted) {
        if (eviction_cb_) eviction_cb_(evicted_key, evicted_path);
    }

    attach_head(node);
    map_[key] = node;
    return true;
}

bool CacheManager::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) return false;

    Node* node = it->second;
    detach(node);
    if (!node->entry.file_path.empty()) {
        std::error_code ec;
        std::filesystem::remove(node->entry.file_path, ec);
    }
    byte_used_ -= node->entry.byte_size;
    map_.erase(it);
    delete node;
    return true;
}

void CacheManager::invalidate_older_than(int64_t max_age_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_seconds();

    // Walk from tail (LRU) to head; stop once we hit an entry young enough.
    Node* node = tail_;
    while (node) {
        Node* prev = node->prev;
        if (now - node->entry.created_at > max_age_seconds) {
            detach(node);
            if (!node->entry.file_path.empty()) {
                std::error_code ec;
                std::filesystem::remove(node->entry.file_path, ec);
            }
            byte_used_ -= node->entry.byte_size;
            map_.erase(node->key);
            delete node;
        }
        node = prev;
    }
}

void CacheManager::clean_orphans() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Build the set of tracked file paths.
    std::vector<std::string> tracked;
    tracked.reserve(map_.size());
    for (const auto& [key, node] : map_) {
        if (!node->entry.file_path.empty()) {
            tracked.push_back(node->entry.file_path);
        }
    }

    // Scan the cache directory for files we don't track.
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(cache_dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string path = entry.path().string();
        if (std::find(tracked.begin(), tracked.end(), path) ==
            tracked.end()) {
            std::filesystem::remove(path, ec);
        }
    }
}

void CacheManager::detach(Node* node) {
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (head_ == node) head_ = node->next;
    if (tail_ == node) tail_ = node->prev;
    node->prev = nullptr;
    node->next = nullptr;
}

void CacheManager::attach_head(Node* node) {
    node->next = head_;
    node->prev = nullptr;
    if (head_) head_->prev = node;
    head_ = node;
    if (!tail_) tail_ = node;
}

std::vector<std::pair<std::string, std::string>> CacheManager::evict_to_fit(
    std::size_t needed) {
    std::vector<std::pair<std::string, std::string>> evicted;
    while (tail_ && byte_used_ + needed > byte_budget_) {
        Node* lru = tail_;
        detach(lru);
        if (!lru->entry.file_path.empty()) {
            evicted.emplace_back(lru->key, lru->entry.file_path);
            std::error_code ec;
            std::filesystem::remove(lru->entry.file_path, ec);
        }
        byte_used_ -= lru->entry.byte_size;
        map_.erase(lru->key);
        delete lru;
    }
    return evicted;
}

}  // namespace lollypop
