/**
 * Lollypop C++ - Spectrum Router
 *
 * Single frontend subscriber for EventBus "spectrum" frames. The backend owns
 * publication; this class only receives frames and fans them out to the
 * visible waveform widgets. Handlers run on the GTK main context (EventBus
 * guarantees this), so no extra marshalling is needed.
 */

#pragma once

#include <vector>

#include "event_bus.hpp"

namespace lollypop {

class ProgressWave;

class SpectrumRouter {
public:
    static SpectrumRouter& instance();

    void register_wave(ProgressWave* wave);
    void unregister_wave(ProgressWave* wave);

    SpectrumRouter(const SpectrumRouter&) = delete;
    SpectrumRouter& operator=(const SpectrumRouter&) = delete;

private:
    SpectrumRouter();
    ~SpectrumRouter();

    EventBus::HandlerId handler_id_ = 0;
    std::vector<ProgressWave*> waves_;
};

} // namespace lollypop
