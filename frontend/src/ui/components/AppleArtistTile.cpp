/**
 * Lollypop C++ - AppleArtistTile Implementation
 */

#include "ui/components/AppleArtistTile.hpp"
#include "util/accessibility.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace lollypop {

namespace {
constexpr int kScrimRevealDurationMs = 160;  // matches --motion-base
constexpr int kScrimPercent = 20;
}  // namespace

AppleArtistTile::AppleArtistTile(const Artist& artist, int tile_size)
    : artist_(artist),
      tile_width_(tile_size > 0 ? tile_size : kDefaultTileSize),
      tile_height_(tile_width_) {
    setup_ui();
}

AppleArtistTile::~AppleArtistTile() {
    if (fit_tick_id_ != 0 && widget_) {
        gtk_widget_remove_tick_callback(widget_.get(), fit_tick_id_);
        fit_tick_id_ = 0;
    }
}

void AppleArtistTile::fit_to_width(int width) { apply_size(width); }

void AppleArtistTile::apply_size(int width, bool force) {
    if (width <= 0) return;
    if (!force && width == tile_width_ && width == tile_height_) return;

    tile_width_ = width;
    tile_height_ = width;

    gtk_widget_set_size_request(widget_.get(), width, width);
    gtk_widget_set_size_request(tile_.get(), width, width);
    artwork_view_->set_preferred_size(width, width);
    gtk_widget_set_size_request(scrim_.get(), -1,
                                (width * kScrimPercent) / 100);

    const int inset = width < 200 ? 6 : 10;
    const int bottom_inset = width < 200 ? 3 : 7;
    gtk_widget_set_margin_start(meta_box_.get(), inset);
    gtk_widget_set_margin_end(meta_box_.get(), inset);
    gtk_widget_set_margin_bottom(meta_box_.get(), bottom_inset);

    if (!force && artwork_visible_) {
        artwork_view_->set_artwork_path(artist_.artwork_path);
        artwork_path_applied_ = artist_.artwork_path;
    }

    gtk_widget_queue_resize(widget_.get());
}

gboolean AppleArtistTile::fit_to_parent(GtkWidget*, GdkFrameClock*,
                                        gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    GtkWidget* parent = gtk_widget_get_parent(self->widget_.get());
    if (!parent) return G_SOURCE_CONTINUE;

    const int width = gtk_widget_get_width(parent);
    if (width > 0 && width != self->tile_width_) self->apply_size(width);
    return G_SOURCE_CONTINUE;
}

void AppleArtistTile::update_artist(const Artist& artist, bool reload_artwork) {
    const bool artwork_changed = artist_.artwork_path != artist.artwork_path;
    artist_ = artist;

    gtk_widget_set_tooltip_text(widget_.get(), artist_.name.c_str());
    const std::string label = "Artist: " + artist_.name;
    accessibility::set_label(widget_.get(), label.c_str());

    if (name_label_) gtk_label_set_text(GTK_LABEL(name_label_), artist_.name.c_str());
    if (detail_label_) {
        const size_t count = artist_.album_ids.size();
        char text[48];
        snprintf(text, sizeof(text), "%zu album%s", count,
                 count == 1 ? "" : "s");
        gtk_label_set_text(GTK_LABEL(detail_label_), text);
    }

    if (artwork_changed || (reload_artwork && artwork_visible_)) {
        if (artwork_visible_) {
            artwork_view_->set_artwork_path(artist_.artwork_path);
            artwork_path_applied_ = artist_.artwork_path;
        } else {
            artwork_view_->set_artwork_path("");
            artwork_path_applied_.clear();
        }
    }
}

void AppleArtistTile::set_artwork_visible(bool visible) {
    const bool applied = visible ? artwork_path_applied_ == artist_.artwork_path
                                 : artwork_path_applied_.empty();
    if (artwork_visible_ == visible && applied) return;

    artwork_visible_ = visible;
    const std::string target = visible ? artist_.artwork_path : std::string();
    artwork_view_->set_artwork_path(target);
    artwork_path_applied_ = target;
}

void AppleArtistTile::setup_ui() {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_add_css_class(widget_.get(), "apple-album-tile");
    gtk_widget_add_css_class(widget_.get(), "apple-artist-tile");
    gtk_widget_add_css_class(widget_.get(), "apple-artist-tile-hover-scrim");
    gtk_widget_set_focusable(widget_.get(), TRUE);
    gtk_widget_set_size_request(widget_.get(), tile_width_, tile_height_);
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_vexpand(widget_.get(), FALSE);
    gtk_widget_set_overflow(widget_.get(), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_tooltip_text(widget_.get(), artist_.name.c_str());
    const std::string label = "Artist: " + artist_.name;
    accessibility::set_label(widget_.get(), label.c_str());
    gtk_widget_set_visible(widget_.get(), TRUE);

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

    name_label_ = gtk_label_new(artist_.name.c_str());
    gtk_widget_add_css_class(name_label_, "apple-album-name");
    gtk_label_set_xalign(GTK_LABEL(name_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(name_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(name_label_), 10);
    gtk_label_set_max_width_chars(GTK_LABEL(name_label_), 10);
    gtk_widget_set_halign(name_label_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(name_label_, TRUE);
    gtk_box_append(GTK_BOX(meta_box_.get()), name_label_);

    const size_t album_count = artist_.album_ids.size();
    char detail[48];
    snprintf(detail, sizeof(detail), "%zu album%s", album_count,
             album_count == 1 ? "" : "s");
    detail_label_ = gtk_label_new(detail);
    gtk_widget_add_css_class(detail_label_, "apple-album-artist");
    gtk_label_set_xalign(GTK_LABEL(detail_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(detail_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(detail_label_), 10);
    gtk_label_set_max_width_chars(GTK_LABEL(detail_label_), 10);
    gtk_widget_set_halign(detail_label_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(detail_label_, TRUE);
    gtk_box_append(GTK_BOX(meta_box_.get()), detail_label_);

    GtkWidget* bar_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(bar_row, TRUE);
    gtk_widget_set_halign(bar_row, GTK_ALIGN_FILL);
    gtk_widget_set_valign(bar_row, GTK_ALIGN_FILL);
    gtk_widget_set_visible(bar_row, TRUE);
    gtk_box_append(GTK_BOX(scrim_.get()), bar_row);
    gtk_box_append(GTK_BOX(bar_row), meta_box_.get());

    // The scrim grows from 0 to its 20% height on hover or keyboard focus,
    // matching every other translucent artwork scrim in the app.
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

    play_button_.reset(gtk_button_new_from_icon_name(
        "media-playback-start-symbolic"));
    g_object_ref_sink(play_button_.get());
    gtk_widget_add_css_class(play_button_.get(), "apple-album-play-button");
    gtk_button_set_has_frame(GTK_BUTTON(play_button_.get()), FALSE);
    gtk_widget_set_halign(play_button_.get(), GTK_ALIGN_END);
    gtk_widget_set_valign(play_button_.get(), GTK_ALIGN_START);
    gtk_widget_set_margin_top(play_button_.get(), 8);
    gtk_widget_set_margin_end(play_button_.get(), 8);
    gtk_widget_set_visible(play_button_.get(), TRUE);
    g_signal_connect(play_button_.get(), "clicked",
                     G_CALLBACK(on_play_clicked), this);
    gtk_box_append(GTK_BOX(bar_row), play_button_.get());
    refresh_play_icon();

    auto* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion);

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

    fit_tick_id_ = gtk_widget_add_tick_callback(
        widget_.get(), fit_to_parent, this, nullptr);
    apply_size(tile_width_, true);
    artwork_view_->set_artwork_path(artist_.artwork_path);
    artwork_path_applied_ = artist_.artwork_path;
}

void AppleArtistTile::set_playing(bool playing) {
    playing_ = playing;
    refresh_play_icon();
}

void AppleArtistTile::refresh_play_icon() {
    if (!play_button_) return;
    const char* icon = playing_ ? "media-playback-pause-symbolic"
                                : "media-playback-start-symbolic";
    gtk_button_set_icon_name(GTK_BUTTON(play_button_.get()), icon);
    const char* label = playing_ ? "Pause artist" : "Play artist";
    accessibility::set_label(play_button_.get(), label);
    gtk_widget_set_tooltip_text(play_button_.get(), label);
}

void AppleArtistTile::animate_hover(bool hover) {
    pointer_inside_ = hover;
    if (hover) {
        gtk_widget_add_css_class(widget_.get(), "hover");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "hover");
    }
    update_scrim_reveal();
}

void AppleArtistTile::update_scrim_reveal() {
    if (!scrim_revealer_) return;
    const bool reveal = pointer_inside_ || focus_inside_;
    gtk_revealer_set_reveal_child(GTK_REVEALER(scrim_revealer_.get()),
                                  reveal ? TRUE : FALSE);
}

gboolean AppleArtistTile::on_enter(GtkEventControllerMotion*, double, double,
                                   gpointer user_data) {
    if (auto* self = static_cast<AppleArtistTile*>(user_data)) {
        self->animate_hover(true);
    }
    return FALSE;
}

gboolean AppleArtistTile::on_leave(GtkEventControllerMotion*, gpointer user_data) {
    if (auto* self = static_cast<AppleArtistTile*>(user_data)) {
        self->animate_hover(false);
    }
    return FALSE;
}

gboolean AppleArtistTile::on_focus_enter(GtkEventControllerFocus*,
                                         gpointer user_data) {
    if (auto* self = static_cast<AppleArtistTile*>(user_data)) {
        self->focus_inside_ = true;
        self->update_scrim_reveal();
    }
    return FALSE;
}

gboolean AppleArtistTile::on_focus_leave(GtkEventControllerFocus*,
                                         gpointer user_data) {
    if (auto* self = static_cast<AppleArtistTile*>(user_data)) {
        self->focus_inside_ = false;
        self->update_scrim_reveal();
    }
    return FALSE;
}

gboolean AppleArtistTile::on_released(GtkGestureClick*, int n_press, double,
                                      double, gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    if (!self) return FALSE;
    if (n_press == 2 && self->play_cb_) {
        self->play_cb_(self->artist_);
    } else if (self->click_cb_) {
        self->click_cb_(self->artist_);
    }
    return FALSE;
}

gboolean AppleArtistTile::on_key_pressed(GtkEventControllerKey*, guint keyval,
                                         guint, GdkModifierType,
                                         gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    if (!self || !self->click_cb_) return FALSE;
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter ||
        keyval == GDK_KEY_space) {
        self->click_cb_(self->artist_);
        return TRUE;
    }
    return FALSE;
}

void AppleArtistTile::on_play_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    if (!self) return;
    self->playing_ = !self->playing_;
    self->refresh_play_icon();
    if (self->play_toggle_cb_) {
        self->play_toggle_cb_(self->artist_);
    } else if (self->play_cb_) {
        self->play_cb_(self->artist_);
    }
}

void AppleArtistTile::create_context_menu() {
    auto* right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click),
                                  GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_context_menu), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(right_click));

    auto* action_group = g_simple_action_group_new();
    const char* actions[] = {
        "play", "shuffle", "add-to-queue", "add-to-playlist",
    };
    for (const char* name : actions) {
        auto* action = g_simple_action_new(name, nullptr);
        g_signal_connect(action, "activate",
                         G_CALLBACK(on_action_activated), this);
        g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
        g_object_unref(action);
    }
    gtk_widget_insert_action_group(widget_.get(), "apple-artist-tile",
                                   G_ACTION_GROUP(action_group));
}

void AppleArtistTile::on_action_activated(GSimpleAction* action, GVariant*,
                                          gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    if (!self || !self->artist_action_cb_) return;

    const char* name = g_action_get_name(G_ACTION(action));
    const char* action_name = "play";
    if (std::strcmp(name, "shuffle") == 0) {
        action_name = "shuffle";
    } else if (std::strcmp(name, "add-to-queue") == 0) {
        action_name = "add_to_queue";
    } else if (std::strcmp(name, "add-to-playlist") == 0) {
        action_name = "add_to_playlist";
    }
    self->artist_action_cb_(self->artist_, action_name);
}

void AppleArtistTile::on_context_menu(GtkGestureClick*, int, double x, double y,
                                      gpointer user_data) {
    auto* self = static_cast<AppleArtistTile*>(user_data);
    if (!self || !self->artist_action_cb_) return;

    GtkWidget* popover = gtk_popover_menu_new_from_model(
        G_MENU_MODEL(self->create_menu_model()));
    gtk_widget_set_parent(popover, self->widget_.get());

    GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_visible(popover, TRUE);
}

GMenu* AppleArtistTile::create_menu_model() {
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Play", "apple-artist-tile.play");
    g_menu_append(menu, "Shuffle", "apple-artist-tile.shuffle");
    g_menu_append(menu, "Add to Queue", "apple-artist-tile.add-to-queue");
    g_menu_append(menu, "Add to Playlist...", "apple-artist-tile.add-to-playlist");
    return menu;
}

} // namespace lollypop
