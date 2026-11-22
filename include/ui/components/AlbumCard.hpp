/**
 * Lollypop C++ - AlbumCard
 *
 * Album artwork card used by Albums and Favourite Albums pages.
 * Single click opens album detail page. Hover and keyboard focus
 * reveal Play button (plan Section 5.3).
 * Spring animations for hover/press states.
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

class AlbumCard {
public:
    using PlayCallback = std::function<void(const Album&)>;
    using ClickCallback = std::function<void(const Album&)>;

    AlbumCard(MainWindow* main_window, const Album& album);
    ~AlbumCard();

    AlbumCard(const AlbumCard&) = delete;
    AlbumCard& operator=(const AlbumCard&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }

    void set_active(bool active);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artwork_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_button_;

    MainWindow* main_window_;
    Album album_;
    PlayCallback play_cb_;
    ClickCallback click_cb_;
    bool active_ = false;

    void setup_ui();
    static void on_clicked(GtkButton* button, gpointer user_data);
    static void on_play_clicked(GtkButton* button, gpointer user_data);
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
