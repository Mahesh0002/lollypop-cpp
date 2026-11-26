/**
 * Lollypop C++ - AccentColorManager Implementation
 */

#include "ui/theme/AccentColorManager.hpp"

namespace lollypop {

AccentColorManager::AccentColorManager() {
    // Apple system accents, dark seed / light seed (plan §5.1).
    colors_[0] = {"Violet", "#8B6CFF", "#6D4AE0"};
    colors_[1] = {"Blue", "#0A84FF", "#0066CC"};
    colors_[2] = {"Teal", "#64D2FF", "#0979AA"};
    colors_[3] = {"Green", "#32D74B", "#1C7F2E"};
    colors_[4] = {"Yellow", "#FFD60A", "#8C6900"};
    colors_[5] = {"Orange", "#FF9F0A", "#B05F00"};
    colors_[6] = {"Red", "#FF453A", "#D70015"};
    colors_[7] = {"Pink", "#FF375F", "#D81E4A"};
    colors_[8] = {"Indigo", "#6E6CFF", "#4B49C6"};
}

const AccentColor& AccentColorManager::get(std::size_t index) const {
    if (index >= colors_.size()) index = 0;
    return colors_[index];
}

bool AccentColorManager::select(std::size_t index) {
    if (index >= colors_.size()) return false;
    selected_index_ = index;
    return true;
}

std::string AccentColorManager::seed(bool dark) const {
    return dark ? selected().dark_hex : selected().light_hex;
}

} // namespace lollypop
