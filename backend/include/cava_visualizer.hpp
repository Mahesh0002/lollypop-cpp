/**
 * Lollypop C++ - CAVA Visualizer
 * Runs CAVA as a subprocess and parses semicolon-separated ASCII bar values
 * for real-time waveform visualization.
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

    bool start(const std::string& config_path = "");
    void stop();
    bool is_running() const;

    void set_points_callback(PointsCallback cb);

    static bool is_cava_available();
    static std::string generate_default_config(int bars = 50, int framerate = 60);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop
