/**
 * Lollypop C++ - AlbumCard Implementation
 *
 * The card is a compact, Apple Music-like tile: square artwork, one-line
 * title, one-line artist. Hover/press feedback is CSS-driven; artwork decoding
 * is delegated to ArtworkView so the UI thread is never blocked by a cover.
 */

#include "ui/components/AlbumCard.hpp"
#include "main_window.hpp"
#include "util/accessibility.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cstring>
#include <string>

namespace lollypop {

namespace {

GtkWidget* create_meta_label(const char* text, const char* css_class) {
    GtkWidget* label = gtk_label_new(text);
    gtk_widget_add_css_class(label, css_class);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    // Keep the natural width at or below the 150px artwork so four columns
    // fit in the current middle panel; longer names are ellipsized.
    gtk_label_set_max_width_chars(GTK_LABEL(label), 18);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(label, TRUE);
    return label;
}

} // namespace

AlbumCard::AlbumCard(MainWindow* main_window, const Album& album)
    : main_window_(main_window), album_(album) {
    setup_ui();
}

AlbumCard::~AlbumCard() = default;

void AlbumCard::setup_ui() {
    // A plain GtkBox avoids GtkButton's unavoidable theme padding; the click
    // gesture below provides the activation behaviour instead.
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_focusable(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "album-card");
    const std::string artist =
        album_.artist.empty() ? "Unknown Artist" : album_.artist;
    const std::string album_label = album_.title + " by " + artist;
    accessibility::set_label(widget_.get(), album_label.c_str());
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_START);
    gtk_widget_set_size_request(widget_.get(), kArtworkSize, -1);
    gtk_widget_set_visible(widget_.get(), TRUE);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content, "album-card-content");
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_visible(content, TRUE);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(kArtworkSize, kArtworkSize);
    artwork_view_->set_artwork_path(album_.artwork_path);
    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "album-artwork");
    gtk_widget_set_hexpand(artwork, TRUE);
    gtk_box_append(GTK_BOX(content), artwork);

    title_label_.reset(create_meta_label(album_.title.c_str(),
                                         "album-title"));
    g_object_ref_sink(title_label_.get());
    artist_label_.reset(create_meta_label(
        album_.artist.empty() ? "Unknown Artist" : album_.artist.c_str(),
        "album-artist"));
    g_object_ref_sink(artist_label_.get());
    gtk_box_append(GTK_BOX(content), title_label_.get());
    gtk_box_append(GTK_BOX(content), artist_label_.get());

    gtk_box_append(GTK_BOX(widget_.get()), content);

    auto* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion);

    auto* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
                                  GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), this);
    g_signal_connect(click, "released", G_CALLBACK(on_released), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(click));

    auto* key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), this);
    gtk_widget_add_controller(widget_.get(), key);
}

void AlbumCard::set_active(bool active) {
    active_ = active;
    if (active) {
        gtk_widget_add_css_class(widget_.get(), "active");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "active");
    }
}

gboolean AlbumCard::on_enter(GtkEventControllerMotion*, double, double,
                             gpointer user_data) {
    static_cast<AlbumCard*>(user_data)->animate_hover(true);
    return FALSE;
}

gboolean AlbumCard::on_leave(GtkEventControllerMotion*, gpointer user_data) {
    static_cast<AlbumCard*>(user_data)->animate_hover(false);
    return FALSE;
}

gboolean AlbumCard::on_pressed(GtkGestureClick*, int, double, double,
                               gpointer user_data) {
    auto* self = static_cast<AlbumCard*>(user_data);
    self->animate_press(true);
    return FALSE;
}

gboolean AlbumCard::on_released(GtkGestureClick*, int n_press, double, double,
                                gpointer user_data) {
    auto* self = static_cast<AlbumCard*>(user_data);
    self->animate_press(false);
    if (n_press == 1 && self->click_cb_) {
        self->click_cb_(self->album_);
    }
    return FALSE;
}

gboolean AlbumCard::on_key_pressed(GtkEventControllerKey*, guint keyval, guint,
                                   GdkModifierType, gpointer user_data) {
    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter &&
        keyval != GDK_KEY_space) {
        return FALSE;
    }
    auto* self = static_cast<AlbumCard*>(user_data);
    if (self->click_cb_) self->click_cb_(self->album_);
    return TRUE;
}

void AlbumCard::animate_hover(bool hover) {
    if (hover) {
        gtk_widget_add_css_class(widget_.get(), "hover");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "hover");
    }
}

void AlbumCard::animate_press(bool press) {
    if (press) {
        gtk_widget_add_css_class(widget_.get(), "active");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "active");
    }
}


void AlbumCard::create_context_menu() {
    auto* right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_context_menu), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(right_click));

    auto* action_group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(action_group),
        (GActionEntry[]){
            { "play", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* card = static_cast<AlbumCard*>(self);
                if (card->album_action_cb_) card->album_action_cb_(card->album_, "play");
            }, nullptr, nullptr, nullptr },
            { "shuffle", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* card = static_cast<AlbumCard*>(self);
                if (card->album_action_cb_) card->album_action_cb_(card->album_, "shuffle");
            }, nullptr, nullptr, nullptr },
            { "add-to-queue", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* card = static_cast<AlbumCard*>(self);
                if (card->album_action_cb_) card->album_action_cb_(card->album_, "add_to_queue");
            }, nullptr, nullptr, nullptr },
            { "add-to-playlist", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* card = static_cast<AlbumCard*>(self);
                if (card->album_action_cb_) card->album_action_cb_(card->album_, "add_to_playlist");
            }, nullptr, nullptr, nullptr },
            { "toggle-favorite", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* card = static_cast<AlbumCard*>(self);
                if (card->album_action_cb_) card->album_action_cb_(card->album_, "toggle_favorite");
            }, nullptr, nullptr, nullptr },
        }, 5, this);
    gtk_widget_insert_action_group(widget_.get(), "album-card", G_ACTION_GROUP(action_group));
}

void AlbumCard::on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    if (!self->album_action_cb_) return;

    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(
        self->create_menu_model()));
    gtk_widget_set_parent(popover, self->widget_.get());

    GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_visible(popover, TRUE);
}

GMenu* AlbumCard::create_menu_model() {
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Play", "album-card.play");
    g_menu_append(menu, "Shuffle", "album-card.shuffle");
    g_menu_append(menu, "Add to Queue", "album-card.add-to-queue");
    g_menu_append(menu, "Add to Playlist...", "album-card.add-to-playlist");
    g_menu_append(menu, "Favourite", "album-card.toggle-favorite");
    return menu;
}

} // namespace lollypop
