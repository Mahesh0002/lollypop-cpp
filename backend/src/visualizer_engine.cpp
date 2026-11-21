/**
 * Lollypop C++ - VisualizerEngine Implementation
 */

#include "visualizer_engine.hpp"

#include "audio_engine.hpp"
#include "event_bus.hpp"

#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <thread>

namespace lollypop {

namespace {

constexpr std::size_t kRingCapacity = 8192;
constexpr std::size_t kFrameSize = 1024;  // power of two for FFT
constexpr int kDefaultBands = 64;

// Simple iterative radix-2 FFT, in-place. n must be a power of two.
void fft_in_place(std::vector<std::complex<double>>& x) {
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; i++) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (std::size_t j = 0; j < len / 2; j++) {
                const std::complex<double> u = x[i + j];
                const std::complex<double> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Hann window of the given size.
std::vector<float> make_hann_window(std::size_t size) {
    std::vector<float> w(size);
    for (std::size_t i = 0; i < size; i++) {
        w[i] = static_cast<float>(
            0.5 * (1.0 - std::cos(2.0 * M_PI * static_cast<double>(i) /
                                         static_cast<double>(size - 1))));
    }
    return w;
}

// Map FFT magnitude spectrum (n/2+1 bins) to `bands` logarithmic bands.
// Returns a vector of `bands` magnitudes in [0, 1].
std::vector<float> band_map(const std::vector<double>& magnitudes,
                            std::size_t n_fft, int bands,
                            double sample_rate) {
    const std::size_t band_count = static_cast<std::size_t>(bands);
    std::vector<float> out(band_count, 0.0f);
    if (bands <= 0) return out;

    const std::size_t n_bins = n_fft / 2 + 1;
    const double nyquist = sample_rate / 2.0;

    // Logarithmic band edges from 40 Hz to nyquist.
    const double f_min = 40.0;
    const double f_max = std::max(nyquist, f_min * 2.0);

    std::vector<double> edges(band_count + 1);
    for (std::size_t b = 0; b <= band_count; b++) {
        const double t = static_cast<double>(b) /
                         static_cast<double>(band_count);
        edges[b] = f_min * std::pow(f_max / f_min, t);
    }

    for (std::size_t b = 0; b < band_count; b++) {
        const std::size_t bin_start =
            static_cast<std::size_t>(std::floor(edges[b] / nyquist *
                                                static_cast<double>(n_bins - 1)));
        const std::size_t bin_end =
            static_cast<std::size_t>(std::ceil(edges[b + 1] / nyquist *
                                               static_cast<double>(n_bins - 1)));

        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t bin = bin_start; bin < bin_end && bin < n_bins; bin++) {
            sum += magnitudes[bin];
            count++;
        }
        if (count > 0) {
            out[b] = static_cast<float>(sum / static_cast<double>(count));
        }
    }
    return out;
}

// Simple one-pole smoothing: out = alpha * new + (1 - alpha) * prev.
void smooth_spectrum(std::vector<float>& spectrum,
                     const std::vector<float>& previous,
                     float alpha) {
    for (std::size_t i = 0; i < spectrum.size(); i++) {
        spectrum[i] =
            alpha * spectrum[i] + (1.0f - alpha) * previous[i];
    }
}

}  // namespace

struct VisualizerEngine::Impl {
    AudioEngine* engine = nullptr;
    SpectrumCallback spectrum_cb;
    std::mutex cb_mutex;

    // Guards band_count, enabled, and previous_spectrum against concurrent
    // access by the FFT worker thread and the UI thread.
    std::mutex config_mutex;

    // Ring buffer fed by the GStreamer streaming thread (appsink callback).
    SpscRingBuffer<float, kRingCapacity> ring;

    // FFT worker.
    std::thread worker_thread;
    std::atomic<bool> running{false};

    // Lifetime guard: the appsink callback captures this shared flag so it can
    // detect that the VisualizerEngine has been destroyed and avoid dereferencing
    // a dangling impl_.
    std::shared_ptr<std::atomic<bool>> alive =
        std::make_shared<std::atomic<bool>>(true);

    // Triple buffer for spectrum frames consumed by the GTK draw callback.
    TripleBuffer<std::vector<float>> triple;

    std::atomic<int> band_count = kDefaultBands;
    std::atomic<bool> enabled{false};

    // Reusable frame buffer and Hann window.
    std::vector<float> frame;
    std::vector<float> window;
    std::vector<float> previous_spectrum;

    // FFT scratch space (complex).
    std::vector<std::complex<double>> fft_in;

    double sample_rate = 44100.0;

    void fft_worker() {
        frame.resize(kFrameSize, 0.0f);
        window = make_hann_window(kFrameSize);
        {
            std::lock_guard<std::mutex> lock(config_mutex);
            previous_spectrum.assign(static_cast<std::size_t>(band_count), 0.0f);
        }
        fft_in.resize(kFrameSize);

        while (running && alive->load()) {
            // Fill a frame from the ring buffer. If the ring is empty, yield
            // briefly rather than spinning, then retry.
            std::size_t filled = 0;
            while (filled < kFrameSize && running && alive->load()) {
                float sample = 0.0f;
                if (ring.pop(sample)) {
                    frame[filled++] = sample;
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
            if (!running || !alive->load()) break;
            if (filled < kFrameSize) break;

            // Read configuration under lock; the worker must not block on UI
            // writes for long, so we copy the values out.
            int bands;
            bool en;
            {
                std::lock_guard<std::mutex> lock(config_mutex);
                bands = band_count;
                en = enabled;
            }
            if (!en) continue;

            // Apply Hann window.
            for (std::size_t i = 0; i < kFrameSize; i++) {
                fft_in[i] =
                    std::complex<double>(frame[i] * window[i], 0.0);
            }

            // FFT.
            fft_in_place(fft_in);

            // Magnitudes for the positive-frequency bins.
            std::vector<double> magnitudes(kFrameSize / 2 + 1, 0.0);
            const double scale = 2.0 / static_cast<double>(kFrameSize);
            for (std::size_t i = 0; i <= kFrameSize / 2; i++) {
                magnitudes[i] = std::abs(fft_in[i]) * scale;
            }

            // Map to bands.
            std::vector<float> spectrum =
                band_map(magnitudes, kFrameSize, bands, sample_rate);

            // Smooth using the previous spectrum (guarded by config_mutex).
            {
                std::lock_guard<std::mutex> lock(config_mutex);
                if (previous_spectrum.size() == spectrum.size()) {
                    smooth_spectrum(spectrum, previous_spectrum, 0.6f);
                }
                previous_spectrum = spectrum;
            }

            // Keep a copy for the direct callback before moving into the
            // triple buffer.
            std::vector<float> spectrum_copy = spectrum;

            // Publish to triple buffer for the UI.
            if (auto* buf = triple.begin_write()) {
                *buf = std::move(spectrum);
                triple.end_write();
            }

            // Invoke the direct spectrum callback (if set) on the worker thread.
            SpectrumCallback cb;
            {
                std::lock_guard<std::mutex> lock(cb_mutex);
                cb = spectrum_cb;
            }
            if (cb) cb(spectrum_copy);

            // Publish spectrum as a conflated EventBus event so UI consumers
            // receive it on the GTK main context (thread-safe). Conflated
            // means a slow UI only sees the latest frame, never a backlog.
            Event event;
            event.type = "spectrum";
            event.event_class = EventClass::Conflated;
            event.payload = SpectrumFrame{spectrum_copy};
            EventBus::instance().publish_conflated(event.type, event);
        }
    }
};

VisualizerEngine::VisualizerEngine() : impl_(std::make_unique<Impl>()) {}

VisualizerEngine::~VisualizerEngine() {
    stop();
    // Mark the visualizer as destroyed so any in-flight appsink callback
    // (capturing the alive flag) will bail out before dereferencing impl_.
    impl_->alive->store(false);
}

void VisualizerEngine::set_audio_engine(AudioEngine* engine) {
    // Disconnect the callback from any previous engine so it cannot call
    // into a destroyed visualizer.
    if (impl_->engine) {
        impl_->engine->set_viz_samples_callback(nullptr);
    }
    impl_->engine = engine;
    if (engine) {
        auto alive = impl_->alive;
        engine->set_viz_samples_callback([this, alive](const float* samples,
                                                       size_t count) {
            if (!alive->load() || !impl_->running || !impl_->enabled) return;
            for (size_t i = 0; i < count; i++) {
                impl_->ring.push(samples[i]);
            }
        });
    }
}

void VisualizerEngine::start() {
    if (impl_->running) return;
    impl_->ring.clear();
    impl_->previous_spectrum.assign(static_cast<std::size_t>(impl_->band_count), 0.0f);
    impl_->running = true;
    impl_->worker_thread = std::thread(&Impl::fft_worker, impl_.get());
}

void VisualizerEngine::stop() {
    if (!impl_->running) return;
    impl_->running = false;
    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }
}

bool VisualizerEngine::is_running() const {
    return impl_->running;
}

void VisualizerEngine::set_spectrum_callback(SpectrumCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->cb_mutex);
    impl_->spectrum_cb = std::move(cb);
}

void VisualizerEngine::set_band_count(int bands) {
    if (bands < 1) bands = 1;
    if (bands > 256) bands = 256;
    impl_->band_count = bands;
    // Resize the previous-spectrum buffer under the config mutex so the FFT
    // worker never reads a vector that is being resized out from under it.
    if (bands > 0) {
        std::lock_guard<std::mutex> lock(impl_->config_mutex);
        impl_->previous_spectrum.assign(static_cast<std::size_t>(bands), 0.0f);
    }
}

void VisualizerEngine::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

bool VisualizerEngine::is_enabled() const {
    return impl_->enabled.load();
}

}  // namespace lollypop
