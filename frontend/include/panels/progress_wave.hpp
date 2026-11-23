/**
 * Lollypop C++ - Shared Wavy Progress Bar
 *
 * One Cairo sine-wave progress strip used by the bottom bar, the right
 * Now Playing panel, and the PiP window. A fully transparent GtkScale sits on
 * top so pointer/keyboard seeking and accessibility keep working while the
 * wave remains the only visible progress element.
 *
 * Widget pointers are registered as GLib weak pointers: GTK may destroy the
 * window tree before this helper is destructed, after which the pointers are
 * automatically nulled and no GTK calls are attempted.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>

namespace lollypop {

class ProgressWave {
public:
    using SeekCallback = std::function<void(double seconds)>;

    ProgressWave();
    ~ProgressWave();

    ProgressWave(const ProgressWave&) = delete;
    ProgressWave& operator=(const ProgressWave&) = delete;

    GtkWidget* get_widget() const { return overlay_; }

    void set_duration(double duration);
    void set_progress(double fraction);
    void set_playing(bool playing);
    void set_can_seek(bool can_seek);
    void set_seek_callback(SeekCallback callback);

private:
    static constexpr guint kWaveFrameMs = 33;
    static constexpr guint kSeekCommitDelayMs = 120;
    static constexpr double kWaveLineWidth = 4.0;
    static constexpr double kWaveAmplitudeMultiplier = 0.5;
    static constexpr double kWaveFrequency = 6.0;

    GtkWidget* overlay_ = nullptr;
    GtkWidget* wave_area_ = nullptr;
    GtkWidget* scale_ = nullptr;
    GtkGesture* gesture_ = nullptr;

    SeekCallback seek_callback_;
    double duration_ = 0.0;
    double progress_ = 0.0;
    double pending_fraction_ = -1.0;
    double wave_phase_ = 0.0;
    gint64 last_frame_time_ = 0;
    guint wave_tick_source_ = 0;
    guint seek_commit_source_ = 0;
    bool playing_ = false;
    bool seeking_ = false;
    bool updating_ = false;

    void start_animation();
    void stop_animation();
    void commit_pending_seek();
    void remove_weak_pointers();

    static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width,
                        int height, gpointer user_data);
    static gboolean on_tick(gpointer user_data);
    static void on_value_changed(GtkRange* range, gpointer user_data);
    static gboolean on_pressed(GtkGestureClick* gesture, int n_press,
                               double x, double y, gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static gboolean on_commit_timeout(gpointer user_data);
};

} // namespace lollypop
