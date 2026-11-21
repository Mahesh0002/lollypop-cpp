/**
 * Lollypop C++ - Playback Source Manager
 * Determines the active playback source (internal audio engine vs external
 * MPRIS player) and routes playback commands to it. Only one source is
 * authoritative at any given time.
 */

#pragma once

#include <string>
#include <memory>
#include <optional>

#include "audio_engine.hpp"
#include "mpris_manager.hpp"

namespace lollypop {

enum class PlaybackSource {
    Internal,   // Local audio engine (playbin)
    External,   // External MPRIS player
};

class PlaybackSourceManager {
public:
    PlaybackSourceManager(std::shared_ptr<AudioEngine> audio_engine,
                          std::shared_ptr<MprisManager> mpris_manager);
    ~PlaybackSourceManager();

    PlaybackSourceManager(const PlaybackSourceManager&) = delete;
    PlaybackSourceManager& operator=(const PlaybackSourceManager&) = delete;

    // Select the active source.
    void set_source(PlaybackSource source);
    PlaybackSource source() const;

    // Whether the given source is available.
    bool internal_available() const;
    bool external_available() const;

    // The active external player (if any), as an optional.
    std::optional<MprisPlayerInfo> active_external_player() const;

    // Capability queries for the active source.
    bool can_control() const;
    bool can_seek() const;
    bool can_go_next() const;
    bool can_go_previous() const;

    // Playback control, routed to the active source.
    void play();
    void pause();
    void stop();
    void play_pause();
    void next();
    void previous();
    void seek(double position_seconds);      // absolute
    void seek_by(double offset_seconds);     // relative
    void set_volume(double volume);
    double get_volume() const;

    // State queries (from the active source).
    double get_position() const;
    double get_duration() const;

    // MPRIS-specific
    void set_active_player(const std::string& dbus_name);
    std::string active_player_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
