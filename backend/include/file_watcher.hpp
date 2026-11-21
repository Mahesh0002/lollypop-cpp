/**
 * Lollypop C++ - File Watcher
 * Watches library directories for changes using inotify (Linux).
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

enum class FileEventType {
    Created,
    Modified,
    Deleted,
    Moved,
    Rescan,  // Full rescan needed (e.g. after inotify queue overflow)
};

struct FileEvent {
    FileEventType type;
    std::string path;
    std::string dest_path;  // For move events (destination)
    bool is_dir = false;
};

class FileWatcher {
public:
    using EventCallback = std::function<void(const FileEvent&)>;

    FileWatcher();
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    bool add_watch(const std::string& path, bool recursive = true);
    void remove_watch(const std::string& path);
    void remove_all_watches();

    void set_event_callback(EventCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
