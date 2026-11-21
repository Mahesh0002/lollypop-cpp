/**
 * Lollypop C++ - LibraryManager Implementation
 */

#include "library_manager.hpp"

#include "manager.hpp"

#include <chrono>
#include <iostream>

namespace lollypop {

LibraryManager::LibraryManager() = default;

LibraryManager::~LibraryManager() {
    shutdown();
}

bool LibraryManager::initialize(StorageManager* storage,
                                 LibraryScanner* scanner,
                                 std::size_t max_pending_jobs) {
    storage_ = storage;
    scanner_ = scanner;
    max_pending_jobs_ = max_pending_jobs;

    if (!scanner_) {
        std::cerr << "LibraryManager: no scanner provided" << std::endl;
        return false;
    }

    scanner_->set_track_found_callback([this](const Track& track) {
        if (track_cb_) track_cb_(track);
    });
    scanner_->set_error_callback([this](const std::string& file,
                                         const std::string& error) {
        if (error_cb_) error_cb_(file, error);
    });

    running_ = true;
    worker_thread_ = std::thread(&LibraryManager::worker_loop, this);
    return true;
}

void LibraryManager::shutdown() {
    if (!running_) return;
    running_ = false;
    cancel_requested_ = true;
    // Scanner cancellation is handled by the owner (BackendController)
    // to avoid raw-pointer lifetime issues.
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) worker_thread_.join();
}

void LibraryManager::set_scan_progress_callback(ScanProgressCallback cb) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    progress_cb_ = std::move(cb);
    scanner_->set_progress_callback(progress_cb_);
}

void LibraryManager::set_scan_finished_callback(ScanFinishedCallback cb) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    finished_cb_ = std::move(cb);
}

void LibraryManager::set_track_found_callback(TrackFoundCallback cb) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    track_cb_ = std::move(cb);
}

void LibraryManager::set_error_callback(ErrorCallback cb) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    error_cb_ = std::move(cb);
}

bool LibraryManager::scan_directory(const std::string& path, bool recursive) {
    if (path.empty()) return false;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!running_) return false;

    // A new scan request clears any prior cancellation state.
    cancel_requested_ = false;

    // Coalesce duplicates: one pending job per path.
    if (pending_paths_.count(path)) return true;
    if (queue_.size() >= max_pending_jobs_) return false;

    pending_paths_.insert(path);
    queue_.push(ScanJob{ScanJob::Type::Directory, path, recursive});
    queue_cv_.notify_one();
    return true;
}

bool LibraryManager::scan_file(const std::string& path) {
    if (path.empty()) return false;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!running_) return false;

    cancel_requested_ = false;

    if (pending_paths_.count(path)) return true;
    if (queue_.size() >= max_pending_jobs_) return false;

    pending_paths_.insert(path);
    queue_.push(ScanJob{ScanJob::Type::File, path, false});
    queue_cv_.notify_one();
    return true;
}

bool LibraryManager::reconcile(const std::string& path) {
    if (path.empty()) return false;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!running_) return false;

    cancel_requested_ = false;

    // Reconciliation is a full rescan; coalesce too.
    if (pending_paths_.count(path)) return true;
    if (queue_.size() >= max_pending_jobs_) return false;

    pending_paths_.insert(path);
    queue_.push(ScanJob{ScanJob::Type::Reconcile, path, true});
    queue_cv_.notify_one();
    return true;
}

void LibraryManager::cancel() {
    cancel_requested_ = true;
    // Scanner cancellation is handled by the owner (BackendController)
    // to avoid raw-pointer lifetime issues.
}

bool LibraryManager::is_scanning() const {
    return scanning_;
}

void LibraryManager::worker_loop() {
    while (running_) {
        ScanJob job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !queue_.empty();
            });
            if (!running_ && queue_.empty()) return;
            if (queue_.empty()) continue;
            job = std::move(queue_.front());
            queue_.pop();
            pending_paths_.erase(job.path);
        }

        if (cancel_requested_.load()) {
            // Cancelled: drain (skip) all remaining jobs without processing,
            // then exit. Do not clear cancel_requested_ here; a new scan
            // request (scan_directory/scan_file) resets it.
            continue;
        }

        scanning_ = true;
        process_job(job);
        scanning_ = false;
    }
}

void LibraryManager::process_job(const ScanJob& job) {
    if (!scanner_) return;

    bool cancelled = false;

    // Batch all track writes for this job into a single storage transaction.
    if (storage_) {
        storage_->begin_transaction();
    }

    switch (job.type) {
        case ScanJob::Type::Directory: {
            std::cout << "LibraryManager: scanning directory " << job.path
                      << " (recursive=" << job.recursive << ")" << std::endl;
            scanner_->scan_directory(job.path, job.recursive);
            cancelled = scanner_->is_cancelled();
            break;
        }
        case ScanJob::Type::File: {
            std::cout << "LibraryManager: scanning file " << job.path
                      << std::endl;
            scanner_->scan_file(job.path);
            cancelled = scanner_->is_cancelled();
            break;
        }
        case ScanJob::Type::Reconcile: {
            std::cout << "LibraryManager: reconciling " << job.path << std::endl;
            // Reconciliation is a full rescan of the library root.
            scanner_->scan_directory(job.path, true);
            cancelled = scanner_->is_cancelled();
            break;
        }
    }

    if (storage_) {
        if (cancelled) {
            storage_->rollback_transaction();
        } else {
            storage_->commit_transaction();
        }
    }

    scanner_->reset_cancellation();

    if (finished_cb_) {
        finished_cb_(!cancelled, cancelled);
    }
}

}  // namespace lollypop
