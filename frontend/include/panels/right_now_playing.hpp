/**
 * Lollypop C++ - Right Now Playing Panel
 * Album artwork, track info, waveform progress, playback modes, Lyrics/Queue tabs
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class WaveformWidget;

class RightNowPlaying {
public:
    explicit RightNowPlaying(MainWindow* main_window);
    ~RightNowPlaying();

    GtkWidget* get_widget() const { return main_box_; }

    void update_track_info(const std::string& title, const std::string& artist, const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_queue(const std::vector<std::string>& queue);
    void set_lyrics(const std::string& lyrics);
    void set_points(const std::vector<float>& points);
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

private:
    MainWindow* main_window_;
    GtkWidget* main_box_;
    GtkWidget* scrolled_window_;
    GtkWidget* content_box_;

    // Artwork section
    GtkWidget* artwork_box_;
    GtkWidget* artwork_image_;

    // Track info
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    // Waveform progress
    GtkWidget* waveform_box_;
    WaveformWidget* waveform_widget_;
    GtkWidget* time_label_;
    GtkWidget* prev_button_;
    GtkWidget* next_button_;

    // Playback modes
    GtkWidget* modes_box_;
    GtkWidget* shuffle_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;

    // Lyrics/Queue tabs
    GtkWidget* tabs_box_;
    GtkWidget* lyrics_tab_;
    GtkWidget* queue_tab_;
    GtkWidget* tab_content_stack_;
    GtkWidget* lyrics_view_;
    GtkWidget* queue_view_;

    std::string current_artwork_path_;

    void setup_ui();
    void create_artwork_section();
    void create_track_info();
    void create_waveform_section();
    void create_playback_modes();
    void create_tabs();

    // Callbacks
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_lyrics_tab_clicked(GtkButton* button, gpointer user_data);
    static void on_queue_tab_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop