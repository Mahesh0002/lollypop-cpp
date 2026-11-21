/**
 * Lollypop C++ - QueueSnapshot Model
 *
 * Immutable snapshot of the current queue. Returned by IPlayerService::get_queue().
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lollypop {

struct QueueSnapshot {
    std::vector<std::string> trackIds;  // Ordered track IDs
    std::size_t currentIndex = 0;       // Index of the current/playing track
    bool isEmpty() const { return trackIds.empty(); }
    std::size_t size() const { return trackIds.size(); }
};

} // namespace lollypop
