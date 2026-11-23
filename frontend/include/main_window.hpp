/**
 * Lollypop C++ - Main Window
 * Four-panel layout: Left Sidebar, Middle Library, Right Now Playing, Bottom Playback
 * Uses AdwToolbarView + AdwHeaderBar (libadwaita) for native toolbar.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "panels/left_sidebar.hpp"
#include "panels/middle_library.hpp"
#include "panels/right_now_playing.hpp"
#include "panels/bottom_playback.hpp"
#include "services/MockLibraryService.hpp"
#include "services/MockLyricsService.hpp"

namespace lollypop {

class Application;
class IPlayerService;
class ThemeManager;
class ILibraryService;
class ILyricsService;
class VolumeDial;

class MainWindow {
public:
    explicit MainWindow(Application* app);
    ~MainWindow();

    void present();
    GtkWindow* get_window() const { return GTK_WINDOW(window_); }

    // Panel accessors
    LeftSidebar* get_left_sidebar() const { return left_sidebar_.get(); }
    MiddleLibrary* get_middle_library() const { return middle_library_.get(); }
    RightNowPlaying* get_right_now_playing() const { return right_now_playing_.get(); }
    BottomPlayback* get_bottom_playback() const { return bottom_playback_.get(); }

    // Backend accessor (returns the IPlayerService interface; shell owns playback)
    IPlayerService* get_backend() const;

    // Theme accessor
    ThemeManager* get_theme_manager() const;

    // Application accessor
    Application* get_application() const { return app_; }

    // Volume dial accessor
    VolumeDial* get_volume_dial() const;

    // Called after BackendController::initialize() so the window can present
    // immediately while the backend loads on a low-priority idle source.
    void setup_after_backend();

private:
    Application* app_;
    GtkWidget* window_;
    GtkWidget* toolbar_view_;
    GtkWidget* header_bar_;
    GtkWidget* main_box_;
    GtkWidget* content_stack_;
    GtkWidget* content_grid_;

    // Four panels
    std::unique_ptr<LeftSidebar> left_sidebar_;
    std::unique_ptr<MiddleLibrary> middle_library_;
    std::unique_ptr<RightNowPlaying> right_now_playing_;
    std::unique_ptr<BottomPlayback> bottom_playback_;

    // Library service (mock for Phase 4, real implementation in Phase 10)
    std::unique_ptr<MockLibraryService> library_service_;

    // Lyrics service (mock for Phase 4, real implementation in Phase 10)
    std::unique_ptr<MockLyricsService> lyrics_service_;

    void setup_window();
    void setup_toolbar();
    void setup_layout();
    void setup_css();
    void connect_signals();
    void connect_backend();
    void disconnect_backend();

    // Callbacks
    static gboolean on_window_close(GtkWindow* window, gpointer user_data);
};

} // namespace lollypop
