/**
 * Lollypop C++ - Lyric Row Implementation
 */

#include "ui/components/LyricRow.hpp"

#include <gtk/gtk.h>
#include <memory>

namespace lollypop {

void LyricRow::create_widget() {
    // Use a GtkBox as the row container
    widget_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_ref_sink(widget_);
    gtk_widget_add_css_class(widget_, "lyrics-row");
    gtk_widget_add_css_class(widget_, "future");
    gtk_widget_set_hexpand(widget_, TRUE);
    gtk_widget_set_cursor_from_name(widget_, "pointer");

    // Create the label
    label_ = SyncedLyricsLabel(line_.text.empty() ? " " : line_.text, 0.5);
    label_.set_accent(accent_);
    label_.set_line_wrap(true);
    label_.set_line_wrap_mode(PANGO_WRAP_WORD_CHAR);

    if (line_.is_gap) {
        label_.set_as_gap(true);
    }

    GtkWidget* label_widget = label_.get_widget();
    gtk_widget_set_hexpand(label_widget, TRUE);
    gtk_widget_set_halign(label_widget, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(widget_), label_widget);

    // Click gesture for seeking
    auto* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "released", G_CALLBACK(on_button_release), this);
    gtk_widget_add_controller(widget_, GTK_EVENT_CONTROLLER(click));

    // Hover effects
    auto* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_, GTK_EVENT_CONTROLLER(motion));

    widget_owner_.reset(widget_);

    // Initial state
    set_state(State::Future);
}

LyricRow::LyricRow(const SyncedLyricLine& line, const GdkRGBA& accent, SeekCallback on_seek, double xalign)
    : line_(line), accent_(accent), on_seek_(std::move(on_seek)) {
    create_widget();
}

LyricRow::~LyricRow() = default;

void LyricRow::set_accent(const GdkRGBA& accent) {
    accent_ = accent;
    label_.set_accent(accent);
}

void LyricRow::set_state(State state) {
    if (state_ == state) return;
    state_ = state;

    gtk_widget_remove_css_class(widget_, "active");
    gtk_widget_remove_css_class(widget_, "past");
    gtk_widget_remove_css_class(widget_, "future");

    switch (state) {
        case State::Active:
            gtk_widget_add_css_class(widget_, "active");
            label_.set_active(true);
            break;
        case State::Past:
            gtk_widget_add_css_class(widget_, "past");
            label_.set_active(false);
            break;
        case State::Future:
            gtk_widget_add_css_class(widget_, "future");
            label_.set_active(false);
            break;
    }

    // The active row uses a larger font; remeasure after the style change.
    label_.queue_resize();
}

void LyricRow::set_progress(double fraction) {
    if (state_ == State::Active) {
        label_.set_progress(fraction);
    }
}

void LyricRow::set_gap_phase(double phase) {
    if (line_.is_gap) {
        label_.set_gap_phase(phase);
    }
}

void LyricRow::on_button_release(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    auto* self = static_cast<LyricRow*>(user_data);
    if (self->on_seek_) {
        self->on_seek_(self->line_.start_ms);
    }
}

void LyricRow::on_enter(GtkEventControllerMotion* motion, double x, double y, gpointer user_data) {
    (void)motion; (void)x; (void)y;
    auto* self = static_cast<LyricRow*>(user_data);
    gtk_widget_add_css_class(self->widget_, "hover");
}

void LyricRow::on_leave(GtkEventControllerMotion* motion, gpointer user_data) {
    (void)motion;
    auto* self = static_cast<LyricRow*>(user_data);
    gtk_widget_remove_css_class(self->widget_, "hover");
}

} // namespace lollypop