/**
 * Lollypop C++ - MockLyricsService Implementation
 *
 * Returns std::nullopt (no lyrics available) so the UI can display the
 * appropriate unavailable state. Replace with real service when
 * BackendController implements ILyricsService.
 */

#include "services/MockLyricsService.hpp"

#include <iostream>

namespace lollypop {

MockLyricsService::MockLyricsService() {
    std::cout << "[MockLyricsService] Created (development mock)" << std::endl;
}

MockLyricsService::~MockLyricsService() = default;

std::optional<Lyrics> MockLyricsService::get_lyrics(
    const std::string& /*track_id*/) {
    return std::nullopt;
}

void MockLyricsService::request_lyrics(
    const std::string& /*track_id*/,
    LyricsCallback callback) {
    // Deliver asynchronously on main context (mock: immediate nullopt)
    if (callback) {
        callback(std::nullopt);
    }
}

void MockLyricsService::set_lyrics(
    const std::string& /*track_id*/,
    const std::string& /*text*/,
    bool /*synced*/,
    const std::string& /*source*/) {
    // Mock: no-op
}

} // namespace lollypop
