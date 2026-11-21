/**
 * Lollypop C++ - File Watcher Implementation
 * inotify-based directory watching on Linux.
 */

#include "file_watcher.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lollypop {
namespace {

constexpr uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE |
                                IN_MOVED_FROM | IN_MOVED_TO |
                                IN_DELETE_SELF | IN_MOVE_SELF |
                                IN_Q_OVERFLOW;

}  // namespace

struct FileWatcher::Impl {
    int inotify_fd = -1;
    int event_fd = -1;  // Wake-up fd for clean shutdown
    std::thread watch_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;  // Protects wd_to_path / path_to_wd / event_cb
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
            std::error_code dir_ec;
            if (!it->is_directory(dir_ec) || dir_ec) {
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
              const std::string& dest = {}, bool is_dir = false) {
        EventCallback cb;
        {
            std::shared_lock lock(callback_mutex);
            cb = event_cb;
        }
        if (cb) cb(FileEvent{type, path, dest, is_dir});
    }

    // Protects event_cb replacement and invocation.
    std::shared_mutex callback_mutex;

    // Debounce state: batch repeated modifications of the same file.
    struct DebounceEntry {
        std::string path;
        bool is_dir = false;
        FileEventType type = FileEventType::Modified;
        std::chrono::steady_clock::time_point seen;
    };
    std::unordered_map<std::string, DebounceEntry> debounce;

    // Pending move pairing state.
    struct PendingMove {
        std::string path;
        std::chrono::steady_clock::time_point seen;
        bool is_dir = false;
    };
    std::unordered_map<uint32_t, PendingMove> pending_moves;

    static constexpr std::chrono::milliseconds kDebounceMs{150};

    void run() {
        char buffer[16384] __attribute__((aligned(__alignof__(inotify_event))));
        // MOVED_FROM and MOVED_TO are usually delivered in the same read, but
        // that is not guaranteed. Keep unmatched moves briefly instead of
        // immediately reporting a delete followed by a create.

        while (running) {
            pollfd poll_fds[2];
            poll_fds[0].fd = inotify_fd;
            poll_fds[0].events = POLLIN;
            poll_fds[1].fd = event_fd;
            poll_fds[1].events = POLLIN;

            const int ready = poll(poll_fds, 2, 200);
            if (ready <= 0) {
                // Timeout: flush any pending debounced events.
                flush_debounce(debounce);
                continue;
            }

            if (poll_fds[1].revents & POLLIN) {
                // Shutdown requested via eventfd.
                break;
            }

            if (!(poll_fds[0].revents & POLLIN)) continue;

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

                if (event->mask & IN_Q_OVERFLOW) {
                    // Buffer overflow: we missed events. Signal a full rescan
                    // of all watched directories instead of deleting an
                    // arbitrary path.
                    events.push_back(FileEvent{
                        FileEventType::Rescan, {}, {}, true});
                    i += event_size;
                    continue;
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
                        pending_moves[event->cookie] = PendingMove{
                            full_path, std::chrono::steady_clock::now(), is_dir};
                    } else if (event->mask & IN_MOVED_TO) {
                        auto pending = pending_moves.find(event->cookie);
                        if (pending != pending_moves.end()) {
                            events.push_back(FileEvent{FileEventType::Moved,
                                                       pending->second.path, full_path,
                                                       pending->second.is_dir});
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
                            events.push_back(FileEvent{FileEventType::Created,
                                                       full_path, {}, true});
                        } else {
                            debounce_or_emit(events, debounce, full_path, is_dir,
                                             FileEventType::Created);
                        }
                    } else if (event->mask & (IN_DELETE | IN_DELETE_SELF)) {
                        events.push_back(FileEvent{FileEventType::Deleted,
                                                   full_path, {}, is_dir});
                        if (is_dir) {
                            // Directory removed: drop all watches under it.
                            std::lock_guard<std::mutex> lock(mutex);
                            remove_watches_under_locked(full_path);
                        }
                    } else if (event->mask & IN_CLOSE_WRITE) {
                        debounce_or_emit(events, debounce, full_path, is_dir,
                                         FileEventType::Modified);
                    } else if (event->mask & IN_MOVE_SELF) {
                        // The watched directory itself was moved. The old watch
                        // is auto-removed by the kernel; re-add a watch for the
                        // new location and report Created so the new path is
                        // scanned. (Moved directories stop being monitored
                        // otherwise.)
                        std::lock_guard<std::mutex> lock(mutex);
                        auto it = wd_to_path.find(event->wd);
                        if (it != wd_to_path.end()) {
                            path_to_wd.erase(it->second);
                            wd_to_path.erase(it);
                        }
                        if (is_dir) {
                            add_watch_locked(full_path, true);
                            events.push_back(FileEvent{FileEventType::Created,
                                                       full_path, {}, true});
                        }
                    }
                }

                i += event_size;
            }

            // A MOVED_FROM without a matching MOVED_TO after one second has
            // left the watched tree (or the event was lost): treat it as a
            // deletion.
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

            // Flush debounced modifications that have been quiet long enough.
            flush_debounce(debounce, events);

            for (const auto& event : events) {
                emit(event.type, event.path, event.dest_path, event.is_dir);
            }
        }

        // Flush remaining debounced events before exit.
        flush_debounce(debounce);
    }

    void debounce_or_emit(std::vector<FileEvent>& events,
                          std::unordered_map<std::string, DebounceEntry>& debounce,
                          const std::string& path, bool is_dir,
                          FileEventType type) {
        auto now = std::chrono::steady_clock::now();
        auto it = debounce.find(path);
        if (it != debounce.end()) {
            // Update the existing entry; it will be emitted after quiet period.
            it->second.seen = now;
            it->second.is_dir = is_dir;
            it->second.type = type;
            return;
        }
        debounce[path] = DebounceEntry{path, is_dir, type, now};
    }

    void flush_debounce(std::unordered_map<std::string, DebounceEntry>& debounce,
                        std::vector<FileEvent>& events) {
        auto now = std::chrono::steady_clock::now();
        for (auto it = debounce.begin(); it != debounce.end();) {
            if (now - it->second.seen > kDebounceMs) {
                events.push_back(FileEvent{it->second.type,
                                           it->second.path, {},
                                           it->second.is_dir});
                it = debounce.erase(it);
            } else {
                ++it;
            }
        }
    }

    void flush_debounce(std::unordered_map<std::string, DebounceEntry>& debounce) {
        std::vector<FileEvent> events;
        flush_debounce(debounce, events);
        for (const auto& event : events) {
            emit(event.type, event.path, event.dest_path, event.is_dir);
        }
    }

    void remove_watches_under_locked(const std::string& path) {
        for (auto it = path_to_wd.begin(); it != path_to_wd.end();) {
            if (it->first == path || it->first.starts_with(path + "/")) {
                inotify_rm_watch(inotify_fd, it->second);
                wd_to_path.erase(it->second);
                it = path_to_wd.erase(it);
            } else {
                ++it;
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
    impl_->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (impl_->event_fd < 0) {
        std::cerr << "FileWatcher: failed to create eventfd: "
                  << std::strerror(errno) << std::endl;
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
        return;
    }
    impl_->running = true;
    impl_->watch_thread = std::thread(&Impl::run, impl_.get());
}

FileWatcher::~FileWatcher() {
    // Wake up the watcher thread via eventfd, then join.
    if (impl_->running) {
        impl_->running = false;
        if (impl_->event_fd >= 0) {
            uint64_t val = 1;
            ssize_t written = ::write(impl_->event_fd, &val, sizeof(val));
            (void)written;
        }
    }
    if (impl_->watch_thread.joinable()) impl_->watch_thread.join();
    if (impl_->inotify_fd >= 0) {
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }
    if (impl_->event_fd >= 0) {
        ::close(impl_->event_fd);
        impl_->event_fd = -1;
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
    std::lock_guard<std::shared_mutex> lock(impl_->callback_mutex);
    impl_->event_cb = std::move(cb);
}

}  // namespace lollypop
