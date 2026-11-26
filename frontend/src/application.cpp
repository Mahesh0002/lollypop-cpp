/**
 * Lollypop C++ - Application Implementation
 */

#include "application.hpp"

#include "data/ArtworkCache.hpp"
#include "data/ThumbnailGenerator.hpp"
#include "ui/components/ArtworkView.hpp"
#include "main_window.hpp"
#include "panels/pip_window.hpp"
#include "controller.hpp"
#include "services/IPlayerService.hpp"
#include "ui/theme/ThemeManager.hpp"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <iostream>

namespace lollypop {

namespace {

// The project does not ship a GSettings schema, so the accent preset lives in
// a small XDG key file. It can move to GSettings without touching callers.
constexpr const char* kThemeConfigDir = "lollypop-cpp";
constexpr const char* kThemeConfigFile = "theme.ini";
constexpr const char* kThemeConfigGroup = "appearance";
constexpr const char* kThemeAccentKey = "accent";

std::string theme_config_path() {
    return std::string(g_get_user_config_dir()) + "/" + kThemeConfigDir + "/" +
           kThemeConfigFile;
}

std::size_t load_accent_preset() {
    GKeyFile* key_file = g_key_file_new();
    std::size_t index = 0;
    GError* error = nullptr;
    const std::string path = theme_config_path();
    if (g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE,
                                  &error)) {
        const int stored = g_key_file_get_integer(
            key_file, kThemeConfigGroup, kThemeAccentKey, nullptr);
        if (stored >= 0) index = static_cast<std::size_t>(stored);
    }
    g_clear_error(&error);
    g_key_file_unref(key_file);
    return index;
}

void save_accent_preset(std::size_t index) {
    const std::string dir =
        std::string(g_get_user_config_dir()) + "/" + kThemeConfigDir;
    g_mkdir_with_parents(dir.c_str(), 0700);
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_integer(key_file, kThemeConfigGroup, kThemeAccentKey,
                           static_cast<int>(index));
    const std::string path = theme_config_path();
    g_key_file_save_to_file(key_file, path.c_str(), nullptr);
    g_key_file_unref(key_file);
}

} // namespace

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
    thumbnail_generator_.reset();
    artwork_cache_.reset();
    ArtworkView::configure_cache(nullptr, nullptr);
    backend_.reset();
    theme_manager_.reset();
}

int Application::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app_.get()), argc, argv);
}

IPlayerService* Application::get_backend() const {
    return backend_.get();
}

void Application::init_artwork_cache() {
    if (artwork_cache_) return;

    const char* base = g_get_user_cache_dir();
    const std::string cache_dir = base ? std::string(base) + "/lollypop-cpp/thumbnails"
                                       : "/tmp/lollypop-cpp/thumbnails";

    auto cache = std::make_unique<ArtworkCache>();
    if (!cache->initialize(cache_dir)) {
        std::cerr << "Application: artwork cache unavailable at " << cache_dir << std::endl;
        ArtworkView::configure_cache(nullptr, nullptr);
        return;
    }

    auto generator = std::make_unique<ThumbnailGenerator>();
    if (!generator->initialize(cache_dir, cache.get())) {
        std::cerr << "Application: thumbnail generator failed to start" << std::endl;
        ArtworkView::configure_cache(nullptr, nullptr);
        return;
    }

    artwork_cache_ = std::move(cache);
    thumbnail_generator_ = std::move(generator);
    ArtworkView::configure_cache(artwork_cache_.get(), thumbnail_generator_.get());
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
    // GApplication can emit activate more than once (for example when a second
    // launch is handed to the same instance). Re-presenting the existing
    // window avoids tearing down widgets that still have pending frame clocks.
    if (main_window_) {
        main_window_->present();
        return;
    }

    // Create the backend first (not yet initialized) so MainWindow's layout
    // can obtain a valid service pointer when building the UI widgets.
    backend_ = std::make_unique<BackendController>();

    // Configure the shared thumbnail cache before any ArtworkView is built.
    init_artwork_cache();

    // Create and present the window immediately so the user sees something
    // before any slow backend work. Backend initialization is deferred to
    // idle so the first frame paints without waiting for storage/audio/MPRIS.
    main_window_ = std::make_unique<MainWindow>(this);
    main_window_->present();

    g_idle_add(
        [](gpointer data) -> gboolean {
            auto* self = static_cast<Application*>(data);
            if (!self->backend_) return G_SOURCE_REMOVE;
            if (!self->backend_->initialize()) {
                std::cerr << "Failed to initialize backend" << std::endl;
            }
            if (self->main_window_) {
                self->main_window_->setup_after_backend();
            }
            return G_SOURCE_REMOVE;
        },
        this);

    // Delay the initial scan until the window has had a chance to paint.
    g_timeout_add_full(
        G_PRIORITY_DEFAULT, 1500,
        [](gpointer data) -> gboolean {
            auto* self = static_cast<Application*>(data);
            if (self->backend_) self->backend_->scan_default_music_library();
            return G_SOURCE_REMOVE;
        },
        this, nullptr);
}

void Application::on_startup() {
    // Create and initialize theme manager
    theme_manager_ = std::make_unique<ThemeManager>();
    // Always load CSS from the compiled-in gresource. No relative paths -
    // the app must look identical regardless of CWD or install location.
    theme_manager_->load_stylesheet("/com/mahesh/lollypop/style.css");

    // Restore the persisted preset and seed the accent before the first
    // frame. The seed depends on the scheme, so re-apply whenever Adw flips.
    accent_manager_.select(load_accent_preset());
    theme_manager_->set_scheme_changed_callback([this](bool) {
        apply_accent_seed();
    });
    apply_accent_seed();
}

void Application::apply_accent_seed() {
    if (!theme_manager_) return;
    const std::string hex = accent_manager_.seed(theme_manager_->is_dark());
    const design_tokens::Rgb seed = design_tokens::rgb_from_hex(hex);
    theme_manager_->set_accent_color(seed.r, seed.g, seed.b);
}

void Application::set_accent_preset(std::size_t index) {
    if (!accent_manager_.select(index)) return;
    save_accent_preset(index);
    apply_accent_seed();
}

void Application::on_shutdown() {
    // Cleanup - destructor handles remaining cleanup
    main_window_.reset();
    pip_window_.reset();
    thumbnail_generator_.reset();
    artwork_cache_.reset();
    ArtworkView::configure_cache(nullptr, nullptr);
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
