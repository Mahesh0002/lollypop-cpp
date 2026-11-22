/**
 * Lollypop C++ - Playback Source Manager Implementation
 */

#include "playback_source_manager.hpp"

#include <iostream>

namespace lollypop {

struct PlaybackSourceManager::Impl {
    std::shared_ptr<AudioEngine> audio_engine;
    std::shared_ptr<MprisManager> mpris_manager;
    PlaybackSource current_source = PlaybackSource::Internal;
};

PlaybackSourceManager::PlaybackSourceManager(
    std::shared_ptr<AudioEngine> audio_engine,
    std::shared_ptr<MprisManager> mpris_manager)
    : impl_(std::make_unique<Impl>()) {
    impl_->audio_engine = std::move(audio_engine);
    impl_->mpris_manager = std::move(mpris_manager);
}

PlaybackSourceManager::~PlaybackSourceManager() = default;

PlaybackSource PlaybackSourceManager::source() const {
    return impl_->current_source;
}

void PlaybackSourceManager::set_source(PlaybackSource source) {
    impl_->current_source = source;
}

bool PlaybackSourceManager::internal_available() const {
    return impl_->audio_engine != nullptr;
}

bool PlaybackSourceManager::external_available() const {
    return impl_->mpris_manager != nullptr &&
           impl_->mpris_manager->is_initialized();
}

std::optional<MprisPlayerInfo> PlaybackSourceManager::active_external_player() const {
    if (!impl_->mpris_manager) return std::nullopt;
    return impl_->mpris_manager->get_active_player();
}

bool PlaybackSourceManager::can_control() const {
    if (impl_->current_source == PlaybackSource::Internal) return true;
    return impl_->mpris_manager && impl_->mpris_manager->can_control();
}

bool PlaybackSourceManager::can_seek() const {
    if (impl_->current_source == PlaybackSource::Internal) return true;
    return impl_->mpris_manager && impl_->mpris_manager->can_seek();
}

bool PlaybackSourceManager::can_go_next() const {
    if (impl_->current_source == PlaybackSource::Internal) return true;
    return impl_->mpris_manager && impl_->mpris_manager->can_go_next();
}

bool PlaybackSourceManager::can_go_previous() const {
    if (impl_->current_source == PlaybackSource::Internal) return true;
    return impl_->mpris_manager && impl_->mpris_manager->can_go_previous();
}

void PlaybackSourceManager::play() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->play();
    } else if (impl_->audio_engine) {
        impl_->audio_engine->play();
    }
}

void PlaybackSourceManager::pause() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->pause();
    } else if (impl_->audio_engine) {
        impl_->audio_engine->pause();
    }
}

void PlaybackSourceManager::stop() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->stop();
    } else if (impl_->audio_engine) {
        impl_->audio_engine->stop();
    }
}

void PlaybackSourceManager::play_pause() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->play_pause();
    } else if (impl_->audio_engine) {
        auto state = impl_->audio_engine->get_state();
        if (state == AudioEngine::PlaybackState::Playing ||
            state == AudioEngine::PlaybackState::Buffering) {
            impl_->audio_engine->pause();
        } else {
            impl_->audio_engine->play();
        }
    }
}

void PlaybackSourceManager::next() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->next();
    }
    // Internal next is handled by the QueueManager/controller.
}

void PlaybackSourceManager::previous() {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->previous();
    }
}

void PlaybackSourceManager::seek(double position_seconds) {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->seek(position_seconds);
    } else if (impl_->audio_engine) {
        impl_->audio_engine->seek(position_seconds);
    }
}

void PlaybackSourceManager::seek_by(double offset_seconds) {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->seek_by(offset_seconds);
    } else if (impl_->audio_engine) {
        auto pos = impl_->audio_engine->get_position();
        double current = pos ? *pos : 0.0;
        impl_->audio_engine->seek(current + offset_seconds);
    }
}

void PlaybackSourceManager::set_volume(double volume) {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        impl_->mpris_manager->set_volume(volume);
    } else if (impl_->audio_engine) {
        impl_->audio_engine->set_volume(volume);
    }
}

double PlaybackSourceManager::get_volume() const {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        return impl_->mpris_manager->get_volume();
    }
    if (impl_->audio_engine) return impl_->audio_engine->get_volume();
    return 0.0;
}

double PlaybackSourceManager::get_position() const {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        return impl_->mpris_manager->get_position();
    }
    if (impl_->audio_engine) {
        auto pos = impl_->audio_engine->get_position();
        if (pos) return *pos;
    }
    return 0.0;
}

double PlaybackSourceManager::get_duration() const {
    if (impl_->current_source == PlaybackSource::External && impl_->mpris_manager) {
        return impl_->mpris_manager->get_duration();
    }
    if (impl_->audio_engine) {
        auto dur = impl_->audio_engine->get_duration();
        if (dur) return *dur;
    }
    return 0.0;
}

void PlaybackSourceManager::set_active_player(const std::string& dbus_name) {
    if (impl_->mpris_manager) {
        impl_->mpris_manager->set_active_player(dbus_name);
    }
}

std::string PlaybackSourceManager::active_player_name() const {
    if (impl_->mpris_manager) return impl_->mpris_manager->get_active_player_name();
    return {};
}

}  // namespace lollypop
