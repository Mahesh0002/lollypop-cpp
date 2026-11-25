/**
 * Lollypop C++ - LibraryManager
 *
 * Library scan coordination. LibraryScanner does the directory traversal and
 * TagLib extraction; LibraryManager owns:
 *
 *   - A unique FIFO work queue of pending scan paths (path -> pending hash
 *     set, so one pending scan job per path).
 *   - Coordination between FileWatcher events and LibraryScanner.
 *   - Batch commits to StorageManager through a single writer.
 *
 * LibraryManager does not touch GStreamer, GTK, or MPRIS. It is the bridge
 * between filesystem events/scans and the database.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "library_scanner.hpp"
#include "types.hpp"

namespace lollypop {

// Periodic scan state for automatic incremental library scanning.
struct PeriodicScanState {
    std::atomic<bool> enabled{false};
    std::chrono::minutes interval{30};
    std::mutex roots_mutex;
    std::unordered_set<std::string> roots;
    std::thread timer_thread;
    std::atomic<bool> timer_running{false};
    std::condition_variable timer_cv;
    std::mutex timer_mutex;
};

}  // namespace lollypop

namespace lollypop {

class StorageManager;

// A single pending scan job.
struct ScanJob {
    enum class Type {
        Directory,   // scan_directory(path, recursive)
        File,        // scan_file(path)
        Reconcile,   // reconcile a library root after IN_Q_OVERFLOW
    };

    Type type = Type::Directory;
    std::string path;
    bool recursive = true;
};

// LibraryManager is single-writer: all database writes happen on its worker
// thread, so StorageManager is only touched from one thread.
class LibraryManager {
public:
    using ScanProgressCallback = std::function<void(const ScanProgress&)>;
    using ScanFinishedCallback = std::function<void(bool success, bool cancelled)>;
    using TrackFoundCallback = std::function<void(const Track&)>;
    using TrackFoundWithCacheCallback =
        LibraryScanner::TrackFoundWithCacheCallback;
    using ErrorCallback = std::function<void(const std::string& file,
                                              const std::string& error)>;

    LibraryManager();
    ~LibraryManager();

    LibraryManager(const LibraryManager&) = delete;
    LibraryManager& operator=(const LibraryManager&) = delete;

    // Configure storage and scanner; starts the worker thread.
    bool initialize(StorageManager* storage,
                    LibraryScanner* scanner,
                    std::size_t max_pending_jobs = 256);
    void shutdown();

    // Request a directory scan. Duplicates are coalesced via the pending
    // path set: the same path is never queued twice while a job for it is
    // already pending or running.
    bool scan_directory(const std::string& path, bool recursive = true);
    bool scan_file(const std::string& path);
    // Schedule a full reconciliation of a library root (used after
    // inotify queue overflow).
    bool reconcile(const std::string& path);

    void cancel();
    bool is_scanning() const;

    void set_scan_progress_callback(ScanProgressCallback cb);
    void set_scan_finished_callback(ScanFinishedCallback cb);
    void set_track_found_callback(TrackFoundCallback cb);
    void set_track_found_with_cache_callback(TrackFoundWithCacheCallback cb);
    void set_error_callback(ErrorCallback cb);

    // Periodic incremental scanning
    void set_periodic_scan_enabled(bool enabled);
    bool is_periodic_scan_enabled() const;
    void set_periodic_scan_interval(std::chrono::minutes interval);
    std::chrono::minutes get_periodic_scan_interval() const;

    // Called when a library root is added/removed (for periodic scanning)
    void add_library_root(const std::string& path);
    void remove_library_root(const std::string& path);
    std::vector<std::string> get_library_roots() const;

private:
    void worker_loop();
    void process_job(const ScanJob& job);

    // Periodic scan timer helpers
    void start_periodic_scan_timer();
    void stop_periodic_scan_timer();

    StorageManager* storage_ = nullptr;
    LibraryScanner* scanner_ = nullptr;

    std::queue<ScanJob> queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Pending path set: one pending job per path.
    std::unordered_set<std::string> pending_paths_;
    std::size_t max_pending_jobs_ = 256;

    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<bool> cancel_requested_{false};

    ScanProgressCallback progress_cb_;
    ScanFinishedCallback finished_cb_;
    TrackFoundCallback track_cb_;
    TrackFoundWithCacheCallback track_with_cache_cb_;
    ErrorCallback error_cb_;

    std::unique_ptr<PeriodicScanState> periodic_;
};

}  // namespace lollypop
