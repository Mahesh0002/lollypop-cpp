/**
 * Lollypop C++ - Canonical design tokens
 *
 * Single C++ source for every semantic colour token. ThemeManager injects
 * these at runtime through a high-priority GtkCssProvider; resources/style.css
 * keeps matching dark defaults in :root as a first-frame fallback.
 *
 * Architecture (docs/THEME_AND_COLOR_PLAN.md §4):
 *   primitive ramp  ->  semantic role  ->  component
 *
 * One accent seed produces the full nine-token accent ramp and the on-accent
 * ink, selected by measured contrast. Nothing here hardcodes a second purple.
 *
 * scripts/check_theme_contrast.py verifies the emitted values in CI.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace lollypop {
namespace design_tokens {

inline constexpr const char* kFontFamilyName = "--font-family";
inline constexpr const char* kFontFamily =
    "\"Inter\", \"Noto Sans\", \"Liberation Sans\", sans-serif";
inline constexpr const char* kFontScaleName = "--font-scale";

struct FontToken {
    const char* name;
    const char* css_name;
    double px;
};

inline constexpr FontToken kFontTokens[] = {
    {"kFontDisplay", "--font-display", 34.0},
    {"kFontDisplaySmall", "--font-display-small", 30.0},
    {"kFontHero", "--font-hero", 48.0},
    {"kFontTitle1", "--font-title1", 28.0},
    {"kFontTitle2", "--font-title2", 22.0},
    {"kFontTitle3", "--font-title3", 20.0},
    {"kFontHeadline", "--font-headline", 17.0},
    {"kFontUiMedium", "--font-ui-medium", 18.0},
    {"kFontBody", "--font-body", 17.0},
    {"kFontCallout", "--font-callout", 16.0},
    {"kFontSubhead", "--font-subhead", 15.0},
    {"kFontUiSmall", "--font-ui-small", 14.0},
    {"kFontFootnote", "--font-footnote", 13.0},
    {"kFontCaption1", "--font-caption1", 12.0},
    {"kFontCaption2", "--font-caption2", 11.0},
};

struct MotionToken {
    const char* name;
    const char* css_name;
    int ms;
};

inline constexpr MotionToken kMotionTokens[] = {
    {"kMotionFast", "--motion-fast", 120},
    {"kMotionBase", "--motion-base", 160},
    {"kMotionSlow", "--motion-slow", 240},
};

inline constexpr const char* kEaseOutName = "--ease-out";
inline constexpr const char* kEaseOut = "cubic-bezier(0.2, 0.8, 0.2, 1)";
inline constexpr const char* kBorderFocusName = "--border-focus";
inline constexpr const char* kAccentColorName = "--accent-color";
inline constexpr const char* kAccentSoftName = "--accent-soft";
inline constexpr const char* kTextOnAccentName = "--text-on-accent";
inline constexpr const char* kDetailActionBgName = "--detail-action-bg";
inline constexpr const char* kDetailActionHoverName = "--detail-action-hover";
inline constexpr const char* kDetailActionActiveName = "--detail-action-active";
inline constexpr const char* kDetailActionOnName = "--detail-action-on";
inline constexpr const char* kTextOnAccent = "#0B0B10";

inline std::string format_number(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/* ------------------------------------------------------------------------- */
/* Colour primitives                                                          */
/* ------------------------------------------------------------------------- */

struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

struct Hsl {
    double h = 0.0;  // degrees, 0-360
    double s = 0.0;  // 0-1
    double l = 0.0;  // 0-1
};

inline double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

inline std::string hex_from_rgb(Rgb color) {
    const auto channel = [](double value) {
        const int scaled =
            static_cast<int>(std::lround(clamp01(value) * 255.0));
        char buffer[3];
        std::snprintf(buffer, sizeof(buffer), "%02X", scaled);
        return std::string(buffer);
    };
    return "#" + channel(color.r) + channel(color.g) + channel(color.b);
}

inline std::string rgba_from_rgb(Rgb color, double alpha) {
    const int r = static_cast<int>(std::lround(clamp01(color.r) * 255.0));
    const int g = static_cast<int>(std::lround(clamp01(color.g) * 255.0));
    const int b = static_cast<int>(std::lround(clamp01(color.b) * 255.0));
    std::ostringstream out;
    out << "rgba(" << r << ", " << g << ", " << b << ", "
        << format_number(alpha) << ")";
    return out.str();
}

inline Rgb rgb_from_hex(std::string_view hex) {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    if (hex.size() == 3) {
        const auto expand = [](char c) {
            const int value = std::isdigit(static_cast<unsigned char>(c))
                                  ? c - '0'
                                  : (std::tolower(c) - 'a' + 10);
            return value * 17;
        };
        return {expand(hex[0]) / 255.0, expand(hex[1]) / 255.0,
                expand(hex[2]) / 255.0};
    }
    if (hex.size() < 6) return {};
    const auto pair = [&](std::size_t offset) {
        return static_cast<double>(std::stoi(std::string(hex.substr(offset, 2)),
                                             nullptr, 16)) /
               255.0;
    };
    return {pair(0), pair(2), pair(4)};
}

inline Hsl hsl_from_rgb(Rgb color) {
    const double max = std::max({color.r, color.g, color.b});
    const double min = std::min({color.r, color.g, color.b});
    const double delta = max - min;
    Hsl hsl;
    hsl.l = (max + min) / 2.0;
    if (delta <= 1e-9) return hsl;
    hsl.s = hsl.l > 0.5 ? delta / (2.0 - max - min) : delta / (max + min);
    if (max == color.r) {
        hsl.h = 60.0 * std::fmod((color.g - color.b) / delta, 6.0);
    } else if (max == color.g) {
        hsl.h = 60.0 * ((color.b - color.r) / delta + 2.0);
    } else {
        hsl.h = 60.0 * ((color.r - color.g) / delta + 4.0);
    }
    if (hsl.h < 0.0) hsl.h += 360.0;
    return hsl;
}

inline double hue_to_rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

inline Rgb rgb_from_hsl(Hsl hsl) {
    if (hsl.s <= 1e-9) return {hsl.l, hsl.l, hsl.l};
    const double q =
        hsl.l < 0.5 ? hsl.l * (1.0 + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
    const double p = 2.0 * hsl.l - q;
    const double h = hsl.h / 360.0;
    return {hue_to_rgb(p, q, h + 1.0 / 3.0), hue_to_rgb(p, q, h),
            hue_to_rgb(p, q, h - 1.0 / 3.0)};
}

inline double channel_luminance(double channel) {
    return channel <= 0.03928 ? channel / 12.92
                              : std::pow((channel + 0.055) / 1.055, 2.4);
}

inline double relative_luminance(Rgb color) {
    return 0.2126 * channel_luminance(color.r) +
           0.7152 * channel_luminance(color.g) +
           0.0722 * channel_luminance(color.b);
}

inline double contrast_ratio(Rgb a, Rgb b) {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/* ------------------------------------------------------------------------- */
/* Accent ramp: one seed -> nine derived tokens (§5.1)                       */
/* ------------------------------------------------------------------------- */

struct AccentRamp {
    Rgb color;
    Rgb hover;
    Rgb active;
    Rgb soft;
    Rgb softer;
    Rgb border;
    Rgb shadow;
    Rgb shadow_strong;
    Rgb on;
    double soft_alpha = 0.14;
    double softer_alpha = 0.07;
    double border_alpha = 0.35;
    double shadow_alpha = 0.28;
    double shadow_strong_alpha = 0.42;
};

inline constexpr Rgb kInkDark = {0.043, 0.043, 0.063};   // #0B0B10
inline constexpr Rgb kInkLight = {1.0, 1.0, 1.0};
inline constexpr Rgb kBlack = {0.0, 0.0, 0.0};

inline AccentRamp derive_accent(Rgb seed) {
    AccentRamp ramp;
    const Hsl hsl = hsl_from_rgb(seed);
    ramp.color = seed;
    Hsl lighter = hsl;
    lighter.l = clamp01(hsl.l + 0.08);
    Hsl darker = hsl;
    darker.l = clamp01(hsl.l - 0.10);
    ramp.hover = rgb_from_hsl(lighter);
    ramp.active = rgb_from_hsl(darker);
    ramp.soft = seed;
    ramp.softer = seed;
    ramp.border = seed;
    ramp.shadow = seed;
    ramp.shadow_strong = seed;

    // Choose the ink with the higher measured contrast rather than assuming
    // white. Violet #8B6CFF, for example, is unreadable with white text.
    const double dark_contrast = contrast_ratio(seed, kInkDark);
    const double light_contrast = contrast_ratio(seed, kInkLight);
    ramp.on = dark_contrast >= light_contrast ? kInkDark : kInkLight;
    // Enforce the AA rule from §5.1 even if both are close.
    if (contrast_ratio(seed, ramp.on) < 4.5) {
        ramp.on = light_contrast > dark_contrast ? kInkLight : kInkDark;
    }
    return ramp;
}

/* ------------------------------------------------------------------------- */
/* Semantic token block (§4, §5.3, §6)                                       */
/* ------------------------------------------------------------------------- */

struct NamedColor {
    const char* name;
    Rgb value;
};

// Surface, text, border, status and scrim tokens. The accent ramp and the
// white/black opacity ramps are generated separately so each scheme retunes
// together.
inline void append_scheme_tokens(std::ostringstream& css, bool dark,
                                 Rgb accent) {
    const NamedColor dark_colors[] = {
        {"--surface-app", rgb_from_hex("#0B0B10")},
        {"--surface-sidebar", rgb_from_hex("#111118")},
        {"--surface-panel", rgb_from_hex("#16161F")},
        {"--surface-elevated", rgb_from_hex("#1C1C27")},
        {"--surface-raised", rgb_from_hex("#232331")},
        {"--text-primary", rgb_from_hex("#F5F5F7")},
        {"--text-secondary", rgb_from_hex("#B8B6C6")},
        {"--text-tertiary", rgb_from_hex("#8E8B9E")},
        {"--text-quaternary", rgb_from_hex("#6A6778")},
        {"--text-disabled", rgb_from_hex("#5A5768")},
        {"--border-subtle", rgb_from_hex("#2A2A38")},
        {"--border-strong", rgb_from_hex("#3A3A4C")},
        {"--success", rgb_from_hex("#32D74B")},
        {"--warning", rgb_from_hex("#FFD60A")},
        {"--error", rgb_from_hex("#FF453A")},
        {"--info", rgb_from_hex("#64D2FF")},
        {"--scrim-text", rgb_from_hex("#FFFFFF")},
    };
    const NamedColor light_colors[] = {
        {"--surface-app", rgb_from_hex("#F5F5F7")},
        {"--surface-sidebar", rgb_from_hex("#EFEFF2")},
        {"--surface-panel", rgb_from_hex("#FFFFFF")},
        {"--surface-elevated", rgb_from_hex("#FFFFFF")},
        {"--surface-raised", rgb_from_hex("#F7F7FA")},
        {"--text-primary", rgb_from_hex("#1D1D1F")},
        {"--text-secondary", rgb_from_hex("#55555D")},
        {"--text-tertiary", rgb_from_hex("#6B6B73")},
        {"--text-quaternary", rgb_from_hex("#A0A0A8")},
        {"--text-disabled", rgb_from_hex("#B4B4BC")},
        {"--border-subtle", rgb_from_hex("#E2E2E8")},
        {"--border-strong", rgb_from_hex("#C7C7D1")},
        {"--success", rgb_from_hex("#1E8A32")},
        {"--warning", rgb_from_hex("#B58900")},
        {"--error", rgb_from_hex("#D70015")},
        {"--info", rgb_from_hex("#0A84B8")},
        {"--scrim-text", rgb_from_hex("#1D1D1F")},
    };
    const NamedColor* colors = dark ? dark_colors : light_colors;
    const std::size_t color_count =
        sizeof(dark_colors) / sizeof(dark_colors[0]);
    for (std::size_t i = 0; i < color_count; ++i) {
        css << "  " << colors[i].name << ": " << hex_from_rgb(colors[i].value)
            << ";\n";
    }

    // Overlay ramp: white alphas in dark mode, black alphas in light mode.
    const double overlay_steps[] = {0.02, 0.04, 0.06, 0.08, 0.10,
                                    0.12, 0.16, 0.72, 0.82, 0.92};
    for (double alpha : overlay_steps) {
        const std::string name =
            "--overlay-" +
            std::to_string(static_cast<int>(std::lround(alpha * 100.0)));
        css << "  " << name << ": "
            << rgba_from_rgb(dark ? kInkLight : kInkDark, alpha) << ";\n";
    }

    // Shadows lighten in light mode so panels do not look bruised.
    const double shadow_steps[] = {0.06, 0.08, 0.10, 0.16, 0.20,
                                   0.24, 0.28, 0.38, 0.40, 0.50};
    for (double alpha : shadow_steps) {
        const double value = dark ? alpha : alpha * 0.4;
        const std::string name =
            "--shadow-" +
            std::to_string(static_cast<int>(std::lround(alpha * 100.0)));
        css << "  " << name << ": " << rgba_from_rgb(kBlack, value) << ";\n";
    }

    css << "  --surface-hover: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.06 : 0.05)
        << ";\n";
    css << "  --surface-active: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.10 : 0.09)
        << ";\n";
    css << "  --surface-selected: " << rgba_from_rgb(accent, 0.12) << ";\n";

    css << "  --overlay-hairline: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.10 : 0.08)
        << ";\n";
    css << "  --toolbar-bg: "
        << rgba_from_rgb(dark ? kInkDark : kInkLight, dark ? 0.88 : 0.82)
        << ";\n";
    css << "  --glass-thin-bg: " << rgba_from_rgb(kInkLight, dark ? 0.06 : 0.72)
        << ";\n";
    css << "  --glass-regular-bg: "
        << rgba_from_rgb(kInkLight, dark ? 0.08 : 0.82) << ";\n";
    css << "  --glass-thick-bg: "
        << rgba_from_rgb(kInkLight, dark ? 0.12 : 0.92) << ";\n";
    css << "  --glass-thin-border: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.08 : 0.08)
        << ";\n";
    css << "  --glass-regular-border: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.12 : 0.10)
        << ";\n";
    css << "  --glass-thick-border: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.16 : 0.12)
        << ";\n";
    css << "  --scrim-bg: "
        << rgba_from_rgb(dark ? kBlack : kInkLight, dark ? 0.34 : 0.72)
        << ";\n";
    css << "  --scrim-strong: "
        << rgba_from_rgb(dark ? kBlack : kInkLight, dark ? 0.55 : 0.86)
        << ";\n";
    // Same colour as the scrim with zero alpha, so gradients fade without
    // interpolating towards transparent black in light mode.
    css << "  --scrim-transparent: "
        << rgba_from_rgb(dark ? kBlack : kInkLight, 0.0) << ";\n";
    css << "  --scrim-border: "
        << rgba_from_rgb(dark ? kInkLight : kInkDark, dark ? 0.16 : 0.08)
        << ";\n";
    css << "  --shadow-strong: "
        << rgba_from_rgb(kBlack, dark ? 0.50 : 0.16) << ";\n";
    css << "  --shadow-subtle: "
        << rgba_from_rgb(kBlack, dark ? 0.20 : 0.08) << ";\n";

    // Status soft variants for banners.
    const NamedColor status[] = {
        {"--success", dark ? rgb_from_hex("#32D74B") : rgb_from_hex("#1E8A32")},
        {"--warning", dark ? rgb_from_hex("#FFD60A") : rgb_from_hex("#B58900")},
        {"--error", dark ? rgb_from_hex("#FF453A") : rgb_from_hex("#D70015")},
        {"--info", dark ? rgb_from_hex("#64D2FF") : rgb_from_hex("#0A84B8")},
    };
    for (const NamedColor& entry : status) {
        css << "  " << entry.name << "-soft: "
            << rgba_from_rgb(entry.value, 0.14) << ";\n";
    }
    css << "  --destructive-bg: "
        << rgba_from_rgb(status[2].value, 0.14) << ";\n";
}

/* ------------------------------------------------------------------------- */
/* Runtime stylesheet                                                         */
/* ------------------------------------------------------------------------- */

struct Tint {
    bool valid = false;
    Rgb color;
};

inline std::string build_css(double font_scale, Rgb accent_seed, bool dark,
                             Tint tint = {}) {
    const AccentRamp accent = derive_accent(accent_seed);
    std::ostringstream css;
    css << ":root {\n";
    css << "  " << kFontFamilyName << ": " << kFontFamily << ";\n";
    css << "  " << kFontScaleName << ": " << format_number(font_scale) << ";\n";
    for (const FontToken& token : kFontTokens) {
        css << "  " << token.css_name << ": calc("
            << format_number(token.px) << "px * var(--font-scale));\n";
    }
    for (const MotionToken& token : kMotionTokens) {
        css << "  " << token.css_name << ": " << token.ms << "ms;\n";
    }
    css << "  " << kEaseOutName << ": " << kEaseOut << ";\n";

    // Accent ramp (§5.1): every derived value is computed here, never in CSS.
    css << "  --accent-color: " << hex_from_rgb(accent.color) << ";\n";
    css << "  --accent-hover: " << hex_from_rgb(accent.hover) << ";\n";
    css << "  --accent-active: " << hex_from_rgb(accent.active) << ";\n";
    css << "  --accent-soft: "
        << rgba_from_rgb(accent.soft, accent.soft_alpha) << ";\n";
    css << "  --accent-bg: " << rgba_from_rgb(accent.soft, accent.soft_alpha)
        << ";\n";
    css << "  --accent-softer: "
        << rgba_from_rgb(accent.softer, accent.softer_alpha) << ";\n";
    css << "  --accent-border: "
        << rgba_from_rgb(accent.border, accent.border_alpha) << ";\n";
    css << "  --accent-shadow: "
        << rgba_from_rgb(accent.shadow, accent.shadow_alpha) << ";\n";
    css << "  --accent-shadow-strong: "
        << rgba_from_rgb(accent.shadow_strong, accent.shadow_strong_alpha)
        << ";\n";
    css << "  --accent-on: " << hex_from_rgb(accent.on) << ";\n";
    css << "  " << kTextOnAccentName << ": " << hex_from_rgb(accent.on)
        << ";\n";
    css << "  " << kBorderFocusName << ": var(--accent-color);\n";

    // Detail-page primary controls follow the artwork palette when one is
    // available, and fall back to the accent ramp otherwise. Apple's album
    // pages use the same content-derived colour for their filled buttons.
    const Rgb action_seed = tint.valid ? tint.color : accent.color;
    const AccentRamp action = derive_accent(action_seed);
    css << "  " << kDetailActionBgName << ": "
        << hex_from_rgb(action.color) << ";\n";
    css << "  " << kDetailActionHoverName << ": "
        << hex_from_rgb(action.hover) << ";\n";
    css << "  " << kDetailActionActiveName << ": "
        << hex_from_rgb(action.active) << ";\n";
    css << "  " << kDetailActionOnName << ": "
        << hex_from_rgb(action.on) << ";\n";

    append_scheme_tokens(css, dark, accent.color);

    // Artwork tint (§7). Invalid tint intentionally falls back to the panel
    // surface with no synthetic wash.
    if (tint.valid) {
        css << "  --tint-color: " << hex_from_rgb(tint.color) << ";\n";
        css << "  --tint-strong: "
            << rgba_from_rgb(tint.color, dark ? 0.35 : 0.18) << ";\n";
        css << "  --tint-soft: "
            << rgba_from_rgb(tint.color, dark ? 0.10 : 0.06) << ";\n";
    } else {
        css << "  --tint-color: " << (dark ? "#16161F" : "#FFFFFF") << ";\n";
        css << "  --tint-strong: transparent;\n";
        css << "  --tint-soft: transparent;\n";
    }

    css << "}\n";

    // High contrast must win over the runtime provider, so restate the glass
    // overrides here rather than relying on the lower-priority base sheet.
    css << "@media (prefers-contrast: more) {\n";
    css << "  :root {\n";
    css << "    --glass-thin-bg: var(--overlay-10);\n";
    css << "    --glass-thin-border: var(--overlay-12);\n";
    css << "    --glass-regular-bg: var(--overlay-12);\n";
    css << "    --glass-regular-border: var(--overlay-16);\n";
    css << "    --glass-thick-bg: var(--overlay-16);\n";
    css << "    --glass-thick-border: var(--overlay-16);\n";
    css << "  }\n";
    css << "}\n";
    return css.str();
}

} // namespace design_tokens
} // namespace lollypop
