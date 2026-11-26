/**
 * Lollypop C++ - Fullscreen Queue Panel
 *
 * Owns only the queue surface: the revealer, framed scrolling list, and the
 * row actions. Like FullscreenLyricsPanel it is an overlay sibling of
 * FullscreenPlayerPanel, so revealing it never resizes the player. Lyrics and
 * queue share the same side slot and are shown one at a time.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lollypop {

class MainWindow;
class ILibraryService;
class TrackRow;

class FullscreenQueuePanel {
public:
    FullscreenQueuePanel(MainWindow* main_window,
                         ILibraryService* library_service);
    ~FullscreenQueuePanel();

    FullscreenQueuePanel(const FullscreenQueuePanel&) = delete;
    FullscreenQueuePanel& operator=(const FullscreenQueuePanel&) = delete;

    GtkWidget* get_widget() const { return revealer_; }

    void set_visible(bool visible);
    void set_width(int width);
    void set_right_margin(int margin);
    void set_queue(const std::vector<std::string>& queue_track_ids);

private:
    MainWindow* main_window_ = nullptr;
    ILibraryService* library_service_ = nullptr;
    GtkWidget* revealer_ = nullptr;
    GtkWidget* frame_ = nullptr;
    GtkWidget* queue_view_ = nullptr;

    // Row callbacks outlive a click; keep the panel alive until idle actions
    // have run and guard against callbacks arriving after destruction.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    // Retain TrackRow wrappers while their widgets are parented in the list.
    std::vector<std::unique_ptr<TrackRow>> queue_rows_;

    int last_width_ = 0;
    int last_right_margin_ = -1;

    void schedule_action(std::function<void()> action);
};

} // namespace lollypop
