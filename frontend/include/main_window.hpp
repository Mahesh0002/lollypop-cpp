/**
 * Lollypop C++ - Main Window
 * Four-panel layout: Left Sidebar, Middle Library, Right Now Playing, Bottom Playback
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "panels/left_sidebar.hpp"
#include "panels/middle_library.hpp"
#include "panels/right_now_playing.hpp"
#include "panels/bottom_playback.hpp"

namespace lollypop {

class Application;
class BackendController;

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

    // Backend accessor
    BackendController* get_backend() const;

private:
    Application* app_;
    GtkWidget* window_;
    GtkWidget* main_grid_;

    // Four panels
    std::unique_ptr<LeftSidebar> left_sidebar_;
    std::unique_ptr<MiddleLibrary> middle_library_;
    std::unique_ptr<RightNowPlaying> right_now_playing_;
    std::unique_ptr<BottomPlayback> bottom_playback_;

    void setup_window();
    void setup_layout();
    void setup_css();
    void connect_signals();
    void connect_backend();

    // Callbacks
    static void on_window_close(GtkWindow* window, gpointer user_data);
    static void on_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data);
};

} // namespace lollypop