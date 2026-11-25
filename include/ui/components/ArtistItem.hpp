/**
 * Lollypop C++ - ArtistItem
 *
 * Artist card used by the Artists page.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>

#include "types.hpp"

namespace lollypop {

class MainWindow;

class ArtistItem {
public:
    using ClickCallback = std::function<void(const Artist&)>;
    using ArtistActionCallback = std::function<void(const Artist&, const char* action)>;

    ArtistItem(MainWindow* main_window, const Artist& artist);
    ~ArtistItem();

    ArtistItem(const ArtistItem&) = delete;
    ArtistItem& operator=(const ArtistItem&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_artist_action_callback(ArtistActionCallback cb);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artwork_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> name_label_;

    MainWindow* main_window_;
    Artist artist_;
    ClickCallback click_cb_;
    ArtistActionCallback artist_action_cb_;

    void setup_ui();
    void create_context_menu();
    GMenu* create_menu_model();
    static void on_clicked(GtkButton* button, gpointer user_data);
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
};

} // namespace lollypop
