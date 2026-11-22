/**
 * Lollypop C++ - Bottom Playback Panel
 * Persistent playback controls spanning full width
 *
 * Layout:
 * [Left: Artwork + Title/Artist] [Center: Prev | Waveform | Next + Time Labels] [Right: Modes | Volume | PiP]
 * No play/pause button - shell/media keys are the authority.
 * Previous/Next flank the waveform progress bar.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class WaveformView;
class VisualizerModel;
class ArtworkView;

class BottomPlayback {
public:
    explicit BottomPlayback(MainWindow* main_window);
    ~BottomPlayback();

    GtkWidget* get_widget() const { return main_box_; }

    void update_track_info(const std::string& title, const std::string& artist, const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_volume(double volume);
    void set_points(const std::vector<float>& points);

    // Shell media controls are the playback authority. GTK buttons are read-only
    // by default; this allows a future pass-through mode if desired.
    void set_controls_sensitive(bool sensitive);

    // Mode button setters
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

private:
    MainWindow* main_window_;
    GtkWidget* main_box_;
    GtkWidget* separator_;

    // Left: Artwork + track info
    GtkWidget* left_box_;
    ArtworkView* artwork_view_;
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    // Center: Prev | Waveform + Time Labels | Next
    GtkWidget* center_box_;
    GtkWidget* prev_button_;
    GtkWidget* next_button_;
    GtkWidget* progress_box_;       // Contains waveform + time labels
    WaveformView* waveform_view_;
    std::unique_ptr<VisualizerModel> waveform_model_;
    GtkWidget* time_label_left_;    // Current time (left of waveform)
    GtkWidget* time_label_right_;   // Duration (right of waveform)

    // Right: Volume + PiP + Playback Modes
    GtkWidget* right_box_;
    GtkWidget* volume_button_;
    GtkWidget* volume_scale_;
    GtkWidget* volume_popover_;
    GtkWidget* pip_button_;
    // Playback mode buttons
    GtkWidget* shuffle_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;

    void setup_ui();
    void create_left_section();
    void create_center_section();
    void create_right_section();

    // Callbacks
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_changed(GtkRange* range, gpointer user_data);
    static void on_pip_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
};

} // namespace lollypop