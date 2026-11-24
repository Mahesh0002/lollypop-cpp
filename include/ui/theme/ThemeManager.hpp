/**
 * Lollypop C++ - ThemeManager
 *
 * Manages the application-scoped GtkCssProvider and runtime accent color.
 * All CSS custom properties are defined here and applied application-wide.
 * Supports dynamic type scaling via GTK settings (prefers-reduced-motion, font scaling).
 * No global GTK theme files are modified (plan Section 12.5).
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "services/IPlayerService.hpp"

namespace lollypop {

class ThemeManager {
public:
    ThemeManager();
    ~ThemeManager();

    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    /// Load the application stylesheet into an application-scoped provider.
    void load_stylesheet(const std::string& css_path);

    /// Get the base GtkCssProvider for attaching to the default display.
    GtkCssProvider* get_css_provider() const { return base_css_provider_.get(); }

    /// Set the accent color (RGBA, 0-1). Updates CSS custom property at runtime.
    void set_accent_color(double r, double g, double b, double a = 1.0);

    /// Get the current accent color.
    void get_accent_color(double& r, double& g, double& b, double& a) const;

    /// Persist the selected palette/accent to settings (placeholder).
    void persist_accent_color();

    /// Get the current font scale factor (1.0 = default, >1.0 = larger text)
    double get_font_scale() const { return font_scale_; }

    /// Set font scale factor and update CSS
    void set_font_scale(double scale);

private:
    struct CssProviderDeleter {
        void operator()(GtkCssProvider* p) const {
            if (p) g_object_unref(p);
        }
    };
    std::unique_ptr<GtkCssProvider, CssProviderDeleter> base_css_provider_;
    std::unique_ptr<GtkCssProvider, CssProviderDeleter> accent_css_provider_;

    double accent_r_ = 0.4;
    double accent_g_ = 0.7;
    double accent_b_ = 1.0;
    double accent_a_ = 1.0;

    // Font scale for dynamic type (1.0 = default)
    double font_scale_ = 1.0;
    double base_font_size_ = 0.0;
    guint font_scale_signal_id_ = 0;

    void update_css_accent();
    void update_css_font_scale();
    void update_css_overrides();
    static double font_size_from_settings(GtkSettings* settings);
    static void on_gtk_font_scale_changed(GtkSettings* settings, GParamSpec* pspec, gpointer user_data);
};

} // namespace lollypop
