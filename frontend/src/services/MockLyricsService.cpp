/**
 * Lollypop C++ - MockLyricsService Implementation
 *
 * Returns empty results (no lyrics available) so the UI can display the
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

bool MockLyricsService::has_synced_lyrics(const std::string& /*track_id*/) {
    return false;
}

std::string MockLyricsService::get_synced_lyrics(const std::string& /*track_id*/) {
    return "";
}

std::string MockLyricsService::get_plain_lyrics(const std::string& /*track_id*/) {
    return "";
}

void MockLyricsService::request_synced_lyrics(
    const std::string& track_id,
    SyncedLyricsReadyCallback cb) {
    // Deliver asynchronously on main context (mock: immediate empty)
    if (cb) {
        cb(track_id, "");
    }
}

void MockLyricsService::request_plain_lyrics(
    const std::string& track_id,
    LyricsReadyCallback cb) {
    // Deliver asynchronously on main context (mock: immediate empty)
    if (cb) {
        cb(track_id, "");
    }
}

bool MockLyricsService::save_synced_lyrics(const std::string& /*track_id*/, const std::string& /*lrc_text*/) {
    return false;
}

bool MockLyricsService::save_plain_lyrics(const std::string& /*track_id*/, const std::string& /*lyrics*/) {
    return false;
}

std::string MockLyricsService::get_embedded_synced_lyrics(const std::string& /*track_id*/) {
    return "";
}

std::string MockLyricsService::get_embedded_plain_lyrics(const std::string& /*track_id*/) {
    return "";
}

std::vector<std::string> MockLyricsService::get_lyrics_providers() {
    return {"mock"};
}

} // namespace lollypop