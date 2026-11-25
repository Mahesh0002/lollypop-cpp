/**
 * Lollypop C++ - PiP (Picture-in-Picture) Window
 *
 * Compact floating player that observes IPlayerService multicast callbacks.
 * It provides track navigation, the shared wavy progress bar, volume, and a
 * close control. Playback starts from track activation, media keys, or MPRIS.
 *
 *   [Artwork + Title/Artist] [Prev] [Wavy Progress] [Next] [Volume] [Close]
 *
 * Wayland note: GTK 4 does not provide portable always-on-top behavior.
 * Add a Hyprland windowrule to float/pin this window if desired.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "services/ILyricsService.hpp"
#include "ui/components/SyncedLyricsLabel.hpp"
#include "lyrics/LyricTypes.hpp"

namespace lollypop {

class MainWindow;
class BackendController;
class IPlayerService;
class ThemeManager;
class ArtworkView;
class ProgressWave;
class Application;

class PiPWindow {
public:
    explicit PiPWindow(Application* app);
    ~PiPWindow();

    PiPWindow(const PiPWindow&) = delete;
    PiPWindow& operator=(const PiPWindow&) = delete;

    void present();
    void set_backend(IPlayerService* backend);
    GtkWidget* get_widget() const { return window_.get(); }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> window_;
    std::unique_ptr<GtkWidget, WidgetDeleter> root_box_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> info_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> album_label_;

    // Transport controls
    std::unique_ptr<GtkWidget, WidgetDeleter> transport_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> prev_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_pause_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> next_button_;

    // Progress
    std::unique_ptr<ProgressWave> progress_wave_;
    std::unique_ptr<GtkWidget, WidgetDeleter> time_elapsed_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> time_remaining_label_;

    // Compact lyrics
    std::unique_ptr<SyncedLyricsLabel> compact_lyrics_label_;
    SyncedLyrics current_lyrics_;
    int current_lyric_index_ = -1;
    bool has_lyrics_ = false;

    // Footer / mode controls
    std::unique_ptr<GtkWidget, WidgetDeleter> footer_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> shuffle_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> repeat_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> loop_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_scale_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_popover_;

    // Header buttons
    std::unique_ptr<GtkWidget, WidgetDeleter> menu_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> close_button_;

    Application* app_;
    IPlayerService* backend_ = nullptr;
    ILyricsService* lyrics_service_ = nullptr;

    // Backend callbacks may outlive this window. They capture the shared flag
    // and become no-ops as soon as the destructor runs.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::string current_track_id_;
    bool shuffle_active_ = false;
    bool repeat_active_ = false;
    bool loop_active_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;

    void setup_ui();
    void update_track_info(const std::string& title,
                           const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void update_control_sensitivity();
    void set_lyrics(const std::string& lyrics);
    void request_lyrics_for_track(const std::string& track_id);
    static std::string format_time(int seconds);
    void seek_relative(double seconds);
    void adjust_volume(double delta);

    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_play_pause_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_volume_changed(GtkRange* range, gpointer user_data);
    static void on_close_clicked(GtkButton* button, gpointer user_data);
    static void on_menu_clicked(GtkMenuButton* button, gpointer user_data);
    static void on_drag_begin(GtkGestureDrag* gesture, double x, double y, gpointer user_data);
    static void on_drag_update(GtkGestureDrag* gesture, double x, double y, gpointer user_data);
    static void on_progress_enter(GtkEventControllerMotion* controller, double x, double y, gpointer user_data);
    static void on_progress_leave(GtkEventControllerMotion* controller, gpointer user_data);
};

} // namespace lollypop