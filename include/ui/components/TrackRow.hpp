/**
 * Lollypop C++ - TrackRow
 *
 * Shared track row component used by Songs, Album details, Artist details,
 * Liked Music, and Queue pages (plan Sections 5.7, 5.9, 5.10, 5.11).
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>

#include "types.hpp"

namespace lollypop {

class MainWindow;

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
    std::unique_ptr<GtkWidget, WidgetDeleter> play_button_;

    MainWindow* main_window_;
    Track track_;
    int track_number_;
    PlayCallback play_cb_;
    LikeCallback like_cb_;
    bool playing_ = false;
    bool liked_ = false;

    void setup_ui();
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_like_clicked(GtkButton* button, gpointer user_data);
    static void on_row_activated(GtkListBoxRow* row, gpointer user_data);
    static gboolean on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data);
};

} // namespace lollypop
