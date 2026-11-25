/**
 * Lollypop C++ - ArtistItem Implementation
 */

#include "ui/components/ArtistItem.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cstring>
#include <iostream>

namespace lollypop {

ArtistItem::ArtistItem(MainWindow* main_window, const Artist& artist)
    : main_window_(main_window), artist_(artist) {
    setup_ui();
}

ArtistItem::~ArtistItem() = default;

void ArtistItem::setup_ui() {
    widget_.reset(gtk_button_new());
    g_object_ref_sink(widget_.get());
    gtk_button_set_has_frame(GTK_BUTTON(widget_.get()), FALSE);
    gtk_widget_add_css_class(widget_.get(), "artist-card");
    gtk_widget_set_hexpand(widget_.get(), FALSE);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_START);
    gtk_widget_set_size_request(widget_.get(), 160, 220);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(widget_.get(), 10);
    gtk_widget_set_margin_end(widget_.get(), 10);
    gtk_widget_set_margin_top(widget_.get(), 10);
    gtk_widget_set_margin_bottom(widget_.get(), 10);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    // Artwork uses the shared view so missing art gets a fallback icon and
    // available art is decoded through the cached thumbnail pipeline.
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(140, 140);
    gtk_widget_add_css_class(artwork_view_->get_widget(), "artist-artwork");
    artwork_view_->set_artwork_path(artist_.artwork_path);
    gtk_box_append(GTK_BOX(box), artwork_view_->get_widget());

    // Name
    name_label_.reset(gtk_label_new(artist_.name.c_str()));
    g_object_ref_sink(name_label_.get());
    gtk_widget_add_css_class(name_label_.get(), "album-title");
    gtk_label_set_xalign(GTK_LABEL(name_label_.get()), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(name_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(name_label_.get()), 14);
    gtk_box_append(GTK_BOX(box), name_label_.get());

    gtk_button_set_child(GTK_BUTTON(widget_.get()), box);

    g_signal_connect(widget_.get(), "clicked", G_CALLBACK(on_clicked), this);

    // Add right-click context menu
    create_context_menu();
}

void ArtistItem::on_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ArtistItem* self = static_cast<ArtistItem*>(user_data);
    if (self->click_cb_) self->click_cb_(self->artist_);
}

void ArtistItem::set_artist_action_callback(ArtistActionCallback cb) {
    artist_action_cb_ = std::move(cb);
    if (artist_action_cb_) {
        create_context_menu();
    }
}

void ArtistItem::create_context_menu() {
    auto* right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_context_menu), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(right_click));

    auto* action_group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(action_group),
        (GActionEntry[]){
            { "play", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<ArtistItem*>(self);
                if (item->artist_action_cb_) item->artist_action_cb_(item->artist_, "play");
            }, nullptr, nullptr, nullptr },
            { "shuffle", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<ArtistItem*>(self);
                if (item->artist_action_cb_) item->artist_action_cb_(item->artist_, "shuffle");
            }, nullptr, nullptr, nullptr },
            { "add-to-queue", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<ArtistItem*>(self);
                if (item->artist_action_cb_) item->artist_action_cb_(item->artist_, "add_to_queue");
            }, nullptr, nullptr, nullptr },
            { "add-to-playlist", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<ArtistItem*>(self);
                if (item->artist_action_cb_) item->artist_action_cb_(item->artist_, "add_to_playlist");
            }, nullptr, nullptr, nullptr },
        }, 4, this);
    gtk_widget_insert_action_group(widget_.get(), "artist-item", G_ACTION_GROUP(action_group));
}

void ArtistItem::on_context_menu(GtkGestureClick* gesture, int n_press,
                                 double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    ArtistItem* self = static_cast<ArtistItem*>(user_data);
    if (!self->artist_action_cb_) return;

    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(
        self->create_menu_model()));
    gtk_widget_set_parent(popover, self->widget_.get());

    GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_visible(popover, TRUE);
}

GMenu* ArtistItem::create_menu_model() {
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Play", "artist-item.play");
    g_menu_append(menu, "Shuffle", "artist-item.shuffle");
    g_menu_append(menu, "Add to Queue", "artist-item.add-to-queue");
    g_menu_append(menu, "Add to Playlist...", "artist-item.add-to-playlist");
    return menu;
}

} // namespace lollypop
