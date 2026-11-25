/**
 * Square Apple-style artist tile for the Artists page.
 *
 * It mirrors AppleAlbumTile's grid behaviour and scrim treatment: the
 * homogeneous grid owns the width, the tile stays square, and artwork fills
 * it exactly. The secondary line shows the artist's album count.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

#include "types.hpp"
#include "ui/components/ArtworkView.hpp"

namespace lollypop {

class AppleArtistTile {
public:
    static constexpr int kDefaultTileSize = 150;

    using ClickCallback = std::function<void(const Artist&)>;
    using PlayCallback = std::function<void(const Artist&)>;
    using ArtistActionCallback = std::function<void(const Artist&, const char*)>;
    using PlayToggleCallback = std::function<void(const Artist&)>;

    explicit AppleArtistTile(const Artist& artist,
                             int tile_size = kDefaultTileSize);
    ~AppleArtistTile();

    AppleArtistTile(const AppleArtistTile&) = delete;
    AppleArtistTile& operator=(const AppleArtistTile&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_artist_action_callback(ArtistActionCallback cb) {
        artist_action_cb_ = std::move(cb);
        if (artist_action_cb_) create_context_menu();
    }
    void set_play_toggle_callback(PlayToggleCallback cb) {
        play_toggle_cb_ = std::move(cb);
    }

    void set_playing(bool playing);
    void update_artist(const Artist& artist, bool reload_artwork);
    void set_artwork_visible(bool visible);
    void fit_to_width(int width);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> tile_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrim_;
    std::unique_ptr<GtkWidget, WidgetDeleter> meta_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_button_;

    GtkWidget* name_label_ = nullptr;
    GtkWidget* detail_label_ = nullptr;

    Artist artist_;
    int tile_width_ = kDefaultTileSize;
    int tile_height_ = kDefaultTileSize;
    bool playing_ = false;
    bool artwork_visible_ = true;
    std::string artwork_path_applied_;
    guint fit_tick_id_ = 0;

    ClickCallback click_cb_;
    PlayCallback play_cb_;
    ArtistActionCallback artist_action_cb_;
    PlayToggleCallback play_toggle_cb_;

    void apply_size(int width, bool force = false);
    void setup_ui();
    void create_context_menu();
    GMenu* create_menu_model();
    void refresh_play_icon();
    void animate_hover(bool hover);

    static gboolean fit_to_parent(GtkWidget* widget, GdkFrameClock* clock,
                                  gpointer user_data);
    static gboolean on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static gboolean on_key_pressed(GtkEventControllerKey* controller,
                                   guint keyval, guint keycode,
                                   GdkModifierType state, gpointer user_data);
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static void on_action_activated(GSimpleAction* action, GVariant* parameter,
                                    gpointer user_data);
};

} // namespace lollypop
