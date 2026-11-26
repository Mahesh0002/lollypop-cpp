/**
 * Lollypop C++ - Shared Wavy Progress Bar Implementation
 */

#include "panels/progress_wave.hpp"

#include <gtk/gtk.h>
#include <libadwaita-1/adwaita.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lollypop {

ProgressWave::ProgressWave()
    : overlay_(gtk_overlay_new()),
      wave_area_(gtk_drawing_area_new()),
      scale_(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                      0.0, 1.0, 0.001)) {
    gtk_widget_add_css_class(overlay_, "progress-wave");
    gtk_widget_set_size_request(overlay_, -1, 28);
    gtk_widget_set_valign(overlay_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(overlay_, TRUE);

    gtk_widget_add_css_class(wave_area_, "progress-wavy-area");
    gtk_widget_set_hexpand(wave_area_, TRUE);
    gtk_widget_set_halign(wave_area_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(wave_area_, GTK_ALIGN_FILL);
    gtk_widget_set_can_target(wave_area_, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(wave_area_),
                                   on_draw, this, nullptr);
    gtk_widget_set_visible(wave_area_, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay_), wave_area_);

    gtk_widget_add_css_class(scale_, "progress-wavy-scale");
    gtk_widget_set_hexpand(scale_, TRUE);
    gtk_widget_set_halign(scale_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(scale_, GTK_ALIGN_FILL);
    gtk_widget_set_opacity(scale_, 0.0);
    gtk_scale_set_draw_value(GTK_SCALE(scale_), FALSE);
    gtk_range_set_value(GTK_RANGE(scale_), 0.0);
    gtk_accessible_update_property(GTK_ACCESSIBLE(scale_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Playback position", -1);
    gtk_widget_set_visible(scale_, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay_), scale_);

    g_signal_connect(scale_, "value-changed",
                     G_CALLBACK(on_value_changed), this);

    gesture_ = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture_), GDK_BUTTON_PRIMARY);
    gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(gesture_), FALSE);
    g_signal_connect(gesture_, "pressed", G_CALLBACK(on_pressed), this);
    g_signal_connect(gesture_, "released", G_CALLBACK(on_released), this);
    gtk_widget_add_controller(scale_, GTK_EVENT_CONTROLLER(gesture_));

    // GTK owns the widgets once they are attached to the tree. Weak pointers
    // let us detect teardown without ever touching a finalized GObject.
    g_object_add_weak_pointer(G_OBJECT(overlay_),
                              reinterpret_cast<gpointer*>(&overlay_));
    g_object_add_weak_pointer(G_OBJECT(wave_area_),
                              reinterpret_cast<gpointer*>(&wave_area_));
    g_object_add_weak_pointer(G_OBJECT(scale_),
                              reinterpret_cast<gpointer*>(&scale_));
    g_object_add_weak_pointer(G_OBJECT(gesture_),
                              reinterpret_cast<gpointer*>(&gesture_));
}

ProgressWave::~ProgressWave() {
    // Only GLib sources are unconditionally safe here. The GTK tree may already
    // have been destroyed, so all widget access goes through weak pointers.
    if (wave_tick_source_ != 0) {
        g_source_remove(wave_tick_source_);
        wave_tick_source_ = 0;
    }
    if (seek_commit_source_ != 0) {
        g_source_remove(seek_commit_source_);
        seek_commit_source_ = 0;
    }

    if (wave_area_) {
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(wave_area_),
                                       nullptr, nullptr, nullptr);
    }
    if (scale_) {
        g_signal_handlers_disconnect_by_data(scale_, this);
    }
    if (gesture_) {
        g_signal_handlers_disconnect_by_data(gesture_, this);
    }
    remove_weak_pointers();
}

void ProgressWave::remove_weak_pointers() {
    if (overlay_) {
        g_object_remove_weak_pointer(G_OBJECT(overlay_),
                                     reinterpret_cast<gpointer*>(&overlay_));
    }
    if (wave_area_) {
        g_object_remove_weak_pointer(G_OBJECT(wave_area_),
                                     reinterpret_cast<gpointer*>(&wave_area_));
    }
    if (scale_) {
        g_object_remove_weak_pointer(G_OBJECT(scale_),
                                     reinterpret_cast<gpointer*>(&scale_));
    }
    if (gesture_) {
        g_object_remove_weak_pointer(G_OBJECT(gesture_),
                                     reinterpret_cast<gpointer*>(&gesture_));
    }
}

void ProgressWave::set_duration(double duration) {
    duration_ = std::max(0.0, duration);
}

void ProgressWave::set_progress(double fraction) {
    if (seeking_) return;

    progress_ = std::clamp(fraction, 0.0, 1.0);
    if (scale_) {
        updating_ = true;
        gtk_range_set_value(GTK_RANGE(scale_), progress_);
        updating_ = false;
    }
    if (wave_area_) {
        gtk_widget_queue_draw(wave_area_);
    }
}

void ProgressWave::set_playing(bool playing) {
    if (playing_ == playing) return;

    playing_ = playing;
    if (playing_) {
        start_animation();
    } else {
        stop_animation();
        if (wave_area_) {
            gtk_widget_queue_draw(wave_area_);
        }
    }
}

void ProgressWave::set_accent_color(double r, double g, double b, double a) {
    accent_r_ = std::clamp(r, 0.0, 1.0);
    accent_g_ = std::clamp(g, 0.0, 1.0);
    accent_b_ = std::clamp(b, 0.0, 1.0);
    accent_a_ = std::clamp(a, 0.0, 1.0);
    has_accent_override_ = true;
    if (wave_area_) gtk_widget_queue_draw(wave_area_);
}

void ProgressWave::set_scheme(bool dark) {
    dark_ = dark;
    has_scheme_override_ = true;
    if (wave_area_) gtk_widget_queue_draw(wave_area_);
}

void ProgressWave::set_can_seek(bool can_seek) {
    if (scale_) {
        gtk_widget_set_visible(scale_, can_seek);
    }
}

void ProgressWave::set_seek_callback(SeekCallback callback) {
    seek_callback_ = std::move(callback);
}

void ProgressWave::set_wave_frequency(double frequency) {
    wave_frequency_ = std::max(1.0, frequency);
}

void ProgressWave::set_spectrum(const std::vector<float>& magnitudes) {
    spectrum_ = magnitudes;
    if (wave_area_) gtk_widget_queue_draw(wave_area_);
}

void ProgressWave::start_animation() {
    if (wave_tick_source_ != 0) return;
    if (!animations_enabled()) {
        // Reduced motion: keep the wave static instead of animating it.
        if (wave_area_) gtk_widget_queue_draw(wave_area_);
        return;
    }

    last_frame_time_ = g_get_monotonic_time();
    wave_tick_source_ = g_timeout_add(kWaveFrameMs, on_tick, this);
}

void ProgressWave::stop_animation() {
    if (wave_tick_source_ != 0) {
        g_source_remove(wave_tick_source_);
        wave_tick_source_ = 0;
    }
    last_frame_time_ = 0;
}

void ProgressWave::commit_pending_seek() {
    if (pending_fraction_ < 0.0) return;

    const double fraction = std::clamp(pending_fraction_, 0.0, 1.0);
    pending_fraction_ = -1.0;
    if (duration_ <= 0.0 || !seek_callback_) return;

    seek_callback_(fraction * duration_);
}

namespace {

// Rounded rectangle with an automatically clamped corner radius.
void rounded_rect(cairo_t* cr, double x, double y, double w, double h,
                  double radius) {
    const double r = std::min(radius, std::min(w, h) / 2.0);
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

} // namespace

void ProgressWave::on_draw(GtkDrawingArea* area, cairo_t* cr, int width,
                           int height, gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self || width <= 0 || height <= 0) return;

    // Quickshell "Wavy" layout: inset track, wave across the played fraction
    // only, and a slim vertical stopper with tiny rounded corners.
    const double track_left = kEdgePadding;
    const double track_right = std::max(track_left, width - kEdgePadding);
    const double track_length = std::max(1.0, track_right - track_left);
    const double center_y = std::round(height / 2.0);
    const double progress = std::clamp(self->progress_, 0.0, 1.0);
    const double played_end = track_left + progress * track_length;
    const double line_width = kWaveLineWidth;
    const double amplitude = line_width * kWaveAmplitudeMultiplier;
    const double phase =
        self->animations_enabled() ? self->wave_phase_ : 0.0;

    // Read the live --accent-color through the widget's CSS `color` property
    // so custom drawing follows ThemeManager accent changes without plumbing.
    GdkRGBA accent;
    gtk_widget_get_color(GTK_WIDGET(area), &accent);
    const double accent_r =
        self->has_accent_override_ ? self->accent_r_ : accent.red;
    const double accent_g =
        self->has_accent_override_ ? self->accent_g_ : accent.green;
    const double accent_b =
        self->has_accent_override_ ? self->accent_b_ : accent.blue;

    auto wave_y = [&](double x) {
        double local_amplitude = amplitude;
        if (!self->spectrum_.empty()) {
            const double ratio = std::clamp(
                (x - track_left) / track_length, 0.0, 1.0);
            const size_t band = std::min<size_t>(
                static_cast<size_t>(ratio * self->spectrum_.size()),
                self->spectrum_.size() - 1);
            const float magnitude = std::clamp(self->spectrum_[band], 0.0f, 1.0f);
            local_amplitude = amplitude * (0.4 + 1.6 * magnitude);
        }
        // Wavelength is fixed to the full track length so the wave keeps its
        // shape while the played fraction grows (Quickshell fullLength).
        return center_y + local_amplitude *
            std::sin((2.0 * M_PI * self->wave_frequency_ * (x - track_left) /
                      track_length) + phase);
    };

    // 1. Rounded unplayed track.
    rounded_rect(cr, track_left, center_y - kTrackHeight / 2.0,
                 track_length, kTrackHeight, kTrackRadius);
    // Track follows the scheme; the played wave stays accent-only.
    const bool dark =
        self->has_scheme_override_
            ? self->dark_
            : adw_style_manager_get_dark(adw_style_manager_get_default());
    if (dark) {
        cairo_set_source_rgba(cr, 0.56, 0.55, 0.62, 0.28);
    } else {
        cairo_set_source_rgba(cr, 0.47, 0.47, 0.50, 0.24);
    }
    cairo_fill(cr);

    // 2. Wavy fill, clipped to the played fraction. The stroke is inset by
    // half a line width on each side so the round caps stay inside the clip.
    if (played_end - track_left > line_width) {
        cairo_save(cr);
        cairo_rectangle(cr, track_left, 0.0,
                        played_end - track_left, height);
        cairo_clip(cr);

        auto stroke_wave = [&](double stroke_width, double alpha) {
            const double x0 = track_left + stroke_width / 2.0;
            const double x1 = std::max(x0, played_end - stroke_width / 2.0);
            cairo_set_line_width(cr, stroke_width);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_source_rgba(cr, accent_r, accent_g, accent_b, alpha);
            cairo_move_to(cr, x0, wave_y(x0));
            for (double x = x0 + 1.0; x < x1; x += 1.0) {
                cairo_line_to(cr, x, wave_y(x));
            }
            cairo_line_to(cr, x1, wave_y(x1));
            cairo_stroke(cr);
        };

        stroke_wave(line_width + 6.0, 0.10); // soft glow behind the wave
        stroke_wave(line_width, 0.95);
        cairo_restore(cr);
    }

    // 3. Slim vertical stopper at the play position (narrows while dragging).
    const double handle_width =
        self->seeking_ ? kHandlePressedWidth : kHandleWidth;
    rounded_rect(cr, played_end - handle_width / 2.0,
                 center_y - kHandleHeight / 2.0, handle_width, kHandleHeight,
                 kHandleRadius);
    cairo_set_source_rgba(cr, accent_r, accent_g, accent_b, 1.0);
    cairo_fill(cr);
}

bool ProgressWave::animations_enabled() const {
    if (!wave_area_) return false;
    GtkSettings* settings = gtk_widget_get_settings(wave_area_);
    if (!settings) return true;
    gboolean enable = TRUE;
    g_object_get(settings, "gtk-enable-animations", &enable, nullptr);
    return enable != FALSE;
}

gboolean ProgressWave::on_tick(gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self || !self->wave_area_) return G_SOURCE_REMOVE;

    // Honour reduced motion while running: drop the timer so no idle 30fps
    // work is scheduled. Playback state changes restart it when enabled again.
    if (!self->animations_enabled()) {
        self->wave_tick_source_ = 0;
        self->last_frame_time_ = 0;
        return G_SOURCE_REMOVE;
    }

    const gint64 now = g_get_monotonic_time();
    if (self->last_frame_time_ > 0) {
        const double elapsed_ms =
            static_cast<double>(now - self->last_frame_time_) / 1000.0;
        self->wave_phase_ += elapsed_ms / 400.0;
    }
    self->last_frame_time_ = now;
    gtk_widget_queue_draw(self->wave_area_);
    return G_SOURCE_CONTINUE;
}

void ProgressWave::on_value_changed(GtkRange* range, gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self || !self->wave_area_ || !self->scale_) return;
    if (self->updating_ || self->duration_ <= 0.0) return;

    const double fraction = std::clamp(gtk_range_get_value(range), 0.0, 1.0);
    self->pending_fraction_ = fraction;
    self->progress_ = fraction;
    gtk_widget_queue_draw(self->wave_area_);

    if (self->seeking_) return;

    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
    }
    self->seek_commit_source_ = g_timeout_add(
        kSeekCommitDelayMs, on_commit_timeout, self);
}

gboolean ProgressWave::on_pressed(GtkGestureClick*, int, double, double,
                                   gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self) return FALSE;

    self->seeking_ = true;
    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
        self->seek_commit_source_ = 0;
    }
    return FALSE;
}

gboolean ProgressWave::on_released(GtkGestureClick*, int, double, double,
                                    gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self) return FALSE;

    self->seeking_ = false;
    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
        self->seek_commit_source_ = 0;
    }
    self->commit_pending_seek();
    return FALSE;
}

gboolean ProgressWave::on_commit_timeout(gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self) return G_SOURCE_REMOVE;

    self->seek_commit_source_ = 0;
    self->commit_pending_seek();
    return G_SOURCE_REMOVE;
}

} // namespace lollypop
