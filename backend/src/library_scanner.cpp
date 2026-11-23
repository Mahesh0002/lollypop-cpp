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

void extract_artwork(TagLib::File* file, Track& track, const std::string& key) {
    if (!file) return;

    // ID3v2 APIC picture (MP3 and other ID3v2-tagged formats)
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto* id3v2 = mpeg->ID3v2Tag()) {
            TagLib::ID3v2::FrameList frames = id3v2->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                if (auto* pic =
                        dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(
                            frames.front())) {
                    track.artwork_path = save_artwork(
                        pic->picture(), pic->mimeType().to8Bit(true), key);
                }
            }
        }
    }

    // FLAC PICTURE metadata blocks
    if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
        const auto pictures = flac->pictureList();
        if (!pictures.isEmpty()) {
            const TagLib::FLAC::Picture* picture = pictures.front();
            track.artwork_path = save_artwork(
                picture->data(), picture->mimeType().to8Bit(true), key);
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
                track.artwork_path =
                    save_artwork(cover.data(), mime, key);
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
                    track.artwork_path = save_artwork(
                        picture.data(), picture.mimeType().to8Bit(true), key);
                }
            }
        }
    }
}

}  // namespace

struct LibraryScanner::Impl {
    ScanProgress progress;
    ProgressCallback progress_cb;
    TrackFoundCallback track_cb;
    ErrorCallback error_cb;
    std::atomic<bool> cancelled{false};

    // Incremental scan cache: file_path -> (mtime, size).
    // Persisted to disk to survive restarts.
    struct FileMeta {
        std::time_t mtime = 0;
        std::uintmax_t size = 0;
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

    // Load incremental scan cache from disk.
    void load_cache() {
        if (cache_path_.empty()) return;
        std::ifstream in(cache_path_);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            // Format: path|mtime|size
            size_t pos1 = line.find('|');
            if (pos1 == std::string::npos) continue;
            size_t pos2 = line.find('|', pos1 + 1);
            if (pos2 == std::string::npos) continue;
            std::string path = line.substr(0, pos1);
            std::string mtime_str = line.substr(pos1 + 1, pos2 - pos1 - 1);
            std::string size_str = line.substr(pos2 + 1);
            try {
                FileMeta meta;
                meta.mtime = std::stoll(mtime_str);
                meta.size = std::stoull(size_str);
                std::lock_guard<std::mutex> lock(cache_mutex_);
                scan_cache_[std::move(path)] = meta;
            } catch (...) {
                // Skip malformed lines
            }
        }
    }

    // Save incremental scan cache to disk.
    void save_cache() {
        if (cache_path_.empty()) return;
        std::ofstream out(cache_path_ + ".tmp");
        if (!out) return;
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& [path, meta] : scan_cache_) {
            out << path << '|' << meta.mtime << '|' << meta.size << '\n';
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

    // Update cache entry for a file after successful processing.
    void update_cache(const std::string& file_path) {
        if (cache_path_.empty()) return;
        std::error_code ec;
        std::time_t mtime = std::chrono::system_clock::to_time_t(
            std::chrono::clock_cast<std::chrono::system_clock>(
                std::filesystem::last_write_time(file_path, ec)));
        if (ec) return;
        std::uintmax_t size = std::filesystem::file_size(file_path, ec);
        if (ec) return;

        std::lock_guard<std::mutex> lock(cache_mutex_);
        scan_cache_[file_path] = FileMeta{mtime, size};
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
                    // File unchanged, but we still need to emit track_found
                    // with the existing track data. Since we don't have it here,
                    // we just count it as processed.
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
        extract_artwork(file.file(), track, artwork_key);

        if (track_cb) track_cb(track);

        // Update incremental scan cache on successful processing.
        update_cache(file_path);
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
