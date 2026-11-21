/**
 * Lollypop C++ - AccentColorManager
 *
 * Manages the accent color palette. Provides a set of predefined accent
 * colors and a fallback when no palette picker exists (plan Section 12.2).
 * Purple is used only as a temporary development default, not permanent.
 */

#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace lollypop {

/// A single accent color swatch.
struct AccentColor {
    std::string name;
    double r, g, b;
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

    /// Select an accent color by index.
    void select(std::size_t index);

    /// Get the currently selected AccentColor.
    const AccentColor& selected() const { return colors_[selected_index_]; }

private:
    std::array<AccentColor, 8> colors_;
    std::size_t selected_index_ = 0;  // Development default: purple
};

} // namespace lollypop
