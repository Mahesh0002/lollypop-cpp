/**
 * Lollypop C++ - CAVA Visualizer Implementation
 * Runs CAVA as a subprocess and parses semicolon-separated ASCII bar values.
 */

#include "cava_visualizer.hpp"

#include <giomm.h>
#include <glibmm/dispatcher.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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
            // Take a reference to the subprocess so it stays alive for the
            // duration of this call, even if stop() nulls impl_->cava_process.
            Glib::RefPtr<Gio::Subprocess> proc = cava_process;
            if (!proc) return;

            auto stdout_pipe = proc->get_stdout_pipe();
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
                            float v = std::stof(token);
                            // Validate/normalize: clamp to [0, 1000] and reject NaN.
                            if (v < 0.0f) v = 0.0f;
                            if (v > 1000.0f) v = 1000.0f;
                            points.push_back(v);
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
        } catch (const std::exception& e) {
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
        // Validate default config parameters before writing the file.
        impl_->config_path = generate_default_config(50, 60);
    } else {
        impl_->config_path = config_path;
        // Validate the user-provided config file exists.
        if (access(config_path.c_str(), F_OK) != 0) {
            std::cerr << "CAVA: config file not found: " << config_path << std::endl;
            return false;
        }
    }

    try {
        // CAVA writes diagnostics to stderr. Silence it so the pipe cannot
        // fill and block CAVA.
        impl_->cava_process = Gio::Subprocess::create(
            {"cava", "-p", impl_->config_path},
            Gio::Subprocess::Flags::STDOUT_PIPE |
                Gio::Subprocess::Flags::STDERR_SILENCE);

        impl_->reader_thread = std::thread(&Impl::read_output, impl_.get());
        impl_->running = true;
        return true;
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to start CAVA: " << e.what() << std::endl;
        if (impl_->owns_config && !impl_->config_path.empty()) {
            std::remove(impl_->config_path.c_str());
            impl_->config_path.clear();
        }
        return false;
    } catch (const std::exception& e) {
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

    // Force-exit the CAVA process so its stdout pipe is closed and the
    // reader's read_line() unblocks with EOF. Do not null cava_process
    // before joining: the reader may still be about to call
    // get_stdout_pipe() and would crash on a null RefPtr.
    if (impl_->cava_process) {
        try {
            impl_->cava_process->force_exit();
        } catch (...) {
            // Ignore errors during forced exit
        }
    }

    // Close the stdout pipe to guarantee read_line() unblocks even if
    // force_exit does not immediately EOF the pipe.
    {
        auto pipe = impl_->cava_process;
        if (pipe) {
            try {
                if (auto stdout_pipe = pipe->get_stdout_pipe()) {
                    stdout_pipe->close();
                }
            } catch (...) {
                // Ignore errors during close
            }
        }
    }

    // Join the reader thread (now unblocked by the closed pipe).
    if (impl_->reader_thread.joinable()) {
        impl_->reader_thread.join();
    }

    // Only now is it safe to drop our reference to the subprocess.
    impl_->cava_process = nullptr;

    if (impl_->owns_config && !impl_->config_path.empty()) {
        std::remove(impl_->config_path.c_str());
        impl_->config_path.clear();
        impl_->owns_config = false;
    }
}

bool CavaVisualizer::is_running() const {
    return impl_->running;
}

bool CavaVisualizer::is_available() const {
    return is_cava_available();
}

void CavaVisualizer::set_points_callback(PointsCallback cb) {
    impl_->points_cb = std::move(cb);
}

bool CavaVisualizer::is_cava_available() {
    return !Glib::find_program_in_path("cava").empty();
}

std::string CavaVisualizer::generate_default_config(int bars, int framerate) {
    // Validate configuration parameters.
    if (bars < 1) bars = 1;
    if (bars > 512) bars = 512;
    if (framerate < 1) framerate = 1;
    if (framerate > 240) framerate = 240;

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

    // Use a secure temporary file (mkstemp-style) in the user's runtime dir
    // instead of a predictable /tmp path.
    std::string path;
    if (const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR")) {
        path = xdg_runtime;
    } else if (const char* home = std::getenv("HOME")) {
        path = home + std::string("/.cache/lollypop-cpp");
    } else {
        path = "/tmp";
    }
    path += "/lollypop-cava-config-XXXXXX";

    // Create the temp file securely.
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) {
        // Fall back to a simple pid-based name.
        path = "/tmp/lollypop-cava-config-" + std::to_string(getpid()) + ".txt";
    } else {
        ::close(fd);
        path = buf.data();
    }

    std::ofstream file(path);
    if (file) {
        file << config;
        file.close();
    }
    return path;
}

}  // namespace lollypop
