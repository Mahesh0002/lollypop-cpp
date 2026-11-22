/**
 * Lollypop C++ - PiP (Picture-in-Picture) Window
 *
 * Compact floating player that reuses shared state (PlaybackState,
 * IPlayerService, ArtworkCache, AccentColorManager, VisualizerModel).
 *
 * The PiP window observes PlaybackState through IPlayerService multicast callbacks.
 * It provides local playback controls (prev, play/pause, next, volume).
 *
 * Wayland note: GTK 4 does not provide portable always-on-top behavior.
 * The PiP window is a normal GtkWindow. Users who want it to float above
 * other windows should add a Hyprland windowrule:
 *   windowrule = float,class:^(lollypop-cpp)$
 *   windowrule = pin,class:^(lollypop-cpp)$
 *
 * Design:
 *   [Artwork + Title/Artist] [Prev] [Play/Pause] [Waveform] [Next] [Volume] [Close]
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "models/VisualizerModel.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "services/ILyricsService.hpp"

namespace lollypop {

class MainWindow;
class BackendController;
class IPlayerService;
class ThemeManager;
class ArtworkView;
class WaveformView;
class Application;

class PiPWindow {
public:
    explicit PiPWindow(Application* app);
    ~PiPWindow();

    PiPWindow(const PiPWindow&) = delete;
    PiPWindow& operator=(const PiPWindow&) = delete;

    void present();

    // Set the backend (IPlayerService) — called by Application after construction.
    void set_backend(IPlayerService* backend);

    GtkWidget* get_widget() const { return window_.get(); }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> window_;
    std::unique_ptr<GtkWidget, WidgetDeleter> content_box_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> info_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> controls_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> prev_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> play_pause_button_;
    GtkWidget* play_pause_icon_ = nullptr;
    std::unique_ptr<GtkWidget, WidgetDeleter> next_button_;
    std::unique_ptr<WaveformView> waveform_view_;
    std::unique_ptr<VisualizerModel> waveform_model_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_scale_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_popover_;
    std::unique_ptr<GtkWidget, WidgetDeleter> close_button_;

    Application* app_;
    IPlayerService* backend_ = nullptr;

    bool is_playing_ = false;
    std::string current_track_id_;

    void setup_ui();
    void update_track_info(const std::string& title,
                           const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void set_points(const std::vector<float>& points);
    void update_control_sensitivity();

    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_play_pause_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_changed(GtkRange* range, gpointer user_data);
    static void on_close_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
