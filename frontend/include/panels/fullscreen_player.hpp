/**
 * Lollypop C++ - Fullscreen Now Playing
 *
 * Immersive, full-window playback surface. This class is a thin coordinator:
 *   FullscreenCloseButton  - standalone close control in the top bar
 *   FullscreenPlayerPanel  - artwork, metadata, progress, transport
 *   FullscreenLyricsPanel  - synced lyrics revealer on the right
 *   FullscreenQueuePanel   - queue revealer sharing the same right-side slot
 *   queue/lyrics toggles   - owned here, inserted into the player controls row
 *
 * The player panel keeps its width and only slides left when a side panel
 * opens. That panel is the same width as the player, separated by a 100px gap.
 *
 * All state is observed from IPlayerService through MainWindow; mutations go
 * back through the same interface so the shell/MPRIS remains the authority.
 */

#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;
class ILyricsService;
class ILibraryService;
class FullscreenPlayerPanel;
class FullscreenLyricsPanel;
class FullscreenQueuePanel;
class FullscreenCloseButton;

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
    void set_queue(const std::vector<std::string>& queue_track_ids);

private:
    MainWindow* main_window_;
    ILyricsService* lyrics_service_;
    ILibraryService* library_service_;

    GtkWidget* root_;
    GtkWidget* queue_toggle_;
    GtkWidget* lyrics_toggle_;
    GtkWidget* panel_divider_;

    GtkWidget* volume_button_;
    GtkWidget* volume_revealer_;
    GtkWidget* volume_scale_;
    GtkAdjustment* volume_adjustment_;

    std::unique_ptr<FullscreenCloseButton> close_button_;
    std::unique_ptr<FullscreenPlayerPanel> player_panel_;
    std::unique_ptr<FullscreenQueuePanel> queue_panel_;
    std::unique_ptr<FullscreenLyricsPanel> lyrics_panel_;

    double current_position_ = 0.0;
    double current_duration_ = 0.0;
    bool updating_controls_ = false;
    bool updating_toggles_ = false;
    bool playing_ = false;
    bool queue_visible_ = false;
    bool lyrics_visible_ = false;
    bool panel_visible_ = false;

    // One slide animation drives the player for whichever side panel is open.
    double panel_reveal_ = 0.0;
    double panel_reveal_from_ = 0.0;
    double panel_reveal_target_ = 0.0;
    gint64 panel_anim_start_ = 0;
    guint panel_anim_source_ = 0;

    int last_width_ = 0;
    int last_height_ = 0;
    int last_divider_margin_ = -1;

    void setup_ui();
    void create_top_bar();
    void create_volume();
    void create_divider();
    void create_queue();
    void create_lyrics();
    void set_panel_visible(bool visible);
    void apply_layout();

    static gboolean on_panel_animation_tick(GtkWidget* widget,
                                            GdkFrameClock* clock,
                                            gpointer user_data);
    static gboolean on_allocation_tick(GtkWidget* widget, GdkFrameClock* clock,
                                       gpointer user_data);
    static void on_volume_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_value_changed(GtkAdjustment* adjustment,
                                        gpointer user_data);
    static void on_lyrics_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_queue_toggled(GtkToggleButton* button, gpointer user_data);
};

} // namespace lollypop
