/**
 * Lollypop C++ - Bottom Playback Panel
 * Single-line playback controls spanning full width
 *
 * Layout:
 *   [Artwork + Title/Artist] [Prev] [Stretched Wavy Progress] [Next]
 *   [Shuffle] [Repeat] [Loop] [Volume Dial] [PiP]
 *
 * The progress bar is a real animated sine wave rendered with Cairo. A fully
 * transparent GtkScale sits on top so pointer/keyboard seeking and
 * accessibility keep working without faking the wave with CSS stripes.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class MainWindow;
class ArtworkView;
class VolumeDial;

class BottomPlayback {
public:
    explicit BottomPlayback(MainWindow* main_window);
    ~BottomPlayback();

    GtkWidget* get_widget() const { return main_box_; }

    void update_track_info(const std::string& title, const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_volume(double volume);

    void set_controls_sensitive(bool sensitive);

    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

    VolumeDial* get_volume_dial() const { return volume_dial_.get(); }

private:
    static constexpr guint kWaveFrameMs = 33;
    static constexpr guint kSeekCommitDelayMs = 120;
    static constexpr double kWaveLineWidth = 4.0;
    static constexpr double kWaveAmplitudeMultiplier = 0.5;
    static constexpr double kWaveFrequency = 6.0;

    MainWindow* main_window_;
    GtkWidget* main_box_;

    // Stretched progress region: Cairo wave + transparent interactive scale.
    GtkWidget* progress_box_;
    GtkWidget* progress_bar_;
    GtkWidget* wave_area_;

    // Left: Artwork + track info.
    std::unique_ptr<ArtworkView> artwork_view_;
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    // Transport and right-side controls.
    GtkWidget* prev_button_;
    GtkWidget* next_button_;
    GtkWidget* right_box_;
    std::unique_ptr<VolumeDial> volume_dial_;
    GtkWidget* pip_button_;
    GtkWidget* shuffle_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;

    // Playback/wave state.
    double current_position_ = 0.0;
    double current_duration_ = 0.0;
    double current_progress_ = 0.0;
    double pending_seek_fraction_ = -1.0;
    double wave_phase_ = 0.0;
    gint64 last_frame_time_ = 0;
    guint wave_tick_source_ = 0;
    guint seek_commit_source_ = 0;
    bool playing_ = false;
    bool seeking_ = false;
    bool updating_progress_ = false;

    void setup_ui();
    void create_row();
    void create_progress();
    void create_right_section();
    void start_wave_animation();
    void stop_wave_animation();
    void commit_pending_seek();

    // Callbacks.
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_pip_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);

    static void on_wave_draw(GtkDrawingArea* area, cairo_t* cr,
                             int width, int height, gpointer user_data);
    static gboolean on_wave_tick(gpointer user_data);
    static void on_progress_value_changed(GtkRange* range, gpointer user_data);
    static gboolean on_progress_pressed(GtkGestureClick* gesture, int n_press,
                                        double x, double y, gpointer user_data);
    static gboolean on_progress_released(GtkGestureClick* gesture, int n_press,
                                         double x, double y, gpointer user_data);
    static gboolean on_seek_commit_timeout(gpointer user_data);
};

} // namespace lollypop
