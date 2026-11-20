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
#include <taglib/oggfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4coverart.h>
#include <taglib/apefile.h>

#include <chrono>
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

std::string save_artwork(const TagLib::ByteVector& data, const std::string& mime_type) {
    std::string dir = artwork_dir_path();
    ::mkdir(dir.c_str(), 0755);
    mkdir((dir + "/.").c_str(), 0755);

    std::stringstream ss;
    ss << dir << "/" << std::hex << (std::hash<std::string>()(mime_type) ^ std::random_device{}());
    std::string path = ss.str();

    std::string ext = ".jpg";
    if (mime_type == "image/png") ext = ".png";
    else if (mime_type == "image/gif") ext = ".gif";
    path += ext;

    std::ofstream out(path, std::ios::binary);
    if (!out) return {};
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return path;
}

void extract_artwork(TagLib::File* file, Track& track) {
    if (!file) return;

    // ID3v2 APIC picture (MP3 and other ID3v2-tagged formats)
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto* id3v2 = mpeg->ID3v2Tag()) {
            TagLib::ID3v2::FrameList frames = id3v2->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                if (auto* pic =
                        static_cast<TagLib::ID3v2::AttachedPictureFrame*>(
                            frames.front())) {
                    track.artwork_path =
                        save_artwork(pic->picture(), pic->mimeType().to8Bit(true));
                }
            }
        }
    }

    // TODO: FLAC (pictureList on FLAC::Tag), Ogg (Vorbis comment picture),
    // MP4 (cover art) artwork extraction can be added here once the
    // corresponding TagLib APIs are verified against the installed version.
}

}  // namespace

struct LibraryScanner::Impl {
    ScanProgress progress;
    ProgressCallback progress_cb;
    TrackFoundCallback track_cb;
    ErrorCallback error_cb;
    bool cancelled = false;

    void scan_recursive(const std::filesystem::path& path) {
        try {
            std::error_code ec;
            std::filesystem::directory_iterator it(path, ec);
            if (ec) {
                if (error_cb) error_cb(path.string(), ec.message());
                return;
            }

            for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (cancelled) break;
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

                process_file(file_path.string());
                progress.files_processed++;
                if (progress_cb) progress_cb(progress);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "LibraryScanner: filesystem error: " << e.what() << std::endl;
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
        // tag->albumArtist() is not available on the base Tag class in this
        // TagLib version; album_artist is left empty for now.
        track.genre = tag->genre().to8Bit(true);
        track.track_number = tag->track();
        track.year = tag->year();
        track.duration = props ? static_cast<int>(std::round(props->lengthInSeconds())) : 0;
        track.file_path = file_path;
        track.date_added = now_epoch_seconds();

        extract_artwork(file.file(), track);

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
    impl_->cancelled = false;
    impl_->progress = ScanProgress{};
    if (recursive) {
        impl_->scan_recursive(path);
    } else {
        // Non-recursive: only top-level files
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
            impl_->process_file(it->path().string());
            impl_->progress.files_processed++;
            if (impl_->progress_cb) impl_->progress_cb(impl_->progress);
        }
    }
}

void LibraryScanner::cancel() {
    impl_->cancelled = true;
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
