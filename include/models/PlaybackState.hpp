/**
 * Lollypop C++ - PlaybackState Model
 *
 * Single authoritative playback-state model. Owned by BackendController
 * (the IPlayerService implementation). All GTK UI panels observe this
 * state through callbacks; they never mutate it directly.
 *
 * Shell media controls (MediaControls.qml) write to PlaybackState via
 * MPRIS D-Bus → MprisServer → BackendController.
 */

#pragma once

#include <cstddef>
#include <string>

namespace lollypop {

struct Track;

struct PlaybackState {
    std::string currentTrackId;
    bool isPlaying = false;

    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    double volume = 0.7;

    bool shuffleEnabled = false;

    enum class RepeatMode {
        Off,
        All,    // Repeat entire queue/playlist
        One,    // Repeat current track
    };

    RepeatMode repeatMode = RepeatMode::Off;
};

} // namespace lollypop
