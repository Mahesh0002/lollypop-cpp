/**
 * Lollypop C++ - ThemeManager
 *
 * Owns the application-scoped GtkCssProvider and the runtime design tokens.
 * The high-priority provider emits surface, text, border, status, opacity,
 * accent and artwork-tint tokens from include/ui/theme/design_tokens.hpp, so
 * both schemes and every accent preset come from one source of truth.
 *
 * Scheme follows AdwStyleManager:dark. No partial prefers-color-scheme media
 * queries remain in the stylesheet. No global GTK theme files are modified.
 */

#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>

#include "services/IPlayerService.hpp"
#include "ui/theme/design_tokens.hpp"

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

    /// Set the accent seed color (RGBA, 0-1). Rebuilds the derived ramp.
    void set_accent_color(double r, double g, double b, double a = 1.0);

    /// Get the current accent seed color.
    void get_accent_color(double& r, double& g, double& b, double& a) const;

    /// Set the active scheme and rebuild tokens. Called by AdwStyleManager.
    void set_dark(bool dark);
    bool is_dark() const { return dark_; }

    /// Called after the scheme flips so callers can re-pick scheme-specific
    /// accent seeds. Receives the new dark flag.
    void set_scheme_changed_callback(std::function<void(bool)> callback);
    const std::function<void(bool)>& scheme_changed_callback() const {
        return scheme_changed_;
    }

    /// Artwork-derived tint (§7). Invalid or near-black/white input clears it.
    void set_artwork_tint(double r, double g, double b);
    void clear_artwork_tint();

    /// Extract the dominant tint from an artwork file and publish it. Missing
    /// or colourless artwork clears the current tint.
    void set_artwork_tint_from_path(const std::string& path);

    /// Force the artwork-tinted rectangular shape onto an individual control.
    /// Display-level providers can lose to libadwaita's own button styling, so
    /// detail hero buttons get a user-priority provider attached directly.
    void apply_detail_action_style(GtkWidget* button) const;

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
    std::unique_ptr<GtkCssProvider, CssProviderDeleter> shape_css_provider_;

    double accent_r_ = 0.545;  // #8B6CFF default violet seed
    double accent_g_ = 0.424;
    double accent_b_ = 1.0;
    double accent_a_ = 1.0;

    bool dark_ = true;
    design_tokens::Tint tint_;
    std::function<void(bool)> scheme_changed_;
    gulong scheme_signal_id_ = 0;

    // Font scale for dynamic type (1.0 = default)
    double font_scale_ = 1.0;
    double base_font_size_ = 0.0;
    guint font_scale_signal_id_ = 0;

    void update_css_accent();
    void update_css_font_scale();
    void update_css_overrides();
    static double font_size_from_settings(GtkSettings* settings);
    static void on_gtk_font_scale_changed(GtkSettings* settings, GParamSpec* pspec, gpointer user_data);
    static void on_system_scheme_changed(GObject* object, GParamSpec* pspec, gpointer user_data);
};

} // namespace lollypop
