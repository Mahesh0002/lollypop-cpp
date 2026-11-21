/**
 * Lollypop C++ - MockLibraryService
 *
 * Mock implementation of ILibraryService for Phase 1 development. Returns
 * empty or development-mock data. Clearly marked as mock so it can be
 * replaced by the real service without rewriting UI components.
 */

#pragma once

#include "services/ILibraryService.hpp"

#include <string>
#include <vector>

namespace lollypop {

class MockLibraryService : public ILibraryService {
public:
    MockLibraryService();
    ~MockLibraryService() override;

    // Albums
    std::vector<Album> get_all_albums() override;
    std::vector<Album> get_albums_by_artist(const std::string& artist_id) override;
    Album get_album(const std::string& album_id) override;
    std::vector<Album> get_favourite_albums() override;

    // Artists
    std::vector<Artist> get_all_artists() override;
    Artist get_artist(const std::string& artist_id) override;
    std::vector<Track> get_tracks_by_artist(const std::string& artist_name) override;

    // Playlists
    std::vector<Playlist> get_all_playlists() override;
    Playlist get_playlist(const std::string& playlist_id) override;
    std::vector<Track> get_playlist_tracks(const std::string& playlist_id) override;

    // Genres
    std::vector<std::string> get_all_genres() override;
    std::vector<Track> get_tracks_by_genre(const std::string& genre) override;

    // Tracks
    std::vector<Track> get_all_tracks() override;
    std::vector<Track> get_recently_added(int limit = 20) override;
    std::vector<Track> get_recently_played(int limit = 20) override;
    std::vector<Track> get_liked_tracks() override;
    Track get_track(const std::string& track_id) override;

    // Search
    std::vector<Track> search_tracks(const std::string& query) override;

    // Favourites / Likes
    void toggle_like(const std::string& track_id) override;
    void toggle_favorite_album(const std::string& album_id) override;
    bool is_track_liked(const std::string& track_id) override;
    bool is_album_favorited(const std::string& album_id) override;

    // Database-backed sorting
    std::vector<std::string> get_sorted_track_ids(
        const std::string& sortColumn,
        bool ascending) override;
    std::vector<Track> get_tracks_by_ids(
        const std::vector<std::string>& trackIds) override;

    // Artwork references
    std::string get_artwork_path_for_track(const std::string& track_id) override;
    std::string get_artwork_path_for_album(const std::string& album_id) override;
    std::string get_artwork_path_for_artist(const std::string& artist_id) override;

    // Callbacks
    void set_library_changed_callback(LibraryChangedCallback cb) override;

private:
    LibraryChangedCallback library_changed_cb_;
};

} // namespace lollypop
