/**
 * Lollypop C++ - Library Scanner Implementation
 * Recursively scans directories for audio files and extracts metadata via taglib.
 */

#include "library_scanner.hpp"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/mpegfile.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/oggfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/apefile.h>
#include <taglib/tpropertymap.h>

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>
#include <sys/stat.h>

namespace lollypop {
namespace {

const std::vector<std::string>& audio_extensions() {
    static const std::vector<std::string> exts = {
        ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac",
        ".wv", ".ape", ".mpc", ".wma", ".alac", ".aiff", ".au"
    };
    return exts;
}

std::string lower_ext(const std::string& path) {
    std::string ext = path;
    size_t pos = ext.rfind('.');
    if (pos == std::string::npos) return {};
    ext = ext.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

int64_t now_epoch_seconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string artwork_dir_path() {
    std::string base;
    if (const char* home = std::getenv("HOME")) {
        base = home;
    } else {
        base = "/tmp";
    }
    return base + "/.local/share/lollypop-cpp/artwork";
}

uint64_t fnv1a(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Incremental cache file format:
//   magic (uint32) | version (uint32) | entry_count (uint32)
//   repeated: path, mtime, size, metadata_hash, has_track, [track fields]
// Strings are length-prefixed so paths may contain any delimiter.
constexpr uint32_t kCacheMagic = 0x32434C4C;  // "LLC2"
constexpr uint32_t kCacheVersion = 3;
constexpr uint32_t kMaxCacheStringLength = 16u * 1024u * 1024u;
constexpr uint32_t kMaxCacheEntries = 2'000'000;

template <typename T>
bool write_value(std::ostream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(out);
}

template <typename T>
bool read_value(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return in.gcount() == static_cast<std::streamsize>(sizeof(value));
}

bool write_cache_string(std::ostream& out, const std::string& value) {
    const auto size = static_cast<uint32_t>(value.size());
    return write_value<uint32_t>(out, size) &&
           (out.write(value.data(), static_cast<std::streamsize>(size)),
            static_cast<bool>(out));
}

bool read_cache_string(std::istream& in, std::string& value) {
    uint32_t size = 0;
    if (!read_value<uint32_t>(in, size) || size > kMaxCacheStringLength) {
        return false;
    }
    std::string result(size, '\0');
    if (size > 0) {
        in.read(result.data(), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) return false;
    }
    value = std::move(result);
    return true;
}

uint64_t compute_metadata_hash(const Track& track) {
    std::string key;
    auto append = [&key](const std::string& value) {
        key += value;
        key += '\x1f';
    };
    append(track.title);
    append(track.artist);
    append(track.album);
    append(track.album_artist);
    append(track.genre);
    append(std::to_string(track.track_number));
    append(std::to_string(track.disc_number));
    append(std::to_string(track.year));
    append(std::to_string(track.duration));
    return fnv1a(key);
}

bool write_track(std::ostream& out, const Track& track) {
    return write_cache_string(out, track.id) &&
           write_cache_string(out, track.title) &&
           write_cache_string(out, track.artist) &&
           write_cache_string(out, track.album) &&
           write_cache_string(out, track.album_artist) &&
           write_cache_string(out, track.genre) &&
           write_value<int32_t>(out, track.track_number) &&
           write_value<int32_t>(out, track.disc_number) &&
           write_value<int32_t>(out, track.year) &&
           write_value<int32_t>(out, track.duration) &&
           write_cache_string(out, track.file_path) &&
           write_cache_string(out, track.artwork_path) &&
           write_cache_string(out, track.embedded_lyrics) &&
           write_value<uint8_t>(out, track.liked ? 1 : 0) &&
           write_value<int32_t>(out, track.play_count) &&
           write_value<int64_t>(out, track.last_played) &&
           write_value<int64_t>(out, track.date_added);
}

bool read_track(std::istream& in, Track& track) {
    int32_t track_number = 0;
    int32_t disc_number = 0;
    int32_t year = 0;
    int32_t duration = 0;
    uint8_t liked = 0;
    int32_t play_count = 0;
    int64_t last_played = 0;
    int64_t date_added = 0;

    if (!read_cache_string(in, track.id) ||
        !read_cache_string(in, track.title) ||
        !read_cache_string(in, track.artist) ||
        !read_cache_string(in, track.album) ||
        !read_cache_string(in, track.album_artist) ||
        !read_cache_string(in, track.genre) ||
        !read_value<int32_t>(in, track_number) ||
        !read_value<int32_t>(in, disc_number) ||
        !read_value<int32_t>(in, year) ||
        !read_value<int32_t>(in, duration) ||
        !read_cache_string(in, track.file_path) ||
        !read_cache_string(in, track.artwork_path) ||
        !read_cache_string(in, track.embedded_lyrics) ||
        !read_value<uint8_t>(in, liked) ||
        !read_value<int32_t>(in, play_count) ||
        !read_value<int64_t>(in, last_played) ||
        !read_value<int64_t>(in, date_added)) {
        return false;
    }

    track.track_number = track_number;
    track.disc_number = disc_number;
    track.year = year;
    track.duration = duration;
    track.liked = liked != 0;
    track.play_count = play_count;
    track.last_played = last_played;
    track.date_added = date_added;
    return true;
}

std::string artwork_extension(const TagLib::ByteVector& data,
                              const std::string& mime_type) {
    if (mime_type == "image/png") return ".png";
    if (mime_type == "image/gif") return ".gif";
    if (mime_type == "image/bmp") return ".bmp";
    if (mime_type == "image/jpeg" || mime_type == "image/jpg") return ".jpg";

    // Fall back to magic bytes: FLAC/MP4 often carry an empty MIME type.
    const std::string bytes(data.data(), data.size());
    if (bytes.size() >= 4 && bytes.starts_with("\x89PNG")) return ".png";
    if (bytes.size() >= 4 && bytes.starts_with("GIF8")) return ".gif";
    if (bytes.size() >= 2 && bytes.starts_with("BM")) return ".bmp";
    return ".jpg";
}

// Minimal Base64 decoder (RFC 4648). Returns decoded bytes.
TagLib::ByteVector base64_decode(const std::string& input) {
    static constexpr const char* kBase64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    TagLib::ByteVector out;
    // TagLib::ByteVector has no reserve(); append bytes directly.

    uint32_t buffer = 0;
    int bits_collected = 0;
    for (char c : input) {
        const char* p = std::strchr(kBase64Chars, c);
        if (!p) continue;  // skip whitespace / padding / invalid
        buffer = (buffer << 6) | static_cast<uint32_t>(p - kBase64Chars);
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            out.append(static_cast<char>((buffer >> bits_collected) & 0xFF));
        }
    }
    return out;
}

std::string save_artwork(const TagLib::ByteVector& data, const std::string& mime_type,
                         const std::string& key) {
    if (data.isEmpty()) return {};

    const std::filesystem::path dir(artwork_dir_path());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::cerr << "LibraryScanner: cannot create artwork directory "
                  << dir << ": " << ec.message() << std::endl;
        return {};
    }

    // A stable name per album (or per file when there is no album) means a
    // rescan overwrites the same file instead of leaking a new random one.
    uint64_t hash = fnv1a(key.empty() ? std::string("unknown") : key);
    if (hash == 0) hash = 1;
    const std::filesystem::path path =
        dir / (std::to_string(hash) + artwork_extension(data, mime_type));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) return {};
    return path.string();
}

uint64_t artwork_data_hash(const TagLib::ByteVector& data) {
    if (data.isEmpty()) return 0;
    const std::string bytes(data.data(), data.size());
    const uint64_t hash = fnv1a(bytes);
    return hash == 0 ? 1 : hash;
}

std::string save_artwork_if_changed(const TagLib::ByteVector& data,
                                    const std::string& mime_type,
                                    const std::string& key,
                                    uint64_t cached_hash,
                                    const std::string& cached_path,
                                    uint64_t& out_hash) {
    if (data.isEmpty()) return {};
    out_hash = artwork_data_hash(data);
    if (cached_hash != 0 && cached_hash == out_hash && !cached_path.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(cached_path, ec)) return cached_path;
    }
    return save_artwork(data, mime_type, key);
}

uint64_t extract_artwork(TagLib::File* file, Track& track,
                         const std::string& key, uint64_t cached_hash,
                         const std::string& cached_path) {
    if (!file) return 0;
    uint64_t result_hash = 0;

    // ID3v2 APIC picture (MP3 and other ID3v2-tagged formats)
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto* id3v2 = mpeg->ID3v2Tag()) {
            TagLib::ID3v2::FrameList frames = id3v2->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                if (auto* pic =
                        dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(
                            frames.front())) {
                    track.artwork_path = save_artwork_if_changed(
                        pic->picture(), pic->mimeType().to8Bit(true), key,
                        cached_hash, cached_path, result_hash);
                }
            }
        }
    }

    // FLAC PICTURE metadata blocks
    if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
        const auto pictures = flac->pictureList();
        if (!pictures.isEmpty()) {
            const TagLib::FLAC::Picture* picture = pictures.front();
            track.artwork_path = save_artwork_if_changed(
                picture->data(), picture->mimeType().to8Bit(true), key,
                cached_hash, cached_path, result_hash);
        }
    }

    // MP4/M4A "covr" atom
    if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        if (auto* tag = dynamic_cast<TagLib::MP4::Tag*>(mp4->tag());
            tag && tag->contains("covr")) {
            const TagLib::MP4::CoverArtList covers =
                tag->item("covr").toCoverArtList();
            if (!covers.isEmpty()) {
                const TagLib::MP4::CoverArt cover = covers.front();
                const std::string mime =
                    cover.format() == TagLib::MP4::CoverArt::PNG
                        ? "image/png"
                        : (cover.format() == TagLib::MP4::CoverArt::GIF
                               ? "image/gif"
                               : (cover.format() == TagLib::MP4::CoverArt::BMP
                                      ? "image/bmp"
                                      : "image/jpeg"));
                track.artwork_path = save_artwork_if_changed(
                    cover.data(), mime, key, cached_hash, cached_path,
                    result_hash);
            }
        }
    }

    // Ogg/Opus METADATA_BLOCK_PICTURE (VorbisComment field).
    // The picture data is base64-encoded FLAC PICTURE binary.
    if (auto* ogg = dynamic_cast<TagLib::Ogg::File*>(file)) {
        const TagLib::PropertyMap properties = ogg->properties();
        auto it = properties.find("METADATA_BLOCK_PICTURE");
        if (it != properties.end() && !it->second.isEmpty()) {
            const TagLib::StringList& values = it->second;
            TagLib::ByteVector picture_data =
                base64_decode(values.front().to8Bit(true));
            if (!picture_data.isEmpty()) {
                TagLib::FLAC::Picture picture(picture_data);
                if (!picture.data().isEmpty()) {
                    track.artwork_path = save_artwork_if_changed(
                        picture.data(), picture.mimeType().to8Bit(true), key,
                        cached_hash, cached_path, result_hash);
                }
            }
        }
    }
    return result_hash;
}

}  // namespace

struct LibraryScanner::Impl {
    ScanProgress progress;
    ProgressCallback progress_cb;
    TrackFoundCallback track_cb;
    TrackFoundWithCacheCallback track_with_cache_cb;
    ErrorCallback error_cb;
    std::atomic<bool> cancelled{false};

    // Incremental scan cache: file_path -> extracted metadata + last file stamp.
    // Persisted to disk to survive restarts and avoid TagLib parsing on rescan.
    struct FileMeta {
        std::time_t mtime = 0;
        std::uintmax_t size = 0;
        uint64_t metadata_hash = 0;
        uint64_t artwork_hash = 0;
        bool has_track = false;
        Track track;
    };
    std::unordered_map<std::string, FileMeta> scan_cache_;
    std::string cache_path_;
    std::mutex cache_mutex_;

    using ProgressClock = std::chrono::steady_clock;
    static constexpr auto kProgressInterval = std::chrono::milliseconds(100);
    ProgressClock::time_point last_progress_emit_{};

    void reset_progress_state() {
        last_progress_emit_ = ProgressClock::time_point{};
    }

    void emit_progress(bool force = false) {
        if (!progress_cb) return;
        const auto now = ProgressClock::now();
        if (!force && now - last_progress_emit_ < kProgressInterval) return;
        last_progress_emit_ = now;
        progress_cb(progress);
    }

    void notify_track_found(const Track& track, bool from_cache) {
        if (track_with_cache_cb) {
            track_with_cache_cb(track, from_cache);
        } else if (track_cb) {
            track_cb(track);
        }
    }

    // Legacy cache format: path|mtime|size. Imported once, then rewritten in
    // the versioned binary format on the next save.
    void load_legacy_cache() {
        std::ifstream in(cache_path_);
        if (!in) return;
        std::lock_guard<std::mutex> lock(cache_mutex_);
        scan_cache_.clear();
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const size_t pos1 = line.find('|');
            if (pos1 == std::string::npos) continue;
            const size_t pos2 = line.find('|', pos1 + 1);
            if (pos2 == std::string::npos) continue;
            std::string path = line.substr(0, pos1);
            const std::string mtime_str = line.substr(pos1 + 1, pos2 - pos1 - 1);
            const std::string size_str = line.substr(pos2 + 1);
            try {
                FileMeta meta;
                meta.mtime = std::stoll(mtime_str);
                meta.size = std::stoull(size_str);
                scan_cache_[std::move(path)] = std::move(meta);
            } catch (...) {
                // Skip malformed lines.
            }
        }
    }

    // Load incremental scan cache from disk.
    void load_cache() {
        if (cache_path_.empty()) return;
        std::ifstream in(cache_path_, std::ios::binary);
        if (!in) return;

        uint32_t magic = 0;
        if (!read_value<uint32_t>(in, magic)) return;
        if (magic != kCacheMagic) {
            load_legacy_cache();
            return;
        }

        uint32_t version = 0;
        uint32_t count = 0;
        if (!read_value<uint32_t>(in, version) ||
            (version != 2 && version != kCacheVersion) ||
            !read_value<uint32_t>(in, count) || count > kMaxCacheEntries) {
            return;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);
        scan_cache_.clear();
        for (uint32_t i = 0; i < count; ++i) {
            std::string path;
            FileMeta meta;
            uint8_t has_track = 0;
            if (!read_cache_string(in, path) ||
                !read_value<std::time_t>(in, meta.mtime) ||
                !read_value<std::uintmax_t>(in, meta.size) ||
                !read_value<uint64_t>(in, meta.metadata_hash)) {
                scan_cache_.clear();
                return;
            }
            if (version >= 3 &&
                !read_value<uint64_t>(in, meta.artwork_hash)) {
                scan_cache_.clear();
                return;
            }
            if (!read_value<uint8_t>(in, has_track)) {
                scan_cache_.clear();
                return;
            }
            meta.has_track = has_track != 0;
            if (meta.has_track && !read_track(in, meta.track)) {
                scan_cache_.clear();
                return;
            }
            scan_cache_[std::move(path)] = std::move(meta);
        }
    }

    // Save incremental scan cache to disk with an atomic rename.
    void save_cache() {
        if (cache_path_.empty()) return;

        std::lock_guard<std::mutex> lock(cache_mutex_);
        // Drop entries for deleted files so the cache cannot grow forever.
        for (auto it = scan_cache_.begin(); it != scan_cache_.end();) {
            std::error_code ec;
            if (!std::filesystem::exists(it->first, ec)) {
                it = scan_cache_.erase(it);
            } else {
                ++it;
            }
        }

        std::ofstream out(cache_path_ + ".tmp", std::ios::binary | std::ios::trunc);
        if (!out) return;
        if (!write_value<uint32_t>(out, kCacheMagic) ||
            !write_value<uint32_t>(out, kCacheVersion) ||
            !write_value<uint32_t>(out,
                                   static_cast<uint32_t>(scan_cache_.size()))) {
            return;
        }

        for (const auto& [path, meta] : scan_cache_) {
            const uint8_t has_track = meta.has_track ? 1 : 0;
            if (!write_cache_string(out, path) ||
                !write_value<std::time_t>(out, meta.mtime) ||
                !write_value<std::uintmax_t>(out, meta.size) ||
                !write_value<uint64_t>(out, meta.metadata_hash) ||
                !write_value<uint64_t>(out, meta.artwork_hash) ||
                !write_value<uint8_t>(out, has_track)) {
                return;
            }
            if (meta.has_track && !write_track(out, meta.track)) return;
        }

        out.close();
        if (out) {
            std::error_code ec;
            std::filesystem::rename(cache_path_ + ".tmp", cache_path_, ec);
        }
    }

    // Check if file is unchanged since last scan.
    // Returns true if file should be skipped (unchanged).
    bool is_file_unchanged(const std::string& file_path) {
        if (cache_path_.empty()) return false;
        std::error_code ec;
        auto status = std::filesystem::status(file_path, ec);
        if (ec) return false;
        std::time_t mtime = std::chrono::system_clock::to_time_t(
            std::chrono::clock_cast<std::chrono::system_clock>(
                std::filesystem::last_write_time(file_path, ec)));
        if (ec) return false;
        std::uintmax_t size = std::filesystem::file_size(file_path, ec);
        if (ec) return false;

        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = scan_cache_.find(file_path);
        if (it == scan_cache_.end()) return false;
        return it->second.mtime == mtime && it->second.size == size;
    }

    bool get_cached_track(const std::string& file_path, Track& track) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = scan_cache_.find(file_path);
        if (it == scan_cache_.end() || !it->second.has_track) return false;
        track = it->second.track;
        return true;
    }

    uint64_t get_cached_artwork_hash(const std::string& file_path) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = scan_cache_.find(file_path);
        return it == scan_cache_.end() ? 0 : it->second.artwork_hash;
    }

    // Update cache entry for a file after successful processing.
    void update_cache(const std::string& file_path, const Track& track,
                      uint64_t artwork_hash) {
        if (cache_path_.empty()) return;
        std::error_code ec;
        std::time_t mtime = std::chrono::system_clock::to_time_t(
            std::chrono::clock_cast<std::chrono::system_clock>(
                std::filesystem::last_write_time(file_path, ec)));
        if (ec) return;
        std::uintmax_t size = std::filesystem::file_size(file_path, ec);
        if (ec) return;

        FileMeta meta;
        meta.mtime = mtime;
        meta.size = size;
        meta.metadata_hash = compute_metadata_hash(track);
        meta.artwork_hash = artwork_hash;
        meta.has_track = true;
        meta.track = track;

        std::lock_guard<std::mutex> lock(cache_mutex_);
        scan_cache_[file_path] = std::move(meta);
    }

    void scan_recursive(const std::filesystem::path& path) {
        if (cancelled.load()) return;
        try {
            std::error_code ec;
            std::filesystem::directory_iterator it(path, ec);
            if (ec) {
                if (error_cb) error_cb(path.string(), ec.message());
                return;
            }

            for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (cancelled.load()) break;
                if (ec) {
                    if (error_cb) error_cb(it->path().string(), ec.message());
                    ec.clear();
                    continue;
                }

                std::error_code status_ec;
                bool is_dir = it->is_directory(status_ec);
                if (status_ec || is_dir) {
                    if (is_dir) scan_recursive(it->path());
                    continue;
                }

                const auto& file_path = it->path();
                std::string ext = lower_ext(file_path.string());
                if (!is_audio_file(ext)) continue;

                progress.files_found++;
                progress.current_file = file_path.string();
                emit_progress();

                // Incremental scan: skip unchanged files.
                if (is_file_unchanged(file_path.string())) {
                    Track cached;
                    if (get_cached_track(file_path.string(), cached)) {
                        notify_track_found(cached, true);
                    }
                    progress.files_processed++;
                    emit_progress();
                    continue;
                }

                process_file_safe(file_path.string());
                progress.files_processed++;
                emit_progress();
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "LibraryScanner: filesystem error: " << e.what() << std::endl;
        }
        emit_progress(true);
    }

    void process_file_safe(const std::string& file_path) {
        try {
            process_file(file_path);
        } catch (const std::exception& e) {
            if (error_cb) error_cb(file_path, e.what());
        } catch (...) {
            if (error_cb) error_cb(file_path, "Unknown metadata error");
        }
    }

    void process_file(const std::string& file_path) {
        TagLib::FileRef file(file_path.c_str());
        if (file.isNull() || !file.tag()) {
            if (error_cb) error_cb(file_path, "Failed to read file or no tag");
            return;
        }

        TagLib::Tag* tag = file.tag();
        TagLib::AudioProperties* props = file.audioProperties();

        // Stable identity based on the canonical file path. The same file
        // always produces the same ID across scans.
        std::filesystem::path canonical;
        std::error_code ec;
        canonical = std::filesystem::canonical(file_path, ec);
        std::string identity_source =
            ec ? file_path : canonical.string();

        Track track;
        track.id = generate_id(identity_source);
        track.title = tag->title().to8Bit(true);
        track.artist = tag->artist().to8Bit(true);
        track.album = tag->album().to8Bit(true);
        track.genre = tag->genre().to8Bit(true);
        track.track_number = static_cast<int>(tag->track());
        track.year = static_cast<int>(tag->year());
        track.duration = props ? static_cast<int>(std::round(props->lengthInSeconds())) : 0;
        track.file_path = file_path;
        track.date_added = now_epoch_seconds();

        // Album artist: read from the format-independent property map.
        if (file.file()) {
            const TagLib::PropertyMap properties = file.file()->properties();
            if (properties.contains("ALBUMARTIST")) {
                const TagLib::StringList values = properties.value("ALBUMARTIST");
                if (!values.isEmpty()) {
                    track.album_artist = values.front().to8Bit(true);
                }
            }
            // Vorbis comments also expose ALBUMARTIST as a field.
            if (track.album_artist.empty() &&
                properties.contains("ALBUM ARTIST")) {
                const TagLib::StringList values = properties.value("ALBUM ARTIST");
                if (!values.isEmpty()) {
                    track.album_artist = values.front().to8Bit(true);
                }
            }

            // Embedded lyrics: read LYRICS or UNSYNCEDLYRICS frame from PropertyMap.
            // Common field names: LYRICS, UNSYNCEDLYRICS, SYNCEDLYRICS.
            std::string embedded_lyrics;
            if (properties.contains("LYRICS")) {
                const TagLib::StringList values = properties.value("LYRICS");
                if (!values.isEmpty()) {
                    embedded_lyrics = values.front().to8Bit(true);
                }
            }
            if (embedded_lyrics.empty() && properties.contains("UNSYNCEDLYRICS")) {
                const TagLib::StringList values = properties.value("UNSYNCEDLYRICS");
                if (!values.isEmpty()) {
                    embedded_lyrics = values.front().to8Bit(true);
                }
            }
            if (embedded_lyrics.empty() && properties.contains("SYNCEDLYRICS")) {
                const TagLib::StringList values = properties.value("SYNCEDLYRICS");
                if (!values.isEmpty()) {
                    embedded_lyrics = values.front().to8Bit(true);
                }
            }
            // Vorbis comment field.
            if (embedded_lyrics.empty() && properties.contains("UNSYNCED LYRICS")) {
                const TagLib::StringList values = properties.value("UNSYNCED LYRICS");
                if (!values.isEmpty()) {
                    embedded_lyrics = values.front().to8Bit(true);
                }
            }
            if (!embedded_lyrics.empty()) {
                track.embedded_lyrics = embedded_lyrics;
            }
        }

        // Keep the UI readable when files have missing or partial tags.
        if (track.title.empty()) {
            track.title = std::filesystem::path(file_path).stem().string();
        }
        if (track.artist.empty()) track.artist = "Unknown Artist";
        if (track.album.empty()) track.album = "Unknown Album";
        if (track.album_artist.empty()) track.album_artist = track.artist;

        const std::string artwork_key =
            track.album != "Unknown Album"
                ? (track.album_artist + "|" + track.album)
                : track.file_path;

        Track cached_track;
        const bool has_cached_track = get_cached_track(file_path, cached_track);
        const uint64_t cached_art_hash =
            has_cached_track ? get_cached_artwork_hash(file_path) : 0;
        const std::string cached_art_path =
            has_cached_track ? cached_track.artwork_path : std::string{};

        const uint64_t artwork_hash = extract_artwork(
            file.file(), track, artwork_key, cached_art_hash, cached_art_path);

        notify_track_found(track, false);

        // Update incremental scan cache on successful processing.
        update_cache(file_path, track, artwork_hash);
    }

    std::string generate_id(const std::string& identity_source) {
        // Stable UUID-style ID derived from the canonical file path.
        // Uses UUID version 4 format with the version (4) and variant
        // (10xx) bits set per RFC 4122, but derived deterministically from
        // the source string via FNV-1a so the same file always yields the
        // same ID.
        uint64_t hash1 = fnv1a(identity_source);
        uint64_t hash2 = fnv1a(identity_source + "|lollypop");

        std::stringstream ss;
        const char* hex = "0123456789abcdef";
        auto write_hex = [&](uint64_t v, int nibbles) {
            for (int i = nibbles - 1; i >= 0; --i) {
                ss << hex[(v >> (i * 4)) & 0xF];
            }
        };

        // time_low (8 hex)
        write_hex(hash1, 8);
        ss << '-';
        // time_mid (4 hex)
        write_hex(hash1 >> 32, 4);
        ss << '-';
        // time_hi_and_version: version = 4 (top nibble), time_hi = bits 48-59
        ss << '4';  // version nibble (top 4 bits of time_hi_and_version)
        ss << hex[(hash1 >> 48) & 0xF];              // bits 48-51
        ss << hex[((hash1 >> 48) >> 4) & 0xF];       // bits 52-55
        ss << hex[((hash1 >> 48) >> 8) & 0xF];       // bits 56-59
        ss << '-';
        // clock_seq_hi_and_reserved: variant = 10xx
        uint8_t cs_hi = static_cast<uint8_t>((hash2 >> 56) & 0xFF);
        cs_hi = (cs_hi & 0x3F) | 0x80;  // set variant bits to 10
        ss << hex[cs_hi >> 4];
        ss << hex[cs_hi & 0xF];
        // clock_seq_low (4 hex)
        write_hex(hash2 >> 48, 4);
        ss << '-';
        // node (12 hex)
        write_hex(hash2, 12);

        return ss.str();
    }
};

LibraryScanner::LibraryScanner() : impl_(std::make_unique<Impl>()) {}
LibraryScanner::~LibraryScanner() = default;

void LibraryScanner::scan_directory(const std::string& path, bool recursive) {
    impl_->progress = ScanProgress{};
    impl_->cancelled = false;
    impl_->reset_progress_state();
    // Load incremental scan cache before scanning.
    impl_->load_cache();
    if (recursive) {
        impl_->scan_recursive(path);
    } else {
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec);
        if (ec) {
            if (impl_->error_cb) impl_->error_cb(path, ec.message());
            return;
        }
        for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (impl_->cancelled) break;
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file()) continue;
            std::string ext = lower_ext(it->path().string());
            if (!is_audio_file(ext)) continue;
            impl_->progress.files_found++;
            impl_->progress.current_file = it->path().string();
            impl_->emit_progress();
            // Check incremental cache for non-recursive scan too.
            if (impl_->is_file_unchanged(it->path().string())) {
                Track cached;
                if (impl_->get_cached_track(it->path().string(), cached)) {
                    impl_->notify_track_found(cached, true);
                }
                impl_->progress.files_processed++;
                impl_->emit_progress();
                continue;
            }
            impl_->process_file_safe(it->path().string());
            impl_->progress.files_processed++;
            impl_->emit_progress();
        }
        impl_->emit_progress(true);
    }
    // Save cache after scan completes.
    impl_->save_cache();
}

void LibraryScanner::scan_file(const std::string& path) {
    impl_->progress = ScanProgress{};
    impl_->cancelled = false;
    impl_->reset_progress_state();
    // Load incremental scan cache before scanning.
    impl_->load_cache();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        if (impl_->error_cb) impl_->error_cb(path, "Not a regular file");
        return;
    }
    std::string ext = lower_ext(path);
    if (!is_audio_file(ext)) {
        if (impl_->error_cb) impl_->error_cb(path, "Not an audio file");
        return;
    }
    impl_->progress.files_found++;
    impl_->progress.current_file = path;
    impl_->emit_progress();
    if (impl_->is_file_unchanged(path)) {
        Track cached;
        if (impl_->get_cached_track(path, cached)) {
            impl_->notify_track_found(cached, true);
        }
        impl_->progress.files_processed++;
        impl_->emit_progress(true);
    } else {
        impl_->process_file_safe(path);
        impl_->progress.files_processed++;
        impl_->emit_progress(true);
    }
    // Save cache after scan completes.
    impl_->save_cache();
}

void LibraryScanner::cancel() {
    impl_->cancelled = true;
}

void LibraryScanner::reset_cancellation() {
    impl_->cancelled = false;
}

bool LibraryScanner::is_cancelled() const {
    return impl_->cancelled.load();
}

void LibraryScanner::set_progress_callback(ProgressCallback cb) {
    impl_->progress_cb = std::move(cb);
}

void LibraryScanner::set_track_found_callback(TrackFoundCallback cb) {
    impl_->track_cb = std::move(cb);
}

void LibraryScanner::set_track_found_with_cache_callback(
    TrackFoundWithCacheCallback cb) {
    impl_->track_with_cache_cb = std::move(cb);
}

void LibraryScanner::set_error_callback(ErrorCallback cb) {
    impl_->error_cb = std::move(cb);
}

void LibraryScanner::set_incremental_cache_path(const std::string& cache_path) {
    impl_->cache_path_ = cache_path;
}

bool LibraryScanner::is_audio_file(const std::string& path) {
    std::string ext = lower_ext(path);
    return std::find(audio_extensions().begin(), audio_extensions().end(), ext) != audio_extensions().end();
}

std::vector<std::string> LibraryScanner::get_audio_extensions() {
    return audio_extensions();
}

}  // namespace lollypop
