/**
 * Lollypop C++ - ThemeManager Implementation
 */

#include "ui/theme/ThemeManager.hpp"
#include "ui/theme/design_tokens.hpp"

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libadwaita-1/adwaita.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>

namespace lollypop {

namespace {

// Dominant artwork tint (§7): a small, saturation-weighted average. Loading a
// 24px thumbnail keeps this off the playback-critical path. The result is
// shared by the now-playing panel and every detail-page action button.
std::optional<std::array<double, 3>> artwork_tint(const std::string& path) {
    if (path.empty()) return std::nullopt;
    static std::map<std::string, std::optional<std::array<double, 3>>> cache;
    if (const auto it = cache.find(path); it != cache.end()) return it->second;

    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
        path.c_str(), 24, 24, TRUE, &error);
    if (!pixbuf) {
        if (error) g_error_free(error);
        return std::nullopt;
    }
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar* pixels = gdk_pixbuf_read_pixels(pixbuf);

    double total_weight = 0.0;
    double r = 0.0, g = 0.0, b = 0.0;
    double flat_r = 0.0, flat_g = 0.0, flat_b = 0.0;
    int count = 0;
    for (int y = 0; y < height; ++y) {
        const guchar* row = pixels + y * rowstride;
        for (int x = 0; x < width; ++x) {
            const guchar* px = row + x * channels;
            if (channels == 4 && px[3] < 32) continue;
            const double pr = px[0] / 255.0;
            const double pg = px[1] / 255.0;
            const double pb = px[2] / 255.0;
            flat_r += pr; flat_g += pg; flat_b += pb;
            ++count;
            const double max = std::max({pr, pg, pb});
            const double min = std::min({pr, pg, pb});
            const double saturation = max > 0.0 ? (max - min) / max : 0.0;
            const double luminance =
                0.2126 * pr + 0.7152 * pg + 0.0722 * pb;
            const double weight =
                saturation * (1.0 - std::abs(luminance - 0.5) * 1.4);
            if (weight <= 0.0) continue;
            total_weight += weight;
            r += pr * weight;
            g += pg * weight;
            b += pb * weight;
        }
    }
    g_object_unref(pixbuf);
    if (count == 0) return std::nullopt;

    std::optional<std::array<double, 3>> result;
    if (total_weight > 1e-6) {
        result = std::array<double, 3>{r / total_weight, g / total_weight,
                                       b / total_weight};
    } else {
        result = std::array<double, 3>{flat_r / count, flat_g / count,
                                       flat_b / count};
    }
    if (cache.size() > 256) cache.clear();
    cache[path] = result;
    return result;
}

// GTK styles menu buttons, toggles, dropdowns and scales through element names
// that the generic `button` rule does not cover. Keep this provider above both
// the base stylesheet and the Apple UI layer so every control keeps the same
// rounded-rectangle language.
constexpr const char* kControlShapeCss = R"CSS(
button,
button.flat,
button.raised,
button.image-button,
button.text-button,
button.toggle,
button.suggested-action,
button.destructive-action,
button:checked,
menubutton,
menubutton > button,
checkbutton,
dropdown,
entry,
searchentry,
headerbar button,
headerbar menubutton,
popover button,
flowboxchild button {
    border-radius: 8px;
}

/* Detail hero actions are wide rectangular buttons with modest corner
   rounding — never the fully round pill some themes apply to text buttons.
   These live in the highest-priority provider so older, more specific rules
   further down style.css cannot reintroduce the pill shape or hard-coded
   accent colour. */
.album-detail-action-button,
.favourite-music-action-button {
    background-color: var(--detail-action-bg) !important;
    background-image: none !important;
    border-radius: 6px !important;
    min-height: 36px !important;
    padding: 0 18px !important;
    color: var(--detail-action-on) !important;
}

.album-detail-action-button:hover,
.favourite-music-action-button:hover {
    background-color: var(--detail-action-hover) !important;
}

.album-detail-action-button:active,
.favourite-music-action-button:active {
    background-color: var(--detail-action-active) !important;
}

.album-detail-play-button,
.favourite-music-play-button {
    font-weight: 700 !important;
}

.favourite-music-menu-button {
    border-radius: 6px;
}

.now-playing-mode-button {
    border-radius: 6px;
}

scale slider,
scrollbar slider,
switch slider {
    border-radius: 3px;
}

scale trough,
scale highlight,
switch trough {
    border-radius: 2px;
}
)CSS";

} // namespace

ThemeManager::ThemeManager() {
    base_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(base_css_provider_.get());
    accent_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(accent_css_provider_.get());
    shape_css_provider_.reset(gtk_css_provider_new());
    g_object_ref_sink(shape_css_provider_.get());

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
    g_signal_connect(shape_css_provider_.get(), "parsing-error",
                     G_CALLBACK(log_parse_error), nullptr);

    // Initialize font scale from GTK settings
    GtkSettings* settings = gtk_settings_get_default();
    base_font_size_ = font_size_from_settings(settings);
    font_scale_ = 1.0;

    // Connect to GTK font changes
    font_scale_signal_id_ = g_signal_connect(
        settings, "notify::gtk-font-name",
        G_CALLBACK(on_gtk_font_scale_changed), this);

    // The scheme comes from libadwaita, not from a partial media query. Both
    // the app and Adw widgets therefore flip together. libadwaita 1.x on this
    // baseline has no stable accent API, so this provider stays authoritative
    // for accent tokens and custom Cairo widgets read them from CSS.
    if (AdwStyleManager* style = adw_style_manager_get_default()) {
        dark_ = adw_style_manager_get_dark(style);
        scheme_signal_id_ = g_signal_connect(
            style, "notify::dark", G_CALLBACK(on_system_scheme_changed), this);
    }
}

ThemeManager::~ThemeManager() {
    if (font_scale_signal_id_) {
        GtkSettings* settings = gtk_settings_get_default();
        g_signal_handler_disconnect(settings, font_scale_signal_id_);
    }
    if (scheme_signal_id_) {
        if (AdwStyleManager* style = adw_style_manager_get_default()) {
            g_signal_handler_disconnect(style, scheme_signal_id_);
        }
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

    // Shape is loaded last and at the highest priority so neither the accent
    // tokens nor the Apple UI layer can reintroduce pill-shaped controls.
    gtk_css_provider_load_from_string(shape_css_provider_.get(),
                                      kControlShapeCss);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(shape_css_provider_.get()),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);

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

void ThemeManager::set_dark(bool dark) {
    if (dark_ == dark) return;
    dark_ = dark;
    update_css_overrides();
    if (scheme_changed_) scheme_changed_(dark_);
}

void ThemeManager::set_scheme_changed_callback(
    std::function<void(bool)> callback) {
    scheme_changed_ = std::move(callback);
}

void ThemeManager::set_artwork_tint(double r, double g, double b) {
    const design_tokens::Rgb color{r, g, b};
    const double luminance = design_tokens::relative_luminance(color);
    // Near-black or near-white artwork carries no usable hue; fall back to the
    // panel surface instead of muddying text contrast.
    if (luminance < 0.02 || luminance > 0.92) {
        clear_artwork_tint();
        return;
    }
    // Clamp luminance and saturation so a bright cover cannot wash out the
    // panel text sitting on top of the tint.
    design_tokens::Hsl hsl = design_tokens::hsl_from_rgb(color);
    hsl.l = std::clamp(hsl.l, 0.18, 0.55);
    hsl.s = std::min(hsl.s, 0.55);
    tint_.valid = true;
    tint_.color = design_tokens::rgb_from_hsl(hsl);
    update_css_overrides();
}

void ThemeManager::clear_artwork_tint() {
    if (!tint_.valid) return;
    tint_ = {};
    update_css_overrides();
}

void ThemeManager::set_artwork_tint_from_path(const std::string& path) {
    if (const auto tint = artwork_tint(path)) {
        set_artwork_tint((*tint)[0], (*tint)[1], (*tint)[2]);
    } else {
        clear_artwork_tint();
    }
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

void ThemeManager::on_system_scheme_changed(GObject* object, GParamSpec* pspec,
                                           gpointer user_data) {
    (void)pspec;
    auto* self = static_cast<ThemeManager*>(user_data);
    if (!self) return;
    self->set_dark(adw_style_manager_get_dark(ADW_STYLE_MANAGER(object)));
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
    // One seed produces every accent token. The stylesheet never hardcodes a
    // second purple, and switching schemes re-emits the whole semantic block.
    const design_tokens::Rgb seed{accent_r_, accent_g_, accent_b_};
    (void)accent_a_;
    const std::string css =
        design_tokens::build_css(font_scale_, seed, dark_, tint_);
    gtk_css_provider_load_from_string(accent_css_provider_.get(), css.c_str());
}

} // namespace lollypop
