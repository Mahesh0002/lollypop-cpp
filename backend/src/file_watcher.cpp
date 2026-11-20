/**
 * Lollypop C++ - File Watcher Implementation
 * inotify-based directory watching on Linux.
 */

#include "file_watcher.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace lollypop {

struct FileWatcher::Impl {
    int inotify_fd = -1;
    std::thread watch_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;
    EventCallback event_cb;

    void run() {
        char buffer[4096];
        while (running) {
            ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
            if (len < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::cerr << "FileWatcher: inotify read error: " << errno << std::endl;
                break;
            }
            if (len == 0) break;

            size_t i = 0;
            while (i + sizeof(inotify_event) <= static_cast<size_t>(len)) {
                const inotify_event* event =
                    reinterpret_cast<const inotify_event*>(buffer + i);

                std::string base_path;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it = wd_to_path.find(event->wd);
                    if (it != wd_to_path.end()) base_path = it->second;
                }

                std::string full_path;
                if (!base_path.empty() && event->len > 0) {
                    full_path = base_path + "/" + event->name;
                } else if (!base_path.empty()) {
                    full_path = base_path;
                }

                if (!full_path.empty()) {
                    FileEventType type = FileEventType::Created;
                    if (event->mask & IN_DELETE || event->mask & IN_DELETE_SELF)
                        type = FileEventType::Deleted;
                    else if (event->mask & IN_MODIFY)
                        type = FileEventType::Modified;
                    else if (event->mask & IN_MOVED_FROM)
                        type = FileEventType::Moved;
                    else if (event->mask & IN_MOVED_TO)
                        type = FileEventType::Moved;

                    if (event_cb) {
                        FileEvent fe{type, full_path};
                        if (event->mask & IN_MOVED_TO && event->cookie > 0) {
                            // Will pair with MOVED_FROM; destination filled below.
                        }
                        event_cb(fe);
                    }
                }

                i += sizeof(inotify_event) + event->len;
            }
        }
    }
};

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {
    impl_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->inotify_fd < 0) {
        throw std::runtime_error("Failed to initialize inotify");
    }
    impl_->running = true;
    impl_->watch_thread = std::thread(&Impl::run, impl_.get());
}

FileWatcher::~FileWatcher() {
    impl_->running = false;
    if (impl_->inotify_fd >= 0) close(impl_->inotify_fd);
    if (impl_->watch_thread.joinable()) impl_->watch_thread.join();
}

bool FileWatcher::add_watch(const std::string& path, bool recursive) {
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE | IN_ATTRIB;
    int wd = inotify_add_watch(impl_->inotify_fd, path.c_str(), mask);
    if (wd < 0) {
        std::cerr << "FileWatcher: failed to watch " << path << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wd_to_path[wd] = path;
    impl_->path_to_wd[path] = wd;

    if (recursive) {
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(path, ec)) {
            if (ec) continue;
            if (entry.is_directory(ec)) {
                int sub_wd =
                    inotify_add_watch(impl_->inotify_fd, entry.path().c_str(), mask);
                if (sub_wd >= 0) {
                    impl_->wd_to_path[sub_wd] = entry.path().string();
                    impl_->path_to_wd[entry.path().string()] = sub_wd;
                }
            }
        }
    }
    return true;
}

void FileWatcher::remove_watch(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->path_to_wd.find(path);
    if (it != impl_->path_to_wd.end()) {
        inotify_rm_watch(impl_->inotify_fd, it->second);
        impl_->wd_to_path.erase(it->second);
        impl_->path_to_wd.erase(it);
    }
}

void FileWatcher::remove_all_watches() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& [wd, _] : impl_->wd_to_path) {
        inotify_rm_watch(impl_->inotify_fd, wd);
    }
    impl_->wd_to_path.clear();
    impl_->path_to_wd.clear();
}

void FileWatcher::set_event_callback(EventCallback cb) {
    impl_->event_cb = std::move(cb);
}

}  // namespace lollypop
