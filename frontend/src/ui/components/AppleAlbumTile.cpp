/**
 * Square Apple-style album tile implementation.
 *
 * The homogeneous grid supplies the width. The tile fills that cell exactly,
 * keeps its height equal to the width, and the artwork fills the tile.
 */

#include "ui/components/AppleAlbumTile.hpp"
#include "util/accessibility.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cstring>
#include <random>

#include <algorithm>

namespace lollypop {

namespace {
constexpr int kScrimRevealDurationMs = 160;  // matches --motion-base
constexpr int kScrimPercent = 20;
}  // namespace

AppleAlbumTile::AppleAlbumTile(const Album& album, int tile_size,
                               bool show_favorite, bool reveal_scrim_on_hover)
    : album_(album),
      tile_width_(tile_size > 0 ? tile_size : kDefaultTileSize),
      tile_height_(tile_width_),
      show_favorite_(show_favorite),
      reveal_scrim_on_hover_(reveal_scrim_on_hover) {
    setup_ui();
}

AppleAlbumTile::~AppleAlbumTile() { if (fit_tick_id_ != 0 && widget_) { gtk_widget_remove_tick_callback(widget_.get(), fit_tick_id_); fit_tick_id_ = 0; } }

void AppleAlbumTile::fit_to_width(int width) { apply_size(width); }

void AppleAlbumTile::apply_size(int width, bool force) { if (width <= 0) return; if (!force && width == tile_width_ && width == tile_height_) return;

tile_width_ = width;
tile_height_ = width;

// The grid cell is already fixed by the middle panel, so the tile fills it.
gtk_widget_set_size_request(widget_.get(), width, width);
gtk_widget_set_size_request(tile_.get(), width, width);
artwork_view_->set_preferred_size(width, width);
// The tile is square, so `width` is also the artwork height. Both scrim
// styles occupy 20% of it; the hover variant reveals that height on demand.
gtk_widget_set_size_request(scrim_.get(), -1, (width * kScrimPercent) / 100);

const int meta_inset = width < 200 ? 6 : 10;
const int meta_bottom_inset = width < 200 ? 3 : 7;
gtk_widget_set_margin_start(meta_box_.get(), meta_inset);
gtk_widget_set_margin_end(meta_box_.get(), meta_inset);
gtk_widget_set_margin_bottom(meta_box_.get(), meta_bottom_inset);

if (!force && artwork_visible_) {
    artwork_view_->set_artwork_path(album_.artwork_path);
    artwork_path_applied_ = album_.artwork_path;
}

gtk_widget_queue_resize(widget_.get());

}

gboolean AppleAlbumTile::fit_to_parent(GtkWidget*, GdkFrameClock*, gpointer user_data) {
    auto* self = static_cast<AppleAlbumTile*>(user_data);
    GtkWidget* parent = gtk_widget_get_parent(self->widget_.get());
    if (!parent) return G_SOURCE_CONTINUE;

    // FlowBox wraps each tile in a cell; its width is already the column width.
    const int width = gtk_widget_get_width(parent);
    if (width > 0 && width != self->tile_width_) {
        self->apply_size(width);
    }
    return G_SOURCE_CONTINUE;
}

void AppleAlbumTile::set_favorite(bool favorite) { favorite_ = favorite; refresh_favorite_icon(); }

void AppleAlbumTile::update_album(const Album& album, bool reload_artwork) { const bool artwork_changed = album_.artwork_path != album.artwork_path; album_ = album;

gtk_widget_set_tooltip_text(widget_.get(), album_.title.c_str());
const std::string album_label =
    album_.title + " by " +
    (album_.artist.empty() ? "Unknown Artist" : album_.artist);
accessibility::set_label(widget_.get(), album_label.c_str());
if (name_label_) gtk_label_set_text(GTK_LABEL(name_label_), album_.title.c_str());
if (artist_label_) {
    gtk_label_set_text(GTK_LABEL(artist_label_),
                       album_.artist.empty() ? "Unknown Artist"
                                             : album_.artist.c_str());
}

set_favorite(album_.favorite);

if (artwork_changed || (reload_artwork && artwork_visible_)) {
    if (artwork_visible_) {
        artwork_view_->set_artwork_path(album_.artwork_path);
        artwork_path_applied_ = album_.artwork_path;
    } else {
        artwork_view_->set_artwork_path("");
        artwork_path_applied_.clear();
    }
}

}

void AppleAlbumTile::set_artwork_visible(bool visible) { const bool applied = visible ? artwork_path_applied_ == album_.artwork_path : artwork_path_applied_.empty(); if (artwork_visible_ == visible && applied) return;

artwork_visible_ = visible;
const std::string target = visible ? album_.artwork_path : std::string();
artwork_view_->set_artwork_path(target);
artwork_path_applied_ = target;

}

void AppleAlbumTile::setup_ui() {
    // Fill the homogeneous FlowBox cell; no centering and no fixed width.
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)); g_object_ref_sink(widget_.get()); gtk_widget_add_css_class(widget_.get(), "apple-album-tile");
if (reveal_scrim_on_hover_) {
    gtk_widget_add_css_class(widget_.get(), "apple-album-tile-hover-scrim");
}
gtk_widget_set_focusable(widget_.get(), TRUE); gtk_widget_set_size_request(widget_.get(), tile_width_, tile_height_); gtk_widget_set_halign(widget_.get(), GTK_ALIGN_FILL); gtk_widget_set_valign(widget_.get(), GTK_ALIGN_FILL); gtk_widget_set_hexpand(widget_.get(), TRUE); gtk_widget_set_vexpand(widget_.get(), FALSE); gtk_widget_set_overflow(widget_.get(), GTK_OVERFLOW_HIDDEN); gtk_widget_set_tooltip_text(widget_.get(), album_.title.c_str()); const std::string album_label = album_.title + " by " + (album_.artist.empty() ? "Unknown Artist" : album_.artist); accessibility::set_label(widget_.get(), album_label.c_str()); gtk_widget_set_visible(widget_.get(), TRUE);

// Inner overlay holds the artwork, scrim and favourite button.
tile_.reset(gtk_overlay_new());
g_object_ref_sink(tile_.get());
gtk_widget_add_css_class(tile_.get(), "apple-album-tile-inner");
gtk_widget_set_size_request(tile_.get(), tile_width_, tile_height_);
gtk_widget_set_halign(tile_.get(), GTK_ALIGN_FILL);
gtk_widget_set_valign(tile_.get(), GTK_ALIGN_FILL);
gtk_widget_set_hexpand(tile_.get(), TRUE);
gtk_widget_set_vexpand(tile_.get(), TRUE);
gtk_widget_set_overflow(tile_.get(), GTK_OVERFLOW_HIDDEN);
gtk_widget_set_visible(tile_.get(), TRUE);
gtk_box_append(GTK_BOX(widget_.get()), tile_.get());

artwork_view_ = std::make_unique<ArtworkView>();
artwork_view_->set_corner_radius(12.0);

GtkWidget* artwork = artwork_view_->get_widget();
gtk_widget_add_css_class(artwork, "apple-album-artwork");
gtk_widget_set_halign(artwork, GTK_ALIGN_FILL);
gtk_widget_set_valign(artwork, GTK_ALIGN_FILL);
gtk_widget_set_hexpand(artwork, TRUE);
gtk_widget_set_vexpand(artwork, TRUE);
gtk_overlay_set_child(GTK_OVERLAY(tile_.get()), artwork);

scrim_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 1));
g_object_ref_sink(scrim_.get());
gtk_widget_add_css_class(scrim_.get(), "apple-album-scrim");
// Do not put a width request on the scrim: FILL makes it match the overlay
// width exactly. Height is set by apply_size().
gtk_widget_set_size_request(scrim_.get(), -1, -1);
gtk_widget_set_halign(scrim_.get(), GTK_ALIGN_FILL);
gtk_widget_set_valign(scrim_.get(), GTK_ALIGN_END);
gtk_widget_set_hexpand(scrim_.get(), TRUE);
gtk_widget_set_visible(scrim_.get(), TRUE);

meta_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 1));
g_object_ref_sink(meta_box_.get());
gtk_widget_add_css_class(meta_box_.get(), "apple-album-meta");
gtk_widget_set_halign(meta_box_.get(), GTK_ALIGN_FILL);
gtk_widget_set_valign(meta_box_.get(), GTK_ALIGN_END);
gtk_widget_set_hexpand(meta_box_.get(), TRUE);
gtk_widget_set_visible(meta_box_.get(), TRUE);

name_label_ = gtk_label_new(album_.title.c_str());
gtk_widget_add_css_class(name_label_, "apple-album-name");
gtk_label_set_xalign(GTK_LABEL(name_label_), 0.0);
gtk_label_set_ellipsize(GTK_LABEL(name_label_), PANGO_ELLIPSIZE_END);
gtk_label_set_width_chars(GTK_LABEL(name_label_), 10);
gtk_label_set_max_width_chars(GTK_LABEL(name_label_), 10);
gtk_widget_set_halign(name_label_, GTK_ALIGN_FILL);
gtk_widget_set_visible(name_label_, TRUE);
gtk_box_append(GTK_BOX(meta_box_.get()), name_label_);

artist_label_ = gtk_label_new(
    album_.artist.empty() ? "Unknown Artist" : album_.artist.c_str());
gtk_widget_add_css_class(artist_label_, "apple-album-artist");
gtk_label_set_xalign(GTK_LABEL(artist_label_), 0.0);
gtk_label_set_ellipsize(GTK_LABEL(artist_label_), PANGO_ELLIPSIZE_END);
gtk_label_set_width_chars(GTK_LABEL(artist_label_), 10);
gtk_label_set_max_width_chars(GTK_LABEL(artist_label_), 10);
gtk_widget_set_halign(artist_label_, GTK_ALIGN_FILL);
gtk_widget_set_visible(artist_label_, TRUE);
gtk_box_append(GTK_BOX(meta_box_.get()), artist_label_);

// The translucent bar is a horizontal row: metadata on the left, the
// play/pause control pinned 8px from its top-right corner.
GtkWidget* bar_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_widget_set_hexpand(bar_row, TRUE);
gtk_widget_set_halign(bar_row, GTK_ALIGN_FILL);
gtk_widget_set_valign(bar_row, GTK_ALIGN_FILL);
gtk_widget_set_visible(bar_row, TRUE);
gtk_box_append(GTK_BOX(scrim_.get()), bar_row);
gtk_box_append(GTK_BOX(bar_row), meta_box_.get());

if (reveal_scrim_on_hover_) {
    // A revealer animates the scrim's height from 0 to its 20% request while
    // CSS fades it in, so the bar grows upward instead of popping into view.
    scrim_revealer_.reset(gtk_revealer_new());
    g_object_ref_sink(scrim_revealer_.get());
    gtk_widget_set_halign(scrim_revealer_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(scrim_revealer_.get(), GTK_ALIGN_END);
    gtk_widget_set_hexpand(scrim_revealer_.get(), TRUE);
    gtk_widget_set_visible(scrim_revealer_.get(), TRUE);
    gtk_revealer_set_transition_type(
        GTK_REVEALER(scrim_revealer_.get()),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(
        GTK_REVEALER(scrim_revealer_.get()), kScrimRevealDurationMs);
    gtk_revealer_set_child(GTK_REVEALER(scrim_revealer_.get()), scrim_.get());
    gtk_overlay_add_overlay(GTK_OVERLAY(tile_.get()), scrim_revealer_.get());
} else {
    gtk_overlay_add_overlay(GTK_OVERLAY(tile_.get()), scrim_.get());
}

if (show_favorite_) {
    favorite_button_.reset(gtk_button_new_from_icon_name("non-starred-symbolic"));
    g_object_ref_sink(favorite_button_.get());
    gtk_widget_add_css_class(favorite_button_.get(), "apple-album-favorite");
    gtk_button_set_has_frame(GTK_BUTTON(favorite_button_.get()), FALSE);
    gtk_widget_set_halign(favorite_button_.get(), GTK_ALIGN_END);
    gtk_widget_set_valign(favorite_button_.get(), GTK_ALIGN_START);
    gtk_widget_set_margin_end(favorite_button_.get(), 6);
    gtk_widget_set_margin_top(favorite_button_.get(), 6);
    gtk_widget_set_tooltip_text(favorite_button_.get(), "Add to Favourites");
    gtk_widget_set_visible(favorite_button_.get(), TRUE);

    GtkWidget* image = gtk_image_new_from_icon_name("non-starred-symbolic");
    g_object_ref_sink(image);
    favorite_image_.reset(image);
    gtk_button_set_child(GTK_BUTTON(favorite_button_.get()), favorite_image_.get());

    g_signal_connect(favorite_button_.get(), "clicked",
                     G_CALLBACK(on_favorite_clicked), this);

    auto* favorite_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(favorite_click),
                                  GDK_BUTTON_PRIMARY);
    g_signal_connect(favorite_click, "pressed",
                     G_CALLBACK(on_favorite_pressed), this);
    g_signal_connect(favorite_click, "released",
                     G_CALLBACK(on_favorite_released), this);
    gtk_widget_add_controller(favorite_button_.get(),
                              GTK_EVENT_CONTROLLER(favorite_click));

    gtk_overlay_add_overlay(GTK_OVERLAY(tile_.get()), favorite_button_.get());
    refresh_favorite_icon();
}

// Rectangular play/pause control inside the translucent bar.
play_button_.reset(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
g_object_ref_sink(play_button_.get());
gtk_widget_add_css_class(play_button_.get(), "apple-album-play-button");
gtk_button_set_has_frame(GTK_BUTTON(play_button_.get()), FALSE);
gtk_widget_set_halign(play_button_.get(), GTK_ALIGN_END);
gtk_widget_set_valign(play_button_.get(), GTK_ALIGN_START);
gtk_widget_set_margin_top(play_button_.get(), 8);
gtk_widget_set_margin_end(play_button_.get(), 8);
accessibility::set_label(play_button_.get(), "Play album");
gtk_widget_set_visible(play_button_.get(), TRUE);
g_signal_connect(play_button_.get(), "clicked",
                 G_CALLBACK(on_play_clicked), this);
gtk_box_append(GTK_BOX(bar_row), play_button_.get());
refresh_play_icon();

auto* motion = gtk_event_controller_motion_new();
g_signal_connect(motion, "enter", G_CALLBACK(on_enter), this);
g_signal_connect(motion, "leave", G_CALLBACK(on_leave), this);
gtk_widget_add_controller(widget_.get(), motion);

// Keyboard focus reveals the same controls as hover.
auto* focus = gtk_event_controller_focus_new();
g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), this);
g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), this);
gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(focus));

auto* click = gtk_gesture_click_new();
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
g_signal_connect(click, "released", G_CALLBACK(on_released), this);
gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(click));

auto* key = gtk_event_controller_key_new();
g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), this);
gtk_widget_add_controller(widget_.get(), key);

// Track the width allocated by the homogeneous grid and keep the tile
// square as the window resizes.
fit_tick_id_ = gtk_widget_add_tick_callback(
    widget_.get(), fit_to_parent, this, nullptr);
apply_size(tile_width_, true);
artwork_view_->set_artwork_path(album_.artwork_path);
artwork_path_applied_ = album_.artwork_path;

}

void AppleAlbumTile::set_playing(bool playing) { playing_ = playing; refresh_play_icon(); }

void AppleAlbumTile::refresh_play_icon() { if (!play_button_) return; const char* icon = playing_ ? "media-playback-pause-symbolic" : "media-playback-start-symbolic"; gtk_button_set_icon_name(GTK_BUTTON(play_button_.get()), icon); accessibility::set_label(play_button_.get(), playing_ ? "Pause album" : "Play album"); }

void AppleAlbumTile::on_play_clicked(GtkButton*, gpointer user_data) { auto* self = static_cast<AppleAlbumTile*>(user_data); if (!self) return; self->playing_ = !self->playing_; self->refresh_play_icon(); if (self->play_toggle_cb_) { self->play_toggle_cb_(self->album_); } else if (self->play_cb_) { self->play_cb_(self->album_); } }

void AppleAlbumTile::refresh_favorite_state() { if (favorite_query_cb_) { favorite_ = favorite_query_cb_(album_); } refresh_favorite_icon(); }

void AppleAlbumTile::refresh_favorite_icon() { if (!favorite_image_) return;

gtk_image_set_from_icon_name(
    GTK_IMAGE(favorite_image_.get()),
    favorite_ ? "starred-symbolic" : "non-starred-symbolic");

if (favorite_) {
    gtk_widget_add_css_class(widget_.get(), "favorited");
} else {
    gtk_widget_remove_css_class(widget_.get(), "favorited");
}

if (favorite_button_) {
    const std::string action = favorite_ ? "Remove " : "Add ";
    const std::string suffix = favorite_ ? " from favourites"
                                         : " to favourites";
    const std::string favorite_label = action + album_.title + suffix;
    gtk_widget_set_tooltip_text(favorite_button_.get(), favorite_label.c_str());
    accessibility::set_label(favorite_button_.get(), favorite_label.c_str());
}

}

void AppleAlbumTile::animate_hover(bool hover) { pointer_inside_ = hover; if (hover) { gtk_widget_add_css_class(widget_.get(), "hover"); } else { gtk_widget_remove_css_class(widget_.get(), "hover"); } update_scrim_reveal(); }

void AppleAlbumTile::update_scrim_reveal() { if (!reveal_scrim_on_hover_ || !scrim_revealer_) return;

const bool reveal = pointer_inside_ || focus_inside_;
gtk_revealer_set_reveal_child(GTK_REVEALER(scrim_revealer_.get()),
                              reveal ? TRUE : FALSE);

}

gboolean AppleAlbumTile::on_enter(GtkEventControllerMotion*, double, double, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->animate_hover(true); } return FALSE; }

gboolean AppleAlbumTile::on_leave(GtkEventControllerMotion*, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->animate_hover(false); } return FALSE; }

gboolean AppleAlbumTile::on_focus_enter(GtkEventControllerFocus*, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->focus_inside_ = true; self->update_scrim_reveal(); } return FALSE; }

gboolean AppleAlbumTile::on_focus_leave(GtkEventControllerFocus*, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->focus_inside_ = false; self->update_scrim_reveal(); } return FALSE; }

gboolean AppleAlbumTile::on_released(GtkGestureClick*, int n_press, double, double, gpointer user_data) { auto* self = static_cast<AppleAlbumTile*>(user_data); if (!self) return FALSE;

if (self->favorite_press_active_) {
    self->favorite_press_active_ = false;
    return TRUE;
}

if (n_press == 2 && self->play_cb_) {
    self->play_cb_(self->album_);
} else if (self->click_cb_) {
    self->click_cb_(self->album_);
}
return FALSE;

}

gboolean AppleAlbumTile::on_key_pressed(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer user_data) { auto* self = static_cast<AppleAlbumTile*>(user_data); if (!self || !self->click_cb_) return FALSE;

if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter ||
    keyval == GDK_KEY_space) {
    self->click_cb_(self->album_);
    return TRUE;
}
return FALSE;

}

void AppleAlbumTile::on_favorite_clicked(GtkButton*, gpointer user_data) { auto* self = static_cast<AppleAlbumTile*>(user_data); if (!self) return;

if (self->favorite_toggle_cb_) {
    self->favorite_toggle_cb_(self->album_);
}
self->refresh_favorite_state();

}

gboolean AppleAlbumTile::on_favorite_pressed(GtkGestureClick*, int, double, double, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->favorite_press_active_ = true; } return FALSE; }

gboolean AppleAlbumTile::on_favorite_released(GtkGestureClick*, int, double, double, gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { g_idle_add(clear_favorite_press, self); } return FALSE; }

gboolean AppleAlbumTile::clear_favorite_press(gpointer user_data) { if (auto* self = static_cast<AppleAlbumTile*>(user_data)) { self->favorite_press_active_ = false; } return G_SOURCE_REMOVE; }

void AppleAlbumTile::create_context_menu() { auto* right_click = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY); g_signal_connect(right_click, "pressed", G_CALLBACK(on_context_menu), this); gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(right_click));

auto* action_group = g_simple_action_group_new();
g_action_map_add_action_entries(G_ACTION_MAP(action_group),
    (GActionEntry[]){
        { "play", [](GSimpleAction*, GVariant*, gpointer self) {
            auto* tile = static_cast<AppleAlbumTile*>(self);
            if (tile->album_action_cb_) tile->album_action_cb_(tile->album_, "play");
        }, nullptr, nullptr, nullptr },
        { "shuffle", [](GSimpleAction*, GVariant*, gpointer self) {
            auto* tile = static_cast<AppleAlbumTile*>(self);
            if (tile->album_action_cb_) tile->album_action_cb_(tile->album_, "shuffle");
        }, nullptr, nullptr, nullptr },
        { "add-to-queue", [](GSimpleAction*, GVariant*, gpointer self) {
            auto* tile = static_cast<AppleAlbumTile*>(self);
            if (tile->album_action_cb_) tile->album_action_cb_(tile->album_, "add_to_queue");
        }, nullptr, nullptr, nullptr },
        { "add-to-playlist", [](GSimpleAction*, GVariant*, gpointer self) {
            auto* tile = static_cast<AppleAlbumTile*>(self);
            if (tile->album_action_cb_) tile->album_action_cb_(tile->album_, "add_to_playlist");
        }, nullptr, nullptr, nullptr },
        { "toggle-favorite", [](GSimpleAction*, GVariant*, gpointer self) {
            auto* tile = static_cast<AppleAlbumTile*>(self);
            if (tile->album_action_cb_) tile->album_action_cb_(tile->album_, "toggle_favorite");
        }, nullptr, nullptr, nullptr },
    }, 5, this);
gtk_widget_insert_action_group(widget_.get(), "apple-album-tile", G_ACTION_GROUP(action_group));

}

void AppleAlbumTile::on_context_menu(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) { (void)gesture; (void)n_press; (void)x; (void)y; AppleAlbumTile* self = static_cast<AppleAlbumTile*>(user_data); if (!self->album_action_cb_) return;

GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(
    self->create_menu_model()));
gtk_widget_set_parent(popover, self->widget_.get());

GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};
gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
gtk_widget_set_visible(popover, TRUE);

}

GMenu* AppleAlbumTile::create_menu_model() { GMenu* menu = g_menu_new(); g_menu_append(menu, "Play", "apple-album-tile.play"); g_menu_append(menu, "Shuffle", "apple-album-tile.shuffle"); g_menu_append(menu, "Add to Queue", "apple-album-tile.add-to-queue"); g_menu_append(menu, "Add to Playlist...", "apple-album-tile.add-to-playlist"); g_menu_append(menu, "Favourite", "apple-album-tile.toggle-favorite"); return menu; }

} // namespace lollypop
