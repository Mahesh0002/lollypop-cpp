/**
 * Lollypop C++ - MockLyricsService
 *
 * Mock implementation of ILyricsService for Phase 1 development. Returns
 * std::nullopt (no lyrics available) so the UI can display the
 * appropriate unavailable state.
 */

#pragma once

#include "services/ILyricsService.hpp"

#include <functional>
#include <optional>
#include <string>

namespace lollypop {

class MockLyricsService : public ILyricsService {
public:
    MockLyricsService();
    ~MockLyricsService() override;

    // ILyricsService interface
    bool has_synced_lyrics(const std::string& track_id) override;
    std::string get_synced_lyrics(const std::string& track_id) override;
    std::string get_plain_lyrics(const std::string& track_id) override;
    void request_synced_lyrics(const std::string& track_id, SyncedLyricsReadyCallback cb) override;
    void request_plain_lyrics(const std::string& track_id, LyricsReadyCallback cb) override;
    bool save_synced_lyrics(const std::string& track_id, const std::string& lrc_text) override;
    bool save_plain_lyrics(const std::string& track_id, const std::string& lyrics) override;
    std::string get_embedded_synced_lyrics(const std::string& track_id) override;
    std::string get_embedded_plain_lyrics(const std::string& track_id) override;
    std::vector<std::string> get_lyrics_providers() override;
};

} // namespace lollypop
