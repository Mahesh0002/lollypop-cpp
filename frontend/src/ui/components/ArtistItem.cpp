/**
 * Lollypop C++ - ArtistItem Implementation
 */

#include "ui/components/ArtistItem.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
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
    gtk_widget_add_css_class(artwork_image_.get(), "artist-artwork");
    gtk_widget_set_size_request(artwork_image_.get(), 140, 140);
    if (!artist_.artwork_path.empty()) {
        GFile* file = g_file_new_for_path(artist_.artwork_path.c_str());
        gtk_picture_set_file(GTK_PICTURE(artwork_image_.get()), file);
        g_object_unref(file);
    }
    gtk_box_append(GTK_BOX(box), artwork_image_.get());

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
}

void ArtistItem::on_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    ArtistItem* self = static_cast<ArtistItem*>(user_data);
    if (self->click_cb_) self->click_cb_(self->artist_);
}

} // namespace lollypop
