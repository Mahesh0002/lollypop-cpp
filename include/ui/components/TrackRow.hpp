/**
 * Lollypop C++ - TrackRow
 *
 * Shared track row component used by Songs, Album details, Artist details,
 * Liked Music, and Queue pages. Activating the row starts playback; there is
 * no inline play/pause button. Spring animations for hover/press states.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>

#include "types.hpp"
#include "ui/animation/SpringAnimation.hpp"

namespace lollypop {

class MainWindow;

// Direct children tagged with one of these roles are laid out by
// create_track_columns_layout(). Title/Artist/Album receive 20/30/25 of the
// available column space; every other role keeps its natural width.
enum class TrackColumnRole : int {
    Fixed = 0,
    Title = 1,
    Artist = 2,
    Album = 3,
};

void set_track_column_role(GtkWidget* widget, TrackColumnRole role);
GtkLayoutManager* create_track_columns_layout();

class TrackRow {
public:
    using PlayCallback = std::function<void(const Track&)>;
    using LikeCallback = std::function<void(const Track&, bool)>;

    TrackRow(MainWindow* main_window, const Track& track, int track_number);
    ~TrackRow();

    TrackRow(const TrackRow&) = delete;
    TrackRow& operator=(const TrackRow&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_like_callback(LikeCallback cb) { like_cb_ = std::move(cb); }

    void set_playing(bool playing);
    void set_liked(bool liked);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> number_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> album_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> duration_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> like_button_;

    MainWindow* main_window_;
    Track track_;
    int track_number_;
    PlayCallback play_cb_;
    LikeCallback like_cb_;
    bool playing_ = false;
    bool liked_ = false;

    void setup_ui();
    static void on_like_clicked(GtkButton* button, gpointer user_data);
    static void on_row_activated(GtkListBoxRow* row, gpointer user_data);
    static gboolean on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data);
    static gboolean on_pressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    void animate_hover(bool hover);
    void animate_press(bool press);
};

} // namespace lollypop
