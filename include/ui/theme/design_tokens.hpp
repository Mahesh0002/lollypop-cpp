/**
 * Lollypop C++ - Canonical design tokens
 *
 * Single C++ source for tokens that ThemeManager injects at runtime. The
 * stylesheet keeps matching :root defaults as a fallback, and
 * scripts/check_design_consistency.sh verifies both sources stay in sync.
 */

#pragma once

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
inline constexpr const char* kTextOnAccent = "#ffffff";

inline std::string format_number(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/** Build the runtime :root block consumed by the high-priority provider. */
inline std::string build_css(double font_scale,
                             std::string_view accent_color,
                             std::string_view accent_soft) {
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
    css << "  " << kBorderFocusName << ": var(--accent-color);\n";
    css << "  " << kAccentColorName << ": " << accent_color << ";\n";
    css << "  " << kAccentSoftName << ": " << accent_soft << ";\n";
    css << "  " << kTextOnAccentName << ": " << kTextOnAccent << ";\n";
    css << "}\n";
    return css.str();
}

} // namespace design_tokens
} // namespace lollypop
