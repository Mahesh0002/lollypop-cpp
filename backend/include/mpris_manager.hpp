/**
 * Lollypop C++ - MPRIS Manager
 * Discovers and controls MPRIS2 media players on the session bus.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

struct MprisPlayerInfo {
    std::string identity;
    std::string desktop_entry;
    std::string dbus_name;

    bool is_playing = false;
    std::string playback_status;  // Playing, Paused, Stopped

    std::string track_id;
    std::string track_title;
    std::string track_artist;
    std::string track_album;
    std::string track_art_url;
    double position = 0.0;  // seconds
    double duration = 0.0;  // seconds

    double volume = 1.0;
    bool can_seek = false;
    bool can_control = false;
    bool can_go_next = false;
    bool can_go_previous = false;
    bool can_play = false;
    bool can_pause = false;

    int loop_state = 0;  // 0 = None, 1 = Playlist, 2 = Track
    bool shuffle = false;
};

class MprisManager {
public:
    using PlayerListChangedCallback = std::function<void()>;
    using ActivePlayerChangedCallback = std::function<void(const MprisPlayerInfo&)>;
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using TrackChangedCallback = std::function<void(const MprisPlayerInfo&)>;
    using PositionChangedCallback = std::function<void(double position, double duration)>;
    using VolumeChangedCallback = std::function<void(double volume)>;
    using LoopStateChangedCallback = std::function<void(int loop_state)>;
    using ShuffleChangedCallback = std::function<void(bool shuffle)>;

    MprisManager();
    ~MprisManager();

    MprisManager(const MprisManager&) = delete;
    MprisManager& operator=(const MprisManager&) = delete;

    bool initialize();
    void shutdown();
    bool is_initialized() const;

    // Player discovery
    std::vector<MprisPlayerInfo> get_players() const;
    MprisPlayerInfo get_active_player() const;
    std::string get_active_player_name() const;
    void set_active_player(const std::string& dbus_name);
    void set_preferred_player(const std::string& name);
    void set_filter_duplicates(bool enabled);

    // Playback control (forwarded to the active player)
    void play_pause();
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(double position_seconds);      // absolute
    void seek_by(double offset_seconds);     // relative
    void set_volume(double volume);
    void set_loop_state(int state);          // 0 = None, 1 = Playlist, 2 = Track
    void set_shuffle(bool shuffle);

    // Capability queries for the active player
    bool can_control() const;
    bool can_go_next() const;
    bool can_go_previous() const;
    bool can_seek() const;

    // Callbacks
    void set_player_list_changed_callback(PlayerListChangedCallback cb);
    void set_active_player_changed_callback(ActivePlayerChangedCallback cb);
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb);
    void set_track_changed_callback(TrackChangedCallback cb);
    void set_position_changed_callback(PositionChangedCallback cb);
    void set_volume_changed_callback(VolumeChangedCallback cb);
    void set_loop_state_changed_callback(LoopStateChangedCallback cb);
    void set_shuffle_changed_callback(ShuffleChangedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
