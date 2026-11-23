/**
 * Lollypop C++ - Volume Dial Implementation
 * Circular volume dial with spring animations and glass material.
 */

#include "ui/components/VolumeDial.hpp"
#include "ui/animation/SpringAnimation.hpp"

#include <gtk/gtk.h>
#include <cmath>
#include <algorithm>

namespace lollypop {

VolumeDial::VolumeDial() {
    setup_ui();
}

VolumeDial::~VolumeDial() = default;

void VolumeDial::setup_ui() {
    // Main drawing area for circular dial
    widget_.reset(gtk_drawing_area_new());
    g_object_ref_sink(widget_.get());
    gtk_widget_add_css_class(widget_.get(), "volume-dial");
    gtk_widget_set_size_request(widget_.get(), 48, 48);
    gtk_widget_set_cursor_from_name(widget_.get(), "pointer");
    gtk_widget_set_visible(widget_.get(), TRUE);

    // Accessibility: volume dial is a slider (GtkDrawingArea defaults to generic)
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Volume",
                                   -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_VALUE_MIN,
                                   0.0,
                                   -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_VALUE_MAX,
                                   100.0,
                                   -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                   static_cast<double>(volume_ * 100),
                                   -1);

    // Set up drawing callback
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget_.get()),
                                    on_draw, this, nullptr);

    // Click gesture for mute toggle
    auto* click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_button_press), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(click_gesture));

    // Scroll gesture for volume adjustment
    auto* scroll_controller = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(on_scroll), this);
    gtk_widget_add_controller(widget_.get(), scroll_controller);

    // Hover enter/leave for spring animation
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_signal_connect(motion_ctrl, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion_ctrl);

    // Press/release for spring animation
    auto* press_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(press_gesture), 0);
    g_signal_connect(press_gesture, "pressed", G_CALLBACK(on_pressed), this);
    g_signal_connect(press_gesture, "released", G_CALLBACK(on_released), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(press_gesture));

    // Keyboard support: arrow keys for volume
    auto* key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), this);
    gtk_widget_add_controller(widget_.get(), key_controller);
}

void VolumeDial::set_volume(double volume) {
    volume = std::clamp(volume, 0.0, 1.0);
    if (!muted_) {
        volume_ = volume;
        // Keep the last audible level. Backend state can legitimately be 0.0
        // at startup; overwriting last_volume_ with it would make unmuting
        // silent and require multiple clicks before audio returns.
        if (volume > 0.0) last_volume_ = volume;
    }
    gtk_widget_queue_draw(widget_.get());
    update_icon();
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                   volume_ * 100.0, -1);
}

void VolumeDial::set_muted(bool muted) {
    // Allow the !muted path when the level is 0 so a single click can restore
    // audio even if the persisted backend volume was zero.
    if (muted_ == muted && !(muted == false && volume_ <= 0.0)) return;

    if (muted) {
        if (volume_ > 0.0) last_volume_ = volume_;
        volume_ = 0.0;
    } else {
        if (last_volume_ <= 0.0) last_volume_ = 0.7;
        volume_ = last_volume_;
    }
    muted_ = muted;
    gtk_widget_queue_draw(widget_.get());
    update_icon();
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget_.get()),
                                   GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                   muted_ ? 0.0 : volume_ * 100.0, -1);
    emit_volume_changed();
}

void VolumeDial::toggle_mute() {
    if (muted_ || volume_ <= 0.0) {
        set_muted(false);
    } else {
        set_muted(true);
    }
}

void VolumeDial::update_icon() {
    // The icon is drawn in on_draw, just queue redraw
    gtk_widget_queue_draw(widget_.get());
}

void VolumeDial::emit_volume_changed() {
    if (volume_changed_cb_) {
        volume_changed_cb_(muted_ ? 0.0 : volume_);
    }
}

// Static callback for drawing the circular dial
void VolumeDial::on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    (void)area;

    double center_x = width / 2.0;
    double center_y = height / 2.0;
    double radius = std::min(center_x, center_y) - 4; // 4px padding
    double stroke_width = 3.0;

    // Use hardcoded colors for now (CSS variables not easily accessible in cairo)
    // Background: semi-transparent dark
    // Foreground: blue accent
    // Muted: gray
    GdkRGBA bg_color = {0.2, 0.2, 0.25, 0.3};
    GdkRGBA fg_color = {0.0, 0.6, 1.0, 0.9};
    GdkRGBA muted_color = {0.5, 0.5, 0.5, 0.9};

    // Draw background circle
    cairo_set_line_width(cr, stroke_width);
    cairo_set_source_rgba(cr, bg_color.red, bg_color.green, bg_color.blue, bg_color.alpha);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Draw volume arc
    if (!self->muted_ && self->volume_ > 0.0) {
        double angle = self->volume_ * 2 * M_PI;
        cairo_set_line_width(cr, stroke_width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_source_rgba(cr, fg_color.red, fg_color.green, fg_color.blue, fg_color.alpha);
        cairo_arc(cr, center_x, center_y, radius, -M_PI / 2, -M_PI / 2 + angle);
        cairo_stroke(cr);
    }

    // Draw center icon (speaker/mute)
    double icon_size = radius * 0.5;
    cairo_set_font_size(cr, icon_size);
    cairo_set_source_rgba(cr,
        self->muted_ ? muted_color.red : fg_color.red,
        self->muted_ ? muted_color.green : fg_color.green,
        self->muted_ ? muted_color.blue : fg_color.blue,
        self->muted_ ? muted_color.alpha : fg_color.alpha);

    // Simple text-based icon (in production, use icon font or SVG)
    const char* icon_text = self->muted_ ? "\uf026" : // mute icon
                            (self->volume_ > 0.66 ? "\uf028" : // high volume
                             self->volume_ > 0.33 ? "\uf027" : // medium volume
                             "\uf026"); // low volume
    (void)icon_text; // Suppress unused warning

    // For now, draw a simple speaker shape
    if (self->muted_) {
        // Draw mute: circle with slash
        cairo_set_line_width(cr, 2.0);
        cairo_arc(cr, center_x, center_y, icon_size * 0.6, 0, 2 * M_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, center_x - icon_size * 0.4, center_y - icon_size * 0.4);
        cairo_line_to(cr, center_x + icon_size * 0.4, center_y + icon_size * 0.4);
        cairo_stroke(cr);
    } else {
        // Draw speaker
        double speaker_x = center_x - icon_size * 0.2;
        double speaker_y = center_y - icon_size * 0.3;

        // Speaker body
        cairo_move_to(cr, speaker_x, speaker_y + icon_size * 0.1);
        cairo_line_to(cr, speaker_x + icon_size * 0.2, speaker_y);
        cairo_line_to(cr, speaker_x + icon_size * 0.2, speaker_y + icon_size * 0.6);
        cairo_line_to(cr, speaker_x, speaker_y + icon_size * 0.5);
        cairo_close_path(cr);
        cairo_fill(cr);

        // Sound waves based on volume
        int waves = self->volume_ > 0.66 ? 3 : (self->volume_ > 0.33 ? 2 : 1);
        for (int i = 0; i < waves; i++) {
            double wave_radius = icon_size * 0.3 + i * icon_size * 0.15;
            cairo_set_line_width(cr, 2.0);
            cairo_arc(cr, speaker_x + icon_size * 0.4, center_y, wave_radius,
                      -M_PI / 3, M_PI / 3);
            cairo_stroke(cr);
        }
    }
}

// Click to mute/unmute
gboolean VolumeDial::on_button_press(GtkGestureClick* gesture, int n_press,
                                     double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    self->toggle_mute();
    return FALSE;
}

// Scroll to adjust volume
gboolean VolumeDial::on_scroll(GtkEventControllerScroll* controller,
                               double dx, double dy, gpointer user_data) {
    (void)controller; (void)dx;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);

    // Scroll up = increase volume, scroll down = decrease
    double delta = -dy * 0.05; // 5% per scroll step
    double new_volume = std::clamp(self->volume_ + delta, 0.0, 1.0);

    if (new_volume > 0 && self->muted_) {
        self->set_muted(false);
    }
    self->volume_ = new_volume;
    if (new_volume > 0.0) self->last_volume_ = new_volume;
    gtk_widget_queue_draw(self->widget_.get());
    self->update_icon();
    self->emit_volume_changed();

    return TRUE;
}

// Scale changed in popover (if we add one later)
void VolumeDial::on_scale_changed(GtkRange* range, gpointer user_data) {
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    double value = gtk_range_get_value(range) / 100.0;
    self->volume_ = value;
    if (value > 0 && self->muted_) {
        self->muted_ = false;
    }
    gtk_widget_queue_draw(self->widget_.get());
    self->update_icon();
    self->emit_volume_changed();
}

// Hover enter
gboolean VolumeDial::on_enter(GtkEventControllerMotion* controller,
                              double x, double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    gtk_widget_add_css_class(self->widget_.get(), "hover");
    return FALSE;
}

// Hover leave
gboolean VolumeDial::on_leave(GtkEventControllerMotion* controller,
                              gpointer user_data) {
    (void)controller;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    gtk_widget_remove_css_class(self->widget_.get(), "hover");
    return FALSE;
}

// Press
gboolean VolumeDial::on_pressed(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    gtk_widget_add_css_class(self->widget_.get(), "pressed");
    return FALSE;
}

// Release
gboolean VolumeDial::on_released(GtkGestureClick* gesture, int n_press,
                                 double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);
    gtk_widget_remove_css_class(self->widget_.get(), "pressed");
    return FALSE;
}

// Keyboard support
gboolean VolumeDial::on_key_pressed(GtkEventControllerKey* controller,
                                    guint keyval, guint keycode,
                                    GdkModifierType state, gpointer user_data) {
    (void)controller; (void)keycode; (void)state;
    VolumeDial* self = static_cast<VolumeDial*>(user_data);

    double delta = 0.05; // 5% per key press
    switch (keyval) {
        case GDK_KEY_Up:
        case GDK_KEY_Right:
        case GDK_KEY_KP_Add:
            self->volume_ = std::clamp(self->volume_ + delta, 0.0, 1.0);
            if (self->volume_ > 0.0) self->last_volume_ = self->volume_;
            if (self->muted_ && self->volume_ > 0.0) {
                self->muted_ = false;
            }
            gtk_widget_queue_draw(self->widget_.get());
            self->emit_volume_changed();
            // Update accessibility value
            gtk_accessible_update_property(GTK_ACCESSIBLE(self->widget_.get()),
                                           GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                           static_cast<double>(self->volume_ * 100),
                                           -1);
            return TRUE;

        case GDK_KEY_Down:
        case GDK_KEY_Left:
        case GDK_KEY_KP_Subtract:
            self->volume_ = std::clamp(self->volume_ - delta, 0.0, 1.0);
            if (self->volume_ > 0.0) self->last_volume_ = self->volume_;
            if (self->muted_ && self->volume_ > 0.0) {
                self->muted_ = false;
            }
            gtk_widget_queue_draw(self->widget_.get());
            self->emit_volume_changed();
            // Update accessibility value
            gtk_accessible_update_property(GTK_ACCESSIBLE(self->widget_.get()),
                                           GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                           static_cast<double>(self->volume_ * 100),
                                           -1);
            return TRUE;

        case GDK_KEY_m:
        case GDK_KEY_M:
            self->toggle_mute();
            gtk_accessible_update_property(GTK_ACCESSIBLE(self->widget_.get()),
                                           GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                           self->muted_ ? 0.0 : static_cast<double>(self->volume_ * 100),
                                           -1);
            return TRUE;

        default:
            return FALSE;
    }
}

} // namespace lollypop