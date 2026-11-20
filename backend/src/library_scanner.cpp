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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
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
    if (bytes.starts_with("\x89PNG")) return ".png";
    if (bytes.starts_with("GIF8")) return ".gif";
    if (bytes.starts_with("BM")) return ".bmp";
    return ".jpg";
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
}

}  // namespace

struct LibraryScanner::Impl {
    ScanProgress progress;
    ProgressCallback progress_cb;
    TrackFoundCallback track_cb;
    ErrorCallback error_cb;
    std::atomic<bool> cancelled{false};

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
                if (progress_cb) progress_cb(progress);

                process_file_safe(file_path.string());
                progress.files_processed++;
                if (progress_cb) progress_cb(progress);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "LibraryScanner: filesystem error: " << e.what() << std::endl;
        }
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

        Track track;
        track.id = generate_id();
        track.title = tag->title().to8Bit(true);
        track.artist = tag->artist().to8Bit(true);
        track.album = tag->album().to8Bit(true);
        track.genre = tag->genre().to8Bit(true);
        track.track_number = tag->track();
        track.year = tag->year();
        track.duration = props ? static_cast<int>(std::round(props->lengthInSeconds())) : 0;
        track.file_path = file_path;
        track.date_added = now_epoch_seconds();

        // Album artist is not exposed by TagLib::Tag; read it from the
        // format-independent property map.
        if (file.file()) {
            const TagLib::PropertyMap properties = file.file()->properties();
            if (properties.contains("ALBUMARTIST")) {
                const TagLib::StringList values = properties.value("ALBUMARTIST");
                if (!values.isEmpty()) {
                    track.album_artist = values.front().to8Bit(true);
                }
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
    }

    std::string generate_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        const char* hex = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) {
            if (i == 8 || i == 12 || i == 16 || i == 20) ss << '-';
            ss << hex[dis(gen)];
        }
        return ss.str();
    }
};

LibraryScanner::LibraryScanner() : impl_(std::make_unique<Impl>()) {}
LibraryScanner::~LibraryScanner() = default;

void LibraryScanner::scan_directory(const std::string& path, bool recursive) {
    impl_->progress = ScanProgress{};
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
            if (ec) continue;
            if (!it->is_regular_file()) continue;
            std::string ext = lower_ext(it->path().string());
            if (!is_audio_file(ext)) continue;
            impl_->progress.files_found++;
            impl_->progress.current_file = it->path().string();
            if (impl_->progress_cb) impl_->progress_cb(impl_->progress);
            impl_->process_file_safe(it->path().string());
            impl_->progress.files_processed++;
            if (impl_->progress_cb) impl_->progress_cb(impl_->progress);
        }
    }
}

void LibraryScanner::scan_file(const std::string& path) {
    impl_->cancelled = false;
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
    if (impl_->progress_cb) impl_->progress_cb(impl_->progress);
    impl_->process_file_safe(path);
    impl_->progress.files_processed++;
    if (impl_->progress_cb) impl_->progress_cb(impl_->progress);
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

bool LibraryScanner::is_audio_file(const std::string& path) {
    std::string ext = lower_ext(path);
    return std::find(audio_extensions().begin(), audio_extensions().end(), ext) != audio_extensions().end();
}

std::vector<std::string> LibraryScanner::get_audio_extensions() {
    return audio_extensions();
}

}  // namespace lollypop
