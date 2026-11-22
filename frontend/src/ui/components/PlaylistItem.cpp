/**
 * Lollypop C++ - PlaylistItem Implementation
 */

#include "ui/components/PlaylistItem.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
#include <iostream>

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

    // Play button (revealed on hover/focus)
    play_button_.reset(gtk_button_new());
    g_object_ref_sink(play_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(play_button_.get()), FALSE);
    gtk_widget_add_css_class(play_button_.get(), "album-play-button");
    GtkWidget* play_icon = gtk_image_new_from_icon_name(
        "media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_icon), 20);
    gtk_button_set_child(GTK_BUTTON(play_button_.get()), play_icon);
    gtk_widget_set_halign(play_button_.get(), GTK_ALIGN_CENTER);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(play_button_.get(), 10);
    gtk_widget_set_margin_end(play_button_.get(), 10);
    gtk_widget_set_margin_top(play_button_.get(), 10);
    gtk_widget_set_margin_bottom(play_button_.get(), 10);
    gtk_widget_set_visible(play_button_.get(), FALSE);
    g_signal_connect(play_button_.get(), "clicked",
                     G_CALLBACK(on_play_clicked), this);
    gtk_box_append(GTK_BOX(box), play_button_.get());

    gtk_button_set_child(GTK_BUTTON(widget_.get()), box);

    g_signal_connect(widget_.get(), "clicked", G_CALLBACK(on_clicked), this);

    // Hover reveals play button
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_object_set_data_full(G_OBJECT(motion_ctrl), "playlist-item-this", this,
                           nullptr);
    g_signal_connect(motion_ctrl, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion_ctrl);
}

void PlaylistItem::on_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    if (self->click_cb_) self->click_cb_(self->playlist_);
}

void PlaylistItem::on_play_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    if (self->play_cb_) self->play_cb_(self->playlist_);
}

gboolean PlaylistItem::on_enter(GtkEventControllerMotion* controller, double x,
                                double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), TRUE);
    return FALSE;
}

gboolean PlaylistItem::on_leave(GtkEventControllerMotion* controller,
                                gpointer user_data) {
    (void)controller;
    PlaylistItem* self = static_cast<PlaylistItem*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), FALSE);
    return FALSE;
}

} // namespace lollypop
