/**
 * Lollypop C++ - LRC Parser Implementation
 * Parses LRC format including word-level timestamps
 */

#include "lyrics/LyricTypes.hpp"

#include <regex>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <glib.h>

namespace lollypop {

// Regex patterns matching Python implementation
static const std::regex TIME_TAG(R"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])");
static const std::regex WORD_TAG(R"(<(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?>)");
static const std::regex META_TAG(R"(^\s*\[([A-Za-z][A-Za-z0-9_-]*):([^\]]*)\]\s*$)");

static const std::string GAP_CHARS = "♪♫🎵🎶 \t";

static int timestamp_to_ms(int minutes, int seconds, int fraction) {
    int value = minutes * 60000 + seconds * 1000;
    if (fraction > 0) {
        // Fraction can be 1-3 digits, normalize to milliseconds
        std::string frac_str = std::to_string(fraction);
        while (frac_str.length() < 3) frac_str += "0";
        if (frac_str.length() > 3) frac_str = frac_str.substr(0, 3);
        value += std::stoi(frac_str);
    }
    return value;
}

static int parse_timestamp(const std::smatch& match) {
    int minutes = std::stoi(match[1].str());
    int seconds = std::stoi(match[2].str());
    int fraction = 0;
    if (match.size() > 3 && match[3].matched) {
        fraction = std::stoi(match[3].str());
    }
    return timestamp_to_ms(minutes, seconds, fraction);
}

std::pair<std::string, std::vector<LyricWord>> LrcParser::split_words(const std::string& raw, int line_ms) {
    std::vector<std::smatch> tags;
    std::string::const_iterator search_start(raw.cbegin());
    std::smatch match;

    while (std::regex_search(search_start, raw.cend(), match, WORD_TAG)) {
        tags.push_back(match);
        search_start = match.suffix().first;
    }

    if (tags.empty()) {
        return {raw, {}};
    }

    std::vector<std::string> parts;
    std::vector<LyricWord> words;
    int current_ms = line_ms;
    size_t last_end = 0;
    size_t char_pos = 0;

    for (const auto& tag : tags) {
        std::string segment = raw.substr(last_end, tag.position() - last_end);
        if (!segment.empty()) {
            parts.push_back(segment);
            const size_t length = static_cast<size_t>(
                g_utf8_strlen(segment.c_str(), -1));
            words.push_back({current_ms, static_cast<int>(char_pos),
                             static_cast<int>(char_pos + length)});
            char_pos += length;
        }
        current_ms = parse_timestamp(tag);
        last_end = tag.position() + tag.length();
    }

    std::string tail = raw.substr(last_end);
    if (!tail.empty()) {
        parts.push_back(tail);
        const size_t length = static_cast<size_t>(
            g_utf8_strlen(tail.c_str(), -1));
        words.push_back({current_ms, static_cast<int>(char_pos),
                         static_cast<int>(char_pos + length)});
        char_pos += length;
    }

    std::string plain;
    for (const auto& part : parts) plain += part;

    // Trim leading whitespace and adjust word positions
    size_t leading = plain.length() - plain.find_first_not_of(" \t");
    if (leading > 0 && !words.empty()) {
        plain = plain.substr(leading);
        std::vector<LyricWord> adjusted;
        for (const auto& w : words) {
            int cs = w.char_start - static_cast<int>(leading);
            int ce = w.char_end - static_cast<int>(leading);
            if (ce > 0) {
                adjusted.push_back({w.start_ms, std::max(0, cs), ce});
            }
        }
        words = std::move(adjusted);
    }

    // Ensure monotonic timestamps
    std::vector<LyricWord> monotonic;
    int previous = -1;
    for (auto& w : words) {
        w.start_ms = std::max(w.start_ms, previous);
        previous = w.start_ms;
        monotonic.push_back(w);
    }

    return {plain, monotonic};
}

SyncedLyrics LrcParser::parse(const std::string& lrc_text) {
    SyncedLyrics result;
    if (lrc_text.empty()) return result;

    int offset = 0;
    std::vector<std::tuple<int, std::string, std::vector<LyricWord>>> raw_entries;

    std::istringstream stream(lrc_text);
    std::string raw_line;

    while (std::getline(stream, raw_line)) {
        // Remove trailing \r
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }

        // Check for metadata tags
        std::smatch meta_match;
        if (std::regex_match(raw_line, meta_match, META_TAG)) {
            std::string key = meta_match[1].str();
            std::string value = meta_match[2].str();
            if (key == "offset") {
                try {
                    offset = std::stoi(value);
                } catch (...) {}
            }
            continue;
        }

        // Find all timestamp tags
        std::vector<std::smatch> tags;
        std::string::const_iterator search_start(raw_line.cbegin());
        std::smatch tag_match;
        while (std::regex_search(search_start, raw_line.cend(), tag_match, TIME_TAG)) {
            tags.push_back(tag_match);
            search_start = tag_match.suffix().first;
        }

        if (tags.empty()) continue;

        // Content is after the last timestamp tag
        std::string content = raw_line.substr(tags.back().position() + tags.back().length());
        int first_start = parse_timestamp(tags[0]);
        auto [plain, words] = split_words(content, first_start);

        // Create entry for each timestamp
        for (const auto& tag : tags) {
            int start = parse_timestamp(tag);
            std::vector<LyricWord> entry_words;
            if (!words.empty()) {
                int shift = start - first_start;
                for (const auto& w : words) {
                    entry_words.push_back({std::max(0, w.start_ms + shift), w.char_start, w.char_end});
                }
            }
            raw_entries.emplace_back(start, plain, entry_words);
        }
    }

    // Apply offset and build SyncedLyricLine objects
    std::vector<SyncedLyricLine> entries;
    for (auto& [start, plain, words] : raw_entries) {
        start = std::max(0, start + offset);
        if (!words.empty()) {
            for (auto& w : words) {
                w.start_ms = std::max(0, w.start_ms + offset);
            }
        }
        SyncedLyricLine line;
        line.start_ms = start;
        line.text = plain;
        line.words = std::move(words);
        // Check if gap line (empty or only musical symbols)
        std::string trimmed = line.text;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        line.is_gap = trimmed.empty() ||
                      std::all_of(trimmed.begin(), trimmed.end(),
                                  [](char c) { return GAP_CHARS.find(c) != std::string::npos; });
        entries.push_back(std::move(line));
    }

    // Sort by start time
    std::sort(entries.begin(), entries.end(),
              [](const SyncedLyricLine& a, const SyncedLyricLine& b) { return a.start_ms < b.start_ms; });

    // Merge entries with same timestamp
    for (const auto& entry : entries) {
        if (!result.lines.empty() && result.lines.back().start_ms == entry.start_ms) {
            auto& prev = result.lines.back();
            if (!entry.text.empty()) {
                if (!prev.text.empty()) {
                    prev.text += "\n" + entry.text;
                } else {
                    prev.text = entry.text;
                }
            }
            // Note: word timings are discarded for merged lines (as in Python)
            prev.words.clear();
        } else {
            result.lines.push_back(entry);
        }
    }

    return result;
}

bool LrcParser::has_synced_lyrics(const std::string& lrc_text) {
    return parse(lrc_text).valid();
}

std::string LrcParser::to_plain_lyrics(const std::string& lrc_text) {
    if (lrc_text.empty()) return "";

    std::vector<std::string> lines;
    std::istringstream stream(lrc_text);
    std::string raw_line;

    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') raw_line.pop_back();

        // Skip metadata
        if (std::regex_match(raw_line, META_TAG)) continue;

        // Remove word tags
        std::string line = std::regex_replace(raw_line, WORD_TAG, "");
        // Remove time tags
        line = std::regex_replace(line, TIME_TAG, "");

        // Trim
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        lines.push_back(line);
    }

    // Join and collapse multiple blank lines
    std::string result;
    for (const auto& line : lines) {
        if (!result.empty()) result += "\n";
        result += line;
    }

    // Collapse 3+ newlines to 2
    std::regex multi_nl(R"(\n{3,})");
    result = std::regex_replace(result, multi_nl, "\n\n");

    return result;
}

} // namespace lollypop