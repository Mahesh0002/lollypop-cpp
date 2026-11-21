/**
 * Lollypop C++ - LyricsManager Implementation
 */

#include "lyrics_manager.hpp"

#include "manager.hpp"
#include "cache_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace lollypop {

namespace {

// Parse a single LRC timestamp like [mm:ss.xx] or [mm:ss.xxx] into seconds.
// Returns nullopt if the string is not a valid timestamp.
std::optional<double> parse_lrc_timestamp(const std::string& ts) {
    // Expected format: mm:ss.ms or mm:ss.mmm
    if (ts.size() < 5) return std::nullopt;

    int minutes = 0;
    int seconds = 0;
    double fraction = 0.0;

    std::istringstream iss(ts);
    char colon = 0;
    char dot = 0;
    if (!(iss >> minutes >> colon >> seconds >> dot >> fraction) ||
        colon != ':' || dot != '.') {
        return std::nullopt;
    }

    // Determine the number of fractional digits to scale correctly.
    std::string frac_part;
    std::size_t dot_pos = ts.find('.');
    if (dot_pos != std::string::npos) {
        frac_part = ts.substr(dot_pos + 1);
    }
    int frac_digits = static_cast<int>(frac_part.size());
    double scale = 1.0;
    for (int i = 0; i < frac_digits; ++i) scale *= 10.0;

    return static_cast<double>(minutes) * 60.0 +
           static_cast<double>(seconds) +
           fraction / scale;
}

// Extract timestamps and text from one LRC line.
// A line may contain multiple timestamps: [mm:ss.xx][mm:ss.xx]text
std::vector<std::pair<double, std::string>> parse_lrc_line(
    const std::string& raw_line) {
    std::vector<std::pair<double, std::string>> result;

    std::size_t pos = 0;
    while (pos < raw_line.size()) {
        if (raw_line[pos] != '[') break;
        std::size_t end = raw_line.find(']', pos + 1);
        if (end == std::string::npos) break;

        std::string ts = raw_line.substr(pos + 1, end - pos - 1);
        auto opt_seconds = parse_lrc_timestamp(ts);
        if (opt_seconds) {
            // Remember where the text after this timestamp begins.
            // We'll collect text after the last timestamp.
            pos = end + 1;
        } else {
            break;
        }
    }

    // The text is everything after the last timestamp.
    std::string text = raw_line.substr(pos);
    // Trim leading whitespace.
    std::size_t first = text.find_first_not_of(" \t");
    if (first != std::string::npos) {
        text = text.substr(first);
    } else {
        text.clear();
    }

    // Re-parse to associate each timestamp with the text.
    pos = 0;
    while (pos < raw_line.size()) {
        if (raw_line[pos] != '[') break;
        std::size_t end = raw_line.find(']', pos + 1);
        if (end == std::string::npos) break;
        std::string ts = raw_line.substr(pos + 1, end - pos - 1);
        auto opt_seconds = parse_lrc_timestamp(ts);
        if (opt_seconds) {
            result.emplace_back(*opt_seconds, text);
            pos = end + 1;
        } else {
            break;
        }
    }

    return result;
}

}  // namespace

std::vector<LyricLine> LyricsManager::parse_lrc(const std::string& lrc_text) {
    std::vector<LyricLine> lines;

    std::istringstream iss(lrc_text);
    std::string raw_line;
    while (std::getline(iss, raw_line)) {
        // Strip trailing carriage return.
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }

        auto parsed = parse_lrc_line(raw_line);
        for (const auto& [start_time, text] : parsed) {
            if (text.empty()) continue;  // skip ID-tag-only lines like [ti:...]
            LyricLine line;
            line.start_time_seconds = start_time;
            line.text = text;
            lines.push_back(std::move(line));
        }
    }

    // Sort by start time for correct current-line lookup.
    std::sort(lines.begin(), lines.end(),
              [](const LyricLine& a, const LyricLine& b) {
                  return a.start_time_seconds < b.start_time_seconds;
              });

    return lines;
}

std::optional<LyricLine> LyricsManager::current_line(
    const std::vector<LyricLine>& lines,
    double position_seconds) const {
    std::optional<LyricLine> current;
    for (const auto& line : lines) {
        if (line.start_time_seconds <= position_seconds) {
            current = line;
        } else {
            break;
        }
    }
    return current;
}

std::string LyricsManager::display_text(const Lyrics& lyrics) {
    if (lyrics.synced && !lyrics.text.empty()) {
        return lyrics.text;
    }
    return lyrics.text;
}

std::string LyricsManager::cache_key(const std::string& track_id) const {
    return "lyrics:" + track_id;
}

bool LyricsManager::load_from_storage(const std::string& track_id,
                                       Lyrics& out) const {
    if (!storage_) return false;
    auto opt = storage_->get_lyrics(track_id);
    if (!opt) return false;
    out = *opt;
    return true;
}

bool LyricsManager::persist_to_storage(const Lyrics& lyrics) const {
    if (!storage_) return false;
    return storage_->set_lyrics(lyrics.track_id, lyrics.text, lyrics.synced,
                                lyrics.source);
}

LyricsManager::LyricsManager() = default;

LyricsManager::~LyricsManager() {
    shutdown();
}

bool LyricsManager::initialize(StorageManager* storage,
                                CacheManager* cache,
                                const std::string& /*cache_dir*/) {
    storage_ = storage;
    cache_ = cache;

    running_ = true;
    worker_thread_ = std::thread(&LyricsManager::worker_loop, this);
    return true;
}

void LyricsManager::shutdown() {
    if (!running_) return;
    running_ = false;
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) worker_thread_.join();
}

void LyricsManager::set_fetch_fn(FetchFn fn) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    fetch_fn_ = std::move(fn);
}

std::optional<Lyrics> LyricsManager::get_lyrics(const std::string& track_id) {
    // Check cache first.
    if (cache_) {
        CacheEntry entry;
        if (cache_->get(cache_key(track_id), entry)) {
            Lyrics lyrics;
            lyrics.track_id = track_id;
            // Read the actual text from the cache file. Do not leave the
            // cache path as the text if the file cannot be read.
            std::ifstream in(entry.file_path);
            if (in) {
                std::stringstream buffer;
                buffer << in.rdbuf();
                lyrics.text = buffer.str();
            }
            lyrics.synced = entry.synced;
            lyrics.source = entry.source;
            return lyrics;
        }
    }

    // Fall back to storage.
    Lyrics lyrics;
    if (load_from_storage(track_id, lyrics)) {
        return lyrics;
    }

    return std::nullopt;
}

bool LyricsManager::set_lyrics(const std::string& track_id,
                                const std::string& text,
                                bool synced,
                                const std::string& source) {
    Lyrics lyrics;
    lyrics.track_id = track_id;
    lyrics.text = text;
    lyrics.synced = synced;
    lyrics.source = source;
    lyrics.updated_at = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    // Persist via StorageManager.
    if (!persist_to_storage(lyrics)) return false;

    // Cache via CacheManager: write text to a cache file, then put it.
    if (cache_) {
        std::string cache_path = cache_->cache_path_for_key(cache_key(track_id));
        std::ofstream out(cache_path);
        if (out) {
            out << text;
            out.close();
            std::size_t byte_size = 0;
            std::error_code ec;
            byte_size =
                static_cast<std::size_t>(
                    std::filesystem::file_size(cache_path, ec));
            cache_->put(cache_key(track_id), cache_path, byte_size,
                        /*ttl_seconds=*/0, lyrics.synced, lyrics.source);
        }
    }

    return true;
}

bool LyricsManager::request_lyrics(const std::string& track_id,
                                    const std::string& artist,
                                    const std::string& title,
                                    LyricsReadyCallback cb) {
    // Check cache/storage first.
    std::optional<Lyrics> cached = get_lyrics(track_id);
    if (cached) {
        if (cb) cb(track_id, *cached);
        return true;
    }

    QueuedRequest req;
    req.track_id = track_id;
    req.artist = artist;
    req.title = title;
    req.callback = std::move(cb);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(std::move(req));
    }
    queue_cv_.notify_one();
    return true;
}

void LyricsManager::worker_loop() {
    while (running_) {
        QueuedRequest req;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !queue_.empty();
            });
            if (!running_ && queue_.empty()) return;
            if (queue_.empty()) continue;
            req = std::move(queue_.front());
            queue_.pop();
        }

        Lyrics lyrics;
        lyrics.track_id = req.track_id;
        lyrics.updated_at = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        if (fetch_fn_) {
            std::string text = fetch_fn_(req.track_id, req.artist, req.title);
            if (!text.empty()) {
                // Try to parse as LRC timed lyrics.
                auto parsed = parse_lrc(text);
                if (!parsed.empty()) {
                    // Store the raw LRC text; the UI can parse it via
                    // parse_lrc() and use current_line() for sync.
                    lyrics.text = text;
                    lyrics.synced = true;
                    lyrics.source = "external";
                } else {
                    lyrics.text = text;
                    lyrics.synced = false;
                    lyrics.source = "external";
                }
            }
        }

        // Persist + cache the fetched lyrics.
        if (!lyrics.text.empty()) {
            set_lyrics(req.track_id, lyrics.text, lyrics.synced,
                       lyrics.source);
        }

        if (req.callback) {
            try {
                req.callback(req.track_id, lyrics);
            } catch (const std::exception& e) {
                std::cerr << "LyricsManager: callback threw: " << e.what()
                          << std::endl;
            } catch (...) {
                std::cerr << "LyricsManager: callback threw unknown exception"
                          << std::endl;
            }
        }
    }
}

void LyricsManager::process_request(const QueuedRequest& /*req*/) {
    // Kept for interface symmetry; logic lives in worker_loop().
}

}  // namespace lollypop
