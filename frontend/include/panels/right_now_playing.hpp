/**
 * Lollypop C++ - Right Now Playing Panel
 * Album artwork, track info, waveform progress, playback modes, Lyrics/Queue tabs.
 *
 * Phase 9 enhancements:
 *   - Lyrics tab integrates with ILyricsService (fetches/display lyrics per track)
 *   - Queue tab integrates with IPlayerService (shows actual queue tracks via TrackRow)
 *   - Adaptive artwork sizing (shrinks/collapses based on available panel height)
 *   - Short-height layout (artwork collapses, Lyrics/Queue gets priority)
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

    void update_track_info(const std::string& title, const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_queue(const std::vector<std::string>& queue_track_ids);
    void set_lyrics(const std::string& lyrics);
    void set_points(const std::vector<float>& points);
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

    // Shell media controls are the playback authority. GTK buttons are read-only
    // by default; this allows a future pass-through mode if desired.
    void set_controls_sensitive(bool sensitive);

    // Enable/disable only transport controls (prev/next), not mode buttons.
    void set_transport_controls_sensitive(bool sensitive);

    // Adaptive layout: called when panel allocation changes.
    void set_panel_height(int height);

    // Lyrics service integration (public for MainWindow callback)
    void request_lyrics_for_track(const std::string& track_id);

private:
    MainWindow* main_window_;
    ILyricsService* lyrics_service_;
    ILibraryService* library_service_;
    GtkWidget* main_box_;
    GtkWidget* scrolled_window_;
    GtkWidget* content_box_;

    // Artwork section
    GtkWidget* artwork_box_;
    ArtworkView* artwork_view_;

    // Track info
    GtkWidget* track_title_label_;
    GtkWidget* track_artist_label_;

    // Waveform progress
    GtkWidget* waveform_box_;
    WaveformView* waveform_view_;
    std::unique_ptr<VisualizerModel> waveform_model_;
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
    GtkWidget* lyrics_empty_state_;

    std::string current_artwork_path_;
    std::string current_track_id_;
    int panel_height_ = 600;

    void setup_ui();
    void create_artwork_section();
    void create_track_info();
    void create_waveform_section();
    void create_playback_modes();
    void create_tabs();
    void create_lyrics_empty_state();

    // Adaptive layout
    void adapt_to_height(int height);

    // Queue integration with TrackRow components
    void populate_queue(const std::vector<std::string>& queue_track_ids);

private:
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_lyrics_tab_clicked(GtkButton* button, gpointer user_data);
    static void on_queue_tab_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
