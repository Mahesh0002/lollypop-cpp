/**
 * Lollypop C++ - IPlayerService Interface
 *
 * Single authoritative playback-state interface. The UI depends on this
 * interface, not on the concrete BackendController. All playback mutations
 * originate from the shell via MPRIS; the GTK panels observe state through
 * callbacks.
 *
 * Shell → MPRIS D-Bus → MprisServer → BackendController (implements this
 * interface) → callbacks → GTK UI (read-only observers).
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lollypop {

struct Track;

class IPlayerService {
public:
    virtual ~IPlayerService() = default;

    // --- Playback state queries ---
    virtual bool is_playing() const = 0;
    virtual Track get_current_track() const = 0;
    virtual double get_position() const = 0;
    virtual double get_duration() const = 0;
    virtual double get_volume() const = 0;
    virtual std::vector<std::string> get_queue() const = 0;
    virtual size_t get_queue_index() const = 0;

    virtual bool get_shuffle() const = 0;
    virtual bool get_repeat() const = 0;          // repeat-all
    virtual bool get_repeat_one() const = 0;      // repeat-one

    // --- Playback control (write path: shell via MPRIS is the authority) ---
    virtual void play_track(const std::string& track_id) = 0;
    virtual void play_tracks(const std::vector<std::string>& track_ids,
                             size_t start_index = 0) = 0;
    virtual void toggle_play_pause() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void next_track() = 0;
    virtual void previous_track() = 0;
    virtual void seek(double position_seconds) = 0;
    virtual void set_volume(double volume) = 0;  // 0.0 - 1.0

    // Capability queries
    virtual bool can_seek() const = 0;

    virtual void set_shuffle(bool enabled) = 0;
    virtual void set_repeat(bool enabled) = 0;          // repeat-all
    virtual void set_repeat_one(bool enabled) = 0;      // repeat-one

    // --- Queue operations ---
    virtual bool enqueue_track(const std::string& track_id, bool next = false) = 0;
    virtual bool remove_from_queue(size_t index) = 0;
    virtual bool move_queue_item(size_t from, size_t to) = 0;
    virtual bool play_queue_index(size_t index) = 0;
    virtual void clear_queue() = 0;

    // --- Callbacks for UI updates (marshalled to GTK main context) ---
    using TrackChangedCallback = std::function<void(const Track&)>;
    using PositionChangedCallback = std::function<void(double position, double duration)>;
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using QueueChangedCallback = std::function<void(const std::vector<std::string>&)>;
    using VisualizationCallback = std::function<void(const std::vector<float>&)>;
    using LibraryChangedCallback = std::function<void()>;
    using ShuffleChangedCallback = std::function<void(bool enabled)>;
    using RepeatChangedCallback = std::function<void(bool enabled)>;
    using RepeatOneChangedCallback = std::function<void(bool enabled)>;

    virtual void set_track_changed_callback(TrackChangedCallback cb) = 0;
    virtual void set_position_changed_callback(PositionChangedCallback cb) = 0;
    virtual void set_playback_state_changed_callback(PlaybackStateChangedCallback cb) = 0;
    virtual void set_queue_changed_callback(QueueChangedCallback cb) = 0;
    virtual void set_visualization_changed_callback(VisualizationCallback cb) = 0;
    virtual void set_library_changed_callback(LibraryChangedCallback cb) = 0;

    // Multicast variants: register an additional observer without replacing
    // callbacks already installed by other panels.
    virtual void add_track_changed_callback(TrackChangedCallback cb) = 0;
    virtual void add_position_changed_callback(PositionChangedCallback cb) = 0;
    virtual void add_playback_state_changed_callback(PlaybackStateChangedCallback cb) = 0;
    virtual void add_queue_changed_callback(QueueChangedCallback cb) = 0;
    virtual void add_visualization_changed_callback(VisualizationCallback cb) = 0;
    virtual void add_library_changed_callback(LibraryChangedCallback cb) = 0;
    virtual void add_shuffle_changed_callback(ShuffleChangedCallback cb) = 0;
    virtual void add_repeat_changed_callback(RepeatChangedCallback cb) = 0;
    virtual void add_repeat_one_changed_callback(RepeatOneChangedCallback cb) = 0;
};

} // namespace lollypop
