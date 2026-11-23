/**
 * Lollypop C++ - Library Scanner
 * Recursively scans directories for audio files and extracts metadata via taglib.
 * Supports incremental scanning: skips unchanged files using mtime + size.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <filesystem>

#include "types.hpp"

namespace lollypop {

struct ScanProgress {
    size_t files_found = 0;
    size_t files_processed = 0;
    std::string current_file;
    bool cancelled = false;
};

class LibraryScanner {
public:
    using ProgressCallback = std::function<void(const ScanProgress&)>;
    using TrackFoundCallback = std::function<void(const Track&)>;
    using ErrorCallback = std::function<void(const std::string& file, const std::string& error)>;

    LibraryScanner();
    ~LibraryScanner();

    LibraryScanner(const LibraryScanner&) = delete;
    LibraryScanner& operator=(const LibraryScanner&) = delete;

    // Scan a directory. If recursive is false, only the top-level directory is scanned.
    void scan_directory(const std::string& path, bool recursive = true);
    void scan_file(const std::string& path);
    void cancel();
    void reset_cancellation();
    bool is_cancelled() const;

    void set_progress_callback(ProgressCallback cb);
    void set_track_found_callback(TrackFoundCallback cb);
    void set_error_callback(ErrorCallback cb);

    static bool is_audio_file(const std::string& path);
    static std::vector<std::string> get_audio_extensions();

    // Incremental scanning: set a cache path to persist mtime+size metadata.
    // If empty, incremental scanning is disabled (full scan every time).
    void set_incremental_cache_path(const std::string& cache_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
