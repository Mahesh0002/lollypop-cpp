/**
 * Lollypop C++ - External Providers Implementation
 * 
 * LRCLIB for lyrics: https://lrclib.net/api
 * Cover Art Archive for artwork: https://coverartarchive.org/
 * MusicBrainz for metadata: https://musicbrainz.org/doc/Development/XML_Web_Service/Version_2
 */

#include "external_providers.hpp"

#include <curl/curl.h>
#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace lollypop {

using json = nlohmann::json;

// ============================================================================
// HTTP Helper
// ============================================================================

namespace {

struct CurlResponse {
    std::string data;
    long http_code = 0;
    std::string content_type;
    bool success = false;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total = size * nmemb;
    response->data.append(static_cast<char*>(contents), total);
    return total;
}

static size_t header_callback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
    size_t total = size * nmemb;
    std::string header(static_cast<char*>(contents), total);
    if (header.rfind("Content-Type:", 0) == 0) {
        response->content_type = header.substr(13);
        // Trim whitespace
        auto start = response->content_type.find_first_not_of(" \t\r\n");
        auto end = response->content_type.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            response->content_type = response->content_type.substr(start, end - start + 1);
        }
    }
    return total;
}

CurlResponse http_get(const std::string& url, const std::string& user_agent, int timeout_seconds = 10) {
    CurlResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) return response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.http_code);
        response.success = (response.http_code == 200);
    } else {
        std::cerr << "ExternalProviders: curl error: " << curl_easy_strerror(res) << " for " << url << std::endl;
    }
    curl_easy_cleanup(curl);
    return response;
}

CurlResponse http_download(const std::string& url, const std::string& file_path, const std::string& user_agent, int timeout_seconds = 30) {
    CurlResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) return response;

    FILE* fp = std::fopen(file_path.c_str(), "wb");
    if (!fp) {
        std::cerr << "ExternalProviders: cannot open " << file_path << " for writing" << std::endl;
        curl_easy_cleanup(curl);
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.http_code);
        response.success = (response.http_code == 200);
    } else {
        std::cerr << "ExternalProviders: download error: " << curl_easy_strerror(res) << " for " << url << std::endl;
    }
    std::fclose(fp);
    curl_easy_cleanup(curl);
    return response;
}

std::string url_encode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) return value;
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = encoded ? encoded : value;
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

} // anonymous namespace

// ============================================================================
// LyricsProvider Implementation (LRCLIB)
// ============================================================================

std::string LyricsProvider::user_agent_ = "Lollypop-CPP/1.0 (https://github.com/lollypop-cpp)";
std::string LyricsProvider::base_url_ = "https://lrclib.net/api";

void LyricsProvider::set_user_agent(const std::string& ua) {
    user_agent_ = ua;
}

void LyricsProvider::set_base_url(const std::string& url) {
    base_url_ = url;
}

std::string LyricsProvider::fetch_lyrics(const std::string& artist, const std::string& title,
                                          const std::string& album, int duration) {
    if (artist.empty() || title.empty()) return "";

    // Build query parameters using LRCLIB's structured search API
    // See: https://lrclib.net/api
    std::ostringstream url;
    url << base_url_ << "/search?";
    url << "track_name=" << url_encode(title);
    url << "&artist_name=" << url_encode(artist);
    if (!album.empty()) {
        url << "&album_name=" << url_encode(album);
    }
    if (duration > 0) {
        url << "&duration=" << duration;
    }

    CurlResponse response = http_get(url.str(), user_agent_, 20);
    if (!response.success || response.data.empty()) {
        return "";
    }

    try {
        json results = json::parse(response.data);
        if (!results.is_array() || results.empty()) {
            return "";
        }

        // Find the best match - prefer synced lyrics
        std::string best_synced;
        std::string best_unsynced;

        for (const auto& item : results) {
            // Handle fields that can be string, null, or missing
            std::string synced = "";
            std::string unsynced = "";
            
            if (item.contains("syncedLyrics") && !item["syncedLyrics"].is_null()) {
                synced = item["syncedLyrics"].get<std::string>();
            }
            if (item.contains("plainLyrics") && !item["plainLyrics"].is_null()) {
                unsynced = item["plainLyrics"].get<std::string>();
            }
            
            // Handle instrumental field which can be bool, string, or null
            bool instrumental = false;
            if (item.contains("instrumental") && !item["instrumental"].is_null()) {
                auto& inst = item["instrumental"];
                if (inst.is_boolean()) {
                    instrumental = inst.get<bool>();
                } else if (inst.is_string()) {
                    instrumental = (inst.get<std::string>() == "true");
                }
            }

            if (instrumental) continue;

            if (!synced.empty()) {
                // Validate it's actually LRC format
                if (synced.find('[') != std::string::npos && synced.find(']') != std::string::npos) {
                    return synced;  // Return first synced result
                }
            }
            if (!unsynced.empty() && best_unsynced.empty()) {
                best_unsynced = unsynced;
            }
        }

        return best_unsynced;  // Fall back to unsynced
    } catch (const std::exception& e) {
        std::cerr << "ExternalProviders: lyrics parse error: " << e.what() << std::endl;
        return "";
    }
}

// ============================================================================
// ArtworkProvider Implementation (Cover Art Archive / MusicBrainz)
// ============================================================================

std::string ArtworkProvider::user_agent_ = "Lollypop-CPP/1.0 (https://github.com/lollypop-cpp)";
std::string ArtworkProvider::musicbrainz_url_ = "https://musicbrainz.org/ws/2";
std::string ArtworkProvider::coverartarchive_url_ = "https://coverartarchive.org";

void ArtworkProvider::set_user_agent(const std::string& ua) {
    user_agent_ = ua;
}

void ArtworkProvider::set_musicbrainz_url(const std::string& url) {
    musicbrainz_url_ = url;
}

void ArtworkProvider::set_coverartarchive_url(const std::string& url) {
    coverartarchive_url_ = url;
}

std::optional<std::string> ArtworkProvider::search_release_mbid(const std::string& artist, const std::string& album) {
    if (artist.empty() || album.empty()) return std::nullopt;

    std::ostringstream url;
    url << musicbrainz_url_ << "/release/?fmt=json&query=";
    url << "release:" << url_encode(album);
    url << "+AND+artist:" << url_encode(artist);
    url << "&limit=1";

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (j.contains("releases") && j["releases"].is_array() && !j["releases"].empty()) {
            return j["releases"][0].value("id", "");
        }
    } catch (const std::exception& e) {
        std::cerr << "ExternalProviders: release search parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::optional<std::string> ArtworkProvider::search_artist_mbid(const std::string& artist) {
    if (artist.empty()) return std::nullopt;

    std::ostringstream url;
    url << musicbrainz_url_ << "/artist/?fmt=json&query=";
    url << "artist:" << url_encode(artist);
    url << "&limit=1";

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (j.contains("artists") && j["artists"].is_array() && !j["artists"].empty()) {
            return j["artists"][0].value("id", "");
        }
    } catch (const std::exception& e) {
        std::cerr << "ExternalProviders: artist search parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::get_cover_art_for_release(const std::string& release_mbid) {
    if (release_mbid.empty()) return std::nullopt;

    std::ostringstream url;
    url << coverartarchive_url_ << "/release/" << release_mbid;

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (!j.contains("images") || !j["images"].is_array() || j["images"].empty()) {
            return std::nullopt;
        }

        // Prefer front cover, then any image
        ArtworkResult best;
        for (const auto& img : j["images"]) {
            std::string type = img.value("types", std::vector<std::string>{})[0];
            bool front = img.value("front", false);
            int width = img.value("width", 0);
            int height = img.value("height", 0);

            // Build image URL - use full-size image, not thumbnails
            // Cover Art Archive provides full-size images at /release/{mbid}/{id}
            // without any size suffix. The -250.jpg suffix was requesting low-res.
            std::string image_url;
            if (img.contains("image") && !img["image"].is_null()) {
                image_url = img["image"].get<std::string>();
            } else {
                image_url = coverartarchive_url_ + "/release/" + release_mbid + "/" + img.value("id", "");
            }

            ArtworkResult result;
            result.url = image_url;
            result.mime_type = img.value("mime_type", "image/jpeg");
            result.type = front ? "front" : type;
            result.width = width;
            result.height = height;

            if (front || best.url.empty()) {
                best = result;
                if (front) break;
            }
        }

        if (!best.url.empty()) return best;
    } catch (const std::exception& e) {
        std::cerr << "ExternalProviders: cover art parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::get_cover_art_for_artist(const std::string& artist_mbid) {
    if (artist_mbid.empty()) return std::nullopt;

    std::ostringstream url;
    url << coverartarchive_url_ << "/artist/" << artist_mbid;

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (!j.contains("images") || !j["images"].is_array() || j["images"].empty()) {
            return std::nullopt;
        }

        ArtworkResult best;
        for (const auto& img : j["images"]) {
            // Build image URL - use full-size image, not thumbnails
            std::string image_url;
            if (img.contains("image") && !img["image"].is_null()) {
                image_url = img["image"].get<std::string>();
            } else {
                image_url = coverartarchive_url_ + "/artist/" + artist_mbid + "/" + img.value("id", "");
            }

            ArtworkResult result;
            result.url = image_url;
            result.mime_type = img.value("mime_type", "image/jpeg");
            result.type = img.value("types", std::vector<std::string>{"artist"})[0];
            result.width = img.value("width", 0);
            result.height = img.value("height", 0);

            if (best.url.empty()) best = result;
        }

        if (!best.url.empty()) return best;
    } catch (const std::exception& e) {
        std::cerr << "ExternalProviders: artist cover art parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::fetch_album_artwork(const std::string& musicbrainz_release_id) {
    return get_cover_art_for_release(musicbrainz_release_id);
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::fetch_album_artwork_by_name(const std::string& artist, const std::string& album) {
    auto mbid = search_release_mbid(artist, album);
    if (!mbid) return std::nullopt;
    return get_cover_art_for_release(*mbid);
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::fetch_artist_artwork(const std::string& musicbrainz_artist_id) {
    return get_cover_art_for_artist(musicbrainz_artist_id);
}

std::optional<ArtworkProvider::ArtworkResult> ArtworkProvider::fetch_artist_artwork_by_name(const std::string& artist) {
    auto mbid = search_artist_mbid(artist);
    if (!mbid) return std::nullopt;
    return get_cover_art_for_artist(*mbid);
}

std::string ArtworkProvider::download_artwork(const std::string& url, const std::string& cache_dir, const std::string& key) {
    if (url.empty() || cache_dir.empty()) return "";

    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        std::cerr << "ExternalProviders: cannot create cache dir " << cache_dir << ": " << ec.message() << std::endl;
        return "";
    }

    // Determine extension from URL or content-type
    std::string ext = ".jpg";
    if (url.find(".png") != std::string::npos) ext = ".png";
    else if (url.find(".gif") != std::string::npos) ext = ".gif";
    else if (url.find(".webp") != std::string::npos) ext = ".webp";

    std::string file_path = cache_dir + "/" + key + ext;

    CurlResponse response = http_download(url, file_path, user_agent_);
    if (!response.success) {
        std::error_code rm_ec;
        std::filesystem::remove(file_path, rm_ec);
        return "";
    }

    // Verify it's actually an image by checking magic bytes
    std::ifstream check(file_path, std::ios::binary);
    char header[8];
    check.read(header, 8);
    bool valid = false;
    if (check.gcount() >= 4) {
        // PNG: 89 50 4E 47
        if (header[0] == '\x89' && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') valid = true;
        // JPEG: FF D8 FF
        else if (header[0] == '\xFF' && header[1] == '\xD8' && header[2] == '\xFF') valid = true;
        // GIF: GIF87a or GIF89a
        else if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F' && header[3] == '8') valid = true;
        // WebP: RIFF....WEBP
        else if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' && check.gcount() >= 12) {
            check.seekg(8);
            char webp[4];
            check.read(webp, 4);
            if (webp[0] == 'W' && webp[1] == 'E' && webp[2] == 'B' && webp[3] == 'P') valid = true;
        }
    }
    check.close();

    if (!valid) {
        std::cerr << "ExternalProviders: downloaded file is not a valid image" << std::endl;
        std::error_code rm_ec;
        std::filesystem::remove(file_path, rm_ec);
        return "";
    }

    return file_path;
}

// ============================================================================
// Provider Integration Helpers
// ============================================================================

std::function<std::string(const std::string&, const std::string&, const std::string&)>
make_lyrics_fetch_fn() {
    return [](const std::string& track_id, const std::string& artist, const std::string& title) -> std::string {
        // Note: track_id is not used by LRCLIB, but we accept it for interface compatibility
        return LyricsProvider::fetch_lyrics(artist, title);
    };
}

std::function<std::string(const std::string&, const std::string&, int)>
make_artwork_extract_fn() {
    return [](const std::string& file_path, const std::string& key, int max_size) -> std::string {
        // This function is called by ArtworkManager when it needs to extract artwork
        // The key is typically "album_artist|album" (from ArtworkManager::make_key)
        // max_size is the requested thumbnail size in pixels

        // Parse key to extract artist and album
        // Format: "album_artist|album" or just "album" if album_artist is empty
        std::string album_artist;
        std::string album;
        size_t pos = key.find('|');
        if (pos != std::string::npos) {
            album_artist = key.substr(0, pos);
            album = key.substr(pos + 1);
        } else {
            album = key;
        }

        if (album.empty()) return "";

        // Use album_artist if available, otherwise try to infer from file_path
        std::string artist = album_artist;
        if (artist.empty() && !file_path.empty()) {
            // Try to get artist from the file's tags
            // For now, we'll skip this and just use the album name
        }

        // 1. iTunes Search API first (free, no API key, returns 600px artwork).
        auto itunes = iTunesProvider::search_album(artist, album);
        if (itunes && !itunes->artwork_url.empty()) {
            std::string cache_dir;
            if (const char* home = std::getenv("HOME")) {
                cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
            } else {
                cache_dir = "/tmp/lollypop-cpp/artwork";
            }

            std::string cache_key = key;
            std::replace(cache_key.begin(), cache_key.end(), '/', '_');
            std::replace(cache_key.begin(), cache_key.end(), ':', '_');

            return iTunesProvider::download_artwork(itunes->artwork_url, cache_dir,
                                                    cache_key);
        }

        // 2. Local folder artwork next (e.g. /home/Music/Artist/Album/cover.jpg).
        if (!file_path.empty()) {
            std::filesystem::path p(file_path);
            if (p.has_parent_path()) {
                const std::filesystem::path dir = p.parent_path();
                // Common folder artwork names, in preference order.
                const std::vector<std::string> candidates = {
                    "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg",
                    "front.jpg", "front.png", "album.jpg", "album.png",
                };
                std::error_code ec;
                for (const auto& name : candidates) {
                    std::filesystem::path candidate = dir / name;
                    if (std::filesystem::is_regular_file(candidate, ec)) {
                        return candidate.string();
                    }
                }
            }
        }

        // 3. Fall back to Cover Art Archive (MusicBrainz) when neither iTunes
        //    nor local artwork is available.
        auto result = ArtworkProvider::fetch_album_artwork_by_name(artist, album);
        if (result) {
            std::string cache_dir;
            if (const char* home = std::getenv("HOME")) {
                cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
            } else {
                cache_dir = "/tmp/lollypop-cpp/artwork";
            }

            std::string cache_key = key;
            std::replace(cache_key.begin(), cache_key.end(), '/', '_');
            std::replace(cache_key.begin(), cache_key.end(), ':', '_');

            return ArtworkProvider::download_artwork(result->url, cache_dir, cache_key);
        }

        return "";
    };
}

std::function<std::string(const std::string&)>
make_artist_artwork_fetch_fn() {
    return [](const std::string& artist_name) -> std::string {
        if (artist_name.empty()) return "";

        // Fetch artist artwork from Cover Art Archive
        auto result = ArtworkProvider::fetch_artist_artwork_by_name(artist_name);
        if (!result) return "";

        // Download to cache
        std::string cache_dir;
        if (const char* home = std::getenv("HOME")) {
            cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
        } else {
            cache_dir = "/tmp/lollypop-cpp/artwork";
        }

        std::string key = "artist_" + artist_name;
        std::replace(key.begin(), key.end(), '/', '_');
        std::replace(key.begin(), key.end(), ':', '_');

        return ArtworkProvider::download_artwork(result->url, cache_dir, key);
    };
}

// ============================================================================
// iTunesProvider Implementation (free, no API key required)
// ============================================================================

std::string iTunesProvider::user_agent_ = "Lollypop-CPP/1.0 (https://github.com/lollypop-cpp)";
std::string iTunesProvider::base_url_ = "https://itunes.apple.com/search";

void iTunesProvider::set_user_agent(const std::string& ua) {
    user_agent_ = ua;
}

void iTunesProvider::set_base_url(const std::string& url) {
    base_url_ = url;
}

namespace {

// Parse a single iTunes search result item into a MetadataResult.
std::optional<iTunesProvider::MetadataResult> parse_itunes_result(const json& item) {
    if (!item.is_object()) return std::nullopt;

    std::string wrapper_type = item.value("wrapperType", "");
    // Only accept collections (albums) or tracks (songs).
    if (wrapper_type != "collection" && wrapper_type != "track") return std::nullopt;

    iTunesProvider::MetadataResult result;

    result.artist = item.value("artistName", "");
    result.album = item.value("collectionName", "");
    result.title = item.value("trackName", "");
    if (result.title.empty()) result.title = item.value("collectionName", "");
    result.genre = item.value("primaryGenreName", "");

    // iTunes Search API usually only returns artworkUrl100. Apple's CDN
    // accepts a larger size token, so derive 1200px artwork from it. Keep the
    // smaller URLs as fallbacks for CDNs that reject the larger size.
    result.artwork_url_100 = item.value("artworkUrl100", "");
    result.artwork_url_600 = item.value("artworkUrl600", "");

    auto with_size = [](std::string url, int size) {
        const std::string wanted = std::to_string(size) + "x" +
                                   std::to_string(size) + "bb";
        for (const char* token : {"100x100bb", "600x600bb", "1200x1200bb"}) {
            const auto pos = url.find(token);
            if (pos != std::string::npos) {
                url.replace(pos, std::string(token).size(), wanted);
                return url;
            }
        }
        return url;
    };

    result.artwork_url_1200 = with_size(
        !result.artwork_url_600.empty() ? result.artwork_url_600
                                        : result.artwork_url_100,
        1200);
    if (result.artwork_url_1200.empty() ||
        result.artwork_url_1200.find("1200x1200bb") == std::string::npos) {
        result.artwork_url_1200 = with_size(result.artwork_url_100, 1200);
    }
    if (result.artwork_url_600.empty()) {
        result.artwork_url_600 = with_size(result.artwork_url_100, 600);
    }
    result.artwork_url = !result.artwork_url_1200.empty() &&
                                 result.artwork_url_1200.find("1200x1200bb") !=
                                     std::string::npos
                             ? result.artwork_url_1200
                             : (!result.artwork_url_600.empty()
                                    ? result.artwork_url_600
                                    : result.artwork_url_100);

    // year from releaseDate (ISO 8601: "2020-01-01T00:00:00Z")
    std::string release_date = item.value("releaseDate", "");
    if (release_date.size() >= 4) {
        try {
            result.year = std::stoi(release_date.substr(0, 4));
        } catch (...) {
            result.year = 0;
        }
    }

    // track / disc numbers (meaningful for tracks)
    if (item.contains("trackNumber") && !item["trackNumber"].is_null()) {
        result.track_number = item["trackNumber"].get<int>();
    }
    if (item.contains("discNumber") && !item["discNumber"].is_null()) {
        result.disc_number = item["discNumber"].get<int>();
    }

    // Require at least an artist name to be useful.
    if (result.artist.empty()) return std::nullopt;
    return result;
}

// Case-insensitive contains check.
bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

} // anonymous namespace

std::optional<iTunesProvider::MetadataResult> iTunesProvider::search_album(
    const std::string& artist, const std::string& album) {
    if (artist.empty() && album.empty()) return std::nullopt;

    std::ostringstream url;
    url << base_url_ << "?";
    url << "term=" << url_encode(artist + " " + album);
    url << "&entity=album";
    url << "&limit=5";

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (!j.contains("results") || !j["results"].is_array()) return std::nullopt;

        // Score results: prefer matches on artist + album, then artist only.
        std::optional<MetadataResult> best;
        int best_score = -1;
        for (const auto& item : j["results"]) {
            auto parsed = parse_itunes_result(item);
            if (!parsed) continue;

            int score = 0;
            if (!artist.empty() && icontains(parsed->artist, artist)) score += 2;
            if (!album.empty() && icontains(parsed->album, album)) score += 2;
            if (!parsed->artwork_url.empty()) score += 1;  // prefer results with artwork

            if (score > best_score) {
                best_score = score;
                best = std::move(*parsed);
            }
        }
        return best;
    } catch (const std::exception& e) {
        std::cerr << "iTunesProvider: album search parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::optional<iTunesProvider::MetadataResult> iTunesProvider::search_track(
    const std::string& artist, const std::string& title) {
    if (artist.empty() && title.empty()) return std::nullopt;

    std::ostringstream url;
    url << base_url_ << "?";
    url << "term=" << url_encode(artist + " " + title);
    url << "&entity=song";
    url << "&limit=5";

    CurlResponse response = http_get(url.str(), user_agent_, 15);
    if (!response.success || response.data.empty()) return std::nullopt;

    try {
        json j = json::parse(response.data);
        if (!j.contains("results") || !j["results"].is_array()) return std::nullopt;

        std::optional<MetadataResult> best;
        int best_score = -1;
        for (const auto& item : j["results"]) {
            auto parsed = parse_itunes_result(item);
            if (!parsed) continue;

            int score = 0;
            if (!artist.empty() && icontains(parsed->artist, artist)) score += 2;
            if (!title.empty() && icontains(parsed->title, title)) score += 2;
            if (!parsed->artwork_url.empty()) score += 1;

            if (score > best_score) {
                best_score = score;
                best = std::move(*parsed);
            }
        }
        return best;
    } catch (const std::exception& e) {
        std::cerr << "iTunesProvider: track search parse error: " << e.what() << std::endl;
    }
    return std::nullopt;
}

std::string iTunesProvider::download_artwork(const std::string& url,
                                              const std::string& cache_dir,
                                              const std::string& key) {
    return ArtworkProvider::download_artwork(url, cache_dir, key);
}

// ============================================================================
// Integration Helpers
// ============================================================================

std::function<std::string(const std::string&, const std::string&, int)>
make_itunes_artwork_fn() {
    return [](const std::string& file_path, const std::string& key, int /*max_size*/) -> std::string {
        // Parse key to extract artist and album.
        // Format: "album_artist|album" or just "album".
        std::string album_artist;
        std::string album;
        size_t pos = key.find('|');
        if (pos != std::string::npos) {
            album_artist = key.substr(0, pos);
            album = key.substr(pos + 1);
        } else {
            album = key;
        }

        if (album.empty()) return "";

        std::string artist = album_artist;

        // Search iTunes for the album and use its high-quality artwork (600px).
        auto result = iTunesProvider::search_album(artist, album);
        if (!result || result->artwork_url.empty()) return "";

        // Download artwork to cache.
        std::string cache_dir;
        if (const char* home = std::getenv("HOME")) {
            cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
        } else {
            cache_dir = "/tmp/lollypop-cpp/artwork";
        }

        std::string cache_key = key;
        std::replace(cache_key.begin(), cache_key.end(), '/', '_');
        std::replace(cache_key.begin(), cache_key.end(), ':', '_');

        return iTunesProvider::download_artwork(result->artwork_url, cache_dir, cache_key);
    };
}

std::optional<iTunesProvider::MetadataResult> fetch_itunes_metadata(
    const std::string& artist, const std::string& title,
    const std::string& /*album*/) {
    return iTunesProvider::search_track(artist, title);
}

} // namespace lollypop