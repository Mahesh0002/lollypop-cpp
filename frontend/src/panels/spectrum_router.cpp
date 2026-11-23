/**
 * Lollypop C++ - Spectrum Router Implementation
 */

#include "panels/spectrum_router.hpp"

#include "panels/progress_wave.hpp"

#include <algorithm>

namespace lollypop {

SpectrumRouter& SpectrumRouter::instance() {
    static SpectrumRouter router;
    return router;
}

SpectrumRouter::SpectrumRouter() {
    handler_id_ = EventBus::instance().subscribe(
        "spectrum", [this](const Event& event) {
            const auto* frame = std::get_if<SpectrumFrame>(&event.payload);
            if (!frame) return;
            for (ProgressWave* wave : waves_) {
                if (wave) wave->set_spectrum(frame->magnitudes);
            }
        });
}

SpectrumRouter::~SpectrumRouter() {
    if (handler_id_ != 0) {
        EventBus::instance().unsubscribe(handler_id_);
        handler_id_ = 0;
    }
}

void SpectrumRouter::register_wave(ProgressWave* wave) {
    if (!wave) return;
    if (std::find(waves_.begin(), waves_.end(), wave) == waves_.end()) {
        waves_.push_back(wave);
    }
}

void SpectrumRouter::unregister_wave(ProgressWave* wave) {
    waves_.erase(std::remove(waves_.begin(), waves_.end(), wave), waves_.end());
}

} // namespace lollypop
