/**
 * Lollypop C++ - Bottom Playback Panel
 * Persistent playback controls spanning full width
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class WaveformWidget;

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

private:
    MainWindow* main_window_;
    GtkWidget* main_box_;
    GtkWidget* separator_;

    // Left: Artwork + track info
    GtkWidget* left_box_;
    GtkWidget* artwork_image_;
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    // Center: Playback controls
    GtkWidget* center_box_;
    GtkWidget* prev_button_;
    GtkWidget* play_button_;
    GtkWidget* next_button_;
    GtkWidget* play_icon_;
    bool is_playing_ = false;

    // Progress: Waveform
    GtkWidget* progress_box_;
    WaveformWidget* waveform_widget_;
    GtkWidget* time_label_;

    // Right: Volume + PiP
    GtkWidget* right_box_;
    GtkWidget* volume_button_;
    GtkWidget* volume_scale_;
    GtkWidget* volume_popover_;
    GtkWidget* pip_button_;

    void setup_ui();
    void create_left_section();
    void create_center_section();
    void create_progress_section();
    void create_right_section();

    // Callbacks
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_changed(GtkRange* range, gpointer user_data);
    static void on_pip_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop