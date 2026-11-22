/**
 * Lollypop C++ - TrackRow Implementation
 * Shared track row with play/like actions.
 * Spring animations for hover/press states.
 */

#include "ui/components/TrackRow.hpp"
#include "main_window.hpp"
#include "ui/animation/SpringAnimation.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

TrackRow::TrackRow(MainWindow* main_window, const Track& track, int track_number)
    : main_window_(main_window), track_(track), track_number_(track_number) {
    setup_ui();
}

TrackRow::~TrackRow() = default;

void TrackRow::setup_ui() {
    widget_.reset(gtk_list_box_row_new());
    g_object_ref_sink(widget_.get());
    gtk_widget_add_css_class(widget_.get(), "track-row");
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_visible(widget_.get(), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_visible(box, TRUE);

    // Track number
    number_label_.reset(gtk_label_new(
        (track_number_ > 0 ? std::to_string(track_number_) : "").c_str()));
    g_object_ref_sink(number_label_.get());
    gtk_widget_add_css_class(number_label_.get(), "track-row-number");
    gtk_label_set_xalign(GTK_LABEL(number_label_.get()), 0.0);
    gtk_widget_set_size_request(number_label_.get(), 24, -1);
    gtk_widget_set_visible(number_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), number_label_.get());

    // Title
    title_label_.reset(gtk_label_new(track_.title.c_str()));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "track-row-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_label_.get(), TRUE);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), title_label_.get());

    // Artist
    artist_label_.reset(gtk_label_new(track_.artist.c_str()));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "track-row-artist");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(artist_label_.get(), TRUE);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), artist_label_.get());

    // Album
    album_label_.reset(gtk_label_new(track_.album.c_str()));
    g_object_ref_sink(album_label_.get());
    gtk_widget_add_css_class(album_label_.get(), "track-row-album");
    gtk_label_set_xalign(GTK_LABEL(album_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(album_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(album_label_.get(), TRUE);
    gtk_widget_set_visible(album_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), album_label_.get());

    // Duration
    int dur_min = track_.duration / 60;
    int dur_sec = track_.duration % 60;
    char dur_str[16];
    snprintf(dur_str, sizeof(dur_str), "%d:%02d", dur_min, dur_sec);
    duration_label_.reset(gtk_label_new(dur_str));
    g_object_ref_sink(duration_label_.get());
    gtk_widget_add_css_class(duration_label_.get(), "track-row-duration");
    gtk_label_set_xalign(GTK_LABEL(duration_label_.get()), 1.0);
    gtk_widget_set_size_request(duration_label_.get(), 40, -1);
    gtk_widget_set_visible(duration_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), duration_label_.get());

    // Like button
    like_button_.reset(gtk_button_new());
    g_object_ref_sink(like_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(like_button_.get()), FALSE);
    gtk_widget_add_css_class(like_button_.get(), "track-row-like-button");
    GtkWidget* like_icon = gtk_image_new_from_icon_name(
        liked_ ? "starred-symbolic" : "non-starred-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(like_icon), 18);
    gtk_button_set_child(GTK_BUTTON(like_button_.get()), like_icon);
    gtk_widget_set_halign(like_button_.get(), GTK_ALIGN_END);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(like_button_.get(), 10);
    gtk_widget_set_margin_end(like_button_.get(), 10);
    gtk_widget_set_margin_top(like_button_.get(), 10);
    gtk_widget_set_margin_bottom(like_button_.get(), 10);
    g_signal_connect(like_button_.get(), "clicked",
                     G_CALLBACK(on_like_clicked), this);
    gtk_widget_set_visible(like_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), like_button_.get());

    // Play button (hidden, revealed on hover)
    play_button_.reset(gtk_button_new());
    g_object_ref_sink(play_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(play_button_.get()), FALSE);
    gtk_widget_add_css_class(play_button_.get(), "track-row-play-button");
    GtkWidget* play_icon = gtk_image_new_from_icon_name(
        "media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_icon), 18);
    gtk_button_set_child(GTK_BUTTON(play_button_.get()), play_icon);
    gtk_widget_set_halign(play_button_.get(), GTK_ALIGN_END);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(play_button_.get(), 10);
    gtk_widget_set_margin_end(play_button_.get(), 10);
    gtk_widget_set_margin_top(play_button_.get(), 10);
    gtk_widget_set_margin_bottom(play_button_.get(), 10);
    gtk_widget_set_visible(play_button_.get(), FALSE);
    g_signal_connect(play_button_.get(), "clicked",
                     G_CALLBACK(on_play_clicked), this);
    gtk_box_append(GTK_BOX(box), play_button_.get());

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(widget_.get()), box);

    g_signal_connect(widget_.get(), "activate",
                     G_CALLBACK(on_row_activated), this);

    // Use GtkEventControllerMotion for hover detection (GTK4 compatible)
    // instead of enter-notify-event/leave-notify-event which don't work
    // on GtkListBoxRow.
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_object_set_data_full(G_OBJECT(motion_ctrl), "track-row-this", this,
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

void TrackRow::set_playing(bool playing) {
    playing_ = playing;
    if (playing) {
        gtk_widget_add_css_class(widget_.get(), "playing");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "playing");
    }
}

void TrackRow::set_liked(bool liked) {
    liked_ = liked;
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(like_button_.get()));
    if (GTK_IS_IMAGE(child)) {
        // Reuse existing image widget, just change the icon name
        gtk_image_set_from_icon_name(GTK_IMAGE(child),
                                     liked_ ? "starred-symbolic" : "non-starred-symbolic");
    } else {
        // Fallback: create new icon if child is not an image
        GtkWidget* new_icon = gtk_image_new_from_icon_name(
            liked_ ? "starred-symbolic" : "non-starred-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(new_icon), 18);
        gtk_button_set_child(GTK_BUTTON(like_button_.get()), new_icon);
        // gtk_button_set_child() automatically unrefs the old child
    }
}

void TrackRow::on_play_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    if (self->play_cb_) self->play_cb_(self->track_);
}

void TrackRow::on_like_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    if (self->like_cb_) self->like_cb_(self->track_, !self->liked_);
}

void TrackRow::on_row_activated(GtkListBoxRow* row, gpointer user_data) {
    (void)row;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    if (self->play_cb_) self->play_cb_(self->track_);
}

gboolean TrackRow::on_enter(GtkEventControllerMotion* controller, double x,
                            double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), TRUE);
    return FALSE;
}

gboolean TrackRow::on_leave(GtkEventControllerMotion* controller,
                            gpointer user_data) {
    (void)controller;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    gtk_widget_set_visible(self->play_button_.get(), FALSE);
    self->animate_hover(false);
    return FALSE;
}

gboolean TrackRow::on_pressed(GtkGestureClick* gesture, int n_press,
                              double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_press(true);
    return FALSE;
}

gboolean TrackRow::on_released(GtkGestureClick* gesture, int n_press,
                               double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_press(false);
    return FALSE;
}

void TrackRow::animate_hover(bool hover) {
    // Use CSS class for hover effect (lighter than spring animation for list rows)
    if (hover) {
        gtk_widget_add_css_class(widget_.get(), "hover");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "hover");
    }
}

void TrackRow::animate_press(bool press) {
    // Use CSS class for press effect
    if (press) {
        gtk_widget_add_css_class(widget_.get(), "pressed");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "pressed");
    }
}

} // namespace lollypop
