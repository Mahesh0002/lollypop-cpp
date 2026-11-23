/**
 * Lollypop C++ - Application Implementation
 */

#include "application.hpp"
#include "main_window.hpp"
#include "panels/pip_window.hpp"
#include "controller.hpp"
#include "services/IPlayerService.hpp"
#include "ui/theme/ThemeManager.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

std::unique_ptr<Application> Application::create(const std::string& application_id) {
    return std::unique_ptr<Application>(new Application(application_id));
}

Application::Application(const std::string& application_id)
    : application_id_(application_id),
      app_(nullptr, [](AdwApplication* app) { g_object_unref(app); }) {
    // Initialize libadwaita before creating the AdwApplication
    adw_init();

    // Create AdwApplication (libadwaita application base)
    AdwApplication* gtk_app = adw_application_new(application_id_.c_str(),
                                                  G_APPLICATION_DEFAULT_FLAGS);
    app_.reset(gtk_app);

    // Connect signals
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate_static), this);
    g_signal_connect(gtk_app, "startup", G_CALLBACK(on_startup_static), this);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_shutdown_static), this);
}

Application::~Application() {
    // Destroy the main window first: it clears all backend callbacks, so the
    // PiP window cannot receive a late callback while it is being torn down.
    // PiP needs the complete type here for its own destruction.
    main_window_.reset();
    pip_window_.reset();
    backend_.reset();
    theme_manager_.reset();
}

int Application::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app_.get()), argc, argv);
}

IPlayerService* Application::get_backend() const {
    return backend_.get();
}

void Application::show_pip() {
    if (!pip_window_) {
        pip_window_ = std::make_unique<PiPWindow>(this);
        pip_window_->set_backend(backend_.get());
    }
    pip_window_->present();
}

void Application::hide_pip() {
    if (pip_window_) {
        // Hide the window but keep it alive for reuse
        gtk_widget_set_visible(pip_window_->get_widget(), FALSE);
    }
}

void Application::on_activate() {
    // Initialize backend (owns storage, audio engine and MPRIS)
    backend_ = std::make_unique<BackendController>();

    if (!backend_->initialize()) {
        std::cerr << "Failed to initialize backend" << std::endl;
        return;
    }

    // Create main window
    main_window_ = std::make_unique<MainWindow>(this);
    main_window_->present();

    // Scan the user's Music folder after the UI is connected, so the
    // library-changed callback refreshes the visible page when it finishes.
    backend_->scan_default_music_library();
}

void Application::on_startup() {
    // Create and initialize theme manager
    theme_manager_ = std::make_unique<ThemeManager>();
    theme_manager_->load_stylesheet("resources/style.css");

    // Apply development default accent color
    auto accent_r = 0.65, accent_g = 0.45, accent_b = 1.0;
    theme_manager_->set_accent_color(accent_r, accent_g, accent_b);
}

void Application::on_shutdown() {
    // Cleanup - destructor handles remaining cleanup
    main_window_.reset();
    backend_.reset();
    theme_manager_.reset();
}

void Application::on_activate_static(AdwApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_activate();
}

void Application::on_startup_static(AdwApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_startup();
}

void Application::on_shutdown_static(AdwApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_shutdown();
}

} // namespace lollypop
