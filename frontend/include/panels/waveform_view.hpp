/**
 * Lollypop C++ - WaveformView
 *
 * Shared waveform visualization widget for the right Now Playing panel
 * and bottom playback bar. Both widgets consume the same VisualizerModel.
 *
 * Phase 8 final renderer: Cairo-based GtkDrawingArea with frame-clock
 * driven animation (~30 FPS), sample smoothing, accent-color integration,
 * and split-color playhead (saturated accent left of playhead, dimmed
 * right).
 *
 * NOTE: GTK 4.24.1 on this system does not expose gtk_widget_set_snapshot_func
 * or GSK path/clip APIs. Cairo rendering via GtkDrawingArea is used instead.
 * The public interface is unchanged; a future GtkSnapshot migration requires
 * no changes to WaveformView's public API.
 *
 * Implemented using GTK 4 C API directly. No gtkmm, no Peel.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <vector>

#include "models/VisualizerModel.hpp"

namespace lollypop {

/**
 * WaveformView is a GTK 4 widget rendered with Cairo.
 *
 * It is a presentation-only widget: it does not own audio capture,
 * decoding, or playback. It reads from the shared VisualizerModel.
 *
 * Rendering: filled wave from bottom, accent color saturated on the
 * left of the playhead, dimmed version on the right.
 */
class WaveformView {
public:
    WaveformView();
    ~WaveformView();

    WaveformView(const WaveformView&) = delete;
    WaveformView& operator=(const WaveformView&) = delete;

    /// Return the underlying GtkWidget for container embedding.
    GtkWidget* get_widget() const { return widget_.get(); }

    /// Set the shared visualizer model (non-owning reference).
    void set_model(const VisualizerModel* model);

    /// Set the accent color for the waveform (RGBA, 0-1).
    void set_accent_color(double r, double g, double b, double a = 1.0);

    /// Set playback progress (0.0 - 1.0) for the playhead.
    void set_progress(double progress);

    /// Set playing state (affects animation and frame-clock ticking).
    void set_playing(bool playing);

    /// Reserve layout space: call with preferred width/height.
    void set_preferred_size(int width, int height);

    /// Set smoothing window size (number of neighboring samples to average).
    void set_smoothing(int window);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;

    const VisualizerModel* model_ = nullptr;
    double accent_r_ = 0.4, accent_g_ = 0.7, accent_b_ = 1.0, accent_a_ = 1.0;
    double progress_ = 0.0;
    bool playing_ = false;
    int smoothing_window_ = 2;
    int preferred_width_ = 200;
    int preferred_height_ = 40;

    // Frame-clock tick callback ID (registered via gtk_widget_add_tick_callback)
    guint tick_id_ = 0;

    // Smoothed sample buffer (computed from raw samples)
    std::vector<float> smoothed_samples_;

    // Animation phase for traveling wave effect
    float animation_phase_ = 0.0f;

    // Start the frame-clock tick callback (only when playing).
    void start_tick();
    // Stop the frame-clock tick callback.
    void stop_tick();

    // Cairo draw callback.
    static void draw_callback(GtkDrawingArea* area, cairo_t* cr, int width,
                              int height, gpointer user_data);
    // Tick callback (frame-clock driven, ~30 FPS cap via GDK).
    static gboolean tick_callback(GtkWidget* widget, GdkFrameClock* clock,
                                  gpointer user_data);
    // Compute smoothed samples from raw model data.
    void compute_smoothed_samples();
    // Render the waveform into the cairo context.
    void render(cairo_t* cr, int width, int height);
};

} // namespace lollypop
