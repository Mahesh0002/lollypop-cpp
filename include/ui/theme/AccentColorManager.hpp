/**
 * Lollypop C++ - AccentColorManager
 *
 * Nine Apple system accents (docs/THEME_AND_COLOR_PLAN.md §5.1). Each preset
 * carries a dark and a light seed: light mode uses the darker seed so accent
 * text clears 4.5:1 on white. The on-accent ink is not stored here; it is
 * derived from measured contrast in design_tokens.
 */

#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace lollypop {

/// A single accent color preset.
struct AccentColor {
    std::string name;
    std::string dark_hex;
    std::string light_hex;
};

class AccentColorManager {
public:
    AccentColorManager();
    ~AccentColorManager() = default;

    AccentColorManager(const AccentColorManager&) = delete;
    AccentColorManager& operator=(const AccentColorManager&) = delete;

    /// Number of available accent colors.
    std::size_t count() const { return colors_.size(); }

    /// Get accent color by index.
    const AccentColor& get(std::size_t index) const;

    /// Get the currently selected accent color index.
    std::size_t selected_index() const { return selected_index_; }

    /// Select an accent color by index. Returns false for out-of-range input.
    bool select(std::size_t index);

    /// Get the currently selected AccentColor.
    const AccentColor& selected() const { return colors_[selected_index_]; }

    /// Seed hex for the given scheme. Clamped to a valid preset index.
    std::string seed(bool dark) const;

    /// Human-readable name for the selected preset.
    const std::string& selected_name() const { return selected().name; }

private:
    std::array<AccentColor, 9> colors_;
    std::size_t selected_index_ = 0;  // Violet is the default brand accent.
};

} // namespace lollypop
