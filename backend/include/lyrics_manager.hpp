/**
 * Lollypop C++ - LyricsManager
 *
 * Lyrics retrieval, parsing, and playback synchronization.
 *
 * Design (plan.md Section 13.4):
 *
 *   - Read embedded lyrics from audio files.
 *   - Retrieve external lyrics via a caller-supplied fetch function
 *     (runs on a worker thread; never blocks playback).
 *   - Parse timed lyrics (LRC format) and untimed lyrics.
 *   - Synchronize lyrics with playback position (current-line lookup).
 *   - Cache lyrics via CacheManager; persist via StorageManager.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <optional>
#include <cstdint>

#include "types.hpp"

namespace lollypop {

class StorageManager;
class CacheManager;

// A single line of timed lyrics with its display timestamp.
struct LyricLine {
    double start_time_seconds = 0.0;  // when this line should appear
    double end_time_seconds = 0.0;    // when this line should disappear (0 = unknown)
    std::string text;
};

class LyricsManager {
public:
    using LyricsReadyCallback = std::function<void(const std::string& track_id,
                                                    const Lyrics& lyrics)>;
    // External fetch: track_id, artist, title -> lyrics text (empty if not found).
    using FetchFn = std::function<std::string(
        const std::string& track_id,
        const std::string& artist,
        const std::string& title)>;

    LyricsManager();
    ~LyricsManager();

    LyricsManager(const LyricsManager&) = delete;
    LyricsManager& operator=(const LyricsManager&) = delete;

    // Configure storage (persistence) and cache (fast lookup); starts the
    // worker thread for non-blocking external retrieval.
    bool initialize(StorageManager* storage,
                    CacheManager* cache,
                    const std::string& /*cache_dir*/);
    void shutdown();

    // Get lyrics for a track. Returns cached/stored lyrics if available,
    // std::nullopt otherwise. Reads from cache/storage (cache access updates
    // LRU ordering, so this is not const).
    std::optional<Lyrics> get_lyrics(const std::string& track_id);

    // Store/set lyrics. Persisted via StorageManager and cached via CacheManager.
    bool set_lyrics(const std::string& track_id, const std::string& text,
                    bool synced = false, const std::string& source = {});

    // Request lyrics for a track. If cached/stored, the callback fires
    // immediately (synchronously). Otherwise the lyrics are fetched externally
    // on the worker thread using fetch_fn, and the callback fires on the
    // worker thread when complete. The caller is responsible for dispatching
    // to the GTK main context if it touches UI.
    bool request_lyrics(const std::string& track_id,
                        const std::string& artist,
                        const std::string& title,
                        LyricsReadyCallback cb);

    // Parse LRC-formatted timed lyrics into LyricLines.
    // Supported timestamp formats: [mm:ss.xx], [mm:ss.xxx].
    static std::vector<LyricLine> parse_lrc(const std::string& lrc_text);

    // Get the lyric line active at the given playback position.
    // Returns the last line whose start_time <= position_seconds, or nullopt.
    std::optional<LyricLine> current_line(
        const std::vector<LyricLine>& lines,
        double position_seconds) const;

    // Return the display text for lyrics: synced text if available, else raw text.
    static std::string display_text(const Lyrics& lyrics);

    void set_fetch_fn(FetchFn fn);

private:
    struct QueuedRequest {
        std::string track_id;
        std::string artist;
        std::string title;
        LyricsReadyCallback callback;
    };

    StorageManager* storage_ = nullptr;
    CacheManager* cache_ = nullptr;

    std::queue<QueuedRequest> queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    FetchFn fetch_fn_;

    void worker_loop();
    void process_request(const QueuedRequest& req);
    std::string cache_key(const std::string& track_id) const;
    bool load_from_storage(const std::string& track_id, Lyrics& out) const;
    bool persist_to_storage(const Lyrics& lyrics) const;
};

}  // namespace lollypop
