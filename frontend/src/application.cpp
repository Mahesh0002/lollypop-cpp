/**
 * Lollypop C++ - Application Implementation
 */

#include "application.hpp"
#include "main_window.hpp"
#include "controller.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

std::unique_ptr<Application> Application::create(const std::string& application_id) {
    return std::unique_ptr<Application>(new Application(application_id));
}

Application::Application(const std::string& application_id)
    : application_id_(application_id),
      app_(nullptr, [](GtkApplication* app) { g_object_unref(app); }) {
    // Create GTK application
    GtkApplication* gtk_app = gtk_application_new(application_id_.c_str(), G_APPLICATION_DEFAULT_FLAGS);
    app_.reset(gtk_app);

    // Connect signals
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate_static), this);
    g_signal_connect(gtk_app, "startup", G_CALLBACK(on_startup_static), this);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_shutdown_static), this);
}

int Application::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app_.get()), argc, argv);
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
}

void Application::on_startup() {
    // Set up CSS provider for styling
    GtkCssProvider* provider = gtk_css_provider_new();

    // Try to load CSS from multiple locations
    const char* css_paths[] = {
        "resources/style.css",                          // Build directory (when run from build dir)
        "../resources/style.css",                       // Build directory (when run from build/bin)
        "/usr/local/share/lollypop-cpp/style.css",      // Installed location
        "/usr/share/lollypop-cpp/style.css",            // Alternative installed location
        nullptr
    };

    gboolean loaded = FALSE;
    for (int i = 0; css_paths[i] != nullptr; ++i) {
        GFile* file = g_file_new_for_path(css_paths[i]);
        if (g_file_query_exists(file, nullptr)) {
            gtk_css_provider_load_from_file(provider, file);
            loaded = TRUE;
            g_object_unref(file);
            break;
        }
        g_object_unref(file);
    }

    if (!loaded) {
        std::cerr << "Warning: Could not find style.css in any known location" << std::endl;
    }

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

void Application::on_shutdown() {
    // Cleanup
    main_window_.reset();
    backend_.reset();
}

void Application::on_activate_static(GtkApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_activate();
}

void Application::on_startup_static(GtkApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_startup();
}

void Application::on_shutdown_static(GtkApplication* app, gpointer user_data) {
    static_cast<Application*>(user_data)->on_shutdown();
}

} // namespace lollypop