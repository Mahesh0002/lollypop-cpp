/**
 * Lollypop C++ - Audio Engine
 * GStreamer-based audio playback with visualization support
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <optional>

namespace lollypop {

class AudioEngine {
public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
        Buffering,
        Error
    };

    using PositionCallback = std::function<void(double position, double duration)>;
    using StateCallback = std::function<void(PlaybackState state)>;
    using EndOfStreamCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    using VizSamplesCallback = std::function<void(const float* samples,
                                                   size_t count)>;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool initialize();
    void shutdown();

    // Playback control
    bool load_track(const std::string& file_path);
    void play();
    void pause();
    void stop();
    void seek(double position_seconds);
    void set_volume(double volume);  // 0.0 - 1.0
    double get_volume() const;

    // State queries
    PlaybackState get_state() const;
    std::optional<double> get_position() const;
    std::optional<double> get_duration() const;

    // Callbacks
    void set_position_callback(PositionCallback cb);
    void set_state_callback(StateCallback cb);
    void set_eos_callback(EndOfStreamCallback cb);
    void set_error_callback(ErrorCallback cb);
    void set_viz_samples_callback(VizSamplesCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
