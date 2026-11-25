/**
 * Lollypop C++ - MockLibraryService Implementation
 *
 * All methods return empty/development-mock data. Clearly marked as mock.
 * Replace with real service when BackendController implements ILibraryService.
 */

#include "services/MockLibraryService.hpp"

#include <iostream>

namespace lollypop {

MockLibraryService::MockLibraryService() {
    std::cout << "[MockLibraryService] Created (development mock)" << std::endl;
}

MockLibraryService::~MockLibraryService() = default;

std::vector<Album> MockLibraryService::get_all_albums() {
    return {};
}

std::vector<Album> MockLibraryService::get_albums_by_artist(
    const std::string& /*artist_id*/) {
    return {};
}

Album MockLibraryService::get_album(const std::string& /*album_id*/) {
    return Album{};
}

std::vector<Album> MockLibraryService::get_favourite_albums() {
    return {};
}

std::vector<Artist> MockLibraryService::get_all_artists() {
    return {};
}

Artist MockLibraryService::get_artist(const std::string& /*artist_id*/) {
    return Artist{};
}

std::vector<Track> MockLibraryService::get_tracks_by_artist(
    const std::string& /*artist_name*/) {
    return {};
}

std::vector<Playlist> MockLibraryService::get_all_playlists() {
    return {};
}

Playlist MockLibraryService::get_playlist(const std::string& /*playlist_id*/) {
    return Playlist{};
}

std::vector<Track> MockLibraryService::get_playlist_tracks(
    const std::string& /*playlist_id*/) {
    return {};
}

bool MockLibraryService::create_playlist(const std::string& /*name*/) {
    return false;
}

bool MockLibraryService::rename_playlist(const std::string& /*playlist_id*/,
                                         const std::string& /*name*/) {
    return false;
}

bool MockLibraryService::delete_playlist(const std::string& /*playlist_id*/) {
    return false;
}

bool MockLibraryService::add_tracks_to_playlist(
    const std::string& /*playlist_id*/,
    const std::vector<std::string>& /*track_ids*/) {
    return false;
}

std::vector<std::string> MockLibraryService::get_all_genres() {
    return {};
}

std::vector<Track> MockLibraryService::get_tracks_by_genre(
    const std::string& /*genre*/) {
    return {};
}

std::vector<Track> MockLibraryService::get_all_tracks() {
    return {};
}

std::vector<Track> MockLibraryService::get_recently_added(int /*limit*/) {
    return {};
}

std::vector<Track> MockLibraryService::get_recently_played(int /*limit*/) {
    return {};
}

std::vector<Track> MockLibraryService::get_liked_tracks() {
    return {};
}

Track MockLibraryService::get_track(const std::string& /*track_id*/) {
    return Track{};
}

std::vector<Track> MockLibraryService::search_tracks(
    const std::string& /*query*/) {
    return {};
}

void MockLibraryService::toggle_like(const std::string& /*track_id*/) {
    // Mock: no-op
}

void MockLibraryService::toggle_favorite_album(const std::string& /*album_id*/) {
    // Mock: no-op
}

bool MockLibraryService::is_track_liked(const std::string& /*track_id*/) {
    return false;
}

bool MockLibraryService::is_album_favorited(const std::string& /*album_id*/) {
    return false;
}

std::vector<std::string> MockLibraryService::get_sorted_track_ids(
    const std::string& /*sortColumn*/,
    bool /*ascending*/,
    const std::string& /*genre_filter*/) {
    return {};
}

std::vector<Track> MockLibraryService::get_tracks_by_ids(
    const std::vector<std::string>& /*trackIds*/) {
    return {};
}

std::string MockLibraryService::get_artwork_path_for_track(
    const std::string& /*track_id*/) {
    return "";
}

std::string MockLibraryService::get_artwork_path_for_album(
    const std::string& /*album_id*/) {
    return "";
}

std::string MockLibraryService::get_artwork_path_for_artist(
    const std::string& /*artist_id*/) {
    return "";
}

void MockLibraryService::set_library_changed_callback(
    LibraryChangedCallback /*cb*/) {
    // Mock: no-op
}

void MockLibraryService::add_library_changed_callback(
    LibraryChangedCallback /*cb*/) {
    // Mock: no-op
}

} // namespace lollypop
