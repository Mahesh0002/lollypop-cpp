/**
 * Lollypop C++ - PlaylistItem Implementation
 */

#include "ui/components/PlaylistItem.hpp"
#include "main_window.hpp"
#include "util/accessibility.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cstring>
#include <iostream>
#include <string>

namespace lollypop {

PlaylistItem::PlaylistItem(MainWindow* main_window, const Playlist& playlist)
    : main_window_(main_window), playlist_(playlist) {
    setup_ui();
}

PlaylistItem::~PlaylistItem() = default;

void PlaylistItem::setup_ui() {
    widget_.reset(gtk_button_new());
    g_object_ref_sink(widget_.get());
    gtk_button_set_has_frame(GTK_BUTTON(widget_.get()), FALSE);
    gtk_widget_add_css_class(widget_.get(), "album-card");
    const std::string tooltip = "View " + playlist_.name;
    const std::string accessible_label = "Playlist: " + playlist_.name;
    gtk_widget_set_tooltip_text(widget_.get(), tooltip.c_str());
    accessibility::set_label(widget_.get(), accessible_label.c_str());
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

    // Artwork (placeholder)
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "album-artwork");
    gtk_widget_set_size_request(artwork_image_.get(), 140, 140);
    // Keep the placeholder square inside the wider card instead of letting
    // the vertical box stretch it.
    gtk_widget_set_hexpand(artwork_image_.get(), FALSE);
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), artwork_image_.get());

    // Name
    name_label_.reset(gtk_label_new(playlist_.name.c_str()));
    g_object_ref_sink(name_label_.get());
    gtk_widget_add_css_class(name_label_.get(), "album-title");
    gtk_label_set_xalign(GTK_LABEL(name_label_.get()), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(name_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(name_label_.get()), 14);
    gtk_box_append(GTK_BOX(box), name_label_.get());

    // Track count
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%zu tracks",
             playlist_.track_ids.size());
    count_label_.reset(gtk_label_new(count_str));
    g_object_ref_sink(count_label_.get());
    gtk_widget_add_css_class(count_label_.get(), "album-artist");
    gtk_label_set_xalign(GTK_LABEL(count_label_.get()), 0.5);
    gtk_box_append(GTK_BOX(box), count_label_.get());

    gtk_button_set_child(GTK_BUTTON(widget_.get()), box);

    g_signal_connect(widget_.get(), "clicked", G_CALLBACK(on_clicked), this);

    // Add right-click context menu
    create_context_menu();
}

void PlaylistItem::on_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    if (self->click_cb_) self->click_cb_(self->playlist_);
}

void PlaylistItem::set_playlist_action_callback(PlaylistActionCallback cb) {
    playlist_action_cb_ = std::move(cb);
    if (playlist_action_cb_) {
        create_context_menu();
    }
}

void PlaylistItem::create_context_menu() {
    auto* right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_context_menu), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(right_click));

    auto* action_group = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(action_group),
        (GActionEntry[]){
            { "play", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<PlaylistItem*>(self);
                if (item->playlist_action_cb_) item->playlist_action_cb_(item->playlist_, "play");
            }, nullptr, nullptr, nullptr },
            { "shuffle", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<PlaylistItem*>(self);
                if (item->playlist_action_cb_) item->playlist_action_cb_(item->playlist_, "shuffle");
            }, nullptr, nullptr, nullptr },
            { "add-to-queue", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<PlaylistItem*>(self);
                if (item->playlist_action_cb_) item->playlist_action_cb_(item->playlist_, "add_to_queue");
            }, nullptr, nullptr, nullptr },
            { "rename", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<PlaylistItem*>(self);
                if (item->playlist_action_cb_) item->playlist_action_cb_(item->playlist_, "rename");
            }, nullptr, nullptr, nullptr },
            { "delete", [](GSimpleAction*, GVariant*, gpointer self) {
                auto* item = static_cast<PlaylistItem*>(self);
                if (item->playlist_action_cb_) item->playlist_action_cb_(item->playlist_, "delete");
            }, nullptr, nullptr, nullptr },
        }, 5, this);
    gtk_widget_insert_action_group(widget_.get(), "playlist-item", G_ACTION_GROUP(action_group));
}

void PlaylistItem::on_context_menu(GtkGestureClick* gesture, int n_press,
                                   double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    if (!self->playlist_action_cb_) return;

    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(
        self->create_menu_model()));
    gtk_widget_set_parent(popover, self->widget_.get());

    GdkRectangle rect = {static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_visible(popover, TRUE);
}

GMenu* PlaylistItem::create_menu_model() {
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Play", "playlist-item.play");
    g_menu_append(menu, "Shuffle", "playlist-item.shuffle");
    g_menu_append(menu, "Add to Queue", "playlist-item.add-to-queue");
    g_menu_append(menu, "Rename...", "playlist-item.rename");
    g_menu_append(menu, "Delete", "playlist-item.delete");
    return menu;
}

} // namespace lollypop
