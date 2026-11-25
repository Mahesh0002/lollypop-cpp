/**
 * Lollypop C++ - ILibraryService Interface
 *
 * Library query interface. The UI depends on this interface, not on the
 * concrete BackendController or StorageManager. All queries run on
 * worker threads; results are delivered to the GTK main context via
 * callbacks.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "types.hpp"

namespace lollypop {

class ILibraryService {
public:
    virtual ~ILibraryService() = default;

    // --- Albums ---
    virtual std::vector<Album> get_all_albums() = 0;
    virtual std::vector<Album> get_albums_by_artist(const std::string& artist_id) = 0;
    virtual Album get_album(const std::string& album_id) = 0;
    virtual std::vector<Album> get_favourite_albums() = 0;

    // --- Artists ---
    virtual std::vector<Artist> get_all_artists() = 0;
    virtual Artist get_artist(const std::string& artist_id) = 0;
    virtual std::vector<Track> get_tracks_by_artist(const std::string& artist_name) = 0;

    // --- Playlists ---
    virtual std::vector<Playlist> get_all_playlists() = 0;
    virtual Playlist get_playlist(const std::string& playlist_id) = 0;
    virtual std::vector<Track> get_playlist_tracks(const std::string& playlist_id) = 0;
    virtual bool create_playlist(const std::string& name) = 0;
    virtual bool rename_playlist(const std::string& playlist_id,
                                 const std::string& name) = 0;
    virtual bool delete_playlist(const std::string& playlist_id) = 0;
    virtual bool add_tracks_to_playlist(
        const std::string& playlist_id,
        const std::vector<std::string>& track_ids) = 0;

    // --- Genres ---
    virtual std::vector<std::string> get_all_genres() = 0;
    virtual std::vector<Track> get_tracks_by_genre(const std::string& genre) = 0;

    // --- Tracks ---
    virtual std::vector<Track> get_all_tracks() = 0;
    virtual std::vector<Track> get_recently_added(int limit = 20) = 0;
    virtual std::vector<Track> get_recently_played(int limit = 20) = 0;
    virtual std::vector<Track> get_liked_tracks() = 0;
    virtual Track get_track(const std::string& track_id) = 0;

    // --- Search (FTS5 where suitable) ---
    virtual std::vector<Track> search_tracks(const std::string& query) = 0;

    // --- Favourites / Likes ---
    virtual void toggle_like(const std::string& track_id) = 0;
    virtual void toggle_favorite_album(const std::string& album_id) = 0;
    virtual bool is_track_liked(const std::string& track_id) = 0;
    virtual bool is_album_favorited(const std::string& album_id) = 0;

    // --- Database-backed sorting (for Songs page) ---
    /// Returns globally ordered track IDs for the given sort column/direction.
    /// Optional genre filter restricts results to tracks with the given genre.
    virtual std::vector<std::string> get_sorted_track_ids(
        const std::string& sortColumn,
        bool ascending,
        const std::string& genre_filter = "") = 0;

    /// Lazy retrieval of full track details for the given IDs.
    virtual std::vector<Track> get_tracks_by_ids(
        const std::vector<std::string>& trackIds) = 0;

    // --- Artwork references ---
    virtual std::string get_artwork_path_for_track(const std::string& track_id) = 0;
    virtual std::string get_artwork_path_for_album(const std::string& album_id) = 0;
    virtual std::string get_artwork_path_for_artist(const std::string& artist_id) = 0;

    // --- Callbacks ---
    using LibraryChangedCallback = std::function<void()>;
    virtual void set_library_changed_callback(LibraryChangedCallback cb) = 0;
    virtual void add_library_changed_callback(LibraryChangedCallback cb) = 0;
};

} // namespace lollypop
