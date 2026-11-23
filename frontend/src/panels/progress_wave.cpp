/**
 * Lollypop C++ - Shared Wavy Progress Bar Implementation
 */

#include "panels/progress_wave.hpp"

#include <gtk/gtk.h>

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

void ProgressWave::set_can_seek(bool can_seek) {
    if (scale_) {
        gtk_widget_set_visible(scale_, can_seek);
    }
}

void ProgressWave::set_seek_callback(SeekCallback callback) {
    seek_callback_ = std::move(callback);
}

void ProgressWave::set_spectrum(const std::vector<float>& magnitudes) {
    spectrum_ = magnitudes;
    if (wave_area_) gtk_widget_queue_draw(wave_area_);
}

void ProgressWave::start_animation() {
    if (wave_tick_source_ != 0) return;

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

void ProgressWave::on_draw(GtkDrawingArea*, cairo_t* cr, int width,
                           int height, gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self || width <= 0 || height <= 0) return;

    const double line_width = kWaveLineWidth;
    const double amplitude = line_width * kWaveAmplitudeMultiplier;
    const double center_y = height / 2.0;
    const double phase = self->wave_phase_;

    // When spectrum data is available, modulate the sine amplitude per band
    // so the progress wave reacts to audio. Otherwise keep the decorative
    // sine wave used before backend publication starts.
    auto wave_y = [&](double x) {
        double local_amplitude = amplitude;
        if (!self->spectrum_.empty()) {
            const double ratio = std::clamp(x / static_cast<double>(width),
                                            0.0, 1.0);
            const size_t band = std::min<size_t>(
                static_cast<size_t>(ratio * self->spectrum_.size()),
                self->spectrum_.size() - 1);
            const float magnitude = std::clamp(self->spectrum_[band], 0.0f, 1.0f);
            local_amplitude = amplitude * (0.4 + 1.6 * magnitude);
        }
        return center_y + local_amplitude *
            std::sin((2.0 * M_PI * kWaveFrequency * x / width) + phase);
    };

    const GdkRGBA unplayed = {0.48f, 0.44f, 0.62f, 0.80f};
    const GdkRGBA played = {0.655f, 0.545f, 0.980f, 0.95f};

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_set_line_width(cr, line_width + 6.0);
    cairo_set_source_rgba(cr, played.red, played.green, played.blue, 0.12);
    cairo_move_to(cr, 0.0, wave_y(0.0));
    for (int x = 1; x <= width; ++x) {
        cairo_line_to(cr, static_cast<double>(x), wave_y(x));
    }
    cairo_stroke(cr);

    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgba(cr, unplayed.red, unplayed.green,
                          unplayed.blue, unplayed.alpha);
    cairo_move_to(cr, 0.0, wave_y(0.0));
    for (int x = 1; x <= width; ++x) {
        cairo_line_to(cr, static_cast<double>(x), wave_y(x));
    }
    cairo_stroke(cr);

    const double progress = std::clamp(self->progress_, 0.0, 1.0);
    const double end_x = progress * width;
    if (end_x > 0.0) {
        cairo_set_source_rgba(cr, played.red, played.green,
                              played.blue, played.alpha);
        cairo_move_to(cr, 0.0, wave_y(0.0));
        for (int x = 1; x < static_cast<int>(end_x); ++x) {
            cairo_line_to(cr, static_cast<double>(x), wave_y(x));
        }
        cairo_line_to(cr, end_x, wave_y(end_x));
        cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, played.red, played.green, played.blue, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, end_x, center_y - 9.0);
    cairo_line_to(cr, end_x, center_y + 9.0);
    cairo_stroke(cr);
    cairo_arc(cr, end_x, center_y, 4.0, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
}

gboolean ProgressWave::on_tick(gpointer user_data) {
    auto* self = static_cast<ProgressWave*>(user_data);
    if (!self || !self->wave_area_) return G_SOURCE_REMOVE;

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
