/**
 * Lollypop C++ - External Providers
 * 
 * Implementations for fetching lyrics and artwork from external services.
 * Uses LRCLIB for lyrics (free, no API key required) and Cover Art Archive / MusicBrainz for artwork.
 */

#pragma once

#include <string>
#include <functional>
#include <optional>
#include <vector>

namespace lollypop {

// Forward declarations
struct Track;
struct Album;
struct Artist;

// ============================================================================
// External Lyrics Provider (LRCLIB - https://lrclib.net/)
// ============================================================================

class LyricsProvider {
public:
    // Fetch lyrics for a track. Returns empty string if not found.
    // Uses LRCLIB API: https://lrclib.net/api
    static std::string fetch_lyrics(const std::string& artist, const std::string& title,
                                     const std::string& album = "",
                                     int duration = 0);

    // Set custom user agent (optional)
    static void set_user_agent(const std::string& ua);

    // Set custom base URL (for testing or alternative instances)
    static void set_base_url(const std::string& url);

private:
    static std::string user_agent_;
    static std::string base_url_;
};

// ============================================================================
// External Artwork Provider (Cover Art Archive / MusicBrainz)
// ============================================================================

class ArtworkProvider {
public:
    struct ArtworkResult {
        std::string url;          // Direct image URL
        std::string mime_type;    // image/jpeg, image/png, etc.
        std::string type;         // "front", "back", "medium", etc.
        int width = 0;
        int height = 0;
    };

    // Fetch album artwork. Returns empty optional if not found.
    // Uses Cover Art Archive: https://coverartarchive.org/
    static std::optional<ArtworkResult> fetch_album_artwork(
        const std::string& musicbrainz_release_id);

    // Search for release by artist/album name, then fetch artwork
    static std::optional<ArtworkResult> fetch_album_artwork_by_name(
        const std::string& artist, const std::string& album);

    // Fetch artist artwork. Uses MusicBrainz + Cover Art Archive.
    static std::optional<ArtworkResult> fetch_artist_artwork(
        const std::string& musicbrainz_artist_id);

    // Search for artist by name, then fetch artwork
    static std::optional<ArtworkResult> fetch_artist_artwork_by_name(
        const std::string& artist);

    // Download artwork from URL to a local file
    // Returns path to downloaded file, or empty string on failure
    static std::string download_artwork(const std::string& url,
                                         const std::string& cache_dir,
                                         const std::string& key);

    // Set custom user agent
    static void set_user_agent(const std::string& ua);

    // Set custom base URLs
    static void set_musicbrainz_url(const std::string& url);
    static void set_coverartarchive_url(const std::string& url);

private:
    static std::string user_agent_;
    static std::string musicbrainz_url_;
    static std::string coverartarchive_url_;

    // Internal: search MusicBrainz for release
    static std::optional<std::string> search_release_mbid(
        const std::string& artist, const std::string& album);

    // Internal: search MusicBrainz for artist
    static std::optional<std::string> search_artist_mbid(
        const std::string& artist);

    // Internal: get cover art archive info for release MBID
    static std::optional<ArtworkResult> get_cover_art_for_release(
        const std::string& release_mbid);

    // Internal: get cover art archive info for artist MBID
    static std::optional<ArtworkResult> get_cover_art_for_artist(
        const std::string& artist_mbid);
};

// ============================================================================
// External Metadata & Artwork Provider (iTunes Search API - free, no API key)
// https://developer.apple.com/documentation/itunesstorewebservices/searchapi
// ============================================================================

class iTunesProvider {
public:
    struct MetadataResult {
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        int year = 0;
        int track_number = 0;
        int disc_number = 0;
        std::string artwork_url_100;
        std::string artwork_url_600;
        std::string artwork_url_1200;
        std::string artwork_url;  // best available artwork URL
    };

    // Search for an album by artist + album name.
    static std::optional<MetadataResult> search_album(
        const std::string& artist, const std::string& album);

    // Search for a track by artist + track title.
    static std::optional<MetadataResult> search_track(
        const std::string& artist, const std::string& title);

    // Download artwork from URL to a local file.
    // Returns path to downloaded file, or empty string on failure.
    static std::string download_artwork(const std::string& url,
                                         const std::string& cache_dir,
                                         const std::string& key);

    static void set_user_agent(const std::string& ua);
    static void set_base_url(const std::string& url);

private:
    static std::string user_agent_;
    static std::string base_url_;
};

// ============================================================================
// External Artist Information Provider (Wikipedia REST API - no API key)
// https://en.wikipedia.org/api/rest_v1/#/Page%20content
// ============================================================================

class ArtistInfoProvider {
public:
    struct BiographyResult {
        std::string extract;
        std::string title;
        std::string description;
    };

    // Fetch a short artist biography. Returns an empty optional when the
    // service has no article or the request fails.
    static std::optional<BiographyResult> fetch_biography(const std::string& artist_name);

    // Parse a Wikipedia summary response. Kept separate for unit testing.
    static std::optional<BiographyResult> parse_biography(const std::string& json_body);

    static void set_user_agent(const std::string& ua);
    static void set_base_url(const std::string& url);

private:
    static std::string user_agent_;
    static std::string base_url_;
};

// ============================================================================
// Provider Integration Helpers
// ============================================================================

// Create a lyrics fetch function suitable for LyricsManager::set_fetch_fn()
std::function<std::string(const std::string&, const std::string&, const std::string&)>
make_lyrics_fetch_fn();

// Create an artwork extract function suitable for ArtworkManager::set_extract_fn()
// This handles both embedded extraction (via file_path) and external fetching
std::function<std::string(const std::string&, const std::string&, int)>
make_artwork_extract_fn();

// Create an iTunes-based artwork fetch function (fallback in the extract chain)
std::function<std::string(const std::string&, const std::string&, int)>
make_itunes_artwork_fn();

// Fetch metadata for a track from iTunes (artist + title, optional album)
std::optional<iTunesProvider::MetadataResult> fetch_itunes_metadata(
    const std::string& artist, const std::string& title,
    const std::string& album = "");

// Create an artist artwork fetch function
std::function<std::string(const std::string&)>  // artist_name -> artwork_path
make_artist_artwork_fetch_fn();

} // namespace lollypop