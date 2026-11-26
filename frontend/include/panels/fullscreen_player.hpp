/**
 * Lollypop C++ - Fullscreen Now Playing
 *
 * Immersive, full-window playback surface:
 *   [close]
 *        large square artwork
 *        title / artist — album
 *   [prev] [wavy progress] [next]
 *   [shuffle] [repeat all] [repeat one] [play/pause] [lyrics]
 *          [volume] [inline volume slider]
 *        collapsible synced lyrics
 *
 * All state is observed from IPlayerService through MainWindow; mutations go
 * back through the same interface so the shell/MPRIS remains the authority.
 */

#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>

namespace lollypop {

class MainWindow;
class ArtworkView;
class SyncedLyricsView;
class ProgressWave;
class ILyricsService;
class ILibraryService;

class FullscreenPlayer {
public:
    FullscreenPlayer(MainWindow* main_window,
                     ILyricsService* lyrics_service,
                     ILibraryService* library_service);
    ~FullscreenPlayer();

    FullscreenPlayer(const FullscreenPlayer&) = delete;
    FullscreenPlayer& operator=(const FullscreenPlayer&) = delete;

    GtkWidget* get_widget() const { return root_; }

    // Pull the current authoritative state after the view is revealed.
    void sync_from_backend();

    void update_track_info(const std::string& title,
                           const std::string& artist,
                           const std::string& album,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_volume(double volume);
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_repeat_one(bool enabled);
    void request_lyrics_for_track(const std::string& track_id);

private:
    MainWindow* main_window_;
    ILyricsService* lyrics_service_;
    ILibraryService* library_service_;

    GtkWidget* root_;
    GtkWidget* center_box_;
    GtkWidget* title_label_;
    GtkWidget* subtitle_label_;
    GtkWidget* play_button_;
    GtkWidget* play_icon_;
    GtkWidget* volume_button_;
    GtkWidget* volume_revealer_;
    GtkWidget* volume_scale_;
    GtkWidget* shuffle_button_;
    GtkWidget* repeat_button_;
    GtkWidget* loop_button_;
    GtkWidget* lyrics_toggle_;
    GtkWidget* lyrics_revealer_;

    GtkAdjustment* volume_adjustment_;

    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<ProgressWave> progress_wave_;
    std::unique_ptr<SyncedLyricsView> lyrics_view_;

    // Async lyric requests may outlive the panel; callbacks check this flag.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::string current_track_id_;
    double current_position_ = 0.0;
    double current_duration_ = 0.0;
    bool updating_controls_ = false;
    bool playing_ = false;
    bool lyrics_visible_ = false;
    int last_width_ = 0;
    int last_height_ = 0;
    int last_artwork_side_ = 0;

    void setup_ui();
    void create_top_bar();
    void create_artwork();
    void create_track_info();
    void create_progress();
    void create_controls();
    void create_volume();
    void create_lyrics();

    void apply_playback_icon();

    static gboolean on_allocation_tick(GtkWidget* widget, GdkFrameClock* clock,
                                       gpointer user_data);
    static void on_close_clicked(GtkButton* button, gpointer user_data);
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_value_changed(GtkAdjustment* adjustment,
                                        gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_lyrics_toggled(GtkToggleButton* button, gpointer user_data);
};

} // namespace lollypop
