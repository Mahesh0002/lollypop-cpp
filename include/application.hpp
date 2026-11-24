/**
 * Lollypop C++ - Application Class
 * Main application controller managing the GTK application lifecycle
 * Uses AdwApplication (libadwaita) as the application base.
 */

#pragma once

#include <libadwaita-1/adwaita.h>
#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "ui/theme/ThemeManager.hpp"

namespace lollypop {

class MainWindow;
class BackendController;
class IPlayerService;
class ThemeManager;
class PiPWindow;
class ArtworkCache;
class ThumbnailGenerator;

class Application {
public:
    static std::unique_ptr<Application> create(const std::string& application_id);

    virtual ~Application();

    // Run the application
    int run(int argc, char** argv);

    // Getters
    AdwApplication* get_gtk_application() const { return app_.get(); }
    MainWindow* get_main_window() const { return main_window_.get(); }
    IPlayerService* get_backend() const;
    ThemeManager* get_theme_manager() const { return theme_manager_.get(); }
    PiPWindow* get_pip_window() const { return pip_window_.get(); }

    // Show/hide the Picture-in-Picture window
    void show_pip();
    void hide_pip();

protected:
    Application(const std::string& application_id);

    // Virtual methods for subclassing
    virtual void on_activate();
    virtual void on_startup();
    virtual void on_shutdown();

private:
    std::string application_id_;
    std::unique_ptr<AdwApplication, void(*)(AdwApplication*)> app_;
    // Declared before MainWindow so widgets are destroyed before the shared
    // thumbnail cache and its worker thread.
    std::unique_ptr<ArtworkCache> artwork_cache_;
    std::unique_ptr<ThumbnailGenerator> thumbnail_generator_;
    std::unique_ptr<MainWindow> main_window_;
    std::unique_ptr<BackendController> backend_;
    std::unique_ptr<ThemeManager> theme_manager_;
    std::unique_ptr<PiPWindow> pip_window_;

    void init_artwork_cache();

    // Static callbacks for GTK
    static void on_activate_static(AdwApplication* app, gpointer user_data);
    static void on_startup_static(AdwApplication* app, gpointer user_data);
    static void on_shutdown_static(AdwApplication* app, gpointer user_data);
};

} // namespace lollypop
