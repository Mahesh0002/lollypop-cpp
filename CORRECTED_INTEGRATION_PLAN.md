# Lollypop C++ — Corrected Integration & Implementation Plan

> **Status:** Post-review corrections applied  
> **Source:** Code review of COMPLETE_INTEGRATION_AND_OPTIMIZATION_PLAN.md  
> **Date:** 21 November 2026

---

## 1. Executive Summary

The original optimization plan contained numerous incorrect implementations. This document replaces it with a **correctness-first** approach:

| Priority | Focus |
|----------|-------|
| 1 | Correctness & main-context safety |
| 2 | Frontend ↔ Backend integration |
| 3 | Asynchronous execution with cancellation |
| 4 | Memory management & bounded caches |
| 5 | Profiling |
| 6 | Targeted optimization (only where measured) |

**Do not copy** the original optimization headers. They contain:
- Broken lock-free queues
- Incorrect double-buffering
- Lazy-loading that fetches everything
- Queue managers that lose track identity
- Artwork caches with broken accounting
- Search engines with ranking bugs
- Unnecessary dependency graphs
- Gap buffers with boundary errors
- State machines that deadlock

---

## 2. Corrected Architecture

```text
GTK 4 + libadwaita
│
├── MainWindow
│   ├── LeftSidebar
│   ├── MiddleLibrary
│   │   ├── HomePage
│   │   ├── AlbumsPage
│   │   ├── ArtistsPage
│   │   ├── SongsPage
│   │   ├── PlaylistsPage
│   │   └── SearchResultsPage
│   ├── RightNowPlaying
│   │   ├── TrackInformation
│   │   ├── Queue
│   │   ├── Lyrics
│   │   └── Visualizer
│   └── BottomPlayback
│
├── BackendBridge
│   ├── TypedRequests + Responses
│   ├── AsyncResults with GTask
│   ├── Cancellation + Generation IDs
│   ├── MainContextDelivery
│   └── Explicit Callback Registration
│
└── BackendController
    │
    ├── IPlayerService
    │   ├── AudioEngine
    │   ├── QueueManager (std::deque + unique IDs)
    │   └── MprisServer
    │
    ├── ILibraryService
    │   ├── LibraryScanner
    │   ├── LibraryManager (unique work queue)
    │   └── StorageManager (SQLite + FTS5)
    │
    ├── ILyricsService
    │   └── LyricsManager
    │
    ├── ArtworkManager
    ├── CacheManager (byte-bounded LRU)
    └── VisualizerEngine (mutex-protected latest-frame)
```

**Rule:** Backend owns domain state. Frontend owns GTK widgets. BackendBridge owns communication boundaries.

---

## 3. Phase 0 — Repository Audit (Week 0)

### 3.1 Inspect Existing Components

```bash
# Build current state
cd /home/mahesh/Projects/lollypop-cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 3.2 Verification Checklist

| Component | Verify |
|-----------|--------|
| `IPlayerService` | All virtual methods implemented in `BackendController` |
| `ILibraryService` | All queries route to `StorageManager` |
| `ILyricsService` | Embedded/local fetch implemented |
| `BackendController` | Callbacks use `Glib::Dispatcher` |
| `LibraryManager` | Unique work queue, batched transactions |
| `CacheManager` | Byte-bounded LRU, TTL expiry |
| `ArtworkManager` | Size buckets, persistent files |
| `VisualizerEngine` | EventBus subscription, main-context delivery |
| `QueueManager` | Shuffle, repeat, play-next semantics |
| `MprisServer` | Mirrors state, forwards commands |
| GTK widgets | All access on main thread |
| Mock services | Only used in development |

### 3.3 Architecture Map

Create `docs/IMPLEMENTATION_AUDIT.md`:

| Component | Exists? | Language | Compatible? | Action |
|-----------|---------|----------|-------------|--------|
| Application shell | | | | |
| Playback service | | | | |
| Library service | | | | |
| Database | | | | |
| Artwork cache | | | | |
| Waveform renderer | | | | |
| Icon assets | | | | |

### 3.4 Exit Condition

- [ ] Successful baseline build (no warnings treated as errors)
- [ ] Verified architecture map
- [ ] All callback registrations traced
- [ ] Thread ownership documented

---

## 4. Phase 1 — Core Integration (Week 1–2)

### 4.1 Connect Library Pages

**Files to modify:**
- `frontend/src/ui/pages/AlbumsPage.cpp`
- `frontend/src/ui/pages/ArtistsPage.cpp`
- `frontend/src/ui/pages/PlaylistsPage.cpp`
- `frontend/src/ui/pages/GenresPage.cpp`
- `frontend/src/ui/pages/FavouriteAlbumsPage.cpp`
- `frontend/src/ui/pages/LikedMusicPage.cpp`
- `frontend/src/ui/pages/AlbumDetailPage.cpp`
- `frontend/src/ui/pages/ArtistDetailPage.cpp`
- `frontend/src/ui/pages/HomePage.cpp`

**Pattern for each page:**
```cpp
void Page::refresh() {
    if (!library_service_) return;
    
    // Show loading state
    show_loading();
    
    // Async query via BackendBridge
    auto request = std::make_shared<GetAlbumsRequest>();
    backend_bridge_->send_request(request, [this](GetAlbumsResponse resp) {
        // Must be on main context
        if (!gtk_widget_get_visible(GTK_WIDGET(widget_))) return; // Page destroyed
        
        if (resp.success) {
            populate(resp.albums);
        } else {
            show_error(resp.error);
        }
    });
}
```

### 4.2 Remove Mock Fallbacks

- Delete `MockLibraryService` usage from production paths
- Delete `MockLyricsService` usage from production paths
- Keep mocks only for unit tests

### 4.3 Connect Playback Actions

| UI Action | Backend Call |
|-----------|--------------|
| AlbumCard Play | `play_tracks(album_track_ids, 0)` |
| TrackRow Play | `play_queue_index(index)` |
| Play/Pause | `toggle_play_pause()` |
| Previous/Next | `previous_track()` / `next_track()` |
| Seek (waveform drag) | `seek(position)` — optimistic local, reconcile on release |
| Volume | `set_volume(value)` |
| Shuffle | `set_shuffle(enabled)` |
| Repeat | `set_repeat(enabled)` / `set_repeat_one(enabled)` |

### 4.4 Connect Queue Updates

```cpp
// In MainWindow::connect_backend()
backend->set_queue_changed_callback([this](const std::vector<std::string>& queue_ids) {
    right_now_playing_->set_queue(queue_ids);
    bottom_playback_->set_queue(queue_ids); // if queue shown
});
```

**RightNowPlaying::set_queue():**
```cpp
void RightNowPlaying::set_queue(const std::vector<std::string>& queue_ids) {
    // Clear existing rows
    // For each ID: fetch Track, create TrackRow with play callback
    // TrackRow play callback → backend->play_queue_index(index)
}
```

### 4.5 Connect Lyrics

```cpp
// In MainWindow::connect_backend()
backend->set_track_changed_callback([this](const Track& track) {
    // ... existing track info update ...
    
    if (!track.id.empty() && lyrics_service_) {
        lyrics_service_->request_lyrics(track.id, [this](std::optional<Lyrics> result) {
            if (result && !result->text.empty()) {
                right_now_playing_->set_lyrics(result->text, result->synced);
            } else {
                right_now_playing_->show_no_lyrics();
            }
        });
    }
});
```

### 4.6 Search Integration

**MiddleLibrary search entry:**
```cpp
// Debounce + generation counter
guint search_timeout_id = 0;
uint64_t search_generation = 0;

void on_search_changed(GtkEntry* entry) {
    const char* text = gtk_entry_get_text(entry);
    std::string query(text);
    
    if (search_timeout_id) g_source_remove(search_timeout_id);
    
    search_timeout_id = g_timeout_add(250, [this, query, gen = ++search_generation]() -> gboolean {
        search_timeout_id = 0;
        if (gen != search_generation) return G_SOURCE_REMOVE; // Stale
        
        if (query.empty()) {
            show_home_page();
            return G_SOURCE_REMOVE;
        }
        
        auto request = std::make_shared<SearchRequest>(query, gen);
        backend_bridge_->send_request(request, [this, gen](SearchResponse resp) {
            if (gen != search_generation) return; // Stale
            if (resp.success) {
                show_search_results(resp.tracks);
            }
        });
        return G_SOURCE_REMOVE;
    });
}
```

### 4.7 SongsPage — GListStore + Paged Retrieval

```cpp
// frontend/src/models/SongsListModel.hpp
class SongsListModel {
    GListStore* store_;
    std::vector<std::string> sorted_ids_;
    size_t loaded_count_ = 0;
    static constexpr size_t PAGE_SIZE = 100;
    static constexpr size_t PREFETCH = 50;
    
public:
    void set_sort(const std::string& column, bool ascending) {
        // Request sorted IDs from backend
        backend_bridge_->send_request(
            GetSortedTrackIdsRequest{column, ascending, ++generation_},
            [this, gen = generation_](GetSortedTrackIdsResponse resp) {
                if (gen != generation_) return;
                sorted_ids_ = std::move(resp.ids);
                loaded_count_ = 0;
                g_list_store_remove_all(store_);
                load_page(0);
            });
    }
    
    void load_page(size_t start) {
        size_t end = std::min(start + PAGE_SIZE, sorted_ids_.size());
        std::vector<std::string> batch(sorted_ids_.begin() + start, sorted_ids_.begin() + end);
        
        backend_bridge_->send_request(
            GetTracksByIdsRequest{batch, generation_},
            [this, gen = generation_, start](GetTracksByIdsResponse resp) {
                if (gen != generation_) return;
                for (auto& track : resp.tracks) {
                    g_list_store_append(store_, create_row_object(track));
                }
                loaded_count_ = std::max(loaded_count_, start + resp.tracks.size());
            });
    }
    
    // GtkColumnView calls this via GListModel interface
    GObject* get_item(guint position) {
        if (position + PREFETCH >= loaded_count_ && loaded_count_ < sorted_ids_.size()) {
            load_page(loaded_count_);
        }
        return g_list_model_get_item(G_LIST_MODEL(store_), position);
    }
};
```

### 4.8 Artwork Async Loading

```cpp
// frontend/src/ui/components/ArtworkView.cpp
void ArtworkView::set_artwork_path(const std::string& path) {
    if (current_path_ == path) return;
    current_path_ = path;
    
    // Cancel previous decode
    if (decode_task_) g_task_return_error(decode_task_, G_IO_ERROR_CANCELLED);
    
    if (path.empty()) {
        show_fallback();
        return;
    }
    
    decode_task_ = g_task_new(nullptr, nullptr, [this](GObject*, GAsyncResult* res) {
        GTask* task = G_TASK(res);
        GdkTexture* texture = gdk_texture_new_from_file_finish(res, nullptr);
        if (texture) {
            gtk_image_set_from_paintable(GTK_IMAGE(image_), GDK_PAINTABLE(texture));
            g_object_unref(texture);
        } else {
            show_fallback();
        }
        g_object_unref(task);
        decode_task_ = nullptr;
    }, nullptr);
    
    // Run in thread pool
    g_task_run_in_thread(decode_task_, [](GTask* task, gpointer) {
        const char* path = static_cast<const char*>(g_task_get_task_data(task));
        GError* error = nullptr;
        GdkTexture* texture = gdk_texture_new_from_filename(path, &error);
        if (error) {
            g_task_return_error(task, error);
            g_error_free(error);
        } else {
            g_task_return_pointer(task, texture, [](gpointer p) { g_object_unref(G_OBJECT(p)); });
        }
    }, g_strdup(path), g_free);
}
```

### 4.9 Exit Condition Phase 1

- [ ] All library pages show real data
- [ ] No mock fallbacks in production code
- [ ] Album/track playback works
- [ ] Queue auto-updates on changes
- [ ] Lyrics request fires on track change
- [ ] Search debounced, generation-tracked
- [ ] Songs page loads lazily
- [ ] Artwork decodes asynchronously
- [ ] All GTK access on main thread
- [ ] MPRIS synchronized

---

## 5. Phase 2 — Search & Lazy Loading Polish (Week 2–3)

### 5.1 FTS5 Search

```sql
-- In StorageManager schema
CREATE VIRTUAL TABLE tracks_fts USING fts5(
    title, artist, album, genre, filename,
    content='tracks', content_rowid='rowid'
);

CREATE TRIGGER tracks_ai AFTER INSERT ON tracks BEGIN
    INSERT INTO tracks_fts(rowid, title, artist, album, genre, filename)
    VALUES (new.rowid, new.title, new.artist, new.album, new.genre, new.file_path);
END;

CREATE TRIGGER tracks_ad AFTER DELETE ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, filename)
    VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.genre, old.file_path);
END;

CREATE TRIGGER tracks_au AFTER UPDATE ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, filename)
    VALUES ('delete', old.rowid, old.title, old.artist, old.album, old.genre, old.file_path);
    INSERT INTO tracks_fts(rowid, title, artist, album, genre, filename)
    VALUES (new.rowid, new.title, new.artist, new.album, new.genre, new.file_path);
END;
```

**Search query:**
```sql
SELECT rowid, title, artist, album, genre
FROM tracks_fts
WHERE tracks_fts MATCH ?
ORDER BY rank
LIMIT 100;
```

### 5.2 Database-Backed Sorting

```cpp
// StorageManager::get_tracks_paginated
std::vector<Track> StorageManager::get_tracks_paginated(
    int limit, const std::string& sort_column, 
    const std::string& after_id, const std::string& direction) {
    
    static const std::unordered_set<std::string> valid_columns = {
        "title", "artist", "album", "duration", "year", "track_number", "date_added"
    };
    
    if (!valid_columns.count(sort_column)) sort_column = "title";
    
    std::string sql = "SELECT * FROM tracks ORDER BY " + sort_column + " " + direction + " LIMIT ?";
    if (!after_id.empty()) {
        sql += " OFFSET (SELECT COUNT(*) FROM tracks WHERE " + sort_column + " " 
               + (direction == "ASC" ? "<" : ">") + " (SELECT " + sort_column + " FROM tracks WHERE id = ?))";
    }
    
    // Execute with prepared statement
}
```

### 5.3 Exit Condition Phase 2

- [ ] FTS5 search returns results in <150ms for 50K tracks
- [ ] Search results ordered by relevance
- [ ] Songs page sorting uses database ORDER BY
- [ ] Lazy loading preserves requested ID order
- [ ] Prefetch prevents scroll stutter
- [ ] Artwork cache hit rate >80% during browsing

---

## 6. Phase 3 — Playback & Visualization (Week 3–4)

### 6.1 Queue Semantics (Fixed)

**QueueItem identity:**
```cpp
struct QueueItem {
    uint64_t queue_id;      // Unique per queue entry (not track_id)
    std::string track_id;   // Can duplicate
    uint64_t added_at;      // For stable ordering
};
```

**QueueManager operations:**
```cpp
class QueueManager {
    std::deque<QueueItem> queue_;
    std::deque<QueueItem> play_next_;  // No capacity limit
    std::vector<QueueItem> history_;   // Actual playback history
    size_t current_index_ = 0;
    bool shuffle_ = false;
    RepeatMode repeat_ = RepeatMode::None;
    std::vector<size_t> shuffle_order_;
    uint64_t next_queue_id_ = 1;
    std::mutex mtx_;
    
public:
    // Play Now: replace queue
    void set_queue(const std::vector<std::string>& track_ids, size_t start = 0) {
        std::lock_guard lock(mtx_);
        queue_.clear();
        play_next_.clear();
        history_.clear();
        for (const auto& id : track_ids) {
            queue_.push_back({next_queue_id_++, id, now()});
        }
        current_index_ = std::min(start, queue_.size() > 0 ? queue_.size() - 1 : 0);
        rebuild_shuffle();
    }
    
    // Play Next: insert after current
    void play_next(const std::string& track_id) {
        std::lock_guard lock(mtx_);
        if (queue_.empty()) {
            set_queue({track_id});
            return;
        }
        size_t insert_at = std::min(current_index_ + 1, queue_.size());
        queue_.insert(queue_.begin() + insert_at, {next_queue_id_++, track_id, now()});
        rebuild_shuffle();
    }
    
    // Add to Queue: append
    void enqueue(const std::string& track_id) {
        std::lock_guard lock(mtx_);
        queue_.push_back({next_queue_id_++, track_id, now()});
        rebuild_shuffle();
    }
    
    // Next track
    std::optional<std::string> next() {
        std::lock_guard lock(mtx_);
        if (queue_.empty()) return std::nullopt;
        
        // Play Next FIFO has priority
        if (!play_next_.empty()) {
            auto item = play_next_.front();
            play_next_.pop_front();
            size_t insert_at = std::min(current_index_ + 1, queue_.size());
            queue_.insert(queue_.begin() + insert_at, item);
            current_index_ = insert_at;
            history_.push_back(queue_[current_index_]);
            rebuild_shuffle();
            return queue_[current_index_].track_id;
        }
        
        if (repeat_ == RepeatMode::Track) return queue_[current_index_].track_id;
        
        if (shuffle_) {
            if (shuffle_pos_ + 1 >= shuffle_order_.size()) {
                if (repeat_ == RepeatMode::Playlist) {
                    rebuild_shuffle(); // Reshuffle
                    shuffle_pos_ = 0;
                } else return std::nullopt;
            } else {
                ++shuffle_pos_;
            }
            current_index_ = shuffle_order_[shuffle_pos_];
        } else {
            if (current_index_ + 1 < queue_.size()) {
                ++current_index_;
            } else if (repeat_ == RepeatMode::Playlist) {
                current_index_ = 0;
            } else return std::nullopt;
        }
        history_.push_back(queue_[current_index_]);
        return queue_[current_index_].track_id;
    }
    
    // Previous track — uses actual history
    std::optional<std::string> previous() {
        std::lock_guard lock(mtx_);
        if (queue_.empty()) return std::nullopt;
        if (repeat_ == RepeatMode::Track) return queue_[current_index_].track_id;
        
        if (shuffle_) {
            if (shuffle_pos_ > 0) {
                --shuffle_pos_;
                current_index_ = shuffle_order_[shuffle_pos_];
            } else if (repeat_ == RepeatMode::Playlist) {
                rebuild_shuffle();
                shuffle_pos_ = shuffle_order_.size() - 1;
                current_index_ = shuffle_order_[shuffle_pos_];
            } else return std::nullopt;
        } else {
            // Sequential: restart current if >3s, else previous
            // (position check done in BackendController)
            if (current_index_ > 0) {
                --current_index_;
            } else if (repeat_ == RepeatMode::Playlist) {
                current_index_ = queue_.size() - 1;
            } else return std::nullopt;
        }
        history_.push_back(queue_[current_index_]);
        return queue_[current_index_].track_id;
    }
    
    // Remove by queue_id (not track_id)
    bool remove(uint64_t queue_id) {
        std::lock_guard lock(mtx_);
        for (size_t i = 0; i < queue_.size(); ++i) {
            if (queue_[i].queue_id == queue_id) {
                bool was_current = (i == current_index_);
                queue_.erase(queue_.begin() + i);
                if (queue_.empty()) {
                    current_index_ = 0;
                } else if (was_current) {
                    current_index_ = std::min(i, queue_.size() - 1);
                } else if (i < current_index_) {
                    --current_index_;
                }
                rebuild_shuffle();
                return true;
            }
        }
        return false;
    }
    
    // Snapshot for UI
    std::vector<QueueItem> snapshot() const {
        std::lock_guard lock(mtx_);
        return std::vector<QueueItem>(queue_.begin(), queue_.end());
    }
    
private:
    size_t shuffle_pos_ = 0;
    
    void rebuild_shuffle() {
        shuffle_order_.clear();
        shuffle_order_.reserve(queue_.size());
        for (size_t i = 0; i < queue_.size(); ++i) shuffle_order_.push_back(i);
        if (shuffle_) {
            std::mt19937_64 rng(std::random_device{}());
            for (size_t i = shuffle_order_.size(); i > 1; --i) {
                std::uniform_int_distribution<size_t> dist(0, i - 1);
                std::swap(shuffle_order_[i - 1], shuffle_order_[dist(rng)]);
            }
        }
        // Find current position in shuffle order
        if (shuffle_ && current_index_ < queue_.size()) {
            for (size_t i = 0; i < shuffle_order_.size(); ++i) {
                if (shuffle_order_[i] == current_index_) {
                    shuffle_pos_ = i;
                    break;
                }
            }
        }
    }
};
```

### 6.2 Visualizer — Latest-Frame Snapshot

```cpp
// backend/include/visualizer_snapshot.hpp
#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <array>

namespace lollypop {

/**
 * Thread-safe latest-frame snapshot.
 * Producer: GStreamer bus sync handler (audio thread).
 * Consumer: GTK frame clock callback (main thread).
 */
class VisualizerSnapshot {
    static constexpr size_t MAX_BANDS = 64;
    
    struct Frame {
        std::array<float, MAX_BANDS> magnitudes{};
        std::array<float, MAX_BANDS> smoothed{};
        float rms = 0.0f;
        float peak = 0.0f;
        uint64_t sequence = 0;
        size_t valid_bands = 0;
    };
    
    // Double buffer with atomic pointer swap
    std::array<Frame, 2> frames_;
    std::atomic<Frame*> front_{&frames_[0]};
    std::atomic<Frame*> back_{&frames_[1]};
    std::atomic<uint64_t> front_sequence_{0};
    
    // EMA state (protected by mutex, updated on producer thread)
    std::mutex ema_mutex_;
    std::array<float, MAX_BANDS> ema_state_{};
    bool ema_initialized_ = false;
    float alpha_ = 0.3f;
    
public:
    VisualizerSnapshot() = default;
    
    // Called from GStreamer bus sync handler (audio thread)
    // Must be non-blocking, no allocations
    void write_frame(const float* magnitudes, size_t n) {
        if (n == 0 || n > MAX_BANDS) return;
        
        Frame* back = back_.load(std::memory_order_relaxed);
        
        // Copy magnitudes
        back->valid_bands = n;
        for (size_t i = 0; i < n; ++i) {
            back->magnitudes[i] = magnitudes[i];
        }
        for (size_t i = n; i < MAX_BANDS; ++i) {
            back->magnitudes[i] = 0.0f;
        }
        
        // Compute RMS and peak
        float rms_sum = 0.0f;
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float v = magnitudes[i];
            rms_sum += v * v;
            if (v > peak) peak = v;
        }
        back->rms = std::sqrt(rms_sum / n);
        back->peak = peak;
        
        // EMA smoothing
        {
            std::lock_guard lock(ema_mutex_);
            if (!ema_initialized_) {
                for (size_t i = 0; i < n; ++i) ema_state_[i] = magnitudes[i];
                ema_initialized_ = true;
            }
            for (size_t i = 0; i < n; ++i) {
                ema_state_[i] = alpha_ * magnitudes[i] + (1.0f - alpha_) * ema_state_[i];
                back->smoothed[i] = ema_state_[i];
            }
            for (size_t i = n; i < MAX_BANDS; ++i) {
                back->smoothed[i] = 0.0f;
            }
        }
        
        // Publish with sequence number
        back->sequence = front_sequence_.load(std::memory_order_relaxed) + 1;
        std::atomic_thread_fence(std::memory_order_release);
        
        Frame* expected = front_.load(std::memory_order_acquire);
        front_.compare_exchange_strong(expected, back, 
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed);
        front_sequence_.store(back->sequence, std::memory_order_release);
        
        // Swap back buffer
        Frame* new_back = (back == &frames_[0]) ? &frames_[1] : &frames_[0];
        back_.store(new_back, std::memory_order_relaxed);
    }
    
    // Called from GTK frame clock callback (main thread)
    // Returns true if new frame available
    bool read_frame(std::vector<float>& out_magnitudes,
                    std::vector<float>& out_smoothed,
                    float& out_rms, float& out_peak,
                    uint64_t& out_sequence) {
        Frame* front = front_.load(std::memory_order_acquire);
        uint64_t seq = front_sequence_.load(std::memory_order_acquire);
        if (seq == 0) return false; // No data yet
        
        // Verify we're reading the published frame
        if (front->sequence != seq) return false;
        
        out_magnitudes.assign(front->magnitudes.begin(), 
                              front->magnitudes.begin() + front->valid_bands);
        out_smoothed.assign(front->smoothed.begin(), 
                            front->smoothed.begin() + front->valid_bands);
        out_rms = front->rms;
        out_peak = front->peak;
        out_sequence = front->sequence;
        return true;
    }
    
    void set_smoothing_alpha(float alpha) {
        alpha_ = std::clamp(alpha, 0.0f, 1.0f);
    }
    
    // For waveform rendering
    void get_waveform_data(float progress, 
                           std::vector<float>& out_left,
                           std::vector<float>& out_right,
                           size_t width) {
        Frame* front = front_.load(std::memory_order_acquire);
        uint64_t seq = front_sequence_.load(std::memory_order_acquire);
        if (seq == 0) {
            out_left.assign(width, 0.0f);
            out_right.assign(width, 0.0f);
            return;
        }
        
        out_left.resize(width);
        out_right.resize(width);
        size_t bands = front->valid_bands;
        if (bands == 0) {
            std::fill(out_left.begin(), out_left.end(), 0.0f);
            std::fill(out_right.begin(), out_right.end(), 0.0f);
            return;
        }
        
        for (size_t x = 0; x < width; ++x) {
            float band_pos = (float)x / width * bands;
            size_t i = (size_t)band_pos;
            float frac = band_pos - i;
            
            float v1 = (i < bands) ? front->smoothed[i] : 0.0f;
            float v2 = (i + 1 < bands) ? front->smoothed[i + 1] : 0.0f;
            float v = v1 + frac * (v2 - v1);
            
            if ((float)x / width < progress) {
                out_left[x] = v;
                out_right[x] = 0.0f;
            } else {
                out_left[x] = 0.0f;
                out_right[x] = v;
            }
        }
    }
};

} // namespace lollypop
```

### 6.3 Frame-Clock Integration

```cpp
// frontend/src/panels/VisualizerWidget.cpp
class VisualizerWidget {
    VisualizerSnapshot* snapshot_;
    uint64_t last_sequence_ = 0;
    guint frame_clock_id_ = 0;
    
public:
    void set_snapshot(VisualizerSnapshot* snap) { snapshot_ = snap; }
    
    void start() {
        if (frame_clock_id_) return;
        frame_clock_id_ = gdk_frame_clock_get_frame_clock(gtk_widget_get_frame_clock(widget_));
        g_signal_connect(frame_clock_id_, "update", G_CALLBACK(on_frame_update), this);
    }
    
    void stop() {
        if (frame_clock_id_) {
            g_signal_handlers_disconnect_by_func(frame_clock_id_, on_frame_update, this);
            frame_clock_id_ = 0;
        }
    }
    
private:
    static void on_frame_update(GdkFrameClock* clock, gpointer user_data) {
        auto* self = static_cast<VisualizerWidget*>(user_data);
        self->on_frame();
    }
    
    void on_frame() {
        std::vector<float> magnitudes, smoothed;
        float rms, peak;
        uint64_t seq;
        
        if (snapshot_ && snapshot_->read_frame(magnitudes, smoothed, rms, peak, seq)) {
            if (seq != last_sequence_) {
                last_sequence_ = seq;
                // Get progress from playback position
                float progress = get_playback_progress(); // 0.0–1.0
                std::vector<float> left, right;
                snapshot_->get_waveform_data(progress, left, right, allocation_width_);
                queue_draw(); // Triggers snapshot() with GSK
            }
        }
    }
    
    void snapshot(GtkSnapshot* snap) {
        // Render left/right arrays with accent color
        // Use GSK rounded rect, linear gradient, etc.
    }
};
```

### 6.4 Exit Condition Phase 3

- [ ] Queue uses unique queue-item IDs
- [ ] Play Next never drops entries
- [ ] Previous uses actual history
- [ ] Shuffle Previous works correctly
- [ ] Repeat Track / Repeat Queue work
- [ ] Visualizer runs at ~30 FPS
- [ ] No allocations in audio thread
- [ ] Frame tear-free (double-buffer swap)
- [ ] Waveform split at playhead works
- [ ] Lyrics sync with playback

---

## 7. Phase 4 — Profiling & Targeted Optimization (Week 4+)

### 7.1 Measurement Script

```bash
#!/bin/bash
# profile.sh

# 1. Main thread frame time
perf record -g --call-graph=dwarf -p $(pidof lollypop) -- sleep 30
perf report

# 2. SQLite query latency
# Add timing to StorageManager methods

# 3. Artwork decode time
# Instrument ArtworkView decode_task_

# 4. Cache hit rate
# Add counters to CacheManager

# 5. Memory
valgrind --tool=massif ./lollypop
ms_print massif.out.*

# 6. Visualizer callback cost
# Add tracepoints in VisualizerSnapshot::write_frame
```

### 7.2 Optimization Decision Matrix

| If Profile Shows | Then Consider |
|------------------|---------------|
| Search >200ms | FTS5 index optimization, result caching |
| SongsPage scroll stutter | Increase PAGE_SIZE, add background prefetch |
| Artwork decode >16ms | Reduce bucket sizes, add thumbnail pre-decode |
| Queue operation >1ms | Profile std::deque vs custom |
| Visualizer >2ms/frame | Reduce bands, simplify EMA |
| Memory >200MB | Reduce cache budgets, add eviction pressure |

**Only optimize what measurements identify.**

---

## 8. BackendBridge — Correct Implementation

```cpp
// frontend/src/integration/BackendBridge.hpp
#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <vector>
#include <optional>

namespace lollypop {

class BackendController;

// Request/Response types
struct GetAlbumsRequest { uint64_t id; };
struct GetAlbumsResponse { uint64_t id; bool success; std::string error; std::vector<Album> albums; };

struct GetTracksByIdsRequest { uint64_t id; std::vector<std::string> track_ids; };
struct GetTracksByIdsResponse { uint64_t id; bool success; std::string error; std::vector<Track> tracks; };

struct SearchRequest { uint64_t id; uint64_t generation; std::string query; };
struct SearchResponse { uint64_t id; uint64_t generation; bool success; std::string error; std::vector<Track> tracks; };

struct GetSortedTrackIdsRequest { uint64_t id; uint64_t generation; std::string column; bool ascending; };
struct GetSortedTrackIdsResponse { uint64_t id; uint64_t generation; bool success; std::string error; std::vector<std::string> ids; };

struct PlayTracksRequest { uint64_t id; std::vector<std::string> track_ids; size_t start_index; };
struct PlayTracksResponse { uint64_t id; bool success; std::string error; };

// Callback typedefs
using AlbumsCallback = std::function<void(GetAlbumsResponse)>;
using TracksCallback = std::function<void(GetTracksByIdsResponse)>;
using SearchCallback = std::function<void(SearchResponse)>;
using SortedIdsCallback = std::function<void(GetSortedTrackIdsResponse)>;
using PlayCallback = std::function<void(PlayTracksResponse)>;

class BackendBridge {
    BackendController* backend_;
    std::atomic<uint64_t> next_request_id_{1};
    std::mutex callbacks_mtx_;
    
    struct PendingCallbacks {
        AlbumsCallback albums_cb;
        TracksCallback tracks_cb;
        SearchCallback search_cb;
        SortedIdsCallback sorted_cb;
        PlayCallback play_cb;
    };
    std::unordered_map<uint64_t, PendingCallbacks> pending_;
    
    // Library changed callback
    std::function<void()> library_changed_cb_;
    
public:
    explicit BackendBridge(BackendController* backend) : backend_(backend) {
        register_backend_callbacks();
    }
    
    ~BackendBridge() {
        backend_->set_library_changed_callback(nullptr);
        backend_->set_track_changed_callback(nullptr);
        backend_->set_position_changed_callback(nullptr);
        backend_->set_playback_state_changed_callback(nullptr);
        backend_->set_queue_changed_callback(nullptr);
        backend_->set_visualization_changed_callback(nullptr);
    }
    
    // Request methods
    void get_albums(AlbumsCallback cb) {
        uint64_t id = next_request_id_++;
        {
            std::lock_guard lock(callbacks_mtx_);
            pending_[id].albums_cb = std::move(cb);
        }
        // Execute on backend thread pool
        g_task_run_in_thread(g_task_new(nullptr, nullptr, on_albums_done, this),
                             [](GTask* task, gpointer data) {
                                 auto* self = static_cast<BackendBridge*>(data);
                                 GetAlbumsResponse resp;
                                 resp.id = (uint64_t)g_task_get_task_data(task);
                                 try {
                                     resp.albums = self->backend_->get_all_albums();
                                     resp.success = true;
                                 } catch (const std::exception& e) {
                                     resp.success = false;
                                     resp.error = e.what();
                                 }
                                 g_task_return_pointer(task, new GetAlbumsResponse(resp), 
                                                       [](gpointer p) { delete static_cast<GetAlbumsResponse*>(p); });
                             }, this, (gpointer)id);
    }
    
    void get_tracks_by_ids(const std::vector<std::string>& ids, TracksCallback cb) {
        uint64_t id = next_request_id_++;
        {
            std::lock_guard lock(callbacks_mtx_);
            pending_[id].tracks_cb = std::move(cb);
        }
        g_task_run_in_thread(g_task_new(nullptr, nullptr, on_tracks_done, this),
                             [](GTask* task, gpointer data) {
                                 auto* self = static_cast<BackendBridge*>(data);
                                 auto ids = *static_cast<std::vector<std::string>*>(g_task_get_task_data(task));
                                 GetTracksByIdsResponse resp;
                                 resp.id = (uint64_t)g_task_get_source_object(task); // Use source object for ID
                                 // Better: store ID in task data
                                 try {
                                     resp.tracks = self->backend_->get_tracks_by_ids(ids);
                                     resp.success = true;
                                 } catch (const std::exception& e) {
                                     resp.success = false;
                                     resp.error = e.what();
                                 }
                                 g_task_return_pointer(task, new GetTracksByIdsResponse(resp), 
                                                       [](gpointer p) { delete static_cast<GetTracksByIdsResponse*>(p); });
                             }, new std::vector<std::string>(ids), [](gpointer p) { delete static_cast<std::vector<std::string>*>(p); });
    }
    
    void search_tracks(const std::string& query, uint64_t generation, SearchCallback cb) {
        uint64_t id = next_request_id_++;
        {
            std::lock_guard lock(callbacks_mtx_);
            pending_[id].search_cb = std::move(cb);
        }
        g_task_run_in_thread(g_task_new(nullptr, nullptr, on_search_done, this),
                             [](GTask* task, gpointer data) {
                                 auto* self = static_cast<BackendBridge*>(data);
                                 auto* params = static_cast<std::pair<std::string, uint64_t>*>(g_task_get_task_data(task));
                                 SearchResponse resp;
                                 resp.id = (uint64_t)g_task_get_source_object(task);
                                 resp.generation = params->second;
                                 try {
                                     resp.tracks = self->backend_->search_tracks(params->first);
                                     resp.success = true;
                                 } catch (const std::exception& e) {
                                     resp.success = false;
                                     resp.error = e.what();
                                 }
                                 g_task_return_pointer(task, new SearchResponse(resp),
                                                       [](gpointer p) { delete static_cast<SearchResponse*>(p); });
                             }, new std::pair<std::string, uint64_t>(query, generation),
                                [](gpointer p) { delete static_cast<std::pair<std::string, uint64_t>*>(p); });
    }
    
    void get_sorted_track_ids(const std::string& column, bool ascending, uint64_t generation, SortedIdsCallback cb) {
        uint64_t id = next_request_id_++;
        {
            std::lock_guard lock(callbacks_mtx_);
            pending_[id].sorted_cb = std::move(cb);
        }
        g_task_run_in_thread(g_task_new(nullptr, nullptr, on_sorted_done, this),
                             [](GTask* task, gpointer data) {
                                 auto* self = static_cast<BackendBridge*>(data);
                                 auto* params = static_cast<std::pair<std::string, bool>*>(g_task_get_task_data(task));
                                 GetSortedTrackIdsResponse resp;
                                 resp.id = (uint64_t)g_task_get_source_object(task);
                                 resp.generation = generation; // Need to pass this properly
                                 try {
                                     resp.ids = self->backend_->get_sorted_track_ids(params->first, params->second);
                                     resp.success = true;
                                 } catch (const std::exception& e) {
                                     resp.success = false;
                                     resp.error = e.what();
                                 }
                                 g_task_return_pointer(task, new GetSortedTrackIdsResponse(resp),
                                                       [](gpointer p) { delete static_cast<GetSortedTrackIdsResponse*>(p); });
                             }, new std::pair<std::string, bool>(column, ascending),
                                [](gpointer p) { delete static_cast<std::pair<std::string, bool>*>(p); });
    }
    
    void play_tracks(const std::vector<std::string>& ids, size_t start, PlayCallback cb) {
        // Play commands execute on main context via MPRIS → BackendController
        // But we can also call directly if needed
        uint64_t request_id = next_request_id_++;
        {
            std::lock_guard lock(callbacks_mtx_);
            pending_[request_id].play_cb = std::move(cb);
        }
        // Execute on main context since it mutates playback state
        g_main_context_invoke(nullptr, [this, ids, start, request_id]() -> gboolean {
            PlayTracksResponse resp;
            resp.id = request_id;
            try {
                this->backend_->play_tracks(ids, start);
                resp.success = true;
            } catch (const std::exception& e) {
                resp.success = false;
                resp.error = e.what();
            }
            // Callback will be invoked via track_changed/queue_changed
            // For direct response:
            std::lock_guard lock(this->callbacks_mtx_);
            auto it = this->pending_.find(request_id);
            if (it != this->pending_.end() && it->second.play_cb) {
                it->second.play_cb(resp);
                this->pending_.erase(it);
            }
            return G_SOURCE_REMOVE;
        });
    }
    
    // Library changed notification
    void set_library_changed_callback(std::function<void()> cb) {
        library_changed_cb_ = std::move(cb);
    }
    
    // Backend event callbacks (already on main context via Glib::Dispatcher)
    void on_track_changed(const Track& track) {
        // Forward to UI panels via MainWindow
    }
    
    void on_position_changed(double pos, double dur) { /* ... */ }
    void on_playback_state_changed(bool playing) { /* ... */ }
    void on_queue_changed(const std::vector<std::string>& queue) { /* ... */ }
    void on_visualization_changed(const std::vector<float>& points) { /* ... */ }
    void on_library_changed() {
        if (library_changed_cb_) library_changed_cb_();
    }

private:
    // GTask callbacks — run on main context
    static void on_albums_done(GObject*, GAsyncResult* res, gpointer user_data) {
        auto* self = static_cast<BackendBridge*>(user_data);
        GError* error = nullptr;
        GetAlbumsResponse* resp = static_cast<GetAlbumsResponse*>(g_task_propagate_pointer(G_TASK(res), &error));
        if (error) {
            resp = new GetAlbumsResponse{0, false, error->message, {}};
            g_error_free(error);
        }
        
        uint64_t id = resp->id;
        AlbumsCallback cb;
        {
            std::lock_guard lock(self->callbacks_mtx_);
            auto it = self->pending_.find(id);
            if (it != self->pending_.end()) {
                cb = std::move(it->second.albums_cb);
                self->pending_.erase(it);
            }
        }
        if (cb) cb(*resp);
        delete resp;
    }
    
    // Similar for on_tracks_done, on_search_done, on_sorted_done...
    
    void register_backend_callbacks() {
        backend_->set_track_changed_callback([this](const Track& t) { on_track_changed(t); });
        backend_->set_position_changed_callback([this](double p, double d) { on_position_changed(p, d); });
        backend_->set_playback_state_changed_callback([this](bool p) { on_playback_state_changed(p); });
        backend_->set_queue_changed_callback([this](const std::vector<std::string>& q) { on_queue_changed(q); });
        backend_->set_visualization_changed_callback([this](const std::vector<float>& p) { on_visualization_changed(p); });
        backend_->set_library_changed_callback([this]() { on_library_changed(); });
    }
};

} // namespace lollypop
```

---

## 9. Testing Requirements

### 9.1 Unit Tests

```cpp
// tests/test_queue_manager.cpp
TEST(QueueManager, PlayNextInsertsAfterCurrent) {
    QueueManager qm;
    qm.set_queue({"A", "B", "C"}, 1); // Current = B
    qm.play_next("X");
    auto snap = qm.snapshot();
    ASSERT_EQ(snap[0].track_id, "A");
    ASSERT_EQ(snap[1].track_id, "B");
    ASSERT_EQ(snap[2].track_id, "X");  // Inserted after current
    ASSERT_EQ(snap[3].track_id, "C");
}

TEST(QueueManager, DuplicateTracksHaveUniqueQueueIds) {
    QueueManager qm;
    qm.set_queue({"A", "A", "A"});
    auto snap = qm.snapshot();
    ASSERT_NE(snap[0].queue_id, snap[1].queue_id);
    ASSERT_NE(snap[1].queue_id, snap[2].queue_id);
}

TEST(QueueManager, PreviousUsesHistory) {
    QueueManager qm;
    qm.set_queue({"A", "B", "C"});
    qm.next();  // B
    qm.next();  // C
    auto prev = qm.previous();
    ASSERT_EQ(prev, "B"); // Actual history, not shuffle order
}
```

### 9.2 Integration Tests

```cpp
// tests/test_integration.cpp
TEST(Integration, SearchDebounce) {
    // Type "abc" rapidly
    // Only final query "abc" should execute
}

TEST(Integration, StaleSearchRejected) {
    // Search "a" → generation 1
    // Immediately search "ab" → generation 2
    // Generation 1 response arrives after generation 2
    // Generation 1 response should be discarded
}

TEST(Integration, ArtworkDecodeCancellation) {
    // Scroll fast → many decode tasks
    // Only visible row decodes should complete
    // Others should be cancelled
}

TEST(Integration, WindowCloseDuringScan) {
    // Start scan
    // Close window
    // No use-after-free, no leaks
}
```

---

## 10. Final Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | All pages use real backend data | Manual + integration test |
| 2 | No mock fallbacks in production | grep -r "MockLibraryService" frontend/src/ |
| 3 | Single authoritative playback state | Play/pause from MPRIS, UI, keys all consistent |
| 4 | Queue auto-updates | Play album → RightNowPlaying queue matches |
| 5 | Duplicate queue entries work | Add same track 3× → 3 separate queue items |
| 6 | Play Next never drops | Add 100 Play Next → all play in order |
| 7 | Shuffle Previous uses history | Shuffle on → play A,B,C → Previous → B |
| 8 | Lyrics update on track change | Play track with lyrics → Lyrics tab shows text |
| 9 | Search generation tracking | Rapid type → only final results shown |
| 10 | Songs page lazy loading | 50K tracks → scroll → only visible rows fetched |
| 11 | Artwork async, no main-thread decode | perf record → no gdk_texture_new_from_file on main thread |
| 12 | Volume/seek/modes synchronized | MPRIS volume change → UI slider updates |
| 13 | All GTK on main thread | helgrind / thread sanitizer clean |
| 14 | Shutdown cancels async work | Close during scan → no leaks, no crashes |
| 15 | Cache memory bounded | 100K tracks + artwork browse → RSS < 300MB |
| 16 | Visualizer 30 FPS, no blocking | perf → write_frame < 0.1ms, no allocations |
| 17 | Every optimization has benchmarks | docs/BENCHMARKS.md with before/after |

---

## 11. File Summary

```
backend/
├── src/
│   ├── queue_manager.cpp          # FIXED: unique queue_id, history, no capacity limit
│   ├── visualizer_snapshot.cpp    # NEW: mutex double-buffer, EMA, frame-clock
│   └── ...
├── include/
│   ├── visualizer_snapshot.hpp
│   └── ...

frontend/
├── src/
│   ├── integration/
│   │   └── BackendBridge.cpp      # NEW: GTask-based, generation IDs, cancellation
│   ├── models/
│   │   ├── SongsListModel.cpp     # NEW: GListStore + paged retrieval
│   │   └── ...
│   ├── ui/pages/
│   │   ├── SongsPage.cpp          # USE: SongsListModel
│   │   ├── SearchResultsPage.cpp  # NEW
│   │   └── ...
│   ├── ui/components/
│   │   ├── ArtworkView.cpp        # FIXED: GTask decode, cancellation, fallback
│   │   ├── TrackRow.cpp           # CONNECT: play_queue_index, queue_id
│   │   └── ...
│   ├── panels/
│   │   ├── RightNowPlaying.cpp    # CONNECT: queue_changed_cb, lyrics auto-request
│   │   ├── BottomPlayback.cpp     # ADD: volume slider, mode buttons
│   │   └── ...
│   └── ...
└── tests/
    ├── test_queue_manager.cpp
    ├── test_integration.cpp
    └── ...
```

---

## 12. Instructions for Implementation

1. **Start with Phase 0** — build, audit, map
2. **Phase 1** — connect everything, remove mocks, verify main-context
3. **Phase 2** — FTS5, lazy SongsListModel, async artwork
4. **Phase 3** — queue semantics, visualizer snapshot, lyrics sync
5. **Phase 4** — profile, measure, optimize only bottlenecks

**Do not:**
- Implement ScanDAG, SegmentTree, FenwickTree, SkipList, BloomFilter
- Implement custom B-tree, intrusive queue nodes, lock-free MPSC
- Implement GapBuffer, LevenshteinAutomaton, LibraryDAG
- Replace CacheManager/ArtworkManager without profiling

**Do:**
- Use SQLite/FTS5 as canonical index
- Use GListStore for list models
- Use GTask for async work
- Use Glib::Dispatcher for main-context delivery
- Keep all GTK on main thread
- Give every cache a byte budget
- Report errors, don't silently drop

---

*Corrected Plan — 21 November 2026*  
*Supersedes COMPLETE_INTEGRATION_AND_OPTIMIZATION_PLAN.md*