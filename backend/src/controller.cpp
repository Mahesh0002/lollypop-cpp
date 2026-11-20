/**
 * Lollypop C++ - Backend Controller Implementation
 */

#include "controller.hpp"
#include "manager.hpp"

#include <glibmm/convert.h>
#include <glibmm/dispatcher.h>

#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <ctime>

namespace lollypop {

BackendController::BackendController() {
    file_event_dispatcher_ = std::make_unique<Glib::Dispatcher>();
    file_event_dispatcher_->connect([this] { dispatch_file_events(); });
}

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

    // Restore player preferences before the audio engine starts.
    auto load_bool = [this](const char* key, bool fallback) {
        const auto value = storage_->get_preference(key);
        return value ? (*value == "1") : fallback;
    };
    if (const auto value = storage_->get_preference("player.volume")) {
        try {
            volume_ = std::clamp(std::stod(*value), 0.0, 1.0);
        } catch (...) {
            // Ignore corrupt preference values.
        }
    }
    shuffle_ = load_bool("player.shuffle", shuffle_);
    repeat_ = load_bool("player.repeat", repeat_);
    repeat_one_ = load_bool("player.repeat_one", repeat_one_);
    if (repeat_ && repeat_one_) repeat_one_ = false;

    // Initialize audio engine
    audio_engine_ = std::make_unique<AudioEngine>();
    if (!audio_engine_->initialize()) {
        std::cerr << "BackendController: Failed to initialize audio engine" << std::endl;
        return false;
    }
    audio_engine_->set_volume(volume_);
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
        if (!storage_->add_track(track)) {
            std::cerr << "BackendController: Failed to store track "
                      << track.file_path << std::endl;
        }
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

    // Initialize MPRIS server (Lollypop as an MPRIS player)
    mpris_server_ = std::make_unique<MprisServer>();
    if (!mpris_server_->initialize()) {
        std::cerr << "BackendController: MPRIS server unavailable" << std::endl;
        mpris_server_.reset();
    } else {
        setup_mpris_server_callbacks();
        mpris_server_->update_volume(volume_);
        mpris_server_->update_shuffle(shuffle_);
        mpris_server_->update_loop_state(repeat_one_ ? 2 : (repeat_ ? 1 : 0));
        sync_mpris_capabilities();
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
    if (mpris_server_) {
        mpris_server_->shutdown();
        mpris_server_.reset();
    }
    if (mpris_manager_) {
        mpris_manager_->shutdown();
        mpris_manager_.reset();
    }
    // Stop the file watcher before the dispatcher is destroyed: its thread
    // must not emit into a partially destroyed controller.
    if (file_watcher_) {
        file_watcher_.reset();
    }
    library_scanner_.reset();
    storage_.reset();
    audio_engine_.reset();
}

void BackendController::setup_cava_callbacks() {
    cava_visualizer_->set_points_callback([this](const std::vector<float>& points) {
        if (visualization_cb_) visualization_cb_(points);
    });
}

void BackendController::setup_mpris_server_callbacks() {
    // Remote MPRIS client (media keys, sound menu) wants to play/pause
    mpris_server_->set_playback_state_changed_callback([this](bool playing) {
        if (playing) {
            if (current_track_.id.empty()) {
                if (!queue_.empty()) start_queue_index(queue_index_);
                return;
            }
            if (audio_engine_->get_state() == AudioEngine::PlaybackState::Stopped ||
                audio_engine_->get_state() == AudioEngine::PlaybackState::Error) {
                start_queue_index(queue_index_);
                return;
            }
            audio_engine_->play();
            playing_ = true;
            emit_playback_state_changed(true);
            if (mpris_server_) mpris_server_->update_playback_status(true);
        } else {
            pause();
        }
    });

    // Remote client requests volume change
    mpris_server_->set_volume_changed_callback([this](double volume) {
        set_volume(volume);
    });

    // MPRIS Seek is a relative offset in seconds.
    mpris_server_->set_seek_callback([this](double offset_seconds) {
        seek(position_ + offset_seconds);
    });

    // Remote client requests SetPosition (track_id + absolute position)
    mpris_server_->set_set_position_callback(
        [this](const std::string& /*track_id*/, double position_seconds) {
            seek(position_seconds);
        });

    mpris_server_->set_next_callback([this] { next_track(); });
    mpris_server_->set_previous_callback([this] { previous_track(); });
    mpris_server_->set_play_pause_callback([this] { toggle_play_pause(); });
    mpris_server_->set_stop_callback([this] { stop(); });
    mpris_server_->set_open_uri_callback(
        [this](const std::string& uri) { open_uri(uri); });

    // Remote client requests loop/shuffle change
    mpris_server_->set_loop_state_changed_callback([this](int loop_state) {
        set_repeat(loop_state == 1);     // Playlist -> repeat
        set_repeat_one(loop_state == 2); // Track -> repeat-one
    });
    mpris_server_->set_shuffle_changed_callback([this](bool shuffle) {
        set_shuffle(shuffle);
    });
}

void BackendController::setup_audio_callbacks() {
    // Position updates flow to UI and MPRIS server
    audio_engine_->set_position_callback([this](double position, double duration) {
        position_ = position;
        // GStreamer reports duration 0 briefly after loading; keep the
        // metadata duration until the pipeline knows better.
        if (duration > 0.0) duration_ = duration;
        emit_position_changed(position_, duration_);
        if (mpris_server_) {
            mpris_server_->update_position(position_);
            mpris_server_->update_duration(duration_);
        }
    });

    // Playback state changes flow to UI and MPRIS server. Ignore the
    // transient PAUSED state emitted while loading a track; pause() already
    // reports explicit pauses.
    audio_engine_->set_state_callback([this](AudioEngine::PlaybackState state) {
        if (state == AudioEngine::PlaybackState::Paused ||
            state == AudioEngine::PlaybackState::Buffering) {
            return;
        }
        const bool playing = (state == AudioEngine::PlaybackState::Playing);
        if (playing == playing_) return;
        playing_ = playing;
        emit_playback_state_changed(playing_);
        if (mpris_server_) {
            if (state == AudioEngine::PlaybackState::Stopped ||
                state == AudioEngine::PlaybackState::Error) {
                mpris_server_->update_stopped();
            } else {
                mpris_server_->update_playback_status(playing_);
            }
        }
        sync_mpris_capabilities();
    });

    // End of stream - advance to next track
    audio_engine_->set_eos_callback([this]() {
        std::cout << "BackendController: End of stream reached" << std::endl;
        if (repeat_one_) {
            // Replay current track
            position_ = 0.0;
            audio_engine_->seek(0.0);
            audio_engine_->play();
            emit_position_changed(0.0, duration_);
            if (mpris_server_) {
                mpris_server_->update_position(0.0);
                mpris_server_->notify_seeked(0.0);
            }
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
    // One transaction for the whole scan: hundreds of individual inserts are
    // otherwise flushed to disk one by one.
    const bool in_transaction = storage_->begin_transaction();
    library_scanner_->scan_directory(path, true);
    if (in_transaction) storage_->commit_transaction();

    // New/changed files should be tracked from now on.
    add_watch_directory(path);
    emit_library_changed();
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

void BackendController::open_uri(const std::string& uri) {
    if (!library_scanner_) return;
    if (!uri.starts_with("file://")) {
        std::cerr << "BackendController: Unsupported URI: " << uri << std::endl;
        return;
    }

    std::string path;
    try {
        path = Glib::filename_from_uri(uri);
    } catch (const Glib::Error& error) {
        std::cerr << "BackendController: Invalid URI " << uri << ": "
                  << error.what() << std::endl;
        return;
    }
    if (path.empty() || !LibraryScanner::is_audio_file(path)) return;

    Track track = storage_->get_track_by_path(path);
    if (track.id.empty()) {
        library_scanner_->scan_file(path);
        track = storage_->get_track_by_path(path);
    }
    if (!track.id.empty()) {
        play_tracks({track.id}, 0);
    }
}

void BackendController::handle_file_event(const FileEvent& event) {
    // Called on the FileWatcher thread. Never touch storage or UI here.
    {
        std::lock_guard<std::mutex> lock(file_events_mutex_);
        pending_file_events_.push_back(event);
    }
    if (file_event_dispatcher_) file_event_dispatcher_->emit();
}

void BackendController::dispatch_file_events() {
    std::vector<FileEvent> events;
    {
        std::lock_guard<std::mutex> lock(file_events_mutex_);
        events.swap(pending_file_events_);
    }
    for (const auto& event : events) {
        process_file_event(event);
    }
}

void BackendController::process_file_event(const FileEvent& event) {
    auto current_is_under = [this](const std::string& directory) {
        return !current_track_.file_path.empty() &&
               (current_track_.file_path == directory ||
                current_track_.file_path.starts_with(directory + "/"));
    };

    switch (event.type) {
        case FileEventType::Created:
            std::cout << "BackendController: File created: " << event.path << std::endl;
            if (library_scanner_ && event.is_dir) {
                // Directory moved into the watched tree, or created quickly
                // enough that its files predate the watch.
                library_scanner_->scan_directory(event.path, true);
                emit_library_changed();
            } else if (library_scanner_ &&
                       LibraryScanner::is_audio_file(event.path)) {
                library_scanner_->scan_file(event.path);
                emit_library_changed();
            }
            break;
        case FileEventType::Modified:
            std::cout << "BackendController: File modified: " << event.path << std::endl;
            if (library_scanner_ && LibraryScanner::is_audio_file(event.path)) {
                library_scanner_->scan_file(event.path);
                if (current_track_.file_path == event.path) {
                    Track updated = storage_->get_track_by_path(event.path);
                    if (!updated.id.empty()) {
                        current_track_ = updated;
                        duration_ = updated.duration;
                        emit_track_changed(current_track_);
                        if (mpris_server_) {
                            mpris_server_->update_track(current_track_);
                            mpris_server_->update_duration(duration_);
                        }
                    }
                }
                emit_library_changed();
            }
            break;
        case FileEventType::Deleted:
            std::cout << "BackendController: File deleted: " << event.path << std::endl;
            if (event.is_dir) {
                storage_->delete_tracks_under_path(event.path);
            } else {
                storage_->delete_track_by_path(event.path);
            }
            if (current_is_under(event.path)) {
                stop();
            }
            emit_library_changed();
            break;
        case FileEventType::Moved:
            std::cout << "BackendController: File moved: " << event.path
                      << " -> " << event.dest_path << std::endl;
            if (event.is_dir) {
                storage_->delete_tracks_under_path(event.path);
            } else {
                storage_->delete_track_by_path(event.path);
            }
            if (current_is_under(event.path)) {
                stop();
            }
            if (library_scanner_ && !event.dest_path.empty()) {
                if (event.is_dir) {
                    library_scanner_->scan_directory(event.dest_path, true);
                } else if (LibraryScanner::is_audio_file(event.dest_path)) {
                    library_scanner_->scan_file(event.dest_path);
                }
            }
            emit_library_changed();
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

std::vector<Track> BackendController::get_tracks_by_artist(
    const std::string& artist_name) {
    return storage_->get_tracks_by_artist(artist_name);
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
    start_queue_index(queue_index_);
}

bool BackendController::start_queue_index(size_t index) {
    if (queue_.empty() || index >= queue_.size()) return false;
    queue_index_ = index;

    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;
    playing_ = false;

    if (current_track_.id.empty()) {
        std::cerr << "BackendController: Track not found in queue" << std::endl;
    } else if (current_track_.file_path.empty()) {
        std::cerr << "BackendController: Track has no file path: "
                  << current_track_.title << std::endl;
    } else if (audio_engine_->load_track(current_track_.file_path)) {
        audio_engine_->set_volume(volume_);
        audio_engine_->play();
        playing_ = true;
        update_play_statistics();
    } else {
        std::cerr << "BackendController: Failed to load track: "
                  << current_track_.file_path << std::endl;
    }

    emit_track_changed(current_track_);
    emit_position_changed(0.0, duration_);
    emit_playback_state_changed(playing_);
    emit_queue_changed();

    if (mpris_server_) {
        mpris_server_->update_track(current_track_);
        if (playing_) {
            mpris_server_->update_playback_status(true);
        } else {
            mpris_server_->update_stopped();
        }
        mpris_server_->update_position(0.0);
        mpris_server_->update_duration(duration_);
    }
    sync_mpris_capabilities();
    return playing_;
}

void BackendController::update_play_statistics() {
    if (current_track_.id.empty()) return;
    storage_->update_play_count(current_track_.id);
    storage_->update_last_played(current_track_.id);
    current_track_.play_count++;
    current_track_.last_played = static_cast<int64_t>(std::time(nullptr));
}

void BackendController::toggle_play_pause() {
    if (playing_) {
        pause();
        return;
    }

    if (current_track_.id.empty()) {
        if (!queue_.empty()) start_queue_index(queue_index_);
        return;
    }

    const auto state = audio_engine_->get_state();
    if (state == AudioEngine::PlaybackState::Stopped ||
        state == AudioEngine::PlaybackState::Error) {
        start_queue_index(queue_index_);
        return;
    }

    audio_engine_->play();
    playing_ = true;
    emit_playback_state_changed(true);
    if (mpris_server_) mpris_server_->update_playback_status(true);
    sync_mpris_capabilities();
}

void BackendController::pause() {
    playing_ = false;
    audio_engine_->pause();
    emit_playback_state_changed(false);
    if (mpris_server_) {
        mpris_server_->update_playback_status(false);
    }
    sync_mpris_capabilities();
}

void BackendController::stop() {
    playing_ = false;
    position_ = 0.0;
    current_track_ = Track{};
    audio_engine_->stop();
    emit_playback_state_changed(false);
    emit_track_changed(current_track_);
    emit_position_changed(0, 0);
    if (mpris_server_) {
        mpris_server_->update_stopped();
        mpris_server_->update_track(Track{});
    }
    sync_mpris_capabilities();
}

void BackendController::next_track() {
    if (queue_.empty()) return;

    size_t next_index = queue_index_;
    if (shuffle_ && queue_.size() > 1) {
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dis(0, queue_.size() - 1);
        do {
            next_index = dis(gen);
        } while (next_index == queue_index_);
    } else if (queue_index_ + 1 < queue_.size()) {
        next_index = queue_index_ + 1;
    } else if (repeat_) {
        next_index = 0;
    } else {
        // End of the queue with repeat disabled.
        stop();
        return;
    }

    start_queue_index(next_index);
}

void BackendController::previous_track() {
    if (queue_.empty()) return;

    if (position_ > 3.0) {
        // Restart current track if more than 3 seconds in.
        seek(0.0);
        return;
    }

    size_t prev_index = queue_index_;
    if (shuffle_ && queue_.size() > 1) {
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dis(0, queue_.size() - 1);
        do {
            prev_index = dis(gen);
        } while (prev_index == queue_index_);
    } else if (queue_index_ > 0) {
        prev_index = queue_index_ - 1;
    } else if (repeat_) {
        prev_index = queue_.size() - 1;
    } else {
        // Already at the first track: restart it instead of wrapping.
        seek(0.0);
        return;
    }

    start_queue_index(prev_index);
}

void BackendController::seek(double position_seconds) {
    position_ = std::clamp(position_seconds, 0.0, duration_);
    audio_engine_->seek(position_);
    emit_position_changed(position_, duration_);
    if (mpris_server_) {
        mpris_server_->update_position(position_);
        mpris_server_->notify_seeked(position_);
    }
}

void BackendController::set_volume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
    audio_engine_->set_volume(volume_);
    if (storage_) storage_->set_preference("player.volume", std::to_string(volume_));
    if (mpris_server_) {
        mpris_server_->update_volume(volume_);
    }
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
    if (storage_) storage_->set_preference("player.shuffle", enabled ? "1" : "0");
    if (mpris_server_) {
        mpris_server_->update_shuffle(enabled);
    }
    sync_mpris_capabilities();
}

void BackendController::set_repeat(bool enabled) {
    repeat_ = enabled;
    if (enabled) repeat_one_ = false;
    if (storage_) storage_->set_preference("player.repeat", enabled ? "1" : "0");
    if (storage_) storage_->set_preference("player.repeat_one", repeat_one_ ? "1" : "0");
    if (mpris_server_) {
        mpris_server_->update_loop_state(enabled ? 1 : 0);
    }
    sync_mpris_capabilities();
}

void BackendController::set_repeat_one(bool enabled) {
    repeat_one_ = enabled;
    if (enabled) repeat_ = false;
    if (storage_) storage_->set_preference("player.repeat", repeat_ ? "1" : "0");
    if (storage_) storage_->set_preference("player.repeat_one", repeat_one_ ? "1" : "0");
    if (mpris_server_) {
        mpris_server_->update_loop_state(enabled ? 2 : 0);
    }
    sync_mpris_capabilities();
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

void BackendController::sync_mpris_capabilities() {
    if (!mpris_server_) return;

    const bool has_track = !current_track_.id.empty();
    const bool has_queue = !queue_.empty();
    // CanPlay stays true after stop() when the queue can be restarted.
    const bool can_play = has_track || has_queue;
    const bool can_go_next =
        has_queue && (shuffle_ || repeat_ || queue_index_ + 1 < queue_.size());
    const bool can_go_previous =
        has_queue && (shuffle_ || repeat_ || queue_index_ > 0);
    mpris_server_->update_capabilities(can_go_next, can_go_previous, can_play,
                                       has_track, has_track && duration_ > 0);
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

void BackendController::emit_library_changed() {
    if (library_changed_cb_) {
        library_changed_cb_();
    }
}

} // namespace lollypop