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
    std::unique_ptr<ProgressWave> progress_wave_;
    std::unique_ptr<GtkWidget, WidgetDeleter> next_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_button_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_scale_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_popover_;
    std::unique_ptr<GtkWidget, WidgetDeleter> close_button_;

    Application* app_;
    IPlayerService* backend_ = nullptr;

    // Backend callbacks may outlive this window. They capture the shared flag
    // and become no-ops as soon as the destructor runs.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::string current_track_id_;

    void setup_ui();
    void update_track_info(const std::string& title,
                           const std::string& artist,
                           const std::string& artwork_path);
    void update_playback_position(double position, double duration);
    void set_playing(bool playing);
    void update_control_sensitivity();

    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_volume_changed(GtkRange* range, gpointer user_data);
    static void on_close_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
