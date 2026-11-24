/**
 * Lollypop C++ - Bottom Playback Panel
 * Single-line playback controls spanning full width
 *
 *   [Artwork + Title/Artist] [Prev] [Wavy Progress] [Next] [Modes] [Volume] [PiP]
 *
 * The progress strip is the shared Cairo sine wave used by the right panel and
 * PiP window; only its width changes. A transparent GtkScale overlay keeps
 * pointer/keyboard seeking and accessibility working.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class MainWindow;
class ArtworkView;
class ProgressWave;

class BottomPlayback {
public:
    explicit BottomPlayback(MainWindow* main_window);
    ~BottomPlayback();

    GtkWidget* get_widget() const { return main_box_; }

    void update_track_info(const std::string& title, const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);

    void set_controls_sensitive(bool sensitive);

    void set_shuffle(bool enabled);
    void set_loop(bool enabled);

private:
    MainWindow* main_window_;
    GtkWidget* main_box_;

    std::unique_ptr<ProgressWave> progress_wave_;
    GtkWidget* progress_widget_;

    std::unique_ptr<ArtworkView> artwork_view_;
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    GtkWidget* prev_button_;
    GtkWidget* next_button_;
    GtkWidget* middle_section_;
    GtkWidget* right_box_;
    GtkWidget* pip_button_;
    GtkWidget* shuffle_button_;
    GtkWidget* loop_button_;

    void setup_ui();
    void create_row();
    void create_progress(GtkWidget* parent);
    void create_right_section();

    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_pip_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
};

} // namespace lollypop
