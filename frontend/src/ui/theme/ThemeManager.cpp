/**
 * Lollypop C++ - ThemeManager Implementation
 */

#include "ui/theme/ThemeManager.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace lollypop {

ThemeManager::ThemeManager() {
    base_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(base_css_provider_.get());
    accent_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(accent_css_provider_.get());

    // GTK 4 no longer returns a GError from the load functions; parse errors
    // arrive on this signal instead.
    auto log_parse_error = +[](GtkCssProvider*, GtkCssSection* section,
                               GError* error, gpointer) {
        std::cerr << "[ThemeManager] CSS parse error";
        if (section) {
            const GtkCssLocation* location =
                gtk_css_section_get_start_location(section);
            if (location) {
                std::cerr << " at line " << location->lines + 1
                          << ", column " << location->line_chars + 1;
            }
        }
        if (error) std::cerr << ": " << error->message;
        std::cerr << std::endl;
    };
    g_signal_connect(base_css_provider_.get(), "parsing-error",
                     G_CALLBACK(log_parse_error), nullptr);
    g_signal_connect(accent_css_provider_.get(), "parsing-error",
                     G_CALLBACK(log_parse_error), nullptr);

    // Initialize font scale from GTK settings
    GtkSettings* settings = gtk_settings_get_default();
    base_font_size_ = font_size_from_settings(settings);
    font_scale_ = 1.0;

    // Connect to GTK font changes
    font_scale_signal_id_ = g_signal_connect(
        settings, "notify::gtk-font-name",
        G_CALLBACK(on_gtk_font_scale_changed), this);
}

ThemeManager::~ThemeManager() {
    if (font_scale_signal_id_) {
        GtkSettings* settings = gtk_settings_get_default();
        g_signal_handler_disconnect(settings, font_scale_signal_id_);
    }
}

void ThemeManager::load_stylesheet(const std::string& css_path) {
    // Load the base stylesheet once into the base provider. This provider
    // is never replaced — only the accent provider is updated dynamically.
    constexpr const char* kResourcePath = "/com/mahesh/lollypop/style.css";

    if (!css_path.empty() && g_file_test(css_path.c_str(), G_FILE_TEST_EXISTS)) {
        gtk_css_provider_load_from_path(base_css_provider_.get(),
                                        css_path.c_str());
    } else {
        GError* error = nullptr;
        GBytes* data = g_resources_lookup_data(
            kResourcePath, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
        if (data) {
            g_bytes_unref(data);
            gtk_css_provider_load_from_resource(base_css_provider_.get(),
                                                kResourcePath);
        } else {
            std::cerr << "[ThemeManager] Failed to load stylesheet";
            if (!css_path.empty()) std::cerr << " from " << css_path;
            if (error) std::cerr << ": " << error->message;
            std::cerr << std::endl;
            g_clear_error(&error);
        }
    }

    // Attach both providers to the default display. Base first, then accent
    // (accent has higher priority so its :root variables override).
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(base_css_provider_.get()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(accent_css_provider_.get()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Apply initial accent color and font scale
    update_css_accent();
    update_css_font_scale();
}

void ThemeManager::set_accent_color(double r, double g, double b, double a) {
    accent_r_ = r;
    accent_g_ = g;
    accent_b_ = b;
    accent_a_ = a;
    update_css_accent();
}

void ThemeManager::get_accent_color(double& r, double& g, double& b,
                                    double& a) const {
    r = accent_r_;
    g = accent_g_;
    b = accent_b_;
    a = accent_a_;
}

void ThemeManager::persist_accent_color() {
    // Placeholder: persisted via settings layer in Phase 13 or when
    // a settings backend is wired up.
    std::cout << "[ThemeManager] Accent color persisted (placeholder)"
              << std::endl;
}

void ThemeManager::set_font_scale(double scale) {
    font_scale_ = std::clamp(scale, 0.5, 3.0);
    update_css_font_scale();
}

double ThemeManager::font_size_from_settings(GtkSettings* settings) {
    if (!settings) return 0.0;

    gchar* font_name = nullptr;
    g_object_get(settings, "gtk-font-name", &font_name, nullptr);
    if (!font_name) return 0.0;

    // GTK font names end in the point size, e.g. "Cantarell 11" or
    // "Noto Sans 13". Parsing the last token keeps family names with spaces.
    double size = 0.0;
    if (const char* last_space = std::strrchr(font_name, ' ')) {
        size = g_ascii_strtod(last_space + 1, nullptr);
    }
    g_free(font_name);
    return size > 0.0 ? size : 0.0;
}

void ThemeManager::on_gtk_font_scale_changed(GtkSettings* settings,
                                             GParamSpec* pspec,
                                             gpointer user_data) {
    (void)pspec;
    auto* self = static_cast<ThemeManager*>(user_data);
    if (!self || self->base_font_size_ <= 0.0) return;

    const double size = font_size_from_settings(settings);
    if (size <= 0.0) return;
    self->set_font_scale(size / self->base_font_size_);
}

void ThemeManager::update_css_font_scale() {
    update_css_overrides();
}

void ThemeManager::update_css_accent() {
    update_css_overrides();
}

void ThemeManager::update_css_overrides() {
    // Both providers stay in sync: replacing only one :root block used to
    // wipe the accent variables when the font size changed.
    gchar* css = g_strdup_printf(
        ":root {\n"
        "  --accent-color: rgba(%d, %d, %d, %f);\n"
        "  --accent-soft: rgba(%d, %d, %d, %f);\n"
        "  --font-scale: %f;\n"
        "}\n",
        static_cast<int>(accent_r_ * 255), static_cast<int>(accent_g_ * 255),
        static_cast<int>(accent_b_ * 255), accent_a_,
        static_cast<int>(accent_r_ * 255), static_cast<int>(accent_g_ * 255),
        static_cast<int>(accent_b_ * 255), accent_a_ * 0.15,
        font_scale_);

    gtk_css_provider_load_from_string(accent_css_provider_.get(), css);
    g_free(css);
}

} // namespace lollypop