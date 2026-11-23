/**
 * Lollypop C++ - AlbumCard Implementation
 * Spring animations for hover/press states.
 */

#include "ui/components/AlbumCard.hpp"
#include "main_window.hpp"
#include "ui/animation/SpringAnimation.hpp"

#include <gtk/gtk.h>
#include <algorithm>
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
    // Album cards are square artwork tiles; metadata is intentionally hidden.
    gtk_widget_set_size_request(widget_.get(), 160, 160);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(widget_.get(), 10);
    gtk_widget_set_margin_end(widget_.get(), 10);
    gtk_widget_set_margin_top(widget_.get(), 10);
    gtk_widget_set_margin_bottom(widget_.get(), 10);
    gtk_widget_set_visible(widget_.get(), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Artwork
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "album-artwork");
    gtk_widget_set_size_request(artwork_image_.get(), 160, 160);
    if (!album_.artwork_path.empty()) {
        // Scale to the tile size before giving it to GtkPicture: its natural
        // size otherwise comes from the full-resolution cover and breaks the
        // square card grid.
        GError* error = nullptr;
        GdkPixbuf* source = gdk_pixbuf_new_from_file(album_.artwork_path.c_str(), &error);
        if (source) {
            int width = gdk_pixbuf_get_width(source);
            int height = gdk_pixbuf_get_height(source);
            int side = std::min(width, height);
            if (side > 0) {
                GdkPixbuf* square = gdk_pixbuf_new_subpixbuf(
                    source, (width - side) / 2, (height - side) / 2, side, side);
                GdkPixbuf* scaled = gdk_pixbuf_scale_simple(
                    square, 160, 160, GDK_INTERP_BILINEAR);
                GdkTexture* texture = gdk_texture_new_for_pixbuf(scaled);
                gtk_picture_set_paintable(GTK_PICTURE(artwork_image_.get()),
                                          GDK_PAINTABLE(texture));
                g_object_unref(texture);
                g_object_unref(scaled);
                g_object_unref(square);
            }
            g_object_unref(source);
        } else if (error) {
            g_error_free(error);
        }
    }
    gtk_widget_set_visible(artwork_image_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), artwork_image_.get());

    // The design calls for pure square artwork: no title/artist lines and no
    // inline play button that would change the card's aspect ratio. Hover
    // feedback is provided by CSS instead.

    gtk_button_set_child(GTK_BUTTON(widget_.get()), box);

    // Click handler
    g_signal_connect(widget_.get(), "clicked", G_CALLBACK(on_clicked), this);

    // Hover animation via GtkEventControllerMotion (GTK4 compatible)
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_object_set_data_full(G_OBJECT(motion_ctrl), "album-card-this", this,
                           nullptr);
    g_signal_connect(motion_ctrl, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion_ctrl);

    // Press gesture for spring animation
    auto* click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 0); // All buttons
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_pressed), this);
    g_signal_connect(click_gesture, "released", G_CALLBACK(on_released), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(click_gesture));
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

gboolean AlbumCard::on_enter(GtkEventControllerMotion* controller, double x,
                             double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    self->animate_hover(true);
    return FALSE;
}

gboolean AlbumCard::on_leave(GtkEventControllerMotion* controller,
                             gpointer user_data) {
    (void)controller;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    self->animate_hover(false);
    return FALSE;
}

gboolean AlbumCard::on_pressed(GtkGestureClick* gesture, int n_press,
                               double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    self->animate_press(true);
    return FALSE;
}

gboolean AlbumCard::on_released(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    AlbumCard* self = static_cast<AlbumCard*>(user_data);
    self->animate_press(false);
    return FALSE;
}

void AlbumCard::animate_hover(bool hover) {
    // GTK widgets have no animatable "scale" property; CSS :hover/:active
    // handles the visual feedback instead.
    if (hover) {
        gtk_widget_add_css_class(widget_.get(), "hover");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "hover");
    }
}

void AlbumCard::animate_press(bool /*press*/) {
    // Handled by .album-card:active in CSS.
}

} // namespace lollypop
