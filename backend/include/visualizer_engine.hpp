/**
 * Lollypop C++ - VisualizerEngine
 *
 * Audio-reactive visualization pipeline isolated from the audio output path.
 *
 * Simplified Architecture (Phase 3 - Visualizer Snapshot):
 *
 *   GStreamer appsink (streaming thread)
 *       | pull samples, convert to mono float
 *       v
 *   lock-free SPSC ring buffer   (no malloc, no mutex in streaming callback)
 *       |
 *       v
 *   FFT worker thread            (window -> FFT -> magnitudes -> bands)
 *       |
 *       v
 *   Mutex-protected latest-frame snapshot with EMA smoothing (30 FPS cap)
 *       |
 *       v
 *   GTK draw callback (main thread) reads via get_latest_spectrum()
 *
 * For internal playback the samples come from the appsink branch. For
 * external MPRIS players, CAVA/system capture is the appropriate source and
 * can be wired in separately.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lollypop {

class AudioEngine;

// ---------------------------------------------------------------------------
// Lock-free Single-Producer / Single-Consumer ring buffer.
// Capacity must be a power of two. No allocation after construction.
// ---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SpscRingBuffer capacity must be a power of two");

    T buffer_[Capacity];
    std::atomic<std::size_t> write_index_{0};
    std::atomic<std::size_t> read_index_{0};

public:
    SpscRingBuffer() = default;

    // Producer: push one sample. Returns false if the buffer is full
    // (oldest data is effectively dropped by the caller choosing to drop).
    bool push(const T& sample) {
        const std::size_t wr = write_index_.load(std::memory_order_relaxed);
        const std::size_t rd = read_index_.load(std::memory_order_acquire);
        if ((wr + 1) % Capacity == rd) {
            return false;  // full
        }
        buffer_[wr] = sample;
        write_index_.store((wr + 1) % Capacity, std::memory_order_release);
        return true;
    }

    // Consumer: pop one sample. Returns false if empty.
    bool pop(T& sample) {
        const std::size_t rd = read_index_.load(std::memory_order_relaxed);
        const std::size_t wr = write_index_.load(std::memory_order_acquire);
        if (rd == wr) {
            return false;  // empty
        }
        sample = buffer_[rd];
        read_index_.store((rd + 1) % Capacity, std::memory_order_release);
        return true;
    }

    std::size_t size() const {
        const std::size_t wr = write_index_.load(std::memory_order_acquire);
        const std::size_t rd = read_index_.load(std::memory_order_acquire);
        return (wr + Capacity - rd) % Capacity;
    }

    std::size_t capacity() const { return Capacity; }

    void clear() {
        write_index_.store(0, std::memory_order_relaxed);
        read_index_.store(0, std::memory_order_relaxed);
    }
};

class VisualizerEngine {
public:
    using SpectrumCallback = std::function<void(const std::vector<float>&)>;

    VisualizerEngine();
    ~VisualizerEngine();

    VisualizerEngine(const VisualizerEngine&) = delete;
    VisualizerEngine& operator=(const VisualizerEngine&) = delete;

    // Connect to an AudioEngine so its appsink samples feed the visualizer.
    void set_audio_engine(AudioEngine* engine);

    void start();
    void stop();
    bool is_running() const;

    // Get the latest spectrum frame (thread-safe, for UI polling).
    // Returns empty vector if no frame available yet.
    std::vector<float> get_latest_spectrum() const;

    // Set a direct spectrum callback (called from the FFT worker thread).
    // Prefer polling get_latest_spectrum() from the GTK frame clock.
    void set_spectrum_callback(SpectrumCallback cb);
    void set_band_count(int bands);
    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lollypop