/**
 * Lollypop C++ - ILyricsService Interface
 * Lyrics query and management interface
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "types.hpp"

namespace lollypop {

class ILyricsService {
public:
    virtual ~ILyricsService() = default;

    // --- Synced Lyrics (LRC format) ---
    virtual bool has_synced_lyrics(const std::string& track_id) = 0;
    virtual std::string get_synced_lyrics(const std::string& track_id) = 0;

    // --- Plain Lyrics ---
    virtual std::string get_plain_lyrics(const std::string& track_id) = 0;

    // --- Callbacks ---
    using LyricsReadyCallback = std::function<void(const std::string& track_id, const std::string& lyrics)>;
    using SyncedLyricsReadyCallback = std::function<void(const std::string& track_id, const std::string& lrc_text)>;

    // Request lyrics (async, may fetch from network)
    virtual void request_synced_lyrics(const std::string& track_id, SyncedLyricsReadyCallback cb) = 0;
    virtual void request_plain_lyrics(const std::string& track_id, LyricsReadyCallback cb) = 0;

    // Save lyrics to local storage
    virtual bool save_synced_lyrics(const std::string& track_id, const std::string& lrc_text) = 0;
    virtual bool save_plain_lyrics(const std::string& track_id, const std::string& lyrics) = 0;

    // Embedded lyrics from audio file tags
    virtual std::string get_embedded_synced_lyrics(const std::string& track_id) = 0;
    virtual std::string get_embedded_plain_lyrics(const std::string& track_id) = 0;

    // Lyrics search providers
    virtual std::vector<std::string> get_lyrics_providers() = 0;
};

} // namespace lollypop