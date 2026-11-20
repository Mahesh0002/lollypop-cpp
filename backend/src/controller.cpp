/**
 * Lollypop C++ - Backend Controller Implementation
 */

#include "controller.hpp"
#include "manager.hpp"

#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>

namespace lollypop {

BackendController::BackendController() = default;

BackendController::~BackendController() {
    shutdown();
}

bool BackendController::initialize() {
    std::cout << "BackendController: Initializing..." << std::endl;

    // Initialize storage
    storage_ = std::make_unique<StorageManager>();
    if (!storage_->initialize()) {
        std::cerr << "BackendController: Failed to initialize storage" << std::endl;
        return false;
    }

    // Initialize audio engine
    audio_engine_ = std::make_unique<AudioEngine>();
    if (!audio_engine_->initialize()) {
        std::cerr << "BackendController: Failed to initialize audio engine" << std::endl;
        return false;
    }
    setup_audio_callbacks();

    // Initialize MPRIS (external player discovery/control). Non-fatal when
    // there is no session bus (e.g. headless test runs).
    mpris_manager_ = std::make_unique<MprisManager>();
    if (!mpris_manager_->initialize()) {
        std::cerr << "BackendController: MPRIS unavailable, continuing without it"
                  << std::endl;
        mpris_manager_.reset();
    }

    // Initialize library scanner
    library_scanner_ = std::make_unique<LibraryScanner>();
    library_scanner_->set_track_found_callback([this](const Track& track) {
        if (track.file_path.empty()) return;
        storage_->add_track(track);
    });
    library_scanner_->set_error_callback([](const std::string& file, const std::string& error) {
        std::cerr << "LibraryScanner: error on " << file << ": " << error << std::endl;
    });

    // Initialize file watcher
    file_watcher_ = std::make_unique<FileWatcher>();
    file_watcher_->set_event_callback([this](const FileEvent& event) {
        handle_file_event(event);
    });

    // Initialize CAVA visualizer
    cava_visualizer_ = std::make_unique<CavaVisualizer>();
    if (cava_visualizer_->is_cava_available()) {
        cava_visualizer_->start();
        setup_cava_callbacks();
    } else {
        std::cerr << "BackendController: CAVA not available, visualization disabled"
                  << std::endl;
    }

    std::cout << "BackendController: Initialized successfully" << std::endl;
    return true;
}

void BackendController::shutdown() {
    std::cout << "BackendController: Shutting down..." << std::endl;
    if (audio_engine_) {
        audio_engine_->stop();
    }
    if (cava_visualizer_) {
        cava_visualizer_->stop();
        cava_visualizer_.reset();
    }
    if (mpris_manager_) {
        mpris_manager_->shutdown();
        mpris_manager_.reset();
    }
    storage_.reset();
    audio_engine_.reset();
}

void BackendController::setup_cava_callbacks() {
    cava_visualizer_->set_points_callback([this](const std::vector<float>& points) {
        if (visualization_cb_) visualization_cb_(points);
    });
}

void BackendController::setup_audio_callbacks() {
    // Position updates flow to UI
    audio_engine_->set_position_callback([this](double position, double duration) {
        position_ = position;
        duration_ = duration;
        emit_position_changed(position, duration);
    });

    // Playback state changes flow to UI
    audio_engine_->set_state_callback([this](AudioEngine::PlaybackState state) {
        playing_ = (state == AudioEngine::PlaybackState::Playing);
        emit_playback_state_changed(playing_);
    });

    // End of stream - advance to next track
    audio_engine_->set_eos_callback([this]() {
        std::cout << "BackendController: End of stream reached" << std::endl;
        if (repeat_one_) {
            // Replay current track
            audio_engine_->seek(0.0);
            audio_engine_->play();
        } else {
            next_track();
        }
    });

    // Audio engine errors
    audio_engine_->set_error_callback([this](const std::string& error) {
        std::cerr << "BackendController: Audio engine error: " << error << std::endl;
    });
}

void BackendController::scan_library(const std::string& path) {
    std::cout << "BackendController: Scanning library at " << path << std::endl;
    if (!library_scanner_) {
        std::cerr << "BackendController: Library scanner not initialized" << std::endl;
        return;
    }
    library_scanner_->scan_directory(path, true);
}

void BackendController::add_watch_directory(const std::string& path) {
    std::cout << "BackendController: Watching directory " << path << std::endl;
    if (!file_watcher_) {
        std::cerr << "BackendController: File watcher not initialized" << std::endl;
        return;
    }
    file_watcher_->add_watch(path, true);
}

void BackendController::remove_watch_directory(const std::string& path) {
    if (!file_watcher_) return;
    file_watcher_->remove_watch(path);
}

void BackendController::handle_file_event(const FileEvent& event) {
    switch (event.type) {
        case FileEventType::Created:
            std::cout << "BackendController: File created: " << event.path << std::endl;
            if (LibraryScanner::is_audio_file(event.path)) {
                library_scanner_->scan_file(event.path);
            }
            break;
        case FileEventType::Deleted:
            std::cout << "BackendController: File deleted: " << event.path << std::endl;
            // TODO: remove from storage once StorageManager::remove_track_by_path exists
            break;
        case FileEventType::Modified:
            std::cout << "BackendController: File modified: " << event.path << std::endl;
            break;
        case FileEventType::Moved:
            std::cout << "BackendController: File moved: " << event.path << std::endl;
            break;
    }
}

std::vector<Track> BackendController::get_all_tracks() {
    return storage_->get_all_tracks();
}

std::vector<Track> BackendController::get_recently_added(int limit) {
    return storage_->get_recently_added(limit);
}

std::vector<Track> BackendController::get_recently_played(int limit) {
    return storage_->get_recently_played(limit);
}

std::vector<Track> BackendController::get_liked_tracks() {
    return storage_->get_liked_tracks();
}

std::vector<Track> BackendController::search_tracks(const std::string& query) {
    return storage_->search_tracks(query);
}

Track BackendController::get_track(const std::string& track_id) {
    return storage_->get_track(track_id);
}

std::vector<Album> BackendController::get_all_albums() {
    return storage_->get_all_albums();
}

std::vector<Album> BackendController::get_albums_by_artist(const std::string& artist_id) {
    return storage_->get_albums_by_artist(artist_id);
}

Album BackendController::get_album(const std::string& album_id) {
    return storage_->get_album(album_id);
}

std::vector<Artist> BackendController::get_all_artists() {
    return storage_->get_all_artists();
}

Artist BackendController::get_artist(const std::string& artist_id) {
    return storage_->get_artist(artist_id);
}

std::vector<Playlist> BackendController::get_all_playlists() {
    return storage_->get_all_playlists();
}

Playlist BackendController::get_playlist(const std::string& playlist_id) {
    return storage_->get_playlist(playlist_id);
}

void BackendController::create_playlist(const std::string& name) {
    storage_->create_playlist(name);
}

void BackendController::delete_playlist(const std::string& playlist_id) {
    storage_->delete_playlist(playlist_id);
}

void BackendController::add_track_to_playlist(const std::string& playlist_id, const std::string& track_id) {
    storage_->add_track_to_playlist(playlist_id, track_id);
}

void BackendController::remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id) {
    storage_->remove_track_from_playlist(playlist_id, track_id);
}

void BackendController::play_track(const std::string& track_id) {
    play_tracks({track_id}, 0);
}

void BackendController::play_tracks(const std::vector<std::string>& track_ids, size_t start_index) {
    if (track_ids.empty()) return;

    queue_ = track_ids;
    queue_index_ = std::min(start_index, queue_.size() - 1);

    // Load track metadata from storage
    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;

    // Load and play via audio engine
    if (!current_track_.file_path.empty()) {
        if (audio_engine_->load_track(current_track_.file_path)) {
            audio_engine_->set_volume(volume_);
            audio_engine_->play();
            playing_ = true;
        } else {
            std::cerr << "BackendController: Failed to load track: " << current_track_.file_path << std::endl;
            playing_ = false;
        }
    } else {
        std::cerr << "BackendController: Track has no file path: " << current_track_.title << std::endl;
        playing_ = false;
    }

    emit_track_changed(current_track_);
    emit_playback_state_changed(playing_);
    emit_queue_changed();
}

void BackendController::toggle_play_pause() {
    if (playing_) {
        pause();
    } else {
        playing_ = true;
        audio_engine_->play();
        emit_playback_state_changed(true);
    }
}

void BackendController::pause() {
    playing_ = false;
    audio_engine_->pause();
    emit_playback_state_changed(false);
}

void BackendController::stop() {
    playing_ = false;
    position_ = 0.0;
    current_track_ = Track{};
    audio_engine_->stop();
    emit_playback_state_changed(false);
    emit_track_changed(current_track_);
    emit_position_changed(0, 0);
}

void BackendController::next_track() {
    if (queue_.empty()) return;

    if (shuffle_) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, queue_.size() - 1);
        queue_index_ = dis(gen);
    } else {
        queue_index_ = (queue_index_ + 1) % queue_.size();
    }

    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;
    playing_ = true;

    // Load and play via audio engine
    if (!current_track_.file_path.empty()) {
        audio_engine_->load_track(current_track_.file_path);
        audio_engine_->set_volume(volume_);
        audio_engine_->play();
    }

    emit_track_changed(current_track_);
    emit_position_changed(0, duration_);
    emit_queue_changed();
}

void BackendController::previous_track() {
    if (queue_.empty()) return;

    if (position_ > 3.0) {
        // Restart current track if more than 3 seconds in
        position_ = 0.0;
        audio_engine_->seek(0.0);
        emit_position_changed(0, duration_);
        return;
    }

    if (shuffle_) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, queue_.size() - 1);
        queue_index_ = dis(gen);
    } else {
        if (queue_index_ == 0) {
            queue_index_ = queue_.size() - 1;
        } else {
            queue_index_--;
        }
    }

    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;
    playing_ = true;

    // Load and play via audio engine
    if (!current_track_.file_path.empty()) {
        audio_engine_->load_track(current_track_.file_path);
        audio_engine_->set_volume(volume_);
        audio_engine_->play();
    }

    emit_track_changed(current_track_);
    emit_position_changed(0, duration_);
    emit_queue_changed();
}

void BackendController::seek(double position_seconds) {
    position_ = std::clamp(position_seconds, 0.0, duration_);
    audio_engine_->seek(position_);
    emit_position_changed(position_, duration_);
}

void BackendController::set_volume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
    audio_engine_->set_volume(volume_);
}

double BackendController::get_volume() const {
    return volume_;
}

bool BackendController::is_playing() const {
    return playing_;
}

Track BackendController::get_current_track() const {
    return current_track_;
}

double BackendController::get_position() const {
    return position_;
}

double BackendController::get_duration() const {
    return duration_;
}

std::vector<std::string> BackendController::get_queue() const {
    return queue_;
}

size_t BackendController::get_queue_index() const {
    return queue_index_;
}

void BackendController::set_shuffle(bool enabled) {
    shuffle_ = enabled;
}

void BackendController::set_repeat(bool enabled) {
    repeat_ = enabled;
    if (enabled) repeat_one_ = false;
}

void BackendController::set_repeat_one(bool enabled) {
    repeat_one_ = enabled;
    if (enabled) repeat_ = false;
}

bool BackendController::get_shuffle() const {
    return shuffle_;
}

bool BackendController::get_repeat() const {
    return repeat_;
}

bool BackendController::get_repeat_one() const {
    return repeat_one_;
}

void BackendController::toggle_like(const std::string& track_id) {
    storage_->toggle_like(track_id);
}

void BackendController::toggle_favorite_album(const std::string& album_id) {
    storage_->toggle_favorite_album(album_id);
}

void BackendController::emit_track_changed(const Track& track) {
    if (track_changed_cb_) {
        track_changed_cb_(track);
    }
}

void BackendController::emit_position_changed(double position, double duration) {
    if (position_changed_cb_) {
        position_changed_cb_(position, duration);
    }
}

void BackendController::emit_playback_state_changed(bool playing) {
    if (playback_state_changed_cb_) {
        playback_state_changed_cb_(playing);
    }
}

void BackendController::emit_queue_changed() {
    if (queue_changed_cb_) {
        queue_changed_cb_(queue_);
    }
}

} // namespace lollypop