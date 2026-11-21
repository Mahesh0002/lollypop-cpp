/**
 * Lollypop C++ - VisualizerModel
 *
 * Shared model consumed by both WaveformView instances (right panel and
 * bottom playback bar). Contains live audio sample data and playback
 * progress. Both widgets read from this single model — no duplicate
 * sample buffers.
 *
 * Updated by the backend (CavaVisualizer or VisualizerEngine) via the
 * visualization callback. All updates are marshalled to the GTK main
 * context before reaching the UI.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace lollypop {

struct VisualizerModel {
    // Live sample buffer (0.0 - 1.0 per band)
    std::vector<float> samples;

    // Timestamp / sequence number for detecting stale reads
    std::uint64_t sequenceNumber = 0;

    // Current track identity (so visualizer can reset on track change)
    std::string currentTrackId;

    // Playback status
    bool isPlaying = false;

    // Playback position (0.0 - 1.0)
    double progress = 0.0;

    // Duration in seconds
    double durationSeconds = 0.0;

    // Optional precomputed whole-track peak data (future)
    std::vector<float> peakData;

    // Optional smoothed output data
    std::vector<float> smoothedData;

    bool hasData() const { return !samples.empty(); }
};

} // namespace lollypop
