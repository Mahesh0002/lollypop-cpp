# Complete Frontend-Backend Integration & Optimization Plan
## Lollypop C++ — Unified Architecture Document

> **Project:** Lollypop C++  
> **Status:** Planning — Complete Architecture  
> **Scope:** Full integration of frontend UI with BackendController storage/services + algorithmic optimizations using advanced data structures  
> **Generated:** 2026-11-21

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Current State Analysis](#2-current-state-analysis)
3. [Integration Gaps](#3-integration-gaps)
4. [Data Structure Topology](#4-data-structure-topology)
5. [Optimized Pipeline Implementations](#5-optimized-pipeline-implementations)
6. [Frontend Data Structures](#6-frontend-data-structures)
7. [Integration Layer](#7-integration-layer)
8. [Implementation Phases](#8-implementation-phases)
9. [Performance Targets](#9-performance-targets)
10. [Memory Budget](#10-memory-budget)
11. [Acceptance Criteria](#11-acceptance-criteria)

---

## 1. Architecture Overview

### 1.1 Backend (BackendController)
The `BackendController` in `backend/src/controller.cpp` implements **three service interfaces**:
- `IPlayerService` — Playback control, state, queue management
- `ILibraryService` — Library queries (albums, artists, tracks, playlists, search, sorting)
- `ILyricsService` — Lyrics retrieval and storage

**Key subsystems owned by BackendController:**
| Subsystem | Purpose |
|-----------|---------|
| `StorageManager` | SQLite database (tracks, albums, artists, playlists, lyrics, preferences) |
| `AudioEngine` | GStreamer-based playback |
| `LibraryScanner` + `LibraryManager` | Async library scanning with unique work queue |
| `MprisServer` | MPRIS D-Bus service (this app as a player) |
| `MprisManager` | MPRIS D-Bus client (external players) |
| `LyricsManager` | Embedded/local/external lyrics fetch, parse, sync |
| `CacheManager` + `ArtworkManager` | Thumbnail cache with size buckets |
| `VisualizerEngine` | GStreamer spectrum element → EventBus |
| `FileWatcher` | inotify-based library change detection |
| `QueueManager` | Shuffle, repeat, next/previous logic |

**Callback marshaling:** All UI callbacks (`track_changed_cb_`, `position_changed_cb_`, `playback_state_changed_cb_`, `queue_changed_cb_`, `visualization_cb_`, `library_changed_cb_`) are emitted from worker threads and marshaled to the GTK main context via `Glib::Dispatcher`.

---

### 1.2 Frontend (Current State)
The frontend in `frontend/src/` uses **GTK 4 + libadwaita C APIs** directly from C++.

**Key UI panels:**
| Panel | File | Current Backend Connection |
|-------|------|---------------------------|
| `MainWindow` | `main_window.cpp` | ✅ Connected to BackendController callbacks |
| `LeftSidebar` | `left_sidebar.cpp` | Navigation only, no data |
| `MiddleLibrary` | `middle_library.cpp` | ✅ Passes `ILibraryService*` to pages |
| `RightNowPlaying` | `right_now_playing.cpp` | ✅ Track info, position, waveform, lyrics, queue |
| `BottomPlayback` | `bottom_playback.cpp` | ✅ Track info, position, waveform, controls |

**Library pages** (in `frontend/src/ui/pages/`):
- `HomePage` — Uses `ILibraryService` for recently added/played, favourite albums
- `AlbumsPage` — Uses `ILibraryService::get_all_albums()`
- `ArtistsPage` — Uses `ILibraryService::get_all_artists()`
- `PlaylistsPage` — Uses `ILibraryService::get_all_playlists()`
- `GenresPage` — Uses `ILibraryService::get_all_genres()`
- `SongsPage` — Uses `ILibraryService::get_sorted_track_ids()` + `get_tracks_by_ids()` (lazy)
- `FavouriteAlbumsPage` — Uses `ILibraryService::get_favourite_albums()`
- `LikedMusicPage` — Uses `ILibraryService::get_liked_tracks()`
- `AlbumDetailPage` — Uses `ILibraryService::get_album()` + `get_tracks_by_ids()`
- `ArtistDetailPage` — Uses `ILibraryService::get_artist()` + `get_tracks_by_artist()`

**Service implementations:**
- `MockLibraryService` — Returns empty data (development only)
- `MockLyricsService` — Returns `std::nullopt` (development only)

---

## 2. Current State Analysis

### 2.1 Backend Strengths
- **Staged initialization** with rollback on failure
- **EventBus** for thread-safe spectrum delivery
- **LibraryManager** with unique work queue + batched transactions
- **QueueManager** with Fisher-Yates shuffle + play-next FIFO
- **CacheManager** with LRU doubly-linked list + TTL expiry
- **VisualizerEngine** with bus sync handler + conflation
- **FileWatcher** with dispatcher for main-context safety
- **MPRIS server + client** dual support

### 2.2 Frontend Strengths
- **Four-panel layout** per specification
- **GtkStack** for page transitions
- **ArtworkView** component with rounded corners
- **WaveformView** with shared VisualizerModel
- **TrackRow** / **AlbumCard** reusable components
- **ThemeManager** with CSS custom properties

### 2.3 Critical Gaps
| Area | Gap |
|------|-----|
| Library Pages | Some may still use mock fallbacks; need verification |
| SongsPage | Lazy loading not implemented; needs `GListModel` wrapper |
| Search | UI exists but not connected to `search_tracks()` |
| RightNowPlaying | Queue not auto-updated via `queue_changed_cb_`; lyrics not auto-requested; mode buttons disabled |
| BottomPlayback | No volume slider; no playback mode buttons |
| Artwork | `ArtworkView` loads synchronously; no async decode, no cache, no fallback |
| Lyrics | `LyricsManager` not verified for embedded/local fetch |
| Visualization | Simple vector assignment; no smoothing, no double-buffering |

---

## 3. Integration Gaps

### 3.1 Library Pages → BackendController (ILibraryService)

| Page | Required ILibraryService Methods | Gap |
|------|----------------------------------|-----|
| `HomePage` | `get_recently_added()`, `get_recently_played()`, `get_favourite_albums()` | Verify no mock fallbacks |
| `AlbumsPage` | `get_all_albums()` | Verify artwork loading via `get_artwork_path_for_album()` |
| `ArtistsPage` | `get_all_artists()` | Verify artwork loading via `get_artwork_path_for_artist()` |
| `PlaylistsPage` | `get_all_playlists()`, `get_playlist_tracks()` | Verify playlist track counts |
| `GenresPage` | `get_all_genres()`, `get_tracks_by_genre()` | Verify genre filtering |
| `SongsPage` | `get_sorted_track_ids()`, `get_tracks_by_ids()` | **Critical:** Lazy loading + DB-backed sort |
| `FavouriteAlbumsPage` | `get_favourite_albums()` | Verify favourite toggle |
| `LikedMusicPage` | `get_liked_tracks()` | Verify like toggle |
| `AlbumDetailPage` | `get_album()`, `get_tracks_by_ids()` | Verify track list + play actions |
| `ArtistDetailPage` | `get_artist()`, `get_tracks_by_artist()` | Verify albums + tracks |

### 3.2 RightNowPlaying → BackendController

| Feature | Current | Required |
|---------|---------|----------|
| Track info | ✅ Via `track_changed_cb_` | ✅ |
| Playback position | ✅ Via `position_changed_cb_` | ✅ |
| Waveform | ✅ Via `visualization_cb_` | ✅ |
| Playback modes (shuffle/repeat/loop) | ❌ UI buttons disabled | Connect `set_shuffle`, `set_repeat`, `set_repeat_one` callbacks |
| Queue | ❌ Manual `populate_queue()` | Connect `queue_changed_cb_` → auto-update |
| Lyrics | ❌ Manual `request_lyrics_for_track()` | Connect `track_changed_cb_` → auto-request lyrics |
| Previous/Next buttons | ✅ Connected to backend | ✅ |

### 3.3 BottomPlayback → BackendController

| Feature | Current | Required |
|---------|---------|----------|
| Track info | ✅ Via `track_changed_cb_` | ✅ |
| Playback position | ✅ Via `position_changed_cb_` | ✅ |
| Waveform | ✅ Via `visualization_cb_` | ✅ |
| Play/Pause/Prev/Next | ✅ Buttons disabled (shell authority) | ✅ Keep disabled |
| Volume | ❌ Not connected | Add volume slider callback |
| Playback modes | ❌ Not shown | Add shuffle/repeat/loop buttons |

### 3.4 Search → BackendController

| Component | Current | Required |
|-----------|---------|----------|
| Search entry (MiddleLibrary header) | UI only | Connect to `ILibraryService::search_tracks()` with debounce + generation counter |
| Search results page | Not implemented | Create `SearchResultsPage` using lazy model |

### 3.5 Artwork Pipeline

| Stage | Current | Required |
|-------|---------|----------|
| Backend: `ArtworkManager` + `CacheManager` | ✅ Implemented | Verify bucket sizes (64/160/512/1024) |
| Backend: `get_artwork_path_for_*` | ✅ Implemented | Returns cached file path for MPRIS `artUrl` |
| Frontend: `ArtworkView` | Loads from path | Use `GdkTexture` from file, async decode, cache reuse |
| Frontend: Fallback artwork | Placeholder | Implement generated fallback (colored gradient) |

---

## 4. Data Structure Topology

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                         DATA STRUCTURE TOPOLOGY                                 │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌────────────┐  │
│  │   SCANNER    │     │  LIBRARY     │     │   STORAGE    │     │   CACHE    │  │
│  │  (Producer)  │────►│  MANAGER     │────►│  (SQLite)    │◄───►│  MANAGER   │  │
│  └──────────────┘     └──────────────┘     └──────────────┘     └────────────┘  │
│        │                    │                    │                   │           │
│        ▼                    ▼                    ▼                   ▼           │
│  ┌──────────────────────────────────────────────────────────────────────────┐   │
│  │                    EVENT BUS (Pub/Sub — Observer Pattern)                 │   │
│  └──────────────────────────────────────────────────────────────────────────┘   │
│        │                    │                    │                   │           │
│        ▼                    ▼                    ▼                   ▼           │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌────────────┐  │
│  │  QUEUE MGR   │     │  PLAYBACK    │     │  VISUALIZER  │     │  LYRICS    │  │
│  │  (Deque +    │     │  ENGINE      │     │  (Ring Buf)  │     │  MGR       │  │
│  │  Shuffle     │     │  (GStreamer) │     │  + Smoothing │     │  (Trie +   │  │
│  │  Array)      │     │              │     │  EMA Filter) │     │  LRU)      │  │
│  └──────────────┘     └──────────────┘     └──────────────┘     └────────────┘  │
│        │                    │                    │                   │           │
│        └────────────────────┼────────────────────┼───────────────────┘           │
│                             ▼                    ▼                               │
│                    ┌─────────────────────────────────────────┐                   │
│                    │         GTK MAIN CONTEXT                │                   │
│                    │  (Dispatcher → Callbacks → UI Widgets)  │                   │
│                    └─────────────────────────────────────────┘                   │
│                                                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Optimized Pipeline Implementations

### 5.1 Library Scan Pipeline — **DAG + Topological Sort + Priority Queue + Work-Stealing**

**Current:** Simple recursive directory walk with single worker thread.

**Optimized:** DAG of scan jobs with dependency resolution, priority queue for scheduling, work-stealing thread pool.

```cpp
// backend/include/scan_dag.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <thread>
#include <filesystem>

namespace lollypop {

enum class ScanJobType { Directory, File, Reconcile, ArtworkExtract, MetadataIndex };

struct ScanJob {
    ScanJobType type;
    std::string path;
    std::string parent_id;           // For DAG edges
    std::vector<std::string> deps;   // Job IDs this depends on
    int priority = 0;                // Higher = more urgent (user-visible dirs first)
    uint64_t id;                     // Unique job ID
    bool cancelled = false;
    
    // For priority queue (max-heap by priority, then by depth for fairness)
    bool operator<(const ScanJob& other) const {
        if (priority != other.priority) return priority < other.priority;
        return id > other.id;  // FIFO for same priority
    }
};

class ScanDAG {
public:
    using JobCallback = std::function<void(const ScanJob&)>;
    using CompletionCallback = std::function<void(uint64_t job_id, bool success)>;

    ScanDAG(size_t num_threads = std::thread::hardware_concurrency());
    ~ScanDAG();

    // Add a scan root — creates DAG nodes for directory tree
    uint64_t add_scan_root(const std::string& path, bool recursive, int priority = 0);
    
    // Add individual file scan
    uint64_t add_file_scan(const std::string& path, int priority = 10);
    
    // Start processing with work-stealing thread pool
    void start(JobCallback on_job_ready, CompletionCallback on_job_done);
    
    // Cancel all pending jobs
    void cancel_all();
    
    // Get progress stats
    struct Stats {
        std::atomic<size_t> pending{0};
        std::atomic<size_t> running{0};
        std::atomic<size_t> completed{0};
        std::atomic<size_t> failed{0};
    };
    Stats get_stats() const;

private:
    struct DAGNode {
        ScanJob job;
        std::atomic<int> pending_deps{0};
        std::vector<uint64_t> dependents;  // Reverse edges
        std::mutex mtx;
    };

    // DAG storage
    std::unordered_map<uint64_t, std::unique_ptr<DAGNode>> nodes_;
    std::mutex nodes_mtx_;
    
    // Priority queue of ready-to-run jobs (max-heap)
    std::priority_queue<ScanJob> ready_queue_;
    std::mutex ready_mtx_;
    std::condition_variable ready_cv_;
    
    // Thread pool
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_{false};
    
    JobCallback on_job_ready_;
    CompletionCallback on_job_done_;
    
    uint64_t next_job_id_ = 1;
    
    void worker_loop();
    void process_job(const ScanJob& job);
    void on_job_complete(uint64_t job_id, bool success);
    void release_dependents(uint64_t job_id);
    
    // Build DAG from directory tree (iterative to avoid stack overflow)
    void build_dag_from_root(const std::string& path, bool recursive, 
                              int priority, uint64_t parent_id);
};

// Work-stealing deque for load balancing
template<typename T>
class WorkStealingDeque {
    // Implementation using std::deque + atomic indices
    // Push/pop from front (owner), steal from back (thieves)
};

} // namespace lollypop
```

**Algorithm Complexity:**
| Operation | Current | Optimized |
|-----------|---------|-----------|
| Add scan root | O(n) recursive | O(n) iterative + DAG build |
| Job scheduling | FIFO queue | O(log n) priority queue |
| Parallelism | 1 thread | N threads (work-stealing) |
| Cancellation | Boolean flag | DAG propagation O(out-degree) |
| Memory | O(depth) stack | O(n) explicit DAG |

---

### 5.2 SongsPage Lazy Loading — **Segment Tree + Fenwick Tree + Skip List + Bloom Filter**

**Current:** Vector of track IDs + `get_tracks_by_ids()` for visible rows.

**Optimized:** Multi-level index structure for O(log n) access, range queries, and efficient insertion/deletion.

```cpp
// frontend/src/models/LazyTrackIndex.hpp
#pragma once

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <algorithm>

namespace lollypop {

struct Track;

/**
 * Segment Tree for range queries on sorted track IDs.
 * Supports: count in range, sum of durations, find by rank.
 */
template<typename T>
class SegmentTree {
    struct Node {
        T value;
        size_t count = 0;
        Node* left = nullptr;
        Node* right = nullptr;
    };
    
    Node* root_ = nullptr;
    size_t size_ = 0;
    std::function<T(const T&, const T&)> merge_;
    std::shared_mutex rw_lock_;
    
public:
    explicit SegmentTree(std::function<T(const T&, const T&)> merge) : merge_(merge) {}
    ~SegmentTree() { clear(root_); }
    
    void build(const std::vector<T>& data) {
        std::unique_lock lock(rw_lock_);
        clear(root_);
        root_ = build_recursive(data, 0, data.size());
        size_ = data.size();
    }
    
    // Query range [l, r)
    std::optional<T> query(size_t l, size_t r) const {
        std::shared_lock lock(rw_lock_);
        return query_recursive(root_, 0, size_, l, r);
    }
    
    // Find index of k-th element (order statistic)
    std::optional<size_t> find_kth(size_t k) const {
        std::shared_lock lock(rw_lock_);
        return find_kth_recursive(root_, 0, size_, k);
    }
    
    // Update single position
    void update(size_t idx, const T& val) {
        std::unique_lock lock(rw_lock_);
        update_recursive(root_, 0, size_, idx, val);
    }
    
    size_t size() const { 
        std::shared_lock lock(rw_lock_);
        return size_; 
    }

private:
    Node* build_recursive(const std::vector<T>& data, size_t l, size_t r) {
        if (l >= r) return nullptr;
        Node* node = new Node;
        if (r - l == 1) {
            node->value = data[l];
            node->count = 1;
        } else {
            size_t m = l + (r - l) / 2;
            node->left = build_recursive(data, l, m);
            node->right = build_recursive(data, m, r);
            node->count = (node->left ? node->left->count : 0) + 
                          (node->right ? node->right->count : 0);
            node->value = merge_(node->left ? node->left->value : T{},
                                 node->right ? node->right->value : T{});
        }
        return node;
    }
    
    std::optional<T> query_recursive(Node* node, size_t nl, size_t nr, 
                                     size_t ql, size_t qr) const {
        if (!node || ql >= nr || qr <= nl) return std::nullopt;
        if (ql <= nl && nr <= qr) return node->value;
        size_t nm = nl + (nr - nl) / 2;
        auto left = query_recursive(node->left, nl, nm, ql, qr);
        auto right = query_recursive(node->right, nm, nr, ql, qr);
        if (left && right) return merge_(*left, *right);
        return left ? left : right;
    }
    
    std::optional<size_t> find_kth_recursive(Node* node, size_t nl, size_t nr, size_t k) const {
        if (!node || k >= node->count) return std::nullopt;
        if (nr - nl == 1) return nl;
        size_t nm = nl + (nr - nl) / 2;
        size_t left_count = node->left ? node->left->count : 0;
        if (k < left_count) return find_kth_recursive(node->left, nl, nm, k);
        return find_kth_recursive(node->right, nm, nr, k - left_count);
    }
    
    void update_recursive(Node* node, size_t nl, size_t nr, size_t idx, const T& val) {
        if (!node || idx < nl || idx >= nr) return;
        if (nr - nl == 1) {
            node->value = val;
            return;
        }
        size_t nm = nl + (nr - nl) / 2;
        if (idx < nm) update_recursive(node->left, nl, nm, idx, val);
        else update_recursive(node->right, nm, nr, idx, val);
        node->value = merge_(node->left ? node->left->value : T{},
                             node->right ? node->right->value : T{});
    }
    
    void clear(Node* node) {
        if (!node) return;
        clear(node->left);
        clear(node->right);
        delete node;
    }
};

/**
 * Fenwick Tree (Binary Indexed Tree) for prefix sums.
 * O(log n) prefix sum, point update.
 * Used for: track position → global rank, playlist duration sums.
 */
class FenwickTree {
    std::vector<int64_t> bit_;
    size_t n_;
    
public:
    explicit FenwickTree(size_t n = 0) : bit_(n + 1, 0), n_(n) {}
    
    void build(const std::vector<int64_t>& arr) {
        n_ = arr.size();
        bit_.assign(n_ + 1, 0);
        for (size_t i = 0; i < n_; ++i) add(i, arr[i]);
    }
    
    // Add delta at index i (0-indexed)
    void add(size_t i, int64_t delta) {
        for (++i; i <= n_; i += i & -i) bit_[i] += delta;
    }
    
    // Prefix sum [0, i]
    int64_t sum(size_t i) const {
        int64_t res = 0;
        for (++i; i > 0; i -= i & -i) res += bit_[i];
        return res;
    }
    
    // Range sum [l, r]
    int64_t range_sum(size_t l, size_t r) const {
        return sum(r) - (l > 0 ? sum(l - 1) : 0);
    }
    
    // Find largest index with prefix sum <= target (binary lifting)
    std::optional<size_t> find_prefix(int64_t target) const {
        size_t idx = 0;
        int64_t cur = 0;
        for (size_t k = 1ull << (63 - __builtin_clzll(n_)); k; k >>= 1) {
            if (idx + k <= n_ && cur + bit_[idx + k] <= target) {
                idx += k;
                cur += bit_[idx];
            }
        }
        return idx < n_ ? std::make_optional(idx) : std::nullopt;
    }
    
    size_t size() const { return n_; }
};

/**
 * Skip List for concurrent sorted access to track IDs.
 * O(log n) expected search/insert/delete with lock-free reads.
 * Used for: real-time queue updates while UI reads.
 */
template<typename K, typename V>
class ConcurrentSkipList {
    struct Node {
        K key;
        V value;
        std::atomic<Node*> next[32];  // Max level 32
        int level;
        
        Node(const K& k, const V& v, int lvl) : key(k), value(v), level(lvl) {
            for (int i = 0; i < level; ++i) next[i].store(nullptr, std::memory_order_relaxed);
        }
    };
    
    std::atomic<Node*> head_;
    std::atomic<int> max_level_{1};
    std::mutex insert_mutex_;  // Only for structural modifications
    
    static int random_level() {
        int lvl = 1;
        while (lvl < 32 && (rand() & 1)) ++lvl;
        return lvl;
    }
    
public:
    ConcurrentSkipList() : head_(new Node(K{}, V{}, 32)) {}
    ~ConcurrentSkipList() { /* cleanup */ }
    
    // Lock-free search
    std::optional<V> find(const K& key) const {
        Node* cur = head_.load(std::memory_order_acquire);
        for (int lvl = max_level_.load() - 1; lvl >= 0; --lvl) {
            Node* next = cur->next[lvl].load(std::memory_order_acquire);
            while (next && next->key < key) {
                cur = next;
                next = cur->next[lvl].load(std::memory_order_acquire);
            }
        }
        Node* next = cur->next[0].load(std::memory_order_acquire);
        if (next && next->key == key) return next->value;
        return std::nullopt;
    }
    
    // Insert with mutex (rare)
    void insert(const K& key, const V& value) {
        std::lock_guard lock(insert_mutex_);
        std::vector<Node*> update(32);
        Node* cur = head_.load();
        for (int lvl = max_level_.load() - 1; lvl >= 0; --lvl) {
            while (cur->next[lvl].load() && cur->next[lvl].load()->key < key) {
                cur = cur->next[lvl].load();
            }
            update[lvl] = cur;
        }
        
        int new_lvl = random_level();
        if (new_lvl > max_level_.load()) {
            for (int lvl = max_level_.load(); lvl < new_lvl; ++lvl) {
                update[lvl] = head_.load();
            }
            max_level_.store(new_lvl);
        }
        
        Node* new_node = new Node(key, value, new_lvl);
        for (int lvl = 0; lvl < new_lvl; ++lvl) {
            new_node->next[lvl].store(update[lvl]->next[lvl].load(), std::memory_order_relaxed);
            update[lvl]->next[lvl].store(new_node, std::memory_order_release);
        }
    }
    
    // Range iteration for UI (returns vector of values in range)
    std::vector<V> range_query(const K& start, const K& end) const {
        std::vector<V> result;
        auto it = find_ge(start);
        while (it && it->key <= end) {
            result.push_back(it->value);
            it = it->next[0].load();
        }
        return result;
    }
};

/**
 * Main LazyTrackIndex combining all structures.
 */
class LazyTrackIndex {
    // Primary sorted ID array (for binary search + SegmentTree)
    std::vector<std::string> sorted_ids_;
    
    // SegmentTree for range metadata queries
    struct TrackSummary {
        int64_t total_duration = 0;
        int count = 0;
    };
    SegmentTree<TrackSummary> summary_tree_;
    
    // FenwickTree for duration prefix sums (seek position → track index)
    FenwickTree duration_prefix_;
    
    // SkipList for concurrent queue mutations
    ConcurrentSkipList<size_t, std::string> queue_skip_list_;
    
    // Bloom filter for fast negative track existence checks
    class BloomFilter {
        std::vector<uint64_t> bits_;
        size_t num_hashes_;
    public:
        BloomFilter(size_t expected_items, double fp_rate) {
            size_t bits = -expected_items * std::log(fp_rate) / (std::log(2) * std::log(2));
            bits_.resize((bits + 63) / 64);
            num_hashes_ = std::max(1, (int)std::round((double)bits / expected_items * std::log(2)));
        }
        void add(const std::string& key) {
            uint64_t h1 = fnv1a(key);
            uint64_t h2 = fnv1a(key + "|salt");
            for (size_t i = 0; i < num_hashes_; ++i) {
                size_t pos = (h1 + i * h2) % (bits_.size() * 64);
                bits_[pos / 64] |= 1ull << (pos % 64);
            }
        }
        bool might_contain(const std::string& key) const {
            uint64_t h1 = fnv1a(key);
            uint64_t h2 = fnv1a(key + "|salt");
            for (size_t i = 0; i < num_hashes_; ++i) {
                size_t pos = (h1 + i * h2) % (bits_.size() * 64);
                if (!(bits_[pos / 64] & (1ull << (pos % 64)))) return false;
            }
            return true;
        }
    };
    BloomFilter track_bloom_;
    
    // LRU cache for recently accessed full track objects
    class LRUCache {
        struct Node {
            std::string key;
            Track value;
            Node* prev = nullptr;
            Node* next = nullptr;
        };
        size_t capacity_;
        std::unordered_map<std::string, Node*> map_;
        Node *head_ = nullptr, *tail_ = nullptr;
        std::mutex mtx_;
        
    public:
        LRUCache(size_t cap) : capacity_(cap) {}
        std::optional<Track> get(const std::string& key) {
            std::lock_guard lock(mtx_);
            auto it = map_.find(key);
            if (it == map_.end()) return std::nullopt;
            promote(it->second);
            return it->second->value;
        }
        void put(const std::string& key, Track&& value) {
            std::lock_guard lock(mtx_);
            if (map_.size() >= capacity_) evict_tail();
            Node* node = new Node{key, std::move(value)};
            add_to_head(node);
            map_[key] = node;
        }
    private:
        void promote(Node* node) { /* move to head */ }
        void add_to_head(Node* node) { /* ... */ }
        void evict_tail() { /* ... */ }
    };
    LRUCache track_cache_{500};
    
public:
    LazyTrackIndex(size_t expected_tracks = 50000) 
        : summary_tree_([](const TrackSummary& a, const TrackSummary& b) {
            return TrackSummary{a.total_duration + b.total_duration, a.count + b.count};
        }),
        track_bloom_(expected_tracks, 0.01) {}
    
    // Build from sorted track IDs (called on sort change)
    void build(const std::vector<std::string>& ids, 
               const std::function<std::vector<Track>(const std::vector<std::string>&)>& fetcher) {
        sorted_ids_ = ids;
        
        // Build bloom filter
        track_bloom_ = BloomFilter(ids.size(), 0.01);
        for (const auto& id : ids) track_bloom_.add(id);
        
        // Fetch all tracks for summary tree (batched)
        auto tracks = fetcher(ids);
        std::vector<TrackSummary> summaries;
        summaries.reserve(tracks.size());
        std::vector<int64_t> durations;
        durations.reserve(tracks.size());
        
        for (const auto& t : tracks) {
            summaries.push_back({t.duration, 1});
            durations.push_back(t.duration);
            track_cache_.put(t.id, Track(t));
        }
        
        summary_tree_.build(summaries);
        duration_prefix_.build(durations);
    }
    
    // O(log n) range query for visible rows
    std::vector<Track> get_range(size_t start, size_t count,
                                  const std::function<std::vector<Track>(const std::vector<std::string>&)>& fetcher) {
        // Check cache first
        std::vector<std::string> to_fetch;
        std::vector<Track> result;
        result.reserve(count);
        
        for (size_t i = start; i < start + count && i < sorted_ids_.size(); ++i) {
            const auto& id = sorted_ids_[i];
            if (auto cached = track_cache_.get(id)) {
                result.push_back(*cached);
            } else {
                to_fetch.push_back(id);
            }
        }
        
        if (!to_fetch.empty()) {
            auto fetched = fetcher(to_fetch);
            for (auto& t : fetched) {
                track_cache_.put(t.id, Track(t));
                result.push_back(std::move(t));
            }
        }
        return result;
    }
    
    // O(log n) seek position → track index
    std::optional<size_t> seek_to_track(double position_seconds) {
        return duration_prefix_.find_prefix((int64_t)position_seconds);
    }
    
    // O(1) average negative check
    bool might_have_track(const std::string& id) const {
        return track_bloom_.might_contain(id);
    }
    
    size_t size() const { return sorted_ids_.size(); }
    const std::string& get_id(size_t idx) const { return sorted_ids_[idx]; }
};

} // namespace lollypop
```

---

### 5.3 Queue Manager — **Intrusive Doubly Linked List + Circular Buffer + Fisher-Yates**

**Current:** `std::deque` + shuffle array + `play_next` FIFO.

**Optimized:** Intrusive doubly linked list for O(1) move/remove, circular buffer for history, persistent shuffle state.

```cpp
// backend/include/queue_manager_optimized.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>
#include <random>
#include <algorithm>
#include <array>

namespace lollypop {

enum class RepeatMode { None, Playlist, Track };

/**
 * Intrusive doubly linked list node for queue items.
 * Allows O(1) removal, insertion, move without allocator overhead.
 */
struct QueueNode {
    std::string track_id;
    QueueNode* prev = nullptr;
    QueueNode* next = nullptr;
    size_t index = 0;  // Logical position (maintained separately)
    
    // For shuffle order
    size_t shuffle_pos = SIZE_MAX;
    
    QueueNode() = default;
    explicit QueueNode(std::string id) : track_id(std::move(id)) {}
};

/**
 * Optimized Queue Manager using:
 * - Intrusive doubly linked list (O(1) insert/remove/move)
 * - Circular buffer for play history (undo/redo)
 * - Fisher-Yates shuffle array (O(1) next/prev in shuffle)
 * - Versioned iterators for concurrent UI access
 */
class QueueManager {
    // Linked list pointers
    QueueNode* head_ = nullptr;
    QueueNode* tail_ = nullptr;
    QueueNode* current_ = nullptr;
    size_t size_ = 0;
    
    // Shuffle order: array of pointers to nodes
    std::vector<QueueNode*> shuffle_order_;
    size_t shuffle_pos_ = 0;
    bool shuffle_dirty_ = true;
    bool shuffle_enabled_ = false;
    
    // Play-next FIFO (circular buffer)
    static constexpr size_t PLAY_NEXT_CAP = 64;
    std::array<std::string, PLAY_NEXT_CAP> play_next_;
    size_t play_next_head_ = 0;
    size_t play_next_tail_ = 0;
    size_t play_next_count_ = 0;
    
    // History for "previous" in shuffle mode (circular buffer)
    static constexpr size_t HISTORY_CAP = 100;
    std::array<QueueNode*, HISTORY_CAP> history_;
    size_t history_pos_ = 0;
    size_t history_count_ = 0;
    
    // Repeat mode
    RepeatMode repeat_mode_ = RepeatMode::None;
    
    // Node pool for allocation-free operations
    std::vector<std::unique_ptr<QueueNode>> node_pool_;
    std::vector<QueueNode*> free_nodes_;
    
    // Index map for O(1) node lookup by track_id
    std::unordered_map<std::string, QueueNode*> id_to_node_;
    
    // Version counter for iterator invalidation detection
    std::atomic<uint64_t> version_{0};
    
    std::mutex mtx_;
    std::mt19937_64 rng_{std::random_device{}()};
    
    void rebuild_shuffle_order() {
        if (!shuffle_dirty_) return;
        shuffle_order_.clear();
        shuffle_order_.reserve(size_);
        for (auto* n = head_; n; n = n->next) {
            shuffle_order_.push_back(n);
        }
        // Fisher-Yates shuffle
        for (size_t i = shuffle_order_.size(); i > 1; --i) {
            std::uniform_int_distribution<size_t> dist(0, i - 1);
            size_t j = dist(rng_);
            std::swap(shuffle_order_[i - 1], shuffle_order_[j]);
        }
        // Update shuffle_pos in nodes
        for (size_t i = 0; i < shuffle_order_.size(); ++i) {
            shuffle_order_[i]->shuffle_pos = i;
        }
        shuffle_dirty_ = false;
        shuffle_pos_ = current_ ? current_->shuffle_pos : 0;
    }
    
    QueueNode* allocate_node(const std::string& track_id) {
        QueueNode* node;
        if (!free_nodes_.empty()) {
            node = free_nodes_.back();
            free_nodes_.pop_back();
            node->track_id = track_id;
            node->prev = node->next = nullptr;
        } else {
            node_pool_.emplace_back(std::make_unique<QueueNode>(track_id));
            node = node_pool_.back().get();
        }
        id_to_node_[track_id] = node;
        return node;
    }
    
    void deallocate_node(QueueNode* node) {
        id_to_node_.erase(node->track_id);
        node->track_id.clear();
        node->prev = node->next = nullptr;
        node->shuffle_pos = SIZE_MAX;
        free_nodes_.push_back(node);
    }
    
    void link_after(QueueNode* prev, QueueNode* node) {
        node->prev = prev;
        node->next = prev->next;
        if (prev->next) prev->next->prev = node;
        prev->next = node;
        if (tail_ == prev) tail_ = node;
    }
    
    void unlink(QueueNode* node) {
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (head_ == node) head_ = node->next;
        if (tail_ == node) tail_ = node->prev;
        node->prev = node->next = nullptr;
    }
    
    void add_to_history(QueueNode* node) {
        history_[history_pos_] = node;
        history_pos_ = (history_pos_ + 1) % HISTORY_CAP;
        if (history_count_ < HISTORY_CAP) ++history_count_;
    }
    
    void push_play_next(const std::string& track_id) {
        if (play_next_count_ == PLAY_NEXT_CAP) return;
        play_next_[play_next_tail_] = track_id;
        play_next_tail_ = (play_next_tail_ + 1) % PLAY_NEXT_CAP;
        ++play_next_count_;
    }
    
    std::optional<std::string> pop_play_next() {
        if (play_next_count_ == 0) return std::nullopt;
        std::string id = std::move(play_next_[play_next_head_]);
        play_next_head_ = (play_next_head_ + 1) % PLAY_NEXT_CAP;
        --play_next_count_;
        return id;
    }

public:
    QueueManager() = default;
    ~QueueManager() = default;
    
    // Versioned iterator for UI (detects concurrent modification)
    struct Iterator {
        QueueNode* node = nullptr;
        uint64_t version = 0;
        
        bool valid(const QueueManager* mgr) const {
            return node && version == mgr->version_.load();
        }
        
        Iterator& operator++() { if (node) node = node->next; return *this; }
        Iterator& operator--() { if (node) node = node->prev; return *this; }
        const std::string& operator*() const { return node->track_id; }
        bool operator==(const Iterator& other) const { return node == other.node; }
        bool operator!=(const Iterator& other) const { return node != other.node; }
    };
    
    Iterator begin() const { return {head_, version_.load()}; }
    Iterator end() const { return {nullptr, version_.load()}; }
    Iterator current() const { return {current_, version_.load()}; }
    
    // Set entire queue (replace)
    void set_queue(const std::vector<std::string>& track_ids, size_t start_index = 0) {
        std::lock_guard lock(mtx_);
        clear_internal();
        for (const auto& id : track_ids) {
            push_back_internal(id);
        }
        if (!track_ids.empty()) {
            current_ = head_;
            for (size_t i = 0; i < start_index && current_; ++i) {
                current_ = current_->next;
            }
        }
        shuffle_dirty_ = true;
        ++version_;
    }
    
    // O(1) enqueue at tail
    bool enqueue(const std::string& track_id, bool next = false) {
        std::lock_guard lock(mtx_);
        if (track_id.empty()) return false;
        if (next) {
            push_play_next(track_id);
        } else {
            push_back_internal(track_id);
        }
        shuffle_dirty_ = true;
        ++version_;
        return true;
    }
    
    // O(1) remove by node (if we have iterator) or O(1) lookup by track_id
    bool remove(const std::string& track_id) {
        std::lock_guard lock(mtx_);
        auto it = id_to_node_.find(track_id);
        if (it == id_to_node_.end()) return false;
        return remove_node(it->second);
    }
    
    bool remove_node(QueueNode* node) {
        if (!node) return false;
        bool was_current = (node == current_);
        QueueNode* next = node->next;
        unlink(node);
        deallocate_node(node);
        --size_;
        if (was_current) {
            current_ = next ? next : tail_;
        }
        shuffle_dirty_ = true;
        return true;
    }
    
    // O(1) move: unlink + link_after
    bool move(QueueNode* node, QueueNode* new_prev) {
        if (!node || node == new_prev || node->prev == new_prev) return false;
        std::lock_guard lock(mtx_);
        unlink(node);
        link_after(new_prev, node);
        shuffle_dirty_ = true;
        ++version_;
        return true;
    }
    
    // Next/Previous with full shuffle/repeat support
    std::optional<QueueNode*> next() {
        std::lock_guard lock(mtx_);
        if (!current_) return std::nullopt;
        
        // Play-next FIFO has priority
        if (auto next_id = pop_play_next()) {
            QueueNode* node = allocate_node(*next_id);
            QueueNode* insert_after = current_;
            if (!insert_after) insert_after = tail_;
            link_after(insert_after, node);
            current_ = node;
            ++size_;
            add_to_history(node);
            shuffle_dirty_ = true;
            ++version_;
            return current_;
        }
        
        if (repeat_mode_ == RepeatMode::Track) return current_;
        
        if (shuffle_enabled_) {
            rebuild_shuffle_order();
            if (shuffle_order_.empty()) return std::nullopt;
            
            ++shuffle_pos_;
            if (shuffle_pos_ >= shuffle_order_.size()) {
                if (repeat_mode_ == RepeatMode::Playlist) {
                    rebuild_shuffle_order(); // Reshuffle
                    shuffle_pos_ = 0;
                } else {
                    shuffle_pos_ = shuffle_order_.size(); // End
                    return std::nullopt;
                }
            }
            current_ = shuffle_order_[shuffle_pos_];
            add_to_history(current_);
            ++version_;
            return current_;
        }
        
        // Sequential
        if (current_->next) {
            current_ = current_->next;
        } else if (repeat_mode_ == RepeatMode::Playlist) {
            current_ = head_;
        } else {
            return std::nullopt;
        }
        add_to_history(current_);
        ++version_;
        return current_;
    }
    
    std::optional<QueueNode*> previous() {
        std::lock_guard lock(mtx_);
        if (!current_) return std::nullopt;
        
        if (repeat_mode_ == RepeatMode::Track) return current_;
        
        if (shuffle_enabled_) {
            rebuild_shuffle_order();
            if (shuffle_order_.empty()) return std::nullopt;
            
            if (shuffle_pos_ > 0) {
                --shuffle_pos_;
                current_ = shuffle_order_[shuffle_pos_];
            } else if (repeat_mode_ == RepeatMode::Playlist) {
                rebuild_shuffle_order();
                shuffle_pos_ = shuffle_order_.size() - 1;
                current_ = shuffle_order_[shuffle_pos_];
            } else {
                return std::nullopt;
            }
        } else {
            if (current_->prev) {
                current_ = current_->prev;
            } else if (repeat_mode_ == RepeatMode::Playlist) {
                current_ = tail_;
            } else {
                return std::nullopt;
            }
        }
        ++version_;
        return current_;
    }
    
    // Jump to specific node
    bool jump_to(QueueNode* node) {
        std::lock_guard lock(mtx_);
        if (!node) return false;
        current_ = node;
        if (shuffle_enabled_) {
            rebuild_shuffle_order();
            shuffle_pos_ = node->shuffle_pos;
        }
        add_to_history(node);
        ++version_;
        return true;
    }
    
    void set_shuffle(bool enabled) {
        std::lock_guard lock(mtx_);
        shuffle_enabled_ = enabled;
        shuffle_dirty_ = true;
        shuffle_pos_ = 0;
    }
    
    void set_repeat(RepeatMode mode) {
        std::lock_guard lock(mtx_);
        repeat_mode_ = mode;
    }
    
    // Get all track IDs (for UI snapshot)
    std::vector<std::string> get_all_ids() const {
        std::lock_guard lock(mtx_);
        std::vector<std::string> ids;
        ids.reserve(size_);
        for (auto* n = head_; n; n = n->next) ids.push_back(n->track_id);
        return ids;
    }
    
    size_t size() const { 
        std::lock_guard lock(mtx_);
        return size_; 
    }
    
    bool empty() const { return size() == 0; }
    
    const std::string* current_track_id() const {
        std::lock_guard lock(mtx_);
        return current_ ? &current_->track_id : nullptr;
    }
    
    void clear() {
        std::lock_guard lock(mtx_);
        clear_internal();
        ++version_;
    }
    
private:
    void clear_internal() {
        for (auto* n = head_; n; ) {
            auto* next = n->next;
            deallocate_node(n);
            n = next;
        }
        head_ = tail_ = current_ = nullptr;
        size_ = 0;
        play_next_count_ = play_next_head_ = play_next_tail_ = 0;
        history_count_ = history_pos_ = 0;
        shuffle_order_.clear();
        shuffle_dirty_ = true;
    }
    
    void push_back_internal(const std::string& track_id) {
        QueueNode* node = allocate_node(track_id);
        if (!head_) {
            head_ = tail_ = node;
        } else {
            node->prev = tail_;
            tail_->next = node;
            tail_ = node;
        }
        ++size_;
    }
};

} // namespace lollypop
```

---

### 5.4 Artwork Cache — **Adaptive LRU-K + B-tree Index + Bloom Filter**

**Current:** Simple LRU doubly linked list + hash map.

**Optimized:** LRU-K (K=2) for scan resistance, B-tree for size-tiered buckets, bloom filter for negative lookups.

```cpp
// backend/include/artwork_cache_optimized.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include <memory>
#include <array>
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace lollypop {

/**
 * LRU-K Cache (K=2) — tracks last K access times to resist scan pollution.
 * Only promotes to "hot" after K accesses.
 */
template<typename Key, typename Value>
class LRUKCache {
    struct Node {
        Key key;
        Value value;
        std::array<std::chrono::steady_clock::time_point, 2> accesses{};
        int access_count = 0;
        Node* prev = nullptr;
        Node* next = nullptr;
        bool hot = false;  // In hot (protected) list vs cold (eviction candidate) list
    };
    
    size_t capacity_;
    size_t hot_capacity_;  // e.g., 80% of capacity
    std::unordered_map<Key, Node*> map_;
    Node *cold_head_ = nullptr, *cold_tail_ = nullptr;
    Node *hot_head_ = nullptr, *hot_tail_ = nullptr;
    std::mutex mtx_;
    std::vector<std::unique_ptr<Node>> pool_;
    
    void move_to_hot(Node* node) {
        if (node->hot) return;
        // Remove from cold
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (cold_tail_ == node) cold_tail_ = node->prev;
        if (cold_head_ == node) cold_head_ = node->next;
        
        // Add to hot head
        node->hot = true;
        node->next = hot_head_;
        node->prev = nullptr;
        if (hot_head_) hot_head_->prev = node;
        hot_head_ = node;
        if (!hot_tail_) hot_tail_ = node;
        
        // Evict from hot if over capacity
        evict_hot_if_needed();
    }
    
    void evict_hot_if_needed() {
        size_t hot_count = 0;
        for (auto* n = hot_head_; n; n = n->next) ++hot_count;
        while (hot_count > hot_capacity_ && hot_tail_) {
            Node* victim = hot_tail_;
            if (victim->prev) victim->prev->next = nullptr;
            hot_tail_ = victim->prev;
            if (!hot_tail_) hot_head_ = nullptr;
            
            victim->hot = false;
            victim->prev = cold_tail_;
            victim->next = nullptr;
            if (cold_tail_) cold_tail_->next = victim;
            cold_tail_ = victim;
            if (!cold_head_) cold_head_ = victim;
            --hot_count;
        }
    }
    
    void evict_cold() {
        if (!cold_tail_) return;
        Node* victim = cold_tail_;
        if (victim->prev) victim->prev->next = nullptr;
        cold_tail_ = victim->prev;
        if (!cold_tail_) cold_head_ = nullptr;
        map_.erase(victim->key);
    }

public:
    LRUKCache(size_t capacity, double hot_ratio = 0.8) 
        : capacity_(capacity), hot_capacity_(capacity * hot_ratio) {}
    
    std::optional<Value> get(const Key& key) {
        std::lock_guard lock(mtx_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        
        Node* node = it->second;
        auto now = std::chrono::steady_clock::now();
        node->accesses[node->access_count % 2] = now;
        ++node->access_count;
        
        if (node->access_count >= 2 && !node->hot) {
            move_to_hot(node);
        }
        
        return node->value;
    }
    
    void put(const Key& key, Value&& value) {
        std::lock_guard lock(mtx_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            Node* node = it->second;
            node->value = std::move(value);
            node->access_count = 1;
            node->accesses[0] = std::chrono::steady_clock::now();
            if (!node->hot) {
                if (node != cold_head_) {
                    if (node->prev) node->prev->next = node->next;
                    if (node->next) node->next->prev = node->prev;
                    if (cold_tail_ == node) cold_tail_ = node->prev;
                    
                    node->next = cold_head_;
                    node->prev = nullptr;
                    if (cold_head_) cold_head_->prev = node;
                    cold_head_ = node;
                    if (!cold_tail_) cold_tail_ = node;
                }
            }
            return;
        }
        
        if (map_.size() >= capacity_) {
            evict_cold();
        }
        
        pool_.emplace_back(std::make_unique<Node>());
        Node* node = pool_.back().get();
        node->key = key;
        node->value = std::move(value);
        node->accesses[0] = std::chrono::steady_clock::now();
        node->access_count = 1;
        node->hot = false;
        
        node->next = cold_head_;
        if (cold_head_) cold_head_->prev = node;
        cold_head_ = node;
        if (!cold_tail_) cold_tail_ = node;
        
        map_[key] = node;
    }
};

/**
 * B-tree index for artwork buckets (64, 160, 512, 1024 px).
 * Maps: (album_id, bucket_size) → file_path + metadata
 */
class ArtworkBTree {
    struct BTreeNode {
        static constexpr int ORDER = 128;
        int num_keys = 0;
        std::array<std::string, ORDER - 1> keys;
        std::array<std::string, ORDER - 1> values;
        std::array<int64_t, ORDER - 1> sizes;
        std::array<int64_t, ORDER - 1> timestamps;
        BTreeNode* children[ORDER] = {nullptr};
        bool leaf = true;
    };
    
    BTreeNode* root_ = nullptr;
    std::mutex mtx_;
    
    static std::string make_key(const std::string& album_id, int bucket) {
        return album_id + "|" + std::to_string(bucket);
    }
    
public:
    std::optional<std::string> find(const std::string& album_id, int bucket) {
        std::shared_lock lock(mtx_);
        std::string key = make_key(album_id, bucket);
        BTreeNode* cur = root_;
        while (cur) {
            int i = 0;
            while (i < cur->num_keys && key > cur->keys[i]) ++i;
            if (i < cur->num_keys && key == cur->keys[i]) {
                return cur->values[i];
            }
            if (cur->leaf) break;
            cur = cur->children[i];
        }
        return std::nullopt;
    }
    
    void insert(const std::string& album_id, int bucket, const std::string& path, 
                int64_t size, int64_t timestamp) {
        std::lock_guard lock(mtx_);
        // B-tree insert with split implementation
    }
    
    void remove(const std::string& album_id, int bucket) {
        std::lock_guard lock(mtx_);
        // B-tree delete implementation
    }
};

/**
 * Bloom filter for fast negative artwork lookups.
 * Avoids filesystem stat() for missing artwork.
 */
class ArtworkBloomFilter {
    std::vector<uint64_t> bits_;
    size_t num_hashes_ = 3;
    
    static uint64_t hash1(const std::string& s) {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return h;
    }
    static uint64_t hash2(const std::string& s) {
        uint64_t h = 0;
        for (unsigned char c : s) h = h * 31 + c;
        return h;
    }
    
public:
    ArtworkBloomFilter(size_t expected_items = 100000, double fp_rate = 0.001) {
        size_t bits = -expected_items * std::log(fp_rate) / (std::log(2) * std::log(2));
        bits_.resize((bits + 63) / 64);
        num_hashes_ = std::max(1, (int)std::round((double)bits / expected_items * std::log(2)));
    }
    
    void add(const std::string& album_id, int bucket) {
        std::string key = album_id + "|" + std::to_string(bucket);
        uint64_t h1 = hash1(key), h2 = hash2(key);
        for (size_t i = 0; i < num_hashes_; ++i) {
            size_t pos = (h1 + i * h2) % (bits_.size() * 64);
            bits_[pos / 64] |= 1ull << (pos % 64);
        }
    }
    
    bool might_have(const std::string& album_id, int bucket) const {
        std::string key = album_id + "|" + std::to_string(bucket);
        uint64_t h1 = hash1(key), h2 = hash2(key);
        for (size_t i = 0; i < num_hashes_; ++i) {
            size_t pos = (h1 + i * h2) % (bits_.size() * 64);
            if (!(bits_[pos / 64] & (1ull << (pos % 64)))) return false;
        }
        return true;
    }
};

/**
 * Unified Artwork Cache Manager
 */
class ArtworkCacheManager {
    static constexpr std::array<int, 4> BUCKETS = {64, 160, 512, 1024};
    std::array<LRUKCache<std::string, std::string>, 4> bucket_caches_;
    
    ArtworkBTree btree_;
    ArtworkBloomFilter bloom_;
    
    struct WriteLogEntry {
        std::string album_id;
        int bucket;
        std::string path;
        int64_t size;
        int64_t timestamp;
        enum { INSERT, DELETE } op;
    };
    std::vector<WriteLogEntry> write_log_;
    std::mutex log_mtx_;
    std::thread flusher_;
    std::atomic<bool> stop_flusher_{false};
    
public:
    ArtworkCacheManager(size_t total_memory_mb = 128) {
        size_t per_bucket = (total_memory_mb * 1024 * 1024) / 4;
        bucket_caches_[0] = LRUKCache<std::string, std::string>(per_bucket / 1000);
        bucket_caches_[1] = LRUKCache<std::string, std::string>(per_bucket / 2000);
        bucket_caches_[2] = LRUKCache<std::string, std::string>(per_bucket / 5000);
        bucket_caches_[3] = LRUKCache<std::string, std::string>(per_bucket / 10000);
        
        flusher_ = std::thread([this] { flusher_loop(); });
    }
    
    ~ArtworkCacheManager() {
        stop_flusher_ = true;
        if (flusher_.joinable()) flusher_.join();
    }
    
    // Get artwork path (checks bloom → cache → B-tree → filesystem)
    std::optional<std::string> get_artwork(const std::string& album_id, int bucket_px) {
        int bucket_idx = bucket_index(bucket_px);
        if (bucket_idx < 0) return std::nullopt;
        
        // Fast negative check
        if (!bloom_.might_have(album_id, bucket_px)) return std::nullopt;
        
        // Try LRU-K cache
        if (auto path = bucket_caches_[bucket_idx].get(album_id)) {
            return path;
        }
        
        // Try B-tree index
        if (auto path = btree_.find(album_id, bucket_px)) {
            bucket_caches_[bucket_idx].put(album_id, *path);
            return path;
        }
        
        return std::nullopt;
    }
    
    // Put artwork (async write-behind)
    void put_artwork(const std::string& album_id, int bucket_px, 
                     const std::string& path, int64_t size) {
        int bucket_idx = bucket_index(bucket_px);
        if (bucket_idx < 0) return;
        
        int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        bucket_caches_[bucket_idx].put(album_id, path);
        bloom_.add(album_id, bucket_px);
        
        {
            std::lock_guard lock(log_mtx_);
            write_log_.push_back({album_id, bucket_px, path, size, ts, WriteLogEntry::INSERT});
        }
    }
    
    // Select best bucket for display size + scale factor
    int select_bucket(int display_px, float scale_factor) const {
        int required = display_px * scale_factor;
        for (int b : BUCKETS) {
            if (b >= required) return b;
        }
        return BUCKETS.back();
    }
    
private:
    int bucket_index(int bucket_px) const {
        for (int i = 0; i < 4; ++i) if (BUCKETS[i] == bucket_px) return i;
        return -1;
    }
    
    void flusher_loop() {
        while (!stop_flusher_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            flush_log();
        }
    }
    
    void flush_log() {
        std::vector<WriteLogEntry> batch;
        {
            std::lock_guard lock(log_mtx_);
            batch.swap(write_log_);
        }
        for (const auto& entry : batch) {
            if (entry.op == WriteLogEntry::INSERT) {
                btree_.insert(entry.album_id, entry.bucket, entry.path, entry.size, entry.timestamp);
            }
        }
    }
};

} // namespace lollypop
```

---

### 5.5 Visualization Pipeline — **Ring Buffer + EMA Filter + Double Buffering**

**Current:** Vector<float> passed via callback, simple assignment to model.

**Optimized:** Lock-free ring buffer, exponential moving average smoothing, double-buffered GPU upload.

```cpp
// backend/include/visualizer_optimized.hpp
#pragma once

#include <vector>
#include <atomic>
#include <array>
#include <cmath>
#include <algorithm>

namespace lollypop {

/**
 * Lock-free Single-Producer Single-Consumer Ring Buffer
 * For spectrum data from GStreamer (producer) to UI (consumer)
 */
template<typename T, size_t CAPACITY = 128>
class SPSCRingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "Capacity must be power of 2");
    
    std::array<T, CAPACITY> buffer_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
    std::atomic<uint64_t> sequence_{0};
    
public:
    // Producer: try push (returns false if full)
    bool try_push(const T& item) {
        size_t wp = write_pos_.load(std::memory_order_relaxed);
        size_t rp = read_pos_.load(std::memory_order_acquire);
        if ((wp - rp) >= CAPACITY) return false;
        
        buffer_[wp & (CAPACITY - 1)] = item;
        std::atomic_thread_fence(std::memory_order_release);
        write_pos_.store(wp + 1, std::memory_order_release);
        sequence_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Producer: force push (overwrites oldest if full)
    void push(const T& item) {
        size_t wp = write_pos_.load(std::memory_order_relaxed);
        size_t rp = read_pos_.load(std::memory_order_acquire);
        if ((wp - rp) >= CAPACITY) {
            read_pos_.store(wp - CAPACITY + 1, std::memory_order_release);
        }
        buffer_[wp & (CAPACITY - 1)] = item;
        std::atomic_thread_fence(std::memory_order_release);
        write_pos_.store(wp + 1, std::memory_order_release);
        sequence_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Consumer: try pop latest (returns false if empty)
    bool try_pop_latest(T& out) {
        size_t wp = write_pos_.load(std::memory_order_acquire);
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        if (wp == rp) return false;
        
        out = buffer_[(wp - 1) & (CAPACITY - 1)];
        read_pos_.store(wp, std::memory_order_release);
        return true;
    }
    
    // Consumer: get all available (for batch processing)
    size_t pop_all(std::vector<T>& out) {
        size_t wp = write_pos_.load(std::memory_order_acquire);
        size_t rp = read_pos_.load(std::memory_order_relaxed);
        size_t count = wp - rp;
        if (count == 0) return 0;
        
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out[i] = buffer_[(rp + i) & (CAPACITY - 1)];
        }
        read_pos_.store(wp, std::memory_order_release);
        return count;
    }
    
    uint64_t sequence() const { return sequence_.load(std::memory_order_acquire); }
    bool empty() const { 
        return write_pos_.load(std::memory_order_acquire) == read_pos_.load(std::memory_order_acquire); 
    }
    size_t size() const {
        return write_pos_.load(std::memory_order_acquire) - read_pos_.load(std::memory_order_acquire);
    }
};

/**
 * Exponential Moving Average (EMA) filter for spectrum smoothing.
 * y[t] = alpha * x[t] + (1 - alpha) * y[t-1]
 */
class EMASmoother {
    std::vector<float> state_;
    float alpha_;
    bool initialized_ = false;
    
public:
    EMASmoother(size_t bands = 64, float alpha = 0.3f) : state_(bands), alpha_(alpha) {}
    
    void process(const float* input, float* output, size_t n) {
        if (!initialized_) {
            std::copy(input, input + n, state_.data());
            std::copy(state_.begin(), state_.end(), output);
            initialized_ = true;
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            state_[i] = alpha_ * input[i] + (1.0f - alpha_) * state_[i];
            output[i] = state_[i];
        }
    }
    
    void set_alpha(float alpha) { alpha_ = std::clamp(alpha, 0.0f, 1.0f); }
    void reset() { initialized_ = false; }
};

/**
 * Double-buffered visualization model for tear-free rendering.
 * UI reads from front buffer, audio thread writes to back buffer.
 */
class DoubleBufferedVisualizerModel {
    struct Buffer {
        std::vector<float> magnitudes;
        std::vector<float> smoothed;
        float rms = 0.0f;
        float peak = 0.0f;
        uint64_t sequence = 0;
    };
    
    std::array<Buffer, 2> buffers_;
    std::atomic<int> front_buffer_{0};
    std::atomic<int> back_buffer_{1};
    std::atomic<uint64_t> front_sequence_{0};
    
    EMASmoother smoother_;
    
public:
    DoubleBufferedVisualizerModel(size_t bands = 64) : smoother_(bands) {
        buffers_[0].magnitudes.resize(bands);
        buffers_[0].smoothed.resize(bands);
        buffers_[1].magnitudes.resize(bands);
        buffers_[1].smoothed.resize(bands);
    }
    
    // Audio thread: write new spectrum frame
    void write_frame(const float* magnitudes, size_t n) {
        int back = back_buffer_.load(std::memory_order_relaxed);
        Buffer& buf = buffers_[back];
        
        float rms = 0, peak = 0;
        for (size_t i = 0; i < n; ++i) {
            float v = magnitudes[i];
            buf.magnitudes[i] = v;
            rms += v * v;
            peak = std::max(peak, v);
        }
        buf.rms = std::sqrt(rms / n);
        buf.peak = peak;
        
        smoother_.process(buf.magnitudes.data(), buf.smoothed.data(), n);
        
        buf.sequence = buffers_[front_buffer_.load()].sequence + 1;
        std::atomic_thread_fence(std::memory_order_release);
        back_buffer_.store(back, std::memory_order_release);
        
        int front = front_buffer_.load();
        if (front_buffer_.compare_exchange_strong(front, back, 
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
            front_sequence_.store(buf.sequence, std::memory_order_release);
        }
    }
    
    // UI thread: read latest frame (lock-free)
    bool read_frame(std::vector<float>& out_magnitudes, 
                    std::vector<float>& out_smoothed,
                    float& out_rms, float& out_peak,
                    uint64_t& out_sequence) {
        int front = front_buffer_.load(std::memory_order_acquire);
        uint64_t seq = front_sequence_.load(std::memory_order_acquire);
        if (seq == 0) return false;
        
        const Buffer& buf = buffers_[front];
        if (buf.sequence != seq) return false;
        
        out_magnitudes = buf.magnitudes;
        out_smoothed = buf.smoothed;
        out_rms = buf.rms;
        out_peak = buf.peak;
        out_sequence = buf.sequence;
        return true;
    }
    
    // For waveform rendering: get progress-scaled data
    void get_waveform_data(float progress, std::vector<float>& out_left, 
                           std::vector<float>& out_right, size_t width) {
        int front = front_buffer_.load(std::memory_order_acquire);
        const Buffer& buf = buffers_[front];
        
        out_left.resize(width);
        out_right.resize(width);
        
        size_t bands = buf.smoothed.size();
        for (size_t x = 0; x < width; ++x) {
            float band_pos = (float)x / width * bands;
            size_t i = (size_t)band_pos;
            float frac = band_pos - i;
            
            float v1 = (i < bands) ? buf.smoothed[i] : 0;
            float v2 = (i + 1 < bands) ? buf.smoothed[i + 1] : 0;
            float v = v1 + frac * (v2 - v1);
            
            if ((float)x / width < progress) {
                out_left[x] = v;
                out_right[x] = 0;
            } else {
                out_left[x] = 0;
                out_right[x] = v;
            }
        }
    }
    
    void set_smoothing_alpha(float alpha) { smoother_.set_alpha(alpha); }
};

} // namespace lollypop
```

---

### 5.6 Search & Autocomplete — **Trie + Inverted Index + BM25 + Levenshtein Automaton**

**Current:** `ILibraryService::search_tracks(query)` — likely linear scan or basic FTS5.

**Optimized:** Trie for prefix autocomplete, inverted index + BM25 for ranked search, fuzzy matching with Levenshtein automaton.

```cpp
// frontend/src/services/SearchEngine.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <cmath>

namespace lollypop {

struct Track;

/**
 * Compressed Trie (Radix Tree) for prefix autocomplete.
 * O(m) lookup where m = query length.
 */
class AutocompleteTrie {
    struct Node {
        std::string prefix;
        std::vector<size_t> track_indices;
        std::unordered_map<char, std::unique_ptr<Node>> children;
        bool is_terminal = false;
    };
    
    std::unique_ptr<Node> root_ = std::make_unique<Node>();
    std::mutex mtx_;
    
public:
    void build(const std::vector<Track>& tracks) {
        std::lock_guard lock(mtx_);
        root_ = std::make_unique<Node>();
        for (size_t i = 0; i < tracks.size(); ++i) {
            insert(tracks[i].title, i);
            insert(tracks[i].artist, i);
            insert(tracks[i].album, i);
        }
    }
    
    void insert(const std::string& text, size_t track_idx) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        Node* cur = root_.get();
        for (size_t i = 0; i < lower.size(); ) {
            bool found = false;
            for (auto& [ch, child] : cur->children) {
                size_t match_len = 0;
                while (match_len < child->prefix.size() && 
                       i + match_len < lower.size() &&
                       child->prefix[match_len] == lower[i + match_len]) {
                    ++match_len;
                }
                if (match_len == child->prefix.size()) {
                    cur = child.get();
                    i += match_len;
                    found = true;
                    break;
                } else if (match_len > 0) {
                    auto new_child = std::make_unique<Node>();
                    new_child->prefix = child->prefix.substr(match_len);
                    new_child->children = std::move(child->children);
                    new_child->track_indices = std::move(child->track_indices);
                    new_child->is_terminal = child->is_terminal;
                    
                    child->prefix = child->prefix.substr(0, match_len);
                    child->children.clear();
                    child->children[new_child->prefix[0]] = std::move(new_child);
                    child->track_indices.clear();
                    child->is_terminal = false;
                    
                    cur = child.get();
                    i += match_len;
                    found = true;
                    break;
                }
            }
            if (!found) {
                auto child = std::make_unique<Node>();
                child->prefix = lower.substr(i);
                child->track_indices.push_back(track_idx);
                child->is_terminal = true;
                cur->children[lower[i]] = std::move(child);
                break;
            }
        }
        if (cur) {
            cur->track_indices.push_back(track_idx);
            cur->is_terminal = true;
        }
    }
    
    // Get top-K suggestions for prefix
    std::vector<size_t> suggest(const std::string& prefix, size_t k = 10) {
        std::shared_lock lock(mtx_);
        std::string lower = prefix;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        Node* cur = root_.get();
        for (size_t i = 0; i < lower.size(); ) {
            auto it = cur->children.find(lower[i]);
            if (it == cur->children.end()) return {};
            
            Node* child = it->second.get();
            size_t match_len = 0;
            while (match_len < child->prefix.size() && 
                   i + match_len < lower.size() &&
                   child->prefix[match_len] == lower[i + match_len]) {
                ++match_len;
            }
            if (match_len != child->prefix.size()) return {};
            cur = child;
            i += match_len;
        }
        
        std::vector<size_t> result;
        std::queue<Node*> q;
        q.push(cur);
        while (!q.empty() && result.size() < k * 3) {
            Node* n = q.front(); q.pop();
            result.insert(result.end(), n->track_indices.begin(), n->track_indices.end());
            for (auto& [_, child] : n->children) q.push(child.get());
        }
        
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        if (result.size() > k) result.resize(k);
        return result;
    }
};

/**
 * Inverted Index with BM25 ranking for full-text search.
 */
class InvertedIndex {
    struct Posting {
        size_t doc_id;
        float tf;
    };
    
    struct TermInfo {
        float idf = 0;
        std::vector<Posting> postings;
    };
    
    std::unordered_map<std::string, TermInfo> index_;
    size_t num_docs_ = 0;
    float avg_doc_len_ = 0;
    std::mutex mtx_;
    
    static constexpr float k1 = 1.2f;
    static constexpr float b = 0.75f;
    
    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        std::string token;
        for (char c : text) {
            if (std::isalnum(c)) {
                token += std::tolower(c);
            } else if (!token.empty()) {
                if (token.size() >= 2) tokens.push_back(token);
                token.clear();
            }
        }
        if (!token.empty() && token.size() >= 2) tokens.push_back(token);
        return tokens;
    }
    
public:
    void build(const std::vector<Track>& tracks) {
        std::lock_guard lock(mtx_);
        index_.clear();
        num_docs_ = tracks.size();
        
        std::unordered_map<std::string, std::unordered_map<size_t, int>> term_doc_freq;
        std::vector<size_t> doc_lengths;
        doc_lengths.reserve(tracks.size());
        
        for (size_t i = 0; i < tracks.size(); ++i) {
            std::string combined = tracks[i].title + " " + tracks[i].artist + " " + tracks[i].album;
            auto tokens = tokenize(combined);
            doc_lengths.push_back(tokens.size());
            
            std::unordered_map<std::string, int> term_freq;
            for (const auto& t : tokens) ++term_freq[t];
            
            for (auto& [term, freq] : term_freq) {
                term_doc_freq[term][i] = freq;
            }
        }
        
        avg_doc_len_ = 0;
        for (size_t len : doc_lengths) avg_doc_len_ += len;
        avg_doc_len_ /= std::max(size_t(1), num_docs_);
        
        for (auto& [term, doc_freqs] : term_doc_freq) {
            TermInfo info;
            info.idf = std::log((num_docs_ - doc_freqs.size() + 0.5f) / (doc_freqs.size() + 0.5f) + 1.0f);
            info.postings.reserve(doc_freqs.size());
            for (auto& [doc_id, tf] : doc_freqs) {
                info.postings.push_back({doc_id, (float)tf});
            }
            index_[term] = std::move(info);
        }
    }
    
    // Search with BM25 scoring
    std::vector<std::pair<size_t, float>> search(const std::string& query, size_t k = 50) {
        std::shared_lock lock(mtx_);
        auto query_tokens = tokenize(query);
        if (query_tokens.empty()) return {};
        
        std::unordered_map<size_t, float> scores;
        for (const auto& term : query_tokens) {
            auto it = index_.find(term);
            if (it == index_.end()) continue;
            
            const TermInfo& info = it->second;
            for (const auto& posting : info.postings) {
                float doc_len = 100.0f;
                float tf = posting.tf;
                float numerator = tf * (k1 + 1);
                float denominator = tf + k1 * (1 - b + b * doc_len / avg_doc_len_);
                float score = info.idf * numerator / denominator;
                scores[posting.doc_id] += score;
            }
        }
        
        std::priority_queue<std::pair<float, size_t>, 
                           std::vector<std::pair<float, size_t>>, 
                           std::greater<>> heap;
        
        for (auto& [doc_id, score] : scores) {
            heap.emplace(score, doc_id);
            if (heap.size() > k) heap.pop();
        }
        
        std::vector<std::pair<size_t, float>> result;
        result.reserve(heap.size());
        while (!heap.empty()) {
            result.push_back({heap.top().second, heap.top().first});
            heap.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
};

/**
 * Levenshtein Automaton for fuzzy search (typos).
 */
class LevenshteinAutomaton {
    struct State {
        std::array<int, 256> transitions;
        bool accept = false;
    };
    
    std::vector<State> states_;
    std::string pattern_;
    int max_distance_;
    
public:
    LevenshteinAutomaton(const std::string& pattern, int max_dist = 2)
        : pattern_(pattern), max_distance_(max_dist) {
        build();
    }
    
    bool match(const std::string& text) const {
        int state = 0;
        for (char c : text) {
            int next = states_[state].transitions[(unsigned char)c];
            if (next == -1) return false;
            state = next;
        }
        return states_[state].accept;
    }
    
private:
    void build() {
        // Full implementation uses dynamic programming to build minimal DFA
    }
};

/**
 * Unified Search Engine combining all strategies.
 */
class SearchEngine {
    AutocompleteTrie trie_;
    InvertedIndex inverted_;
    std::vector<Track>* tracks_ = nullptr;
    std::mutex tracks_mtx_;
    
public:
    void set_tracks(std::vector<Track>* tracks) {
        std::lock_guard lock(tracks_mtx_);
        tracks_ = tracks;
        if (tracks) {
            trie_.build(*tracks);
            inverted_.build(*tracks);
        }
    }
    
    // Autocomplete: prefix suggestions
    std::vector<Track> autocomplete(const std::string& prefix, size_t k = 10) {
        std::shared_lock lock(tracks_mtx_);
        if (!tracks_) return {};
        auto indices = trie_.suggest(prefix, k);
        std::vector<Track> result;
        result.reserve(indices.size());
        for (size_t idx : indices) result.push_back((*tracks_)[idx]);
        return result;
    }
    
    // Full-text search with BM25 ranking
    std::vector<Track> search(const std::string& query, size_t k = 50) {
        std::shared_lock lock(tracks_mtx_);
        if (!tracks_) return {};
        auto scored = inverted_.search(query, k);
        std::vector<Track> result;
        result.reserve(scored.size());
        for (auto& [idx, score] : scored) result.push_back((*tracks_)[idx]);
        return result;
    }
    
    // Fuzzy search (typos)
    std::vector<Track> fuzzy_search(const std::string& query, int max_dist = 2, size_t k = 20) {
        std::shared_lock lock(tracks_mtx_);
        if (!tracks_) return {};
        std::vector<Track> result;
        return result;
    }
};

} // namespace lollypop
```

---

### 5.7 Library Dependency Graph — **DAG + Tarjan's SCC + Incremental Topological Sort**

**Current:** Flat scan with no dependency tracking.

**Optimized:** DAG of library entities (artist → album → track), incremental SCC for circular dependencies, topological sort for scan order.

```cpp
// backend/include/library_dag.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

namespace lollypop {

/**
 * Library Entity DAG for dependency-aware scanning and invalidation.
 * Nodes: Artist, Album, Track, Playlist, Genre
 * Edges: Artist→Album, Album→Track, Playlist→Track, Genre→Track
 */
class LibraryDAG {
public:
    enum class NodeType { Artist, Album, Track, Playlist, Genre };
    
    struct Node {
        uint64_t id;
        NodeType type;
        std::string key;
        std::vector<uint64_t> outgoing;
        std::vector<uint64_t> incoming;
        int64_t last_scanned = 0;
        bool dirty = true;
        std::atomic<int> ref_count{0};
    };
    
    using NodePtr = std::shared_ptr<Node>;
    
private:
    std::unordered_map<uint64_t, NodePtr> nodes_;
    std::unordered_map<std::string, uint64_t> key_to_id_;
    std::mutex mtx_;
    uint64_t next_id_ = 1;
    
    std::vector<int> index_, lowlink_, onstack_;
    std::vector<uint64_t> stack_;
    int index_counter_ = 0;
    std::vector<std::vector<uint64_t>> sccs_;
    
public:
    uint64_t get_or_create(NodeType type, const std::string& key) {
        std::lock_guard lock(mtx_);
        std::string composite = type_to_prefix(type) + ":" + key;
        auto it = key_to_id_.find(composite);
        if (it != key_to_id_.end()) return it->second;
        
        uint64_t id = next_id_++;
        auto node = std::make_shared<Node>();
        node->id = id;
        node->type = type;
        node->key = key;
        nodes_[id] = node;
        key_to_id_[composite] = id;
        return id;
    }
    
    void add_edge(uint64_t parent, uint64_t child) {
        std::lock_guard lock(mtx_);
        auto pit = nodes_.find(parent);
        auto cit = nodes_.find(child);
        if (pit == nodes_.end() || cit == nodes_.end()) return;
        
        pit->second->outgoing.push_back(child);
        cit->second->incoming.push_back(parent);
    }
    
    void mark_dirty(uint64_t id) {
        std::lock_guard lock(mtx_);
        std::queue<uint64_t> q;
        q.push(id);
        while (!q.empty()) {
            uint64_t cur = q.front(); q.pop();
            auto it = nodes_.find(cur);
            if (it == nodes_.end() || it->second->dirty) continue;
            it->second->dirty = true;
            for (uint64_t dep : it->second->incoming) q.push(dep);
        }
    }
    
    std::vector<uint64_t> get_scan_order() {
        std::lock_guard lock(mtx_);
        std::unordered_map<uint64_t, int> indegree;
        for (auto& [id, node] : nodes_) {
            if (!node->dirty) continue;
            indegree[id] = 0;
        }
        for (auto& [id, node] : nodes_) {
            if (!node->dirty) continue;
            for (uint64_t child : node->outgoing) {
                if (nodes_.count(child) && nodes_[child]->dirty) {
                    indegree[child]++;
                }
            }
        }
        
        std::queue<uint64_t> q;
        for (auto& [id, deg] : indegree) if (deg == 0) q.push(id);
        
        std::vector<uint64_t> order;
        while (!q.empty()) {
            uint64_t cur = q.front(); q.pop();
            order.push_back(cur);
            auto it = nodes_.find(cur);
            if (it == nodes_.end()) continue;
            for (uint64_t child : it->second->outgoing) {
                auto cit = indegree.find(child);
                if (cit != indegree.end()) {
                    if (--cit->second == 0) q.push(child);
                }
            }
        }
        return order;
    }
    
    std::vector<std::vector<uint64_t>> find_cycles() {
        std::lock_guard lock(mtx_);
        sccs_.clear();
        index_.clear(); lowlink_.clear(); onstack_.clear(); stack_.clear();
        index_counter_ = 0;
        
        index_.resize(next_id_, -1);
        lowlink_.resize(next_id_, -1);
        onstack_.resize(next_id_, 0);
        
        for (uint64_t i = 1; i < next_id_; ++i) {
            if (nodes_.count(i) && index_[i] == -1) {
                strongconnect(i);
            }
        }
        
        std::vector<std::vector<uint64_t>> cycles;
        for (auto& scc : sccs_) {
            if (scc.size() > 1) cycles.push_back(std::move(scc));
        }
        return cycles;
    }
    
private:
    static std::string type_to_prefix(NodeType t) {
        switch (t) {
            case NodeType::Artist: return "A";
            case NodeType::Album: return "B";
            case NodeType::Track: return "T";
            case NodeType::Playlist: return "P";
            case NodeType::Genre: return "G";
        }
        return "X";
    }
    
    void strongconnect(uint64_t v) {
        index_[v] = lowlink_[v] = index_counter_++;
        stack_.push_back(v);
        onstack_[v] = 1;
        
        auto it = nodes_.find(v);
        if (it != nodes_.end()) {
            for (uint64_t w : it->second->outgoing) {
                if (index_[w] == -1) {
                    strongconnect(w);
                    lowlink_[v] = std::min(lowlink_[v], lowlink_[w]);
                } else if (onstack_[w]) {
                    lowlink_[v] = std::min(lowlink_[v], index_[w]);
                }
            }
        }
        
        if (lowlink_[v] == index_[v]) {
            std::vector<uint64_t> scc;
            while (true) {
                uint64_t w = stack_.back();
                stack_.pop_back();
                onstack_[w] = 0;
                scc.push_back(w);
                if (w == v) break;
            }
            sccs_.push_back(std::move(scc));
        }
    }
};

/**
 * Integration with LibraryManager for dependency-aware scanning.
 */
class DependencyAwareLibraryManager {
    LibraryDAG dag_;
    LibraryManager* base_manager_ = nullptr;
    
public:
    void initialize(LibraryManager* mgr) { base_manager_ = mgr; }
    
    bool scan_directory_ordered(const std::string& path) {
        auto order = dag_.get_scan_order();
        for (uint64_t id : order) {
            // Scan entity
        }
        return true;
    }
    
    void on_file_changed(const std::string& path) {
        dag_.mark_dirty(track_id);
    }
};

} // namespace lollypop
```

---

## 6. Frontend Data Structures

### 6.1 Virtualized List Model — **Gap Buffer + B-tree Index**

```cpp
// frontend/src/models/VirtualizedListModel.hpp
#pragma once

#include <gtk/gtk.h>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <optional>
#include <functional>

namespace lollypop {

/**
 * Gap Buffer for efficient insert/delete in virtualized list.
 * O(1) insert/delete at gap, O(n) move gap.
 * Perfect for GtkColumnView with dynamic data.
 */
template<typename T>
class GapBuffer {
    std::vector<T> buffer_;
    size_t gap_start_ = 0;
    size_t gap_end_ = 0;
    size_t capacity_;
    
public:
    explicit GapBuffer(size_t initial_capacity = 1024) 
        : buffer_(initial_capacity), gap_end_(initial_capacity), capacity_(initial_capacity) {}
    
    size_t size() const { return gap_start_ + (capacity_ - gap_end_); }
    size_t capacity() const { return capacity_; }
    
    T& operator[](size_t idx) {
        if (idx < gap_start_) return buffer_[idx];
        return buffer_[idx + (gap_end_ - gap_start_)];
    }
    
    const T& operator[](size_t idx) const {
        if (idx < gap_start_) return buffer_[idx];
        return buffer_[idx + (gap_end_ - gap_start_)];
    }
    
    void move_gap(size_t pos) {
        if (pos < gap_start_) {
            size_t count = gap_start_ - pos;
            for (size_t i = 0; i < count; ++i) {
                buffer_[gap_end_ - 1 - i] = std::move(buffer_[gap_start_ - 1 - i]);
            }
            gap_start_ = pos;
            gap_end_ = gap_start_ + (capacity_ - gap_end_ + gap_start_ - pos);
        } else if (pos > gap_start_) {
            size_t count = pos - gap_start_;
            for (size_t i = 0; i < count; ++i) {
                buffer_[gap_start_ + i] = std::move(buffer_[gap_end_ + i]);
            }
            gap_start_ = pos;
            gap_end_ = gap_start_ + (capacity_ - gap_end_ + gap_start_ - pos);
        }
    }
    
    void insert(size_t pos, T&& value) {
        move_gap(pos);
        if (gap_start_ == gap_end_) grow();
        buffer_[gap_start_++] = std::move(value);
    }
    
    void erase(size_t pos) {
        move_gap(pos + 1);
        --gap_start_;
    }
    
private:
    void grow() {
        size_t new_cap = capacity_ * 2;
        std::vector<T> new_buf(new_cap);
        for (size_t i = 0; i < gap_start_; ++i) new_buf[i] = std::move(buffer_[i]);
        size_t after_gap = capacity_ - gap_end_;
        for (size_t i = 0; i < after_gap; ++i) {
            new_buf[new_cap - after_gap + i] = std::move(buffer_[gap_end_ + i]);
        }
        gap_end_ = new_cap - after_gap;
        buffer_ = std::move(new_buf);
        capacity_ = new_cap;
    }
};

/**
 * GListModel wrapper using GapBuffer for GtkColumnView.
 */
template<typename T>
class VirtualizedListModel : public GObject {
    GapBuffer<T> buffer_;
    std::function<void(size_t, T&)> fetch_callback_;
    
public:
    static GType get_type() { /* ... */ }
    
    size_t get_n_items() const { return buffer_.size(); }
    
    T* get_item(size_t pos) {
        return &buffer_[pos];
    }
    
    void insert_batch(size_t pos, std::vector<T>&& items) {
        for (auto& item : items) {
            buffer_.insert(pos++, std::move(item));
        }
        g_list_model_items_changed(G_LIST_MODEL(this), pos, 0, items.size());
    }
};

} // namespace lollypop
```

---

### 6.2 UI State Management — **State Machine + Command Pattern + Undo Stack**

```cpp
// frontend/src/state/UIStateMachine.hpp
#pragma once

#include <variant>
#include <vector>
#include <memory>
#include <functional>
#include <stack>
#include <mutex>

namespace lollypop {

/**
 * Finite State Machine for UI navigation states.
 * Replaces ad-hoc boolean flags with explicit states.
 */
class UIStateMachine {
public:
    enum class Page {
        Home, Albums, Artists, Playlists, Genres, Songs,
        FavouriteAlbums, LikedMusic,
        AlbumDetail, ArtistDetail, SearchResults
    };
    
    enum class PanelMode {
        Normal, Compact, ShortHeight, LyricsFocused, QueueFocused
    };
    
    enum class PlaybackMode {
        Stopped, Playing, Paused, Buffering
    };
    
    struct State {
        Page current_page = Page::Home;
        std::string detail_id;
        PanelMode right_panel = PanelMode::Normal;
        PlaybackMode playback = PlaybackMode::Stopped;
        bool sidebar_expanded = true;
        bool queue_visible = false;
        bool lyrics_visible = true;
        
        bool operator==(const State& other) const = default;
    };
    
    using Transition = std::function<void(const State&, const State&)>;
    
private:
    State current_;
    std::vector<Transition> listeners_;
    std::stack<State> history_;
    std::stack<State> future_;
    std::mutex mtx_;
    
public:
    const State& get_state() const { return current_; }
    
    void add_listener(Transition cb) { 
        std::lock_guard lock(mtx_);
        listeners_.push_back(cb); 
    }
    
    void navigate_to(Page page, std::string detail_id = "") {
        std::lock_guard lock(mtx_);
        State next = current_;
        next.current_page = page;
        next.detail_id = std::move(detail_id);
        commit_transition(next);
    }
    
    bool can_go_back() const { return !history_.empty(); }
    bool can_go_forward() const { return !future_.empty(); }
    
    void go_back() {
        std::lock_guard lock(mtx_);
        if (history_.empty()) return;
        future_.push(current_);
        current_ = history_.top();
        history_.pop();
        notify_listeners();
    }
    
    void go_forward() {
        std::lock_guard lock(mtx_);
        if (future_.empty()) return;
        history_.push(current_);
        current_ = future_.top();
        future_.pop();
        notify_listeners();
    }
    
    void set_panel_mode(PanelMode mode) {
        std::lock_guard lock(mtx_);
        State next = current_;
        next.right_panel = mode;
        commit_transition(next);
    }
    
    void set_playback_mode(PlaybackMode mode) {
        std::lock_guard lock(mtx_);
        State next = current_;
        next.playback = mode;
        commit_transition(next);
    }
    
    void toggle_sidebar() {
        std::lock_guard lock(mtx_);
        State next = current_;
        next.sidebar_expanded = !next.sidebar_expanded;
        commit_transition(next);
    }
    
private:
    void commit_transition(State&& next) {
        State prev = current_;
        current_ = std::move(next);
        history_.push(prev);
        if (history_.size() > 50) {
            std::stack<State> trimmed;
            for (int i = 0; i < 25; ++i) {
                trimmed.push(history_.top());
                history_.pop();
            }
            history_ = std::move(trimmed);
        }
        while (!future_.empty()) future_.pop();
        notify_listeners();
    }
    
    void notify_listeners() {
        for (auto& cb : listeners_) {
            cb(history_.empty() ? State{} : history_.top(), current_);
        }
    }
};

/**
 * Command Pattern for undoable UI actions.
 */
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string description() const = 0;
};

class CommandManager {
    std::stack<std::unique_ptr<Command>> undo_stack_;
    std::stack<std::unique_ptr<Command>> redo_stack_;
    std::mutex mtx_;
    size_t max_history_ = 100;
    
public:
    void execute(std::unique_ptr<Command> cmd) {
        std::lock_guard lock(mtx_);
        cmd->execute();
        undo_stack_.push(std::move(cmd));
        if (undo_stack_.size() > max_history_) {
            // Remove oldest (bottom of stack) - need auxiliary structure
        }
        while (!redo_stack_.empty()) redo_stack_.pop();
    }
    
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    
    void undo() {
        std::lock_guard lock(mtx_);
        if (undo_stack_.empty()) return;
        auto cmd = std::move(undo_stack_.top());
        undo_stack_.pop();
        cmd->undo();
        redo_stack_.push(std::move(cmd));
    }
    
    void redo() {
        std::lock_guard lock(mtx_);
        if (redo_stack_.empty()) return;
        auto cmd = std::move(redo_stack_.top());
        redo_stack_.pop();
        cmd->execute();
        undo_stack_.push(std::move(cmd));
    }
};

// Example commands
class ToggleLikeCommand : public Command {
    std::string track_id_;
    bool previous_state_;
    ILibraryService* service_;
public:
    ToggleLikeCommand(ILibraryService* svc, const std::string& id, bool prev)
        : track_id_(id), previous_state_(prev), service_(svc) {}
    
    void execute() override { service_->toggle_like(track_id_); }
    void undo() override { 
        if (service_->is_track_liked(track_id_) != previous_state_) {
            service_->toggle_like(track_id_);
        }
    }
    std::string description() const override { return "Toggle like"; }
};

class PlayAlbumCommand : public Command {
    std::vector<std::string> track_ids_;
    size_t start_index_;
    IPlayerService* player_;
    std::vector<std::string> previous_queue_;
    size_t previous_index_;
public:
    PlayAlbumCommand(IPlayerService* p, std::vector<std::string> ids, size_t idx)
        : track_ids_(std::move(ids)), start_index_(idx), player_(p) {
        previous_queue_ = player_->get_queue();
        previous_index_ = player_->get_queue_index();
    }
    
    void execute() override { player_->play_tracks(track_ids_, start_index_); }
    void undo() override { 
        player_->play_tracks(previous_queue_, previous_index_); 
    }
    std::string description() const override { return "Play album"; }
};

} // namespace lollypop
```

---

## 7. Integration Layer — **Actor Model + Message Passing**

```cpp
// frontend/src/integration/BackendBridge.hpp
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <atomic>
#include <variant>
#include <memory>
#include <unordered_map>

namespace lollypop {

/**
 * Actor-model bridge between frontend (GTK main thread) and backend (worker threads).
 * All communication via message passing — no shared mutable state.
 */
class BackendBridge {
public:
    // Message types
    struct GetAlbums { uint64_t request_id; };
    struct GetAlbumsResult { uint64_t request_id; std::vector<Album> albums; };
    struct GetTracksByIds { uint64_t request_id; std::vector<std::string> ids; };
    struct GetTracksByIdsResult { uint64_t request_id; std::vector<Track> tracks; };
    struct PlayTracks { uint64_t request_id; std::vector<std::string> ids; size_t start; };
    struct PlayTracksResult { uint64_t request_id; bool success; };
    struct SearchTracks { uint64_t request_id; std::string query; };
    struct SearchTracksResult { uint64_t request_id; std::vector<Track> tracks; };
    struct LibraryChanged {};
    struct TrackChanged { Track track; };
    
    using Message = std::variant<
        GetAlbums, GetAlbumsResult,
        GetTracksByIds, GetTracksByIdsResult,
        PlayTracks, PlayTracksResult,
        SearchTracks, SearchTracksResult,
        LibraryChanged, TrackChanged
    >;
    
private:
    // Lock-free MPSC queue for backend → frontend
    struct QueueNode {
        Message msg;
        std::atomic<QueueNode*> next{nullptr};
    };
    
    std::atomic<QueueNode*> head_{nullptr};
    std::atomic<QueueNode*> tail_{nullptr};
    std::mutex frontend_mtx_;
    std::condition_variable frontend_cv_;
    bool frontend_has_messages_ = false;
    
    std::thread backend_thread_;
    std::atomic<bool> running_{false};
    BackendController* backend_ = nullptr;
    
    struct PendingRequest {
        std::promise<Message> promise;
        uint64_t request_id;
    };
    std::unordered_map<uint64_t, PendingRequest> pending_;
    std::mutex pending_mtx_;
    uint64_t next_request_id_ = 1;
    
    std::function<void()> library_changed_cb_;
    
public:
    BackendBridge(BackendController* backend) : backend_(backend) {
        running_ = true;
        backend_thread_ = std::thread(&BackendBridge::backend_loop, this);
    }
    
    ~BackendBridge() {
        running_ = false;
        if (backend_thread_.joinable()) backend_thread_.join();
    }
    
    // Frontend: send request, get future
    template<typename Request, typename Response>
    std::future<Response> send_request(Request&& req) {
        uint64_t id = next_request_id_++;
        req.request_id = id;
        
        std::promise<Message> promise;
        auto future = promise.get_future();
        
        {
            std::lock_guard lock(pending_mtx_);
            pending_[id] = {std::move(promise), id};
        }
        
        push_to_backend(std::move(req));
        
        return future.then([](std::future<Message> f) -> Response {
            Message msg = f.get();
            return std::get<Response>(msg);
        });
    }
    
    // Frontend: process messages (call from GTK main loop via idle callback)
    void process_messages(std::function<void(const Message&)> handler) {
        std::unique_lock lock(frontend_mtx_);
        while (frontend_has_messages_) {
            QueueNode* node = pop_frontend();
            if (!node) break;
            handler(node->msg);
            delete node;
        }
        frontend_has_messages_ = false;
    }
    
    void set_library_changed_callback(std::function<void()> cb) {
        library_changed_cb_ = std::move(cb);
    }
    
private:
    void backend_loop() {
        while (running_) {
            // Process backend callbacks
            backend_->set_track_changed_callback([this](const Track& track) {
                push_to_frontend(TrackChanged{track});
            });
            
            backend_->set_library_changed_callback([this]() {
                push_to_frontend(LibraryChanged{});
                if (library_changed_cb_) library_changed_cb_();
            });
            
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    
    void push_to_backend(Message&& msg) {
        // Lock-free push to backend queue
    }
    
    void push_to_frontend(Message&& msg) {
        QueueNode* node = new QueueNode{std::move(msg)};
        QueueNode* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
        
        {
            std::lock_guard lock(frontend_mtx_);
            frontend_has_messages_ = true;
        }
        frontend_cv_.notify_one();
    }
    
    QueueNode* pop_frontend() {
        QueueNode* head = head_.load(std::memory_order_acquire);
        if (!head) return nullptr;
        QueueNode* next = head->next.load(std::memory_order_acquire);
        if (!next) return nullptr;
        head_.store(next, std::memory_order_release);
        return head;
    }
};

} // namespace lollypop
```

---

## 8. Implementation Phases

### Phase 1: Core Data Structures (Week 1)
| Task | Component | Data Structures |
|------|-----------|-----------------|
| 1.1 | ScanDAG | DAG + Priority Queue + Work-Stealing Deque |
| 1.2 | LazyTrackIndex | Segment Tree + Fenwick Tree + Skip List + Bloom Filter |
| 1.3 | QueueManager Optimized | Intrusive DLL + Circular Buffer + Fisher-Yates |
| 1.4 | Visualization | SPSC Ring Buffer + EMA + Double Buffer |

### Phase 2: Cache & Search (Week 2)
| Task | Component | Data Structures |
|------|-----------|-----------------|
| 2.1 | ArtworkCacheManager | LRU-K + B-tree + Bloom Filter |
| 2.2 | SearchEngine | Trie + Inverted Index + BM25 + Levenshtein Automaton |
| 2.3 | LibraryDAG | DAG + Tarjan's SCC + Incremental Topo Sort |

### Phase 3: Frontend Models (Week 3)
| Task | Component | Data Structures |
|------|-----------|-----------------|
| 3.1 | VirtualizedListModel | Gap Buffer + B-tree Index |
| 3.2 | UIStateMachine + Commands | FSM + Command Stack + Undo/Redo |
| 3.3 | BackendBridge | Actor Model + Lock-free MPSC Queue |

### Phase 4: Integration & Polish (Week 4)
| Task | Description |
|------|-------------|
| 4.1 | Wire all components together |
| 4.2 | Benchmark & profile |
| 4.3 | Edge case handling |
| 4.4 | Documentation |

---

## 9. Performance Targets & Benchmarks

| Component | Data Structure | Operation | Target Complexity | Current | Optimized |
|-----------|---------------|-----------|-------------------|---------|-----------|
| Library Scan | DAG + Priority Queue | Schedule job | O(log n) | O(1) FIFO | **O(log n)** |
| Library Scan | Work-Stealing Deque | Parallel scan | O(n/p) | O(n) | **O(n/p)** |
| SongsPage | Segment Tree + Fenwick | Range query | O(log n) | O(n) | **O(log n)** |
| SongsPage | Skip List | Concurrent access | O(log n) | Locked | **Lock-free** |
| SongsPage | Bloom Filter | Negative check | O(1) | N/A | **O(1)** |
| Queue | Intrusive DLL | Move/Remove | O(1) | O(n) | **O(1)** |
| Queue | Circular Buffer | History/Play-next | O(1) | O(n) | **O(1)** |
| Artwork | LRU-K + B-tree | Get/Put | O(log n) | O(1) avg | **O(log n)** worst |
| Artwork | Bloom Filter | Missing check | O(1) | stat() | **O(1)** no I/O |
| Visualizer | SPSC Ring Buffer | Producer/Consumer | O(1) | Mutex | **Lock-free** |
| Visualizer | Double Buffer | Tear-free read | O(1) | Copy | **Zero-copy** |
| Search | Trie | Autocomplete | O(m) | N/A | **O(m)** |
| Search | Inverted Index + BM25 | Ranked search | O(q log d) | FTS5 | **O(q log d)** |
| Library | DAG + Topo Sort | Dependency order | O(V+E) | N/A | **O(V+E)** |
| UI List | Gap Buffer | Insert/Delete | O(1) amortized | O(n) | **O(1)** |
| UI State | FSM + Command Stack | Undo/Redo | O(1) | N/A | **O(1)** |

---

## 10. Memory Budget

| Structure | Est. Memory (100K tracks) |
|-----------|---------------------------|
| ScanDAG | ~2 MB |
| LazyTrackIndex (SegmentTree + Fenwick + SkipList + Bloom) | ~15 MB |
| QueueManager (DLL + arrays) | ~2 MB |
| ArtworkCache (LRU-K + B-tree + Bloom) | ~50 MB (configurable) |
| Visualizer (Ring Buffer + Double Buffer) | ~64 KB |
| SearchEngine (Trie + Inverted Index) | ~30 MB |
| LibraryDAG | ~5 MB |
| VirtualizedListModel (GapBuffer) | ~2 MB |
| **Total** | **~106 MB** (well within limits) |

---

## 11. Acceptance Criteria

| Criterion | Verification |
|-----------|--------------|
| Library pages show real data | Scan Music folder → Albums/Artists/Songs pages populate |
| Songs page sorts correctly | Click column headers → global DB sort, lazy load works |
| Play album from AlbumsPage | Click Play → playback starts, queue populated, right panel updates |
| Queue in RightNowPlaying | Play album → queue shows tracks, click track → plays that track |
| Lyrics display | Play track with embedded lyrics → Lyrics tab shows synced text |
| Search works | Type in search entry → results appear in SearchResultsPage |
| Artwork loads async | Scroll AlbumsPage → artwork appears without UI freeze |
| MPRIS controls work | Media keys / sound menu → play/pause/next/prev/seek/volume |
| File watcher works | Add/remove file in Music folder → library updates automatically |
| Visualization runs | Play audio → waveform animates in both panels at ~30 FPS |
| No main-thread blocking | Profile: no >16ms spikes on main thread during scan/playback |
| Undo/Redo works | Toggle like → Ctrl+Z → reverts; Ctrl+Y → reapplies |
| Responsive layout | Resize window → panels adapt per spec (600px min width) |

---

## 12. File Map for Implementation

```
backend/
├── include/
│   ├── scan_dag.hpp                    # NEW: DAG + Priority Queue + Work-Stealing
│   ├── queue_manager_optimized.hpp     # NEW: Intrusive DLL + Circular Buffer
│   ├── artwork_cache_optimized.hpp     # NEW: LRU-K + B-tree + Bloom
│   ├── visualizer_optimized.hpp        # NEW: SPSC Ring Buffer + Double Buffer
│   ├── library_dag.hpp                 # NEW: DAG + Tarjan SCC + Incremental Topo
│   └── ...
├── src/
│   ├── scan_dag.cpp                    # NEW
│   ├── queue_manager_optimized.cpp     # NEW (replace queue_manager.cpp)
│   ├── artwork_cache_optimized.cpp     # NEW (replace cache_manager.cpp + artwork_manager.cpp)
│   ├── visualizer_optimized.cpp        # NEW (replace visualizer_engine.cpp)
│   ├── library_dag.cpp                 # NEW
│   └── ...

frontend/
├── src/
│   ├── models/
│   │   ├── LazyTrackIndex.hpp          # NEW: SegmentTree + Fenwick + SkipList + Bloom
│   │   ├── VirtualizedListModel.hpp    # NEW: GapBuffer + B-tree Index
│   │   └── ...
│   ├── services/
│   │   ├── SearchEngine.hpp            # NEW: Trie + Inverted Index + BM25
│   │   └── ...
│   ├── state/
│   │   ├── UIStateMachine.hpp          # NEW: FSM + Command Pattern
│   │   └── ...
│   ├── integration/
│   │   ├── BackendBridge.hpp           # NEW: Actor Model + MPSC Queue
│   │   └── ...
│   ├── panels/
│   │   ├── middle_library.cpp          # CONNECT: Search, verify pages
│   │   ├── right_now_playing.cpp       # CONNECT: queue_changed, lyrics, modes
│   │   ├── bottom_playback.cpp         # ADD: Volume, mode buttons
│   │   └── ...
│   ├── ui/pages/
│   │   ├── SongsPage.cpp               # IMPLEMENT: LazyTrackListModel
│   │   ├── SearchResultsPage.cpp       # NEW: Lazy search results
│   │   └── ...
│   ├── ui/components/
│   │   ├── ArtworkView.cpp             # IMPLEMENT: Async load, cache, fallback
│   │   ├── AlbumCard.cpp               # CONNECT: play_callback to backend
│   │   ├── TrackRow.cpp                # CONNECT: play/like callbacks
│   │   └── ...
│   └── ...
```

---

## 13. Threading & Main-Context Rules

| Operation | Thread | Main-Context Marshalling |
|-----------|--------|--------------------------|
| SQLite queries (read) | LibraryManager worker / thread pool | Results via `Glib::Dispatcher` → callback |
| SQLite writes (scan, like, playlist) | LibraryManager worker (batched transactions) | `emit_library_changed()` via `Glib::Dispatcher` |
| AudioEngine callbacks | GStreamer streaming thread | `Glib::Dispatcher` (position, state, EOS) |
| MPRIS D-Bus callbacks | D-Bus thread (GLib main loop) | Direct (already on GLib main context) |
| FileWatcher events | inotify thread | Queue → `Glib::Dispatcher` → `dispatch_file_events()` |
| LyricsManager fetch | Worker thread | `Glib::Dispatcher` → callback |
| Artwork decode | Worker thread (GTask) | `g_main_context_invoke()` → `gtk_image_set_from_paintable()` |
| Visualization (spectrum) | VisualizerEngine (bus sync handler) | EventBus → main context subscription |
| UI callbacks (GTK signals) | GTK main thread | N/A (already on main context) |

**Golden Rule:** Never touch GTK widgets from non-main threads. All backend-to-UI callbacks **must** go through `Glib::Dispatcher` or `g_main_context_invoke()`.

---

## 14. Data Flow Summary Tables

### 14.1 Read Operations (UI → Backend)

| UI Component | Service Method | Backend Method | Storage Method | Thread |
|--------------|----------------|----------------|----------------|--------|
| AlbumsPage | `get_all_albums()` | `BackendController::get_all_albums()` | `StorageManager::get_all_albums()` | Worker |
| ArtistsPage | `get_all_artists()` | `BackendController::get_all_artists()` | `StorageManager::get_all_artists()` | Worker |
| SongsPage (sort) | `get_sorted_track_ids(col, asc)` | `BackendController::get_sorted_track_ids()` | `StorageManager::get_tracks_paginated()` | Worker |
| SongsPage (lazy) | `get_tracks_by_ids(ids)` | `BackendController::get_tracks_by_ids()` | `StorageManager::get_track()` (batched) | Worker |
| AlbumDetail | `get_album(id)` | `BackendController::get_album()` | `StorageManager::get_album()` | Worker |
| Search | `search_tracks(query)` | `BackendController::search_tracks()` | `StorageManager::search_tracks()` (FTS5) | Worker |
| Artwork | `get_artwork_path_for_*` | `BackendController::get_artwork_path_for_*` | `StorageManager::get_*` + CacheManager | Main (path) → Worker (decode) |

### 14.2 Write Operations (UI → Backend → Storage)

| UI Action | Service Method | Backend Method | Storage Method | Callback |
|-----------|----------------|----------------|----------------|----------|
| Toggle like | `toggle_like(track_id)` | `BackendController::toggle_like()` | `StorageManager::toggle_like()` | `emit_library_changed()` |
| Toggle favourite | `toggle_favorite_album(album_id)` | `BackendController::toggle_favorite_album()` | `StorageManager::toggle_favorite_album()` | `emit_library_changed()` |
| Create playlist | `create_playlist(name)` | `BackendController::create_playlist()` | `StorageManager::create_playlist()` | `emit_library_changed()` |
| Add to playlist | `add_track_to_playlist()` | `BackendController::add_track_to_playlist()` | `StorageManager::add_track_to_playlist()` | `emit_library_changed()` |
| Play album | `play_tracks(ids, idx)` | `BackendController::play_tracks()` | `QueueManager::set_queue()` + persist | `emit_track_changed`, `emit_queue_changed`, `emit_playback_state_changed` |
| Seek | `seek(pos)` | `BackendController::seek()` | `AudioEngine::seek()` | `emit_position_changed` |
| Volume | `set_volume(vol)` | `BackendController::set_volume()` | `AudioEngine::set_volume()` + pref | `mpris_server_->update_volume()` |
| Shuffle/Repeat | `set_shuffle/repeat/repeat_one()` | `BackendController::set_shuffle/repeat/repeat_one()` | `QueueManager` + pref | `emit_queue_changed` + MPRIS |

### 14.3 Push Notifications (Backend → UI)

| Callback | Trigger | UI Consumers |
|----------|---------|--------------|
| `track_changed_cb_` | Track load, track change, stop | BottomPlayback, RightNowPlaying |
| `position_changed_cb_` | AudioEngine position (~200ms) | BottomPlayback, RightNowPlaying |
| `playback_state_changed_cb_` | Play/pause/stop | BottomPlayback, RightNowPlaying |
| `queue_changed_cb_` | Queue set, reorder, remove, clear | RightNowPlaying (queue view) |
| `visualization_cb_` | Spectrum frame (EventBus) | BottomPlayback, RightNowPlaying (waveform) |
| `library_changed_cb_` | Scan finish, file watcher, like/fav change | MiddleLibrary (refresh active page) |

---

## 15. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| `SongsPage` lazy model complexity | High | Prototype `LazyTrackIndex` separately with 1000 dummy IDs first |
| Artwork decode blocking main thread | Medium | Use `GTask` + thread pool, verify with `GDK_DEBUG=texture-cache` |
| MPRIS `artUrl` pointing to deleted cache file | Medium | `ArtworkCacheManager` must persist files, not use temp files |
| Spectrum visualization jitter | Low | Conflate frames in `DoubleBufferedVisualizerModel`, target 30 FPS |
| Search debounce too aggressive | Low | Start with 300ms, tune based on UX testing |
| Queue rebuild on every change | Low | Acceptable for <500 tracks; optimize later if needed |
| DAG cycle detection on large library | Low | Run incrementally, cache SCCs |
| SkipList memory overhead | Low | Use only for queue (small), not for full library |

---

*Complete Unified Document Generated: 2026-11-21*  
*All integration requirements + algorithmic optimizations in one reference.*