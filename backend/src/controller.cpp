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

    // Load library from storage
    // TODO: Implement actual library loading

    std::cout << "BackendController: Initialized successfully" << std::endl;
    return true;
}

void BackendController::shutdown() {
    std::cout << "BackendController: Shutting down..." << std::endl;
    storage_.reset();
}

void BackendController::scan_library(const std::string& path) {
    std::cout << "BackendController: Scanning library at " << path << std::endl;
    // TODO: Implement file scanning with metadata extraction (taglib)
}

void BackendController::add_watch_directory(const std::string& path) {
    // TODO: Implement directory watching (inotify)
}

void BackendController::remove_watch_directory(const std::string& path) {
    // TODO: Implement
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

    // Load track metadata
    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;
    playing_ = true;

    // TODO: Actually start audio playback (GStreamer/PipeWire)

    emit_track_changed(current_track_);
    emit_playback_state_changed(true);
    emit_queue_changed();
}

void BackendController::toggle_play_pause() {
    if (playing_) {
        pause();
    } else {
        playing_ = true;
        emit_playback_state_changed(true);
        // TODO: Resume audio
    }
}

void BackendController::pause() {
    playing_ = false;
    emit_playback_state_changed(false);
    // TODO: Pause audio
}

void BackendController::stop() {
    playing_ = false;
    position_ = 0.0;
    current_track_ = Track{};
    emit_playback_state_changed(false);
    emit_track_changed(current_track_);
    emit_position_changed(0, 0);
    // TODO: Stop audio
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

    // TODO: Start playback of new track

    emit_track_changed(current_track_);
    emit_position_changed(0, duration_);
    emit_queue_changed();
}

void BackendController::previous_track() {
    if (queue_.empty()) return;

    if (position_ > 3.0) {
        // Restart current track if more than 3 seconds in
        position_ = 0.0;
        emit_position_changed(0, duration_);
        // TODO: Seek to start
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

    // TODO: Start playback of new track

    emit_track_changed(current_track_);
    emit_position_changed(0, duration_);
    emit_queue_changed();
}

void BackendController::seek(double position_seconds) {
    position_ = std::clamp(position_seconds, 0.0, duration_);
    emit_position_changed(position_, duration_);
    // TODO: Seek audio
}

void BackendController::set_volume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
    // TODO: Set audio volume
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