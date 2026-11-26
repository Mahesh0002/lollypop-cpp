/**
 * Lollypop C++ - WaveformView Implementation
 *
 * Wavy animated waveform like Quickshell's WaveVisualizer.qml / StyledSlider.qml.
 * Cairo-based GtkDrawingArea with frame-clock driven animation (~30 FPS),
 * sample smoothing, accent-color integration, and wavy modulation.
 *
 * Implemented using GTK 4 C API directly. No gtkmm, no Peel.
 */

#include "panels/waveform_view.hpp"

#include <algorithm>
#include <cmath>
#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WaveformView::WaveformView() {
    widget_.reset(gtk_drawing_area_new());
    g_object_ref_sink(widget_.get());

    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_vexpand(widget_.get(), FALSE);
    gtk_widget_add_css_class(widget_.get(), "waveform-view");
    gtk_widget_set_size_request(widget_.get(), preferred_width_,
                                preferred_height_);
    gtk_widget_set_visible(widget_.get(), TRUE);

    // Set up Cairo drawing
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget_.get()),
                                   draw_callback, this, nullptr);

    // No tick callback initially (starts when playing)
}

WaveformView::~WaveformView() {
    stop_tick();
}

void WaveformView::start_tick() {
    if (tick_id_ != 0) return;
    tick_id_ = gtk_widget_add_tick_callback(widget_.get(), tick_callback,
                                            this, nullptr);
}

void WaveformView::stop_tick() {
    if (tick_id_ != 0) {
        gtk_widget_remove_tick_callback(widget_.get(), tick_id_);
        tick_id_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WaveformView::set_model(const VisualizerModel* model) {
    model_ = model;
    compute_smoothed_samples();
    // Always reserve height so the panel layout doesn't collapse when empty.
    gtk_widget_set_visible(widget_.get(), TRUE);
    gtk_widget_queue_draw(widget_.get());
}

void WaveformView::set_scheme(bool dark) {
    dark_ = dark;
    gtk_widget_queue_draw(widget_.get());
}

void WaveformView::set_accent_color(double r, double g, double b, double a) {
    accent_r_ = std::clamp(r, 0.0, 1.0);
    accent_g_ = std::clamp(g, 0.0, 1.0);
    accent_b_ = std::clamp(b, 0.0, 1.0);
    accent_a_ = std::clamp(a, 0.0, 1.0);
    gtk_widget_queue_draw(widget_.get());
}

void WaveformView::set_progress(double progress) {
    progress_ = std::clamp(progress, 0.0, 1.0);
    gtk_widget_queue_draw(widget_.get());
}

void WaveformView::set_playing(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    if (playing) {
        start_tick();
    } else {
        stop_tick();
    }
    if (gtk_widget_get_realized(widget_.get())) {
        gtk_widget_queue_draw(widget_.get());
    }
}

void WaveformView::set_preferred_size(int width, int height) {
    preferred_width_ = width;
    preferred_height_ = height;
    gtk_widget_set_size_request(widget_.get(), width, height);
    gtk_widget_queue_resize(widget_.get());
}

void WaveformView::set_smoothing(int window) {
    smoothing_window_ = std::max(0, window);
    compute_smoothed_samples();
    gtk_widget_queue_draw(widget_.get());
}

// ---------------------------------------------------------------------------
// Sample smoothing (moving average) + wavy modulation
// ---------------------------------------------------------------------------

void WaveformView::compute_smoothed_samples() {
    smoothed_samples_.clear();
    if (!model_ || model_->samples.empty()) return;

    const auto& raw = model_->samples;
    size_t n = raw.size();
    smoothed_samples_.resize(n);

    for (size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        int count = 0;
        int half = smoothing_window_;
        for (int j = -half; j <= half; ++j) {
            long long idx = static_cast<long long>(i) + j;
            if (idx >= 0 && idx < static_cast<long long>(n)) {
                sum += raw[static_cast<size_t>(idx)];
                ++count;
            }
        }
        // Apply wavy modulation: sin wave that travels left-to-right
        float base = static_cast<float>(sum / count);
        float wave_mod = 1.0f + 0.15f * std::sin(animation_phase_ + static_cast<float>(i) * 0.3f);
        smoothed_samples_[i] = base * wave_mod;
    }
}

// ---------------------------------------------------------------------------
// Tick callback (frame-clock driven, ~30 FPS cap via GDK)
// ---------------------------------------------------------------------------

gboolean WaveformView::tick_callback(GtkWidget* widget, GdkFrameClock* clock,
                                      gpointer user_data) {
    auto* self = static_cast<WaveformView*>(user_data);
    (void)widget;
    (void)clock;

    if (!self->playing_) return G_SOURCE_REMOVE;

    // Animate the wavy phase (traveling wave)
    self->animation_phase_ += 0.08f;
    if (self->animation_phase_ > 2.0f * M_PI) {
        self->animation_phase_ -= 2.0f * M_PI;
    }

    // Recompute smoothed samples with new phase
    self->compute_smoothed_samples();

    // Queue a redraw for the next frame
    gtk_widget_queue_draw(self->widget_.get());

    // Continue ticking (GDK will cap at the frame rate)
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Cairo draw callback - Wavy waveform like Quickshell WaveVisualizer
// ---------------------------------------------------------------------------

void WaveformView::draw_callback(GtkDrawingArea* area, cairo_t* cr, int width,
                                  int height, gpointer user_data) {
    auto* self = static_cast<WaveformView*>(user_data);
    (void)area;

    if (width <= 0 || height <= 0) return;

    self->render(cr, width, height);
}

void WaveformView::render(cairo_t* cr, int width, int height) {
    // ── Background: scheme surface ──
    if (dark_) {
        cairo_set_source_rgb(cr, 0.086, 0.086, 0.122);   // --surface-panel
    } else {
        cairo_set_source_rgb(cr, 0.96, 0.96, 0.97);      // --surface-app
    }
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    float mid_y = height * 0.5f;
    float track_height = height * 0.12f;      // Thin track line
    float playhead_x = static_cast<float>(progress_ * width);

    // ── Track line: empty portion (full width, dim) ──
    if (dark_) {
        cairo_set_source_rgba(cr, 0.56, 0.55, 0.62, 0.40);
    } else {
        cairo_set_source_rgba(cr, 0.47, 0.47, 0.50, 0.30);
    }
    cairo_set_line_width(cr, track_height);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, 0, mid_y);
    cairo_line_to(cr, width, mid_y);
    cairo_stroke(cr);

    // ── Filled progress: from left to playhead ──
    if (playhead_x > 0.0f) {
        cairo_set_source_rgba(cr, accent_r_, accent_g_, accent_b_, accent_a_);
        cairo_set_line_width(cr, track_height);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, 0, mid_y);
        cairo_line_to(cr, playhead_x, mid_y);
        cairo_stroke(cr);
    }

    // ── No data or not playing: just show simple progress bar ──
    if (!model_ || !model_->hasData() || smoothed_samples_.empty() || !playing_) {
        return;
    }

    // ── Real wavy waveform bars (only when playing with audio data) ──
    const auto& samples = smoothed_samples_;
    size_t n = samples.size();
    if (n < 2) return;

    constexpr float kMaxVisualizerValue = 1000.0f;
    float max_amplitude = height * 0.45f;

    // Upper wave path
    cairo_new_path(cr);
    cairo_move_to(cr, 0, mid_y);
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i) * width / (n - 1);
        double normalized = samples[i] / kMaxVisualizerValue;
        if (normalized > 1.0) normalized = 1.0;
        double y = mid_y - normalized * max_amplitude;
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, mid_y);
    cairo_close_path(cr);

    // Clip to left of playhead for saturated portion
    cairo_save(cr);
    if (playhead_x > 0.0) {
        cairo_rectangle(cr, 0, 0, playhead_x, height);
        cairo_clip(cr);
    }
    cairo_set_source_rgba(cr, accent_r_, accent_g_, accent_b_, accent_a_ * 0.8);
    cairo_fill(cr);
    cairo_restore(cr);

    // Dimmed right portion (upper)
    cairo_save(cr);
    if (playhead_x < width) {
        cairo_rectangle(cr, playhead_x, 0, width - playhead_x, height);
        cairo_clip(cr);
    }
    cairo_set_source_rgba(cr, accent_r_, accent_g_, accent_b_, accent_a_ * 0.15);
    cairo_fill(cr);
    cairo_restore(cr);

    // Lower wave (symmetric below center)
    cairo_new_path(cr);
    cairo_move_to(cr, 0, mid_y);
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i) * width / (n - 1);
        double normalized = samples[i] / kMaxVisualizerValue;
        if (normalized > 1.0) normalized = 1.0;
        double y = mid_y + normalized * max_amplitude;
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, mid_y);
    cairo_close_path(cr);

    cairo_save(cr);
    if (playhead_x > 0.0) {
        cairo_rectangle(cr, 0, 0, playhead_x, height);
        cairo_clip(cr);
    }
    cairo_set_source_rgba(cr, accent_r_, accent_g_, accent_b_, accent_a_ * 0.8);
    cairo_fill(cr);
    cairo_restore(cr);

    cairo_save(cr);
    if (playhead_x < width) {
        cairo_rectangle(cr, playhead_x, 0, width - playhead_x, height);
        cairo_clip(cr);
    }
    cairo_set_source_rgba(cr, accent_r_, accent_g_, accent_b_, accent_a_ * 0.15);
    cairo_fill(cr);
    cairo_restore(cr);

    // ── Progress playhead line ──
    if (progress_ > 0.0) {
        const double ink = dark_ ? 1.0 : 0.0;
        cairo_set_source_rgba(cr, ink, ink, ink, 0.8);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, playhead_x, 0);
        cairo_line_to(cr, playhead_x, height);
        cairo_stroke(cr);
    }
}

} // namespace lollypop