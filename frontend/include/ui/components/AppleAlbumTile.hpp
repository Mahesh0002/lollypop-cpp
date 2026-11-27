/**
 * Square Apple-style album tile for the Home, Albums and Favourites pages.
 *
 * The homogeneous grid supplies the cell width; the tile fills that width and
 * keeps its height equal to it. Artwork fills the tile exactly.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "types.hpp"
#include "ui/components/ArtworkView.hpp"

namespace lollypop {

class AppleAlbumTile {
public:
    static constexpr int kDefaultTileSize = 150;
    static constexpr int kTileSize = kDefaultTileSize;

    using PlayCallback = std::function<void(const Album&)>;
    using ClickCallback = std::function<void(const Album&)>;
    using FavoriteQueryCallback = std::function<bool(const Album&)>;
    using FavoriteToggleCallback = std::function<void(const Album&)>;
    using AlbumActionCallback = std::function<void(const Album&, const char* action)>;
    using PlayToggleCallback = std::function<void(const Album&)>;

    explicit AppleAlbumTile(const Album& album,
                            int tile_size = kDefaultTileSize,
                            bool show_favorite = false,
                            bool reveal_scrim_on_hover = true);
    ~AppleAlbumTile();

    AppleAlbumTile(const AppleAlbumTile&) = delete;
    AppleAlbumTile& operator=(const AppleAlbumTile&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_click_callback(ClickCallback cb) { click_cb_ = std::move(cb); }
    void set_favorite_query_callback(FavoriteQueryCallback cb) {
        favorite_query_cb_ = std::move(cb);
    }
    void set_favorite_toggle_callback(FavoriteToggleCallback cb) {
        favorite_toggle_cb_ = std::move(cb);
    }
    void set_album_action_callback(AlbumActionCallback cb) {
        album_action_cb_ = std::move(cb);
        if (album_action_cb_) create_context_menu();
    }
    void set_play_toggle_callback(PlayToggleCallback cb) {
        play_toggle_cb_ = std::move(cb);
    }

    /// Reflect global playback state on the tile's play/pause button.
    void set_playing(bool playing);

    /// Set the favourite state directly without querying the library.
    void set_favorite(bool favorite);

    /// Reuse an existing tile for another album. Only reloads artwork when its
    /// path actually changes.
    void update_album(const Album& album, bool reload_artwork);

    /// Hold artwork loading until the tile is near the viewport.
    void set_artwork_visible(bool visible);

    /// Match the allocated homogeneous-grid cell width and stay square.
    void fit_to_width(int width);

    void refresh_favorite_state();

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;  // fixed-size box
    std::unique_ptr<GtkWidget, WidgetDeleter> tile_;    // overlay inside box
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrim_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrim_revealer_;
    std::unique_ptr<GtkWidget, WidgetDeleter> meta_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> favorite_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> favorite_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_button_;
    GtkWidget* name_label_ = nullptr;    // non-owning, parented to meta_box_
    GtkWidget* artist_label_ = nullptr;  // non-owning, parented to meta_box_

    Album album_;
    int tile_width_ = kDefaultTileSize;
    int tile_height_ = kDefaultTileSize;
    bool show_favorite_ = false;
    bool reveal_scrim_on_hover_ = false;
    bool pointer_inside_ = false;
    bool focus_inside_ = false;
    bool favorite_ = false;
    bool favorite_press_active_ = false;
    bool playing_ = false;
    bool artwork_visible_ = true;
    std::string artwork_path_applied_;
    guint fit_tick_id_ = 0;

    PlayCallback play_cb_;
    ClickCallback click_cb_;
    FavoriteQueryCallback favorite_query_cb_;
    FavoriteToggleCallback favorite_toggle_cb_;
    AlbumActionCallback album_action_cb_;
    PlayToggleCallback play_toggle_cb_;

    void apply_size(int width, bool force = false);

    void setup_ui();
    void create_context_menu();
    GMenu* create_menu_model();
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    void refresh_favorite_icon();
    void refresh_play_icon();
    void animate_hover(bool hover);
    void update_scrim_reveal();

    static void on_play_clicked(GtkButton* button, gpointer user_data);

    static gboolean fit_to_parent(GtkWidget* widget, GdkFrameClock* clock,
                                  gpointer user_data);

    static gboolean on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data);
    static gboolean on_focus_enter(GtkEventControllerFocus* controller,
                                   gpointer user_data);
    static gboolean on_focus_leave(GtkEventControllerFocus* controller,
                                   gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static gboolean on_key_pressed(GtkEventControllerKey* controller,
                                   guint keyval, guint keycode,
                                   GdkModifierType state, gpointer user_data);
    static void on_favorite_clicked(GtkButton* button, gpointer user_data);
    static gboolean on_favorite_pressed(GtkGestureClick* gesture, int n_press,
                                        double x, double y, gpointer user_data);
    static gboolean on_favorite_released(GtkGestureClick* gesture, int n_press,
                                         double x, double y, gpointer user_data);
    static gboolean clear_favorite_press(gpointer user_data);
};

} // namespace lollypop
