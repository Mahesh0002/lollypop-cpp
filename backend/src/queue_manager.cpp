/**
 * Lollypop C++ - Queue Manager Implementation
 */

#include "queue_manager.hpp"

#include <algorithm>
#include <iostream>

namespace lollypop {

QueueManager::QueueManager() = default;

QueueManager::~QueueManager() = default;

void QueueManager::set_queue(const std::vector<std::string>& track_ids, size_t start_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.assign(track_ids.begin(), track_ids.end());
    play_next_.clear();
    if (queue_.empty()) {
        current_index_ = 0;
    } else {
        current_index_ = std::min(start_index, queue_.size() - 1);
    }
    shuffle_dirty_ = true;
    shuffle_pos_ = 0;
}

void QueueManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    play_next_.clear();
    current_index_ = 0;
    shuffle_dirty_ = true;
    shuffle_pos_ = 0;
}

bool QueueManager::enqueue(const std::string& track_id, bool next) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (track_id.empty()) return false;

    if (next) {
        play_next_.push_back(track_id);
    } else {
        queue_.push_back(track_id);
    }
    shuffle_dirty_ = true;
    return true;
}

bool QueueManager::remove(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= queue_.size()) return false;

    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));

    if (queue_.empty()) {
        current_index_ = 0;
    } else if (index < current_index_) {
        --current_index_;
    } else if (index == current_index_) {
        current_index_ = std::min(index, queue_.size() - 1);
    }
    shuffle_dirty_ = true;
    return true;
}

bool QueueManager::move(size_t from, size_t to) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (from >= queue_.size() || to >= queue_.size()) return false;
    if (from == to) return true;

    const std::string item = queue_[from];
    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(from));
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(to), item);

    if (current_index_ == from) {
        current_index_ = to;
    } else if (from < current_index_ && to >= current_index_) {
        --current_index_;
    } else if (from > current_index_ && to <= current_index_) {
        ++current_index_;
    }
    shuffle_dirty_ = true;
    return true;
}

void QueueManager::ensure_shuffle_order() const {
    if (!shuffle_dirty_) return;
    shuffle_order_.clear();
    for (size_t i = 0; i < queue_.size(); ++i) shuffle_order_.push_back(i);
    // Fisher–Yates shuffle.
    for (size_t i = queue_.size(); i > 1; --i) {
        std::uniform_int_distribution<size_t> dist(0, i - 1);
        size_t j = dist(rng_);
        std::swap(shuffle_order_[i - 1], shuffle_order_[j]);
    }
    shuffle_dirty_ = false;
    shuffle_pos_ = 0;
}

std::optional<size_t> QueueManager::next_index() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) return std::nullopt;

    // Repeat-track stays on the same index.
    if (repeat_mode_ == RepeatMode::Track) {
        return current_index_;
    }

    // Drain the "play next" FIFO first: insert at current_index_ + 1, then
    // advance the cursor onto it.
    if (!play_next_.empty()) {
        std::string next_id = std::move(play_next_.front());
        play_next_.pop_front();
        const size_t insert_at = std::min(current_index_ + 1, queue_.size());
        queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(insert_at), next_id);
        shuffle_dirty_ = true;
        current_index_ = insert_at;
        return current_index_;
    }

    if (shuffle_) {
        ensure_shuffle_order();
        if (shuffle_order_.empty()) return std::nullopt;
        // Advance the shuffle cursor.
        ++shuffle_pos_;
        if (shuffle_pos_ >= shuffle_order_.size()) {
            if (repeat_mode_ == RepeatMode::Playlist) {
                // Reshuffle and wrap.
                ensure_shuffle_order();
                shuffle_pos_ = 0;
                if (!shuffle_order_.empty()) {
                    current_index_ = shuffle_order_[0];
                    return current_index_;
                }
            }
            shuffle_pos_ = shuffle_order_.size(); // clamp at end
            return std::nullopt;
        }
        current_index_ = shuffle_order_[shuffle_pos_];
        return current_index_;
    }

    // Sequential mode.
    if (current_index_ + 1 < queue_.size()) {
        ++current_index_;
        return current_index_;
    }
    if (repeat_mode_ == RepeatMode::Playlist) {
        current_index_ = 0;
        return current_index_;
    }
    return std::nullopt;
}

std::optional<size_t> QueueManager::previous_index() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) return std::nullopt;

    // Repeat-track stays on the same index.
    if (repeat_mode_ == RepeatMode::Track) {
        return current_index_;
    }

    if (shuffle_) {
        ensure_shuffle_order();
        if (shuffle_order_.empty()) return std::nullopt;
        // Rewind the shuffle cursor.
        if (shuffle_pos_ > 0) {
            --shuffle_pos_;
            current_index_ = shuffle_order_[shuffle_pos_];
            return current_index_;
        }
        if (repeat_mode_ == RepeatMode::Playlist) {
            // Wrap to the end of the reshuffled order.
            ensure_shuffle_order();
            shuffle_pos_ = shuffle_order_.size();
            if (!shuffle_order_.empty()) {
                --shuffle_pos_;
                current_index_ = shuffle_order_[shuffle_pos_];
                return current_index_;
            }
        }
        return std::nullopt;
    }

    // Sequential mode.
    if (current_index_ > 0) {
        --current_index_;
        return current_index_;
    }
    if (repeat_mode_ == RepeatMode::Playlist) {
        current_index_ = queue_.size() - 1;
        return current_index_;
    }
    return std::nullopt;
}

bool QueueManager::set_current_index(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= queue_.size()) return false;
    current_index_ = index;
    shuffle_dirty_ = true;
    shuffle_pos_ = 0;
    return true;
}

void QueueManager::set_shuffle(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    shuffle_ = enabled;
    shuffle_dirty_ = true;
    shuffle_pos_ = 0;
}

void QueueManager::set_repeat(RepeatMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    repeat_mode_ = mode;
}

bool QueueManager::validate_index(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index < queue_.size();
}

}  // namespace lollypop
