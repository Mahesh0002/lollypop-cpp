/**
 * Lollypop C++ - Application Class
 * Main application controller managing the GTK application lifecycle
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class MainWindow;
class BackendController;

class Application {
public:
    static std::unique_ptr<Application> create(const std::string& application_id);

    virtual ~Application() = default;

    // Run the application
    int run(int argc, char** argv);

    // Getters
    GtkApplication* get_gtk_application() const { return app_.get(); }
    MainWindow* get_main_window() const { return main_window_.get(); }
    BackendController* get_backend() const { return backend_.get(); }

protected:
    Application(const std::string& application_id);

    // Virtual methods for subclassing
    virtual void on_activate();
    virtual void on_startup();
    virtual void on_shutdown();

private:
    std::string application_id_;
    std::unique_ptr<GtkApplication, void(*)(GtkApplication*)> app_;
    std::unique_ptr<MainWindow> main_window_;
    std::unique_ptr<BackendController> backend_;

    // Static callbacks for GTK
    static void on_activate_static(GtkApplication* app, gpointer user_data);
    static void on_startup_static(GtkApplication* app, gpointer user_data);
    static void on_shutdown_static(GtkApplication* app, gpointer user_data);
};

} // namespace lollypop