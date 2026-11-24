/**
 * Lollypop C++ - MPRIS Server
 * Exports Lollypop as an MPRIS2 media player on the session bus
 * (org.mpris.MediaPlayer2.lollypop), so desktop media keys and
 * system sound menus can control playback.
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "types.hpp"

namespace lollypop {

class MprisServer {
public:
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using TrackChangedCallback = std::function<void(const Track&)>;
    using SeekCallback = std::function<void(double offset_seconds)>;
    using SetPositionCallback = std::function<void(const std::string& track_id, double position_seconds)>;
    using VolumeChangedCallback = std::function<void(double volume)>;
    using LoopStateChangedCallback = std::function<void(int loop_state)>;
    using ShuffleChangedCallback = std::function<void(bool shuffle)>;
    using NextCallback = std::function<void()>;
    using PreviousCallback = std::function<void()>;
    using PlayPauseCallback = std::function<void()>;
    using StopCallback = std::function<void()>;
    using OpenUriCallback = std::function<void(const std::string& uri)>;
    using RaiseCallback = std::function<void()>;

    MprisServer();
    ~MprisServer();

    MprisServer(const MprisServer&) = delete;
    MprisServer& operator=(const MprisServer&) = delete;

    bool initialize();
    void shutdown();
    bool is_initialized() const;

    // --- Update properties from backend state (call these when backend changes) ---

    void update_playback_status(bool playing);
    void update_stopped();
    void update_track(const Track& track, const std::string& entry_id = {});
    void update_position(double position_seconds);
    void update_duration(double duration_seconds);
    void update_volume(double volume);
    void update_loop_state(int loop_state);   // 0=None, 1=Playlist, 2=Track
    void update_shuffle(bool shuffle);
    void update_capabilities(bool can_go_next, bool can_go_previous,
                             bool can_play, bool can_pause, bool can_seek,
                             bool can_control = true);

    // Emit Seeked after an actual seek (not on every position tick).
    void notify_seeked(double position_seconds);

    // --- Callbacks from remote MPRIS clients (media keys, sound menu) ---
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb);
    void set_track_changed_callback(TrackChangedCallback cb);
    void set_seek_callback(SeekCallback cb);
    void set_set_position_callback(SetPositionCallback cb);
    void set_volume_changed_callback(VolumeChangedCallback cb);
    void set_loop_state_changed_callback(LoopStateChangedCallback cb);
    void set_shuffle_changed_callback(ShuffleChangedCallback cb);
    void set_next_callback(NextCallback cb);
    void set_previous_callback(PreviousCallback cb);
    void set_play_pause_callback(PlayPauseCallback cb);
    void set_stop_callback(StopCallback cb);
    void set_open_uri_callback(OpenUriCallback cb);
    void set_raise_callback(RaiseCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
