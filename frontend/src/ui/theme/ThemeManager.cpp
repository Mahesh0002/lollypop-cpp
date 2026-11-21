/**
 * Lollypop C++ - ThemeManager Implementation
 */

#include "ui/theme/ThemeManager.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

ThemeManager::ThemeManager() {
    base_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(base_css_provider_.get());
    accent_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(accent_css_provider_.get());
}

ThemeManager::~ThemeManager() = default;

void ThemeManager::load_stylesheet(const std::string& css_path) {
    // Load the base stylesheet once into the base provider. This provider
    // is never replaced — only the accent provider is updated dynamically.
    gtk_css_provider_load_from_path(base_css_provider_.get(), css_path.c_str());

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

    // Apply initial accent color
    update_css_accent();
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

void ThemeManager::update_css_accent() {
    // Only update the accent provider with :root CSS custom properties.
    // The base stylesheet in base_css_provider_ is never touched.
    gchar* css = g_strdup_printf(
        ":root {\n"
        "  --accent-color: rgba(%d, %d, %d, %f);\n"
        "  --accent-soft: rgba(%d, %d, %d, %f);\n"
        "}\n",
        static_cast<int>(accent_r_ * 255), static_cast<int>(accent_g_ * 255),
        static_cast<int>(accent_b_ * 255), accent_a_,
        static_cast<int>(accent_r_ * 255), static_cast<int>(accent_g_ * 255),
        static_cast<int>(accent_b_ * 255), accent_a_ * 0.15);

    gtk_css_provider_load_from_data(accent_css_provider_.get(), css, -1);
    g_free(css);
}

} // namespace lollypop
