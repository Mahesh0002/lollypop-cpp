/**
 * Lollypop C++ - CAVA Visualizer
 * Runs CAVA as a subprocess and parses semicolon-separated ASCII bar values
 * for real-time waveform visualization. CAVA is optional: when unavailable,
 * the client can fall back to the internal GStreamer audio analysis.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lollypop {

class CavaVisualizer {
public:
    using PointsCallback = std::function<void(const std::vector<float>&)>;

    CavaVisualizer();
    ~CavaVisualizer();

    CavaVisualizer(const CavaVisualizer&) = delete;
    CavaVisualizer& operator=(const CavaVisualizer&) = delete;

    // Start CAVA with an optional config path. Returns false if CAVA is not
    // installed or fails to start; the caller should fall back to internal
    // analysis. Configuration parameters are validated before launching.
    bool start(const std::string& config_path = "");
    void stop();
    bool is_running() const;
    bool is_available() const;

    void set_points_callback(PointsCallback cb);

    static bool is_cava_available();
    static std::string generate_default_config(int bars = 50, int framerate = 60);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
