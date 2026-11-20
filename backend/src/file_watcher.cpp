/**
 * Lollypop C++ - File Watcher Implementation
 * inotify-based directory watching on Linux.
 */

#include "file_watcher.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lollypop {
namespace {

constexpr uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE |
                                IN_MOVE | IN_DELETE_SELF | IN_MOVE_SELF;

}  // namespace

struct FileWatcher::Impl {
    int inotify_fd = -1;
    std::thread watch_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;
    EventCallback event_cb;

    // Must be called with mutex held.
    bool add_watch_locked(const std::string& path, bool recursive) {
        if (inotify_fd < 0) return false;

        const int wd = inotify_add_watch(inotify_fd, path.c_str(), kWatchMask);
        if (wd < 0) {
            std::cerr << "FileWatcher: failed to watch " << path << ": "
                      << std::strerror(errno) << std::endl;
            return false;
        }
        wd_to_path[wd] = path;
        path_to_wd[path] = wd;

        if (!recursive) return true;

        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            path, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return true;

        for (; it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_directory(ec) || ec) {
                ec.clear();
                continue;
            }
            const std::string sub_path = it->path().string();
            const int sub_wd =
                inotify_add_watch(inotify_fd, sub_path.c_str(), kWatchMask);
            if (sub_wd >= 0) {
                wd_to_path[sub_wd] = sub_path;
                path_to_wd[sub_path] = sub_wd;
            }
        }
        return true;
    }

    void emit(FileEventType type, const std::string& path,
              const std::string& dest = {}) {
        if (!event_cb) return;
        event_cb(FileEvent{type, path, dest});
    }

    void run() {
        char buffer[8192] __attribute__((aligned(__alignof__(inotify_event))));
        struct PendingMove {
            std::string path;
            std::chrono::steady_clock::time_point seen;
            bool is_dir = false;
        };
        // MOVED_FROM and MOVED_TO are usually delivered in the same read, but
        // that is not guaranteed. Keep unmatched moves briefly instead of
        // immediately reporting a delete followed by a create.
        std::unordered_map<uint32_t, PendingMove> pending_moves;

        while (running) {
            pollfd poll_fd{inotify_fd, POLLIN, 0};
            const int ready = poll(&poll_fd, 1, 200);
            if (ready <= 0) continue;
            if (!(poll_fd.revents & POLLIN)) continue;

            const ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
            if (len < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (running) {
                    std::cerr << "FileWatcher: inotify read error: "
                              << std::strerror(errno) << std::endl;
                }
                break;
            }
            if (len == 0) break;

            std::vector<FileEvent> events;
            size_t i = 0;
            while (i + sizeof(inotify_event) <= static_cast<size_t>(len)) {
                const auto* event =
                    reinterpret_cast<const inotify_event*>(buffer + i);
                const size_t event_size = sizeof(inotify_event) + event->len;

                std::string base_path;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it = wd_to_path.find(event->wd);
                    if (it != wd_to_path.end()) base_path = it->second;
                }

                std::string full_path;
                if (!base_path.empty()) {
                    if (event->len > 0) {
                        full_path = base_path + "/" + event->name;
                    } else {
                        full_path = base_path;
                    }
                }

                if (event->mask & IN_IGNORED) {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (auto it = wd_to_path.find(event->wd);
                        it != wd_to_path.end()) {
                        path_to_wd.erase(it->second);
                        wd_to_path.erase(it);
                    }
                    i += event_size;
                    continue;
                }

                if (!full_path.empty()) {
                    const bool is_dir = (event->mask & IN_ISDIR) != 0;

                    if (event->mask & IN_MOVED_FROM) {
                        // Pair with MOVED_TO (same cookie) below so the
                        // controller can delete the old path and scan the new.
                        pending_moves[event->cookie] = PendingMove{
                            full_path, std::chrono::steady_clock::now(), is_dir};
                    } else if (event->mask & IN_MOVED_TO) {
                        auto pending = pending_moves.find(event->cookie);
                        if (pending != pending_moves.end()) {
                            events.push_back(FileEvent{FileEventType::Moved,
                                                       pending->second.path, full_path,
                                                       is_dir});
                            pending_moves.erase(pending);
                        } else {
                            events.push_back(FileEvent{FileEventType::Created,
                                                       full_path, {}, is_dir});
                        }
                        if (is_dir) {
                            std::lock_guard<std::mutex> lock(mutex);
                            add_watch_locked(full_path, true);
                        }
                    } else if (event->mask & IN_CREATE) {
                        if (is_dir) {
                            std::lock_guard<std::mutex> lock(mutex);
                            add_watch_locked(full_path, true);
                        } else {
                            events.push_back(FileEvent{FileEventType::Created,
                                                       full_path, {}, is_dir});
                        }
                    } else if (event->mask & (IN_DELETE | IN_DELETE_SELF)) {
                        events.push_back(FileEvent{FileEventType::Deleted,
                                                   full_path, {}, is_dir});
                    } else if (event->mask & IN_CLOSE_WRITE) {
                        events.push_back(FileEvent{FileEventType::Modified,
                                                   full_path, {}});
                    }
                }

                i += event_size;
            }

            // A MOVED_FROM without a matching MOVED_TO after one second has
            // left the watched tree (or the event was lost): treat it as a
            // deletion. The delay gives MOVED_TO a chance to arrive.
            const auto now = std::chrono::steady_clock::now();
            for (auto it = pending_moves.begin(); it != pending_moves.end();) {
                if (now - it->second.seen > std::chrono::seconds(1)) {
                    events.push_back(FileEvent{FileEventType::Deleted,
                                               it->second.path, {},
                                               it->second.is_dir});
                    it = pending_moves.erase(it);
                } else {
                    ++it;
                }
            }

            for (const auto& event : events) {
                if (!event_cb) continue;
                event_cb(event);
            }
        }
    }
};

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {
    impl_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->inotify_fd < 0) {
        std::cerr << "FileWatcher: failed to initialize inotify: "
                  << std::strerror(errno) << std::endl;
        return;
    }
    impl_->running = true;
    impl_->watch_thread = std::thread(&Impl::run, impl_.get());
}

FileWatcher::~FileWatcher() {
    impl_->running = false;
    // Join before closing the descriptor: closing an fd that the watcher
    // thread is still polling can race with fd reuse in another thread.
    if (impl_->watch_thread.joinable()) impl_->watch_thread.join();
    if (impl_->inotify_fd >= 0) {
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }
}

bool FileWatcher::add_watch(const std::string& path, bool recursive) {
    if (impl_->inotify_fd < 0) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->add_watch_locked(path, recursive);
}

void FileWatcher::remove_watch(const std::string& path) {
    if (impl_->inotify_fd < 0) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::vector<int> to_remove;
    for (auto it = impl_->path_to_wd.begin(); it != impl_->path_to_wd.end();) {
        const std::string& watched = it->first;
        if (watched == path || watched.starts_with(path + "/")) {
            to_remove.push_back(it->second);
            it = impl_->path_to_wd.erase(it);
        } else {
            ++it;
        }
    }
    for (int wd : to_remove) {
        inotify_rm_watch(impl_->inotify_fd, wd);
        impl_->wd_to_path.erase(wd);
    }
}

void FileWatcher::remove_all_watches() {
    if (impl_->inotify_fd < 0) return;
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
