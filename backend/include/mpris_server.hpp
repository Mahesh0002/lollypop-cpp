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
    using SeekCallback = std::function<void(double position_seconds)>;
    using SetPositionCallback = std::function<void(const std::string& track_id, double position_seconds)>;
    using VolumeChangedCallback = std::function<void(double volume)>;
    using LoopStateChangedCallback = std::function<void(int loop_state)>;
    using ShuffleChangedCallback = std::function<void(bool shuffle)>;

    MprisServer();
    ~MprisServer();

    MprisServer(const MprisServer&) = delete;
    MprisServer& operator=(const MprisServer&) = delete;

    bool initialize();
    void shutdown();
    bool is_initialized() const;

    // --- Update properties from backend state (call these when backend changes) ---

    void update_playback_status(bool playing);
    void update_track(const Track& track);
    void update_position(double position_seconds);
    void update_duration(double duration_seconds);
    void update_volume(double volume);
    void update_loop_state(int loop_state);   // 0=None, 1=Playlist, 2=Track
    void update_shuffle(bool shuffle);

    // --- Callbacks from remote MPRIS clients (media keys, sound menu) ---
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb);
    void set_track_changed_callback(TrackChangedCallback cb);
    void set_seek_callback(SeekCallback cb);
    void set_set_position_callback(SetPositionCallback cb);
    void set_volume_changed_callback(VolumeChangedCallback cb);
    void set_loop_state_changed_callback(LoopStateChangedCallback cb);
    void set_shuffle_changed_callback(ShuffleChangedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
