/**
 * Lollypop C++ - Waveform Widget
 * Shared Cairo-based waveform visualization for bottom playback and right panel.
 * Draws CAVA audio data as bars with a playback progress indicator.
 */

#pragma once

#include <gtk/gtk.h>
#include <vector>

namespace lollypop {

class WaveformWidget {
public:
    explicit WaveformWidget(GtkWidget* parent = nullptr);
    ~WaveformWidget();

    GtkWidget* get_widget() const { return drawing_area_; }

    // Set CAVA audio data (bar heights 0.0-1.0)
    void set_points(const std::vector<float>& points);

    // Set playback progress (0.0-1.0)
    void set_progress(double progress);

    // Set playing state (affects animation)
    void set_playing(bool playing);

private:
    GtkWidget* drawing_area_;
    std::vector<float> points_;
    double progress_ = 0.0;
    bool playing_ = false;
    guint animation_timeout_id_ = 0;
    double animation_phase_ = 0.0;

    static void draw_callback(GtkDrawingArea* area, cairo_t* cr,
                              int width, int height, gpointer user_data);
    static gboolean animation_tick(gpointer user_data);
    void start_animation();
    void stop_animation();
};

} // namespace lollypop
