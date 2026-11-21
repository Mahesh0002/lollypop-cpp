/**
 * Lollypop C++ - PlaylistItem
 *
 * Playlist card used by the Playlists page.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>

#include "types.hpp"

namespace lollypop {

class MainWindow;

class PlaylistItem {
public:
    using ClickCallback = std::function<void(const Playlist&)>;
    using PlayCallback = std::function<void(const Playlist&)>;

    PlaylistItem(MainWindow* main_window, const Playlist& playlist);
    ~PlaylistItem();

    PlaylistItem(const PlaylistItem&) = delete;
    PlaylistItem& operator=(const PlaylistItem&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artwork_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> name_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> count_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_button_;

    MainWindow* main_window_;
    Playlist playlist_;
    ClickCallback click_cb_;
    PlayCallback play_cb_;

    void setup_ui();
    static void on_clicked(GtkButton* button, gpointer user_data);
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static gboolean on_enter(GtkEventControllerMotion* controller, double x, double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller, gpointer user_data);
};

} // namespace lollypop
