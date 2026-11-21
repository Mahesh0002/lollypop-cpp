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

    std::optional<Lyrics> get_lyrics(const std::string& track_id) override;
    void request_lyrics(
        const std::string& track_id,
        LyricsCallback callback) override;
    void set_lyrics(
        const std::string& track_id,
        const std::string& text,
        bool synced = false,
        const std::string& source = {}) override;

private:
    LyricsCallback lyrics_callback_;
};

} // namespace lollypop
