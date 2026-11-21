/**
 * Lollypop C++ - AlbumCard Implementation
 */

#include "ui/components/AlbumCard.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

AlbumCard::AlbumCard(MainWindow* main_window, const Album& album)
    : main_window_(main_window), album_(album) {
    setup_ui();
}

AlbumCard::~AlbumCard() = default;

void AlbumCard::setup_ui() {
    widget_.reset(gtk_button_new());
    g_object_ref_sink(widget_.get());
    gtk_button_set_has_frame(GTK_BUTTON(widget_.get()), FALSE);
    gtk_widget_add_css_class(widget_.get(), "album-card");
    gtk_widget_set_hexpand(widget_.get(), FALSE);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_START);
    gtk_widget_set_size_request(widget_.get(), 160, 220);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    // Artwork
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "album-artwork");
    gtk_widget_set_size_request(artwork_image_.get(), 140, 140);
    if (!album_.artwork_path.empty()) {
        GFile* file = g_file_new_for_path(album_.artwork_path.c_str());
        gtk_picture_set_file(GTK_PICTURE(artwork_image_.get()), file);
        g_object_unref(file);
    }
    gtk_box_append(GTK_BOX(box), artwork_image_.get());

    // Title
    title_label_.reset(gtk_label_new(album_.title.c_str()));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "album-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(title_label_.get()), 14);
    gtk_box_append(GTK_BOX(box), title_label_.get());

    // Artist
    artist_label_.reset(gtk_label_new(album_.artist.c_str()));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "album-artist");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(artist_label_.get()), 14);
    gtk_box_append(GTK_BOX(box), artist_label_.get());

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
    gtk_widget_set_visible(play_button_.get(), FALSE);
    g_signal_connect(play_button_.get(), "clicked",
                     G_CALLBACK(on_play_clicked), this);
    gtk_box_append(GTK_BOX(box), play_button_.get());

    gtk_button_set_child(GTK_BUTTON(widget_.get()), box);

    // Click handler
    g_signal_connect(widget_.get(), "clicked", G_CALLBACK(on_clicked), this);

    // Hover reveals play button via GtkEventControllerMotion (GTK4 compatible)
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_object_set_data_full(G_OBJECT(motion_ctrl), "album-card-this", this,
                           nullptr);
    g_signal_connect(motion_ctrl, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion_ctrl);
}

void AlbumCard::set_active(bool active) {
    active_ = active;
    if (active) {
        gtk_widget_add_css_class(widget_.get(), "active");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "active");
    }
}

void AlbumCard::on_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    if (self->click_cb_) self->click_cb_(self->album_);
}

void AlbumCard::on_play_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    if (self->play_cb_) self->play_cb_(self->album_);
}

gboolean AlbumCard::on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), TRUE);
    return FALSE;
}

gboolean AlbumCard::on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data) {
    (void)controller;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), FALSE);
    return FALSE;
}

} // namespace lollypop
