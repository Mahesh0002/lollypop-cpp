/**
 * Lollypop C++ - Right Now Playing Panel
 *
 * Full-height Now Playing surface: square artwork, track metadata, a stretched
 * waveform/progress strip, transport controls, and a tab section that consumes
 * all remaining vertical space.
 *
 * The panel adapts to its own GTK allocation instead of a fixed height seed.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class ArtworkView;
class WaveformView;
class VisualizerModel;
class TrackRow;
class ILibraryService;
class ILyricsService;

class RightNowPlaying {
public:
    RightNowPlaying(MainWindow* main_window,
                    ILyricsService* lyrics_service,
                    ILibraryService* library_service);
    ~RightNowPlaying();

    RightNowPlaying(const RightNowPlaying&) = delete;
    RightNowPlaying& operator=(const RightNowPlaying&) = delete;

    GtkWidget* get_widget() const { return main_box_; }

    void update_track_info(const std::string& title,
                           const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_queue(const std::vector<std::string>& queue_track_ids);
    void set_lyrics(const std::string& lyrics);
    void set_points(const std::vector<float>& points);
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

    // Shell media controls are the playback authority. GTK buttons stay
    // read-only by default; this allows a future pass-through mode if desired.
    void set_controls_sensitive(bool sensitive);

    // Enable/disable only transport controls (prev/play/next), not mode buttons.
    void set_transport_controls_sensitive(bool sensitive);

    // Kept for compatibility; the panel now also tracks live GTK allocation.
    void set_panel_height(int height);

    // Lyrics service integration (public for MainWindow callback).
    void request_lyrics_for_track(const std::string& track_id);

private:
    MainWindow* main_window_;
    ILyricsService* lyrics_service_;
    ILibraryService* library_service_;

    GtkWidget* main_box_;
    GtkWidget* content_box_;

    GtkWidget* artwork_box_;
    std::unique_ptr<ArtworkView> artwork_view_;

    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    GtkWidget* progress_box_;
    GtkWidget* time_label_;
    GtkWidget* duration_label_;
    std::unique_ptr<VisualizerModel> waveform_model_;
    std::unique_ptr<WaveformView> waveform_view_;

    GtkWidget* transport_box_;
    GtkWidget* shuffle_button_;
    GtkWidget* prev_button_;
    GtkWidget* play_pause_button_;
    GtkWidget* play_pause_icon_;
    GtkWidget* next_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;

    GtkWidget* tabs_box_;
    GtkWidget* lyrics_tab_;
    GtkWidget* queue_tab_;
    GtkWidget* tab_content_stack_;
    GtkWidget* lyrics_view_;
    GtkWidget* queue_view_;
    GtkWidget* lyrics_empty_state_;

    int panel_width_ = 0;
    int panel_height_ = 0;
    int last_artwork_side_ = 0;
    int last_waveform_height_ = 0;
    bool has_lyrics_ = false;
    bool lyrics_tab_active_ = true;
    bool playing_ = false;

    std::string current_artwork_path_;
    std::string current_track_id_;

    // Retain TrackRow wrappers while their widgets are parented in the queue.
    std::vector<std::unique_ptr<TrackRow>> queue_rows_;

    void setup_ui();
    void create_artwork_section();
    void create_track_info();
    void create_progress_section();
    void create_transport_row();
    void create_tabs();
    void create_lyrics_empty_state();

    void adapt_to_allocation(int width, int height);
    void show_active_tab();
    void update_play_pause_icon();
    void populate_queue(const std::vector<std::string>& queue_track_ids);

    static gboolean on_allocation_tick(GtkWidget* widget, GdkFrameClock* clock,
                                       gpointer user_data);

    int last_width_ = 0;
    int last_height_ = 0;
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_play_pause_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_lyrics_tab_clicked(GtkButton* button, gpointer user_data);
    static void on_queue_tab_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
