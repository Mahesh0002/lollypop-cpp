/**
 * Lollypop C++ - AlbumCard
 *
 * Apple Music-style album tile: square artwork followed by a single-line
 * album title and artist. Used by Albums and Favourite Albums pages.
 * Single click opens the album detail page; the artwork loads asynchronously
 * and falls back to a music glyph when no cover is available.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>

#include "types.hpp"
#include "ui/components/ArtworkView.hpp"

namespace lollypop {

class MainWindow;

class AlbumCard {
public:
    static constexpr int kArtworkSize = 150;

    using PlayCallback = std::function<void(const Album&)>;
    using ClickCallback = std::function<void(const Album&)>;
    using AlbumActionCallback = std::function<void(const Album&, const char* action)>;

    AlbumCard(MainWindow* main_window, const Album& album);
    ~AlbumCard();

    AlbumCard(const AlbumCard&) = delete;
    AlbumCard& operator=(const AlbumCard&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_album_action_callback(AlbumActionCallback cb) { album_action_cb_ = std::move(cb); }

    void set_active(bool active);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;

    MainWindow* main_window_;
    Album album_;
    PlayCallback play_cb_;
    ClickCallback click_cb_;
    AlbumActionCallback album_action_cb_;
    bool active_ = false;

    void setup_ui();
    void create_context_menu();
    static gboolean on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data);
    static gboolean on_pressed(GtkGestureClick* gesture, int n_press, double x,
                               double y, gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press, double x,
                                double y, gpointer user_data);
    static gboolean on_key_pressed(GtkEventControllerKey* controller,
                                   guint keyval, guint keycode,
                                   GdkModifierType state, gpointer user_data);
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    GMenu* create_menu_model();
    void animate_hover(bool hover);
    void animate_press(bool press);
};

} // namespace lollypop
