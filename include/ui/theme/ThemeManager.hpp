/**
 * Lollypop C++ - ThemeManager
 *
 * Manages the application-scoped GtkCssProvider and runtime accent color.
 * All CSS custom properties are defined here and applied application-wide.
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

    void update_css_accent();
};

} // namespace lollypop
