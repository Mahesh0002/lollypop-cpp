/**
 * Lollypop C++ - AccentColorManager Implementation
 */

#include "ui/theme/AccentColorManager.hpp"

namespace lollypop {

AccentColorManager::AccentColorManager() {
    // Development default palette (plan Section 12.2: purple is temporary dev default)
    colors_[0] = {"Purple", 0.65, 0.45, 1.0};      // Development default
    colors_[1] = {"Pink", 0.95, 0.45, 0.65};
    colors_[2] = {"Rose", 0.85, 0.35, 0.45};
    colors_[3] = {"Orange", 1.0, 0.6, 0.1};
    colors_[4] = {"Amber", 1.0, 0.75, 0.2};
    colors_[5] = {"Emerald", 0.2, 0.85, 0.5};
    colors_[6] = {"Cyan", 0.2, 0.8, 1.0};
    colors_[7] = {"Indigo", 0.5, 0.45, 1.0};
}

const AccentColor& AccentColorManager::get(std::size_t index) const {
    if (index >= colors_.size()) index = 0;
    return colors_[index];
}

void AccentColorManager::select(std::size_t index) {
    if (index < colors_.size()) {
        selected_index_ = index;
    }
}

} // namespace lollypop
