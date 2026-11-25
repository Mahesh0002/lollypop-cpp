/**
 * Lollypop C++ - Right Now Playing Panel
 *
 * Full-height Now Playing surface:
 *   artwork, title/artist
 *   [rewind] [shared wavy progress] [fast-forward]
 *   [shuffle] [repeat] [repeat one]
 *   Lyrics / Queue tabs
 *
 * The progress strip is the same shared Cairo sine wave used by the bottom
 * bar and PiP window; only its width changes.
 */

#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class ArtworkView;
class ProgressWave;
class TrackRow;
class SyncedLyricsView;
class ILibraryService;
class ILyricsService;

class RightNowPlaying {
public:
    // Total visible width, including the 1px CSS separator.
    static constexpr int kPanelWidth = 500;

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
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_loop(bool enabled);

    void set_controls_sensitive(bool sensitive);
    void set_transport_controls_sensitive(bool sensitive);

    // Kept for compatibility; the panel also tracks live GTK allocation.
    void set_panel_height(int height);

    // Lyrics service integration (public for MainWindow callback).
    void request_lyrics_for_track(const std::string& track_id);

    // Forward the same lyrics payload to other panels (compact bottom label).
    using LyricsChangedCallback = std::function<void(const std::string&)>;
    void set_lyrics_changed_callback(LyricsChangedCallback cb) {
        lyrics_changed_callback_ = std::move(cb);
    }

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

    // [prev] [shared wavy progress] [next]
    GtkWidget* progress_box_;
    GtkWidget* prev_button_;
    GtkWidget* next_button_;
    std::unique_ptr<ProgressWave> progress_wave_;

    // Shuffle | Repeat playlist | Repeat track.
    GtkWidget* transport_box_;
    GtkWidget* shuffle_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;

    GtkWidget* tabs_box_;
    GtkWidget* lyrics_tab_;
    GtkWidget* queue_tab_;
    GtkWidget* tab_content_stack_;
    std::unique_ptr<SyncedLyricsView> synced_lyrics_view_;
    GtkWidget* queue_view_;
    GtkWidget* lyrics_empty_state_;
    GtkWidget* queue_undo_bar_;
    GtkWidget* queue_undo_label_;
    GtkWidget* queue_undo_button_;

    int panel_width_ = 0;
    int panel_height_ = 0;
    int last_artwork_side_ = 0;
    int last_width_ = 0;
    int last_height_ = 0;
    bool compact_layout_ = false;

    bool has_lyrics_ = false;
    bool lyrics_tab_active_ = true;

    std::string current_artwork_path_;
    std::string current_track_id_;
    LyricsChangedCallback lyrics_changed_callback_;

    // One-step undo for the most recent queue removal.
    std::string pending_undo_track_id_;
    std::string pending_undo_title_;
    size_t pending_undo_index_ = 0;
    std::vector<std::string> queue_after_removal_;

    // Queue actions are deferred one main-loop turn so a row callback is not
    // destroyed while its own queue-changed signal rebuilds the list.
    std::vector<guint> deferred_queue_actions_;

    // Retain TrackRow wrappers while their widgets are parented in the queue.
    std::vector<std::unique_ptr<TrackRow>> queue_rows_;

    void setup_ui();
    void create_artwork_section();
    void create_track_info();
    void create_progress_section();
    void create_transport_row();
    void create_tabs();
    void create_lyrics_empty_state();
    void create_queue_undo_bar();
    void show_queue_undo(const std::string& title,
                         const std::string& track_id,
                         size_t index,
                         const std::vector<std::string>& queue_after);
    void hide_queue_undo();
    void schedule_queue_action(std::function<void()> action);

    void adapt_to_allocation(int width, int height);
    void show_active_tab();
    void populate_queue(const std::vector<std::string>& queue_track_ids);

    static gboolean on_allocation_tick(GtkWidget* widget, GdkFrameClock* clock,
                                       gpointer user_data);

    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_lyrics_tab_clicked(GtkButton* button, gpointer user_data);
    static void on_queue_tab_clicked(GtkButton* button, gpointer user_data);
    static void on_queue_undo_clicked(GtkButton* button, gpointer user_data);
    static gboolean on_deferred_queue_action(gpointer user_data);
};

} // namespace lollypop
