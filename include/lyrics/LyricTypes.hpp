/**
 * Lollypop C++ - Synced Lyrics Types
 * Core data structures for LRC parsing and rendering
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace lollypop {

struct LyricWord {
    int start_ms = 0;      // Word start time in milliseconds
    int char_start = 0;    // Character index start in line text
    int char_end = 0;      // Character index end in line text
};

struct SyncedLyricLine {
    int start_ms = 0;                    // Line start time (ms)
    std::string text;                    // Plain text (no tags)
    std::vector<LyricWord> words;        // Word-level timings (optional)
    bool is_gap = false;                 // Instrumental/empty line

    bool empty() const { return text.empty(); }
};

struct SyncedLyrics {
    std::vector<SyncedLyricLine> lines;

    bool valid() const { return !lines.empty(); }
    size_t size() const { return lines.size(); }
    const SyncedLyricLine& operator[](size_t i) const { return lines[i]; }
    SyncedLyricLine& operator[](size_t i) { return lines[i]; }
};

// LRC Parser - converts raw LRC text to SyncedLyrics
class LrcParser {
public:
    static SyncedLyrics parse(const std::string& lrc_text);
    static bool has_synced_lyrics(const std::string& lrc_text);
    static std::string to_plain_lyrics(const std::string& lrc_text);

private:
    static int timestamp_to_ms(const std::string& minutes, const std::string& seconds, const std::string& fraction);
    static std::pair<std::string, std::vector<LyricWord>> split_words(const std::string& raw, int line_ms);
};

} // namespace lollypop