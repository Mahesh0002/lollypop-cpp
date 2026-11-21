/**
 * Lollypop C++ - Queue Manager
 * Manages the playback queue: next/previous decisions, shuffle, repeat,
 * and queue persistence.
 *
 * Design notes:
 *  - Stores track IDs only (never full Track objects).
 *  - Uses std::deque<std::string> plus a cursor for O(1) next/previous.
 *  - A separate "play next" FIFO is drained ahead of the main queue.
 *  - Shuffle pre-generates a Fisher–Yates permutation; next/previous walk it
 *    with a cursor in O(1) with no repeated tracks.
 */

#pragma once

#include <string>
#include <deque>
#include <vector>
#include <random>
#include <mutex>
#include <optional>

#include "types.hpp"

namespace lollypop {

enum class RepeatMode {
    None,      // Stop at end of queue
    Playlist,  // Repeat the whole queue
    Track      // Repeat the current track
};

class QueueManager {
public:
    QueueManager();
    ~QueueManager();

    QueueManager(const QueueManager&) = delete;
    QueueManager& operator=(const QueueManager&) = delete;

    // Queue manipulation
    void set_queue(const std::vector<std::string>& track_ids, size_t start_index = 0);
    void clear();
    bool enqueue(const std::string& track_id, bool next = false);
    bool remove(size_t index);
    bool move(size_t from, size_t to);

    // Playback navigation. Advances the cursor and returns the next/previous
    // index, or nullopt when there is no track (e.g. end of queue with repeat
    // disabled).
    std::optional<size_t> next_index();
    std::optional<size_t> previous_index();

    // Set the current cursor position (used when the user selects a track
    // directly). Returns false if the index is invalid.
    bool set_current_index(size_t index);

    // Accessors
    const std::deque<std::string>& queue() const { return queue_; }
    const std::deque<std::string>& play_next() const { return play_next_; }
    size_t current_index() const { return current_index_; }
    bool is_empty() const { return queue_.empty() && play_next_.empty(); }
    bool has_index(size_t index) const { return index < queue_.size(); }

    // Playback modes
    void set_shuffle(bool enabled);
    void set_repeat(RepeatMode mode);
    RepeatMode repeat_mode() const { return repeat_mode_; }
    bool shuffle() const { return shuffle_; }

    // Validation
    bool validate_index(size_t index) const;

private:
    mutable std::mutex mutex_;

    std::deque<std::string> queue_;       // main playback queue
    std::deque<std::string> play_next_;   // "play next" FIFO (ahead of queue_)
    size_t current_index_ = 0;            // cursor into queue_

    bool shuffle_ = false;
    RepeatMode repeat_mode_ = RepeatMode::None;
    mutable std::mt19937 rng_{std::random_device{}()};

    // Fisher–Yates shuffle permutation of indices into queue_.
    // Rebuilt lazily when shuffle is enabled and the queue mutates.
    mutable std::vector<size_t> shuffle_order_;
    mutable size_t shuffle_pos_ = 0;
    mutable bool shuffle_dirty_ = true;

    void ensure_shuffle_order() const;
    size_t queue_size_locked() const;
};

}  // namespace lollypop
