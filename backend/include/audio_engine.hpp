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
    bool can_seek() const;
    void set_volume(double volume);  // 0.0 - 1.0
    double get_volume() const;

    // Attempt to recover from an error by rebuilding the pipeline and
    // reloading the last track. Returns true if recovery succeeded.
    bool recover_from_error();

    // Gapless playback support
    void set_gapless_enabled(bool enabled);
    bool is_gapless_enabled() const;
    // Preload the next track for gapless transition. Call when the current track
    // is near end (e.g., within 2 seconds). The track will be ready when EOS fires.
    void preload_next_track(const std::string& file_path);
    // Called internally when the preloaded track should become active.
    void commit_next_track();

    // State queries
    PlaybackState get_state() const;
    std::optional<double> get_position() const;
    std::optional<double> get_duration() const;

    // Get the visualization branch sample rate (for VisualizerEngine FFT accuracy)
    double get_viz_sample_rate() const;

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
