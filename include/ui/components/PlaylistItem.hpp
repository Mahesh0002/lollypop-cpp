/**
 * Lollypop C++ - PlaylistItem
 *
 * Playlist card used by the Playlists page. Click opens the playlist; there
 * is no inline play button.
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
    using PlaylistActionCallback = std::function<void(const Playlist&, const char* action)>;

    PlaylistItem(MainWindow* main_window, const Playlist& playlist);
    ~PlaylistItem();

    PlaylistItem(const PlaylistItem&) = delete;
    PlaylistItem& operator=(const PlaylistItem&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_playlist_action_callback(PlaylistActionCallback cb);

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

    MainWindow* main_window_;
    Playlist playlist_;
    ClickCallback click_cb_;
    PlayCallback play_cb_;
    PlaylistActionCallback playlist_action_cb_;

    void setup_ui();
    void create_context_menu();
    GMenu* create_menu_model();
    static void on_clicked(GtkButton* button, gpointer user_data);
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
};

} // namespace lollypop
