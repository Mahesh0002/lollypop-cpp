/**
 * Lollypop C++ - CAVA Visualizer Implementation
 * Runs CAVA as a subprocess and parses semicolon-separated ASCII bar values.
 */

#include "cava_visualizer.hpp"

#include <giomm.h>
#include <glibmm/dispatcher.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace lollypop {

struct CavaVisualizer::Impl {
    Glib::RefPtr<Gio::Subprocess> cava_process;
    std::thread reader_thread;
    std::atomic<bool> running{false};
    PointsCallback points_cb;
    std::string config_path;
    bool owns_config = false;

    // The reader thread must never call the UI callback directly: GTK is not
    // thread-safe. It stores the newest frame and wakes the GLib main loop,
    // which then invokes the callback in the main thread.
    std::mutex points_mutex;
    std::vector<float> pending_points;
    Glib::Dispatcher dispatcher;

    Impl() {
        dispatcher.connect([this] { dispatch_points(); });
    }

    void dispatch_points() {
        std::vector<float> points;
        {
            std::lock_guard<std::mutex> lock(points_mutex);
            points.swap(pending_points);
        }
        if (!points.empty() && points_cb) {
            points_cb(points);
        }
    }

    void read_output() {
        try {
            auto stdout_pipe = cava_process->get_stdout_pipe();
            auto input_stream = Gio::DataInputStream::create(stdout_pipe);

            while (running) {
                std::string line;
                if (!input_stream->read_line(line)) break;

                if (line.empty()) continue;

                // Parse "0;0;1;2;5;2;1;0;" format
                std::vector<float> points;
                std::stringstream ss(line);
                std::string token;
                while (std::getline(ss, token, ';')) {
                    if (!token.empty()) {
                        try {
                            points.push_back(std::stof(token));
                        } catch (...) {
                            // Skip non-numeric tokens
                        }
                    }
                }

                if (!points.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(points_mutex);
                        pending_points = std::move(points);
                    }
                    dispatcher.emit();
                }
            }
        } catch (const Glib::Error& e) {
            std::cerr << "CAVA reader error: " << e.what() << std::endl;
        }
    }
};

CavaVisualizer::CavaVisualizer() : impl_(std::make_unique<Impl>()) {}

CavaVisualizer::~CavaVisualizer() {
    stop();
}

bool CavaVisualizer::start(const std::string& config_path) {
    if (impl_->running) return true;

    if (!is_cava_available()) {
        std::cerr << "CAVA: cava binary not found in PATH" << std::endl;
        return false;
    }

    impl_->owns_config = config_path.empty();
    if (impl_->owns_config) {
        impl_->config_path = generate_default_config();
    } else {
        impl_->config_path = config_path;
    }

    try {
        // CAVA writes diagnostics to stderr. Leaving that pipe unread would
        // eventually fill the pipe buffer and block CAVA, so discard it.
        impl_->cava_process = Gio::Subprocess::create(
            {"cava", "-p", impl_->config_path},
            Gio::Subprocess::Flags::STDOUT_PIPE |
                Gio::Subprocess::Flags::STDERR_SILENCE);

        impl_->running = true;
        impl_->reader_thread = std::thread(&Impl::read_output, impl_.get());
        return true;
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to start CAVA: " << e.what() << std::endl;
        if (impl_->owns_config && !impl_->config_path.empty()) {
            std::remove(impl_->config_path.c_str());
            impl_->config_path.clear();
        }
        return false;
    }
}

void CavaVisualizer::stop() {
    impl_->running = false;
    if (impl_->cava_process) {
        try {
            impl_->cava_process->force_exit();
        } catch (...) {
            // Ignore errors during forced exit
        }
        impl_->cava_process = nullptr;
    }
    if (impl_->reader_thread.joinable()) {
        impl_->reader_thread.join();
    }
    if (impl_->owns_config && !impl_->config_path.empty()) {
        std::remove(impl_->config_path.c_str());
        impl_->config_path.clear();
        impl_->owns_config = false;
    }
}

bool CavaVisualizer::is_running() const {
    return impl_->running;
}

void CavaVisualizer::set_points_callback(PointsCallback cb) {
    impl_->points_cb = std::move(cb);
}

bool CavaVisualizer::is_cava_available() {
    return !Glib::find_program_in_path("cava").empty();
}

std::string CavaVisualizer::generate_default_config(int bars, int framerate) {
    std::string config =
        "[general]\n"
        "mode = waves\n"
        "framerate = " + std::to_string(framerate) + "\n"
        "autosens = 1\n"
        "bars = " + std::to_string(bars) + "\n\n"
        "[output]\n"
        "method = raw\n"
        "raw_target = /dev/stdout\n"
        "data_format = ascii\n"
        "channels = mono\n"
        "mono_option = average\n\n"
        "[smoothing]\n"
        "noise_reduction = 20\n";

    // Write to temp file in /tmp
    std::string path = "/tmp/lollypop-cava-config-" +
                       std::to_string(getpid()) + ".txt";
    std::ofstream file(path);
    if (file) {
        file << config;
        file.close();
    }
    return path;
}

}  // namespace lollypop
