/**
 * Lollypop C++ - Backend Controller Implementation
 */

#include "controller.hpp"
#include "manager.hpp"
#include "event_bus.hpp"

#include <glib.h>
#include <glibmm/convert.h>
#include <glibmm/dispatcher.h>

#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <thread>

namespace lollypop {

namespace {

std::string default_music_directory() {
    const char* music_dir = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
    if (music_dir && *music_dir) return music_dir;

    const char* home = g_get_home_dir();
    if (home && *home) return std::string(home) + "/Music";
    return {};
}

}  // namespace

BackendController::BackendController() {
    scan_dispatcher_ = std::make_unique<Glib::Dispatcher>();
    scan_dispatcher_->connect([this] { dispatch_scan_updates(); });
    file_event_dispatcher_ = std::make_unique<Glib::Dispatcher>();
    file_event_dispatcher_->connect([this] { dispatch_file_events(); });
    lyrics_dispatcher_ = std::make_unique<Glib::Dispatcher>();
    lyrics_dispatcher_->connect([this] { dispatch_lyrics_result(); });

    queue_manager_ = std::make_unique<QueueManager>();
    playback_source_manager_ = std::make_unique<PlaybackSourceManager>(
        audio_engine_, mpris_manager_);
}

BackendController::~BackendController() {
    shutdown();
}

bool BackendController::initialize() {
    std::cout << "BackendController: Initializing..." << std::endl;

    // --- Stage 1: storage ---
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
    bool shuffle = load_bool("player.shuffle", false);
    bool repeat = load_bool("player.repeat", false);
    bool repeat_one = load_bool("player.repeat_one", false);
    if (repeat && repeat_one) repeat_one = false;

    // --- Stage 2: audio engine ---
    audio_engine_ = std::make_shared<AudioEngine>();
    if (!audio_engine_->initialize()) {
        std::cerr << "BackendController: Failed to initialize audio engine" << std::endl;
        storage_.reset();
        return false;
    }
    audio_engine_->set_volume(volume_);
    setup_audio_callbacks();

    // --- Stage 3: MPRIS manager (external players) ---
    mpris_manager_ = std::make_shared<MprisManager>();
    if (!mpris_manager_->initialize()) {
        std::cerr << "BackendController: MPRIS unavailable, continuing without it"
                  << std::endl;
        mpris_manager_.reset();
    }
    // Rebuild source manager now that both engines are known.
    playback_source_manager_ = std::make_unique<PlaybackSourceManager>(
        audio_engine_, mpris_manager_);

    // Wire external MPRIS player callbacks if available.
    if (mpris_manager_) {
        setup_mpris_callbacks();
    }

    // --- Stage 4: library scanner ---
    library_scanner_ = std::make_unique<LibraryScanner>();

    // --- Stage 5: file watcher ---
    file_watcher_ = std::make_unique<FileWatcher>();
    file_watcher_->set_event_callback([this](const FileEvent& event) {
        handle_file_event(event);
    });

    // --- Stage 5b: LibraryManager (scan coordination + unique work queue) ---
    library_manager_ = std::make_unique<LibraryManager>();
    if (!library_manager_->initialize(storage_.get(), library_scanner_.get())) {
        std::cerr << "BackendController: LibraryManager initialization failed"
                  << std::endl;
        library_manager_.reset();
    } else {
        library_manager_->set_track_found_callback([this](const Track& track) {
            if (track.file_path.empty()) return;
            std::lock_guard<std::mutex> lock(storage_mutex_);
            if (storage_ && !storage_->add_track(track)) {
                std::cerr << "BackendController: Failed to store track "
                          << track.file_path << std::endl;
            }
        });
        library_manager_->set_error_callback([](const std::string& file,
                                                 const std::string& error) {
            std::cerr << "LibraryScanner: error on " << file << ": "
                      << error << std::endl;
        });
    }

    // --- Stage 6c: Cache manager (must come before LyricsManager) ---
    cache_manager_ = std::make_unique<CacheManager>();
    if (!cache_manager_->initialize(
            "/tmp/lollypop-cpp-cache", 64ull * 1024 * 1024)) {
        std::cerr << "BackendController: CacheManager initialization failed"
                  << std::endl;
        cache_manager_.reset();
    }

    // --- Stage 5c: LyricsManager (retrieval, parse, sync) ---
    lyrics_manager_ = std::make_unique<LyricsManager>();
    if (!lyrics_manager_->initialize(
            storage_.get(), cache_manager_.get(),
            "/tmp/lollypop-cpp-cache/lyrics")) {
        std::cerr << "BackendController: LyricsManager initialization failed"
                  << std::endl;
        lyrics_manager_.reset();
    }

    // --- Stage 7: MPRIS server (Lollypop as an MPRIS player) ---
    mpris_server_ = std::make_unique<MprisServer>();
    if (!mpris_server_->initialize()) {
        std::cerr << "BackendController: MPRIS server unavailable" << std::endl;
        mpris_server_.reset();
    } else {
        setup_mpris_server_callbacks();
        mpris_server_->update_volume(volume_);
        mpris_server_->update_shuffle(shuffle);
        mpris_server_->update_loop_state(repeat_one ? 2 : (repeat ? 1 : 0));
        sync_mpris_capabilities();
    }

    // Apply queue modes.
    queue_manager_->set_shuffle(shuffle);
    queue_manager_->set_repeat(repeat_one ? RepeatMode::Track :
                                           (repeat ? RepeatMode::Playlist :
                                                    RepeatMode::None));

    // Restore saved queue on startup.
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        if (storage_) {
            if (const auto saved_queue = storage_->get_preference("player.queue")) {
                std::vector<std::string> ids;
                std::string buf;
                for (char c : *saved_queue) {
                    if (c == '\n') {
                        if (!buf.empty()) { ids.push_back(buf); buf.clear(); }
                    } else {
                        buf += c;
                    }
                }
                if (!buf.empty()) ids.push_back(buf);
                // Validate IDs exist in storage before adding
                std::vector<std::string> valid_ids;
                for (const auto& id : ids) {
                    Track t = storage_->get_track(id);
                    if (!t.id.empty()) valid_ids.push_back(id);
                }
                if (!valid_ids.empty()) {
                    queue_manager_->set_queue(valid_ids, 0);
                }
            }
            if (const auto saved_index = storage_->get_preference("player.queue_index")) {
                try {
                    size_t idx = static_cast<size_t>(std::stoul(*saved_index));
                    queue_manager_->set_current_index(idx);
                } catch (...) {
                    // Ignore corrupt preference values.
                }
            }
        }
    }

    std::cout << "BackendController: Initialized successfully" << std::endl;
    return true;
}

void BackendController::shutdown() {
    std::cout << "BackendController: Shutting down..." << std::endl;

    cancel_scan_library();
    if (scan_thread_.joinable()) scan_thread_.join();

    // Stop playback first.
    if (visualizer_engine_) {
        visualizer_engine_->stop();
    }
    // Unsubscribe from EventBus spectrum events before destroying subsystems.
    if (spectrum_subscription_id_ != 0) {
        EventBus::instance().unsubscribe(spectrum_subscription_id_);
        spectrum_subscription_id_ = 0;
    }
    if (audio_engine_) {
        audio_engine_->stop();
    }
    if (cava_visualizer_) {
        cava_visualizer_->stop();
    }
    if (cache_manager_) {
        cache_manager_->shutdown();
    }
    if (mpris_server_) {
        mpris_server_->shutdown();
    }
    if (mpris_manager_) {
        mpris_manager_->shutdown();
    }
    // Stop the file watcher before the dispatcher is destroyed.
    if (file_watcher_) {
        file_watcher_.reset();
    }
    if (library_manager_) {
        library_manager_->shutdown();
    }
    if (lyrics_manager_) {
        lyrics_manager_->shutdown();
    }
    active_scan_scanner_.reset();
    library_scanner_.reset();
    scan_dispatcher_.reset();
    file_event_dispatcher_.reset();
    storage_.reset();
    audio_engine_.reset();
}

void BackendController::set_playing(bool playing) {
    if (playing == playing_) return;
    changing_playback_state_ = true;
    playing_ = playing;
    if (playing) just_started_ = true;
    else just_started_ = false;
    if (mpris_server_) {
        if (playing) {
            mpris_server_->update_playback_status(true);
        } else {
            mpris_server_->update_playback_status(false);
        }
    }
    changing_playback_state_ = false;
    sync_mpris_capabilities();
    emit_playback_state_changed(playing);
}

void BackendController::setup_audio_callbacks() {
    // Position updates flow to UI and MPRIS server.
    audio_engine_->set_position_callback([this](double position, double duration) {
        position_ = position;
        if (duration > 0.0) duration_ = duration;
        emit_position_changed(position_, duration_);
        if (mpris_server_) {
            mpris_server_->update_position(position_);
            mpris_server_->update_duration(duration_);
        }
    });

    // Playback state changes: only PlaybackController updates playing_.
    audio_engine_->set_state_callback([this](AudioEngine::PlaybackState state) {
        // Ignore transient states right after we just started playback;
        // the GStreamer pipeline may report Stopped while transitioning
        // to Playing, which would override our playing_=true.
        if (just_started_) {
            just_started_ = false;
            return;
        }
        // Ignore transient Playing state during the PLAYING->PAUSED
        // transition triggered by pause(). Clear the flag once we reach
        // a terminal state (Paused/Stopped/Error).
        if (pausing_) {
            if (state == AudioEngine::PlaybackState::Paused ||
                state == AudioEngine::PlaybackState::Stopped ||
                state == AudioEngine::PlaybackState::Error) {
                pausing_ = false;
            }
            return;
        }
        if (state == AudioEngine::PlaybackState::Paused ||
            state == AudioEngine::PlaybackState::Buffering) {
            return;
        }
        const bool playing = (state == AudioEngine::PlaybackState::Playing);
        set_playing(playing);
        if (mpris_server_) {
            if (state == AudioEngine::PlaybackState::Stopped ||
                state == AudioEngine::PlaybackState::Error) {
                mpris_server_->update_stopped();
            } else {
                mpris_server_->update_playback_status(playing);
            }
        }
        sync_mpris_capabilities();
    });

    // End of stream - advance to next track.
    audio_engine_->set_eos_callback([this]() {
        std::cout << "BackendController: End of stream reached" << std::endl;
        if (repeat_one_) {
            position_ = 0.0;
            audio_engine_->seek(0.0);
            audio_engine_->play();
            set_playing(true);
            emit_position_changed(0.0, duration_);
            if (mpris_server_) {
                mpris_server_->update_position(0.0);
                mpris_server_->notify_seeked(0.0);
            }
        } else {
            next_track();
        }
    });

    // Audio engine errors.
    audio_engine_->set_error_callback([this](const std::string& error) {
        std::cerr << "BackendController: Audio engine error: " << error << std::endl;
        set_playing(false);
        if (mpris_server_) mpris_server_->update_stopped();
    });
}

void BackendController::setup_mpris_callbacks() {
    if (!mpris_manager_) return;

    mpris_manager_->set_playback_state_changed_callback([this](bool) {
        // External player state is informational only; do not overwrite
        // the authoritative internal playing_ unless we are in external mode.
    });

    mpris_manager_->set_position_changed_callback([this](double position, double duration) {
        position_ = position;
        duration_ = duration;
        emit_position_changed(position_, duration_);
        if (mpris_server_) {
            mpris_server_->update_position(position_);
            mpris_server_->update_duration(duration_);
        }
    });

    mpris_manager_->set_active_player_changed_callback([this](const MprisPlayerInfo& info) {
        if (mpris_server_) {
            mpris_server_->update_playback_status(info.is_playing);
        }
    });
}

void BackendController::scan_library(const std::string& path) {
    // Keep source compatibility while making the default scan nonblocking.
    start_scan_library(path);
}

bool BackendController::scan_default_music_library() {
    const std::string path = default_music_directory();
    if (path.empty()) {
        std::cerr << "BackendController: Could not resolve the Music folder"
                  << std::endl;
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        std::cerr << "BackendController: Music folder does not exist: "
                  << path;
        if (ec) std::cerr << " (" << ec.message() << ")";
        std::cerr << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        if (!storage_) return false;
        // Persist the root so later launches and file-watcher reconciliation
        // know which directories make up the library.
        storage_->add_library_root(path);
    }

    std::cout << "BackendController: Scanning Music folder " << path
              << std::endl;
    return start_scan_library(path);
}

bool BackendController::start_scan_library(const std::string& path) {
    if (!library_manager_ || !storage_ || path.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        scan_progress_ = ScanProgress{};
        pending_scan_path_ = path;
        pending_scan_result_.reset();
    }
    scan_cancel_requested_ = false;

    // Delegate to LibraryManager which runs the scan on its worker thread
    // with a unique work queue (no duplicate paths) and batch storage writes.
    library_manager_->set_scan_progress_callback([this](const ScanProgress& progress) {
        {
            std::lock_guard<std::mutex> lock(scan_mutex_);
            scan_progress_ = progress;
        }
        if (scan_dispatcher_) scan_dispatcher_->emit();
    });
    library_manager_->set_scan_finished_callback([this](bool success, bool cancelled) {
        {
            std::lock_guard<std::mutex> lock(scan_mutex_);
            pending_scan_result_ = ScanResult{success, cancelled};
            pending_scan_result_->success = success;
            pending_scan_result_->cancelled = cancelled;
        }
        if (scan_dispatcher_) scan_dispatcher_->emit();
    });

    library_manager_->scan_directory(path, true);
    return true;
}

void BackendController::cancel_scan_library() {
    scan_cancel_requested_ = true;
    if (library_scanner_) {
        library_scanner_->cancel();
    }
    if (library_manager_) {
        library_manager_->cancel();
    }
}

bool BackendController::is_scan_running() const {
    return library_manager_ && library_manager_->is_scanning();
}

void BackendController::dispatch_scan_updates() {
    ScanProgress progress;
    std::optional<ScanResult> result;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        progress = scan_progress_;
        result.swap(pending_scan_result_);
        path.swap(pending_scan_path_);
    }
    if (scan_progress_cb_) scan_progress_cb_(progress);
    if (result) {
        if (result->success && !path.empty()) {
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                if (storage_) storage_->add_library_root(path);
            }
            add_watch_directory(path);
        }
        if (result->success || result->cancelled) emit_library_changed();
        if (scan_finished_cb_) scan_finished_cb_(result->success, result->cancelled);
    }
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

    Track track;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        track = storage_->get_track_by_path(path);
    }
    if (track.id.empty()) {
        library_scanner_->scan_file(path);
        std::lock_guard<std::mutex> lock(storage_mutex_);
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

void BackendController::dispatch_lyrics_result() {
    if (!lyrics_callback_) return;
    auto cb = std::move(lyrics_callback_);
    auto result = std::move(pending_lyrics_result_);
    if (cb) cb(std::move(result));
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
            // Route through LibraryManager so scans are coordinated on its
            // worker thread and never race the scanner directly.
            if (library_manager_ && event.is_dir) {
                library_manager_->scan_directory(event.path, true);
                emit_library_changed();
            } else if (library_manager_ &&
                       LibraryScanner::is_audio_file(event.path)) {
                library_manager_->scan_file(event.path);
                emit_library_changed();
            }
            break;
        case FileEventType::Modified:
            std::cout << "BackendController: File modified: " << event.path << std::endl;
            if (library_manager_ &&
                LibraryScanner::is_audio_file(event.path)) {
                library_manager_->scan_file(event.path);
                if (current_track_.file_path == event.path) {
                    std::lock_guard<std::mutex> lock(storage_mutex_);
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
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                if (event.is_dir) storage_->delete_tracks_under_path(event.path);
                else storage_->delete_track_by_path(event.path);
            }
            if (current_is_under(event.path)) {
                stop();
            }
            emit_library_changed();
            break;
        case FileEventType::Moved:
            std::cout << "BackendController: File moved: " << event.path
                      << " -> " << event.dest_path << std::endl;
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                if (event.is_dir) storage_->delete_tracks_under_path(event.path);
                else storage_->delete_track_by_path(event.path);
            }
            if (current_is_under(event.path)) {
                stop();
            }
            if (library_manager_ && !event.dest_path.empty()) {
                if (event.is_dir) {
                    library_manager_->scan_directory(event.dest_path, true);
                } else if (LibraryScanner::is_audio_file(event.dest_path)) {
                    library_manager_->scan_file(event.dest_path);
                }
            }
            emit_library_changed();
            break;
        case FileEventType::Rescan:
            std::cout << "BackendController: Inotify overflow - full rescan needed"
                      << std::endl;
            if (library_manager_) {
                // Trigger a full rescan through the library manager's unique
                // work queue. We don't know the roots here, so we just signal
                // the library changed and the UI can initiate a rescan.
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

std::vector<Track> BackendController::get_smart_playlist_tracks(
    const std::string& playlist_id) {
    return storage_->get_smart_playlist_tracks(playlist_id);
}

std::vector<Track> BackendController::get_playlist_tracks(
    const std::string& playlist_id) {
    return storage_->get_smart_playlist_tracks(playlist_id);
}

void BackendController::create_smart_playlist(const std::string& name,
                                               const std::string& criteria) {
    if (storage_->create_smart_playlist(name, criteria)) emit_library_changed();
}

void BackendController::update_smart_playlist(const std::string& id,
                                               const std::string& name,
                                               const std::string& criteria) {
    if (storage_->update_smart_playlist(id, name, criteria)) emit_library_changed();
}

std::optional<Lyrics> BackendController::get_lyrics(const std::string& track_id) {
    return storage_->get_lyrics(track_id);
}

void BackendController::set_lyrics(const std::string& track_id,
                                   const std::string& text, bool synced,
                                   const std::string& source) {
    if (storage_->set_lyrics(track_id, text, synced, source)) emit_library_changed();
}

void BackendController::request_lyrics(
    const std::string& track_id,
    ILyricsService::LyricsCallback callback) {
    // Store the callback; the LyricsManager will call back asynchronously.
    lyrics_callback_ = std::move(callback);
    pending_lyrics_track_id_ = track_id;

    if (lyrics_manager_) {
        // Look up the track for artist/title/file_path/embedded_lyrics.
        Track track = storage_->get_track(track_id);
        std::string track_id_copy = track_id;
        // Request lyrics asynchronously from the lyrics manager.
        // Pass file_path and embedded_lyrics for local/embedded lookup.
        lyrics_manager_->request_lyrics(
            track_id,
            track.artist,
            track.title,
            track.file_path,
            track.embedded_lyrics,
            [this, track_id_copy](const std::string& fetched_id,
                                  const Lyrics& lyrics) {
                if (storage_) {
                    storage_->set_lyrics(fetched_id, lyrics.text, lyrics.synced,
                                         lyrics.source);
                }
                pending_lyrics_result_ = lyrics;
                if (lyrics_dispatcher_) lyrics_dispatcher_->emit();
            });
    } else {
        // No lyrics manager - return empty result via dispatcher.
        pending_lyrics_result_ = Lyrics{track_id, "", false, "", 0};
        if (lyrics_dispatcher_) lyrics_dispatcher_->emit();
    }
}

std::vector<std::string> BackendController::get_all_genres() {
    return storage_->get_all_genres();
}

std::vector<Track> BackendController::get_tracks_by_genre(
    const std::string& genre) {
    return storage_->get_tracks_by_genre(genre);
}

std::string BackendController::get_artwork_path_for_track(
    const std::string& track_id) {
    auto track = storage_->get_track(track_id);
    return track.artwork_path;
}

std::string BackendController::get_artwork_path_for_album(
    const std::string& album_id) {
    auto album = storage_->get_album(album_id);
    return album.artwork_path;
}

std::string BackendController::get_artwork_path_for_artist(
    const std::string& artist_id) {
    auto artist = storage_->get_artist(artist_id);
    return artist.artwork_path;
}

std::vector<Album> BackendController::get_favourite_albums() {
    auto all = storage_->get_all_albums();
    std::vector<Album> favourites;
    for (const auto& album : all) {
        if (album.favorite) favourites.push_back(album);
    }
    return favourites;
}

bool BackendController::is_track_liked(const std::string& track_id) {
    auto track = storage_->get_track(track_id);
    return track.liked;
}

bool BackendController::is_album_favorited(const std::string& album_id) {
    auto album = storage_->get_album(album_id);
    return album.favorite;
}

std::vector<std::string> BackendController::get_sorted_track_ids(
    const std::string& sort_column, bool ascending, const std::string& genre_filter) {
    // Use keyset pagination storage method for database-backed global sorting.
    // We fetch a large page (all IDs) to support global sort order.
    // For very large libraries, callers should use keyset pagination directly.
    // If genre_filter is provided, filter by genre.
    auto tracks = storage_->get_tracks_paginated(
        10000, sort_column, "", "", genre_filter);
    std::vector<std::string> ids;
    ids.reserve(tracks.size());
    for (const auto& t : tracks) ids.push_back(t.id);
    return ids;
}

std::vector<Track> BackendController::get_tracks_by_ids(
    const std::vector<std::string>& track_ids) {
    std::vector<Track> tracks;
    tracks.reserve(track_ids.size());
    for (const auto& id : track_ids) {
        auto t = storage_->get_track(id);
        if (!t.id.empty()) tracks.push_back(std::move(t));
    }
    return tracks;
}

void BackendController::play_track(const std::string& track_id) {
    std::cerr << "BackendController: play_track(" << track_id << ")" << std::endl;
    play_tracks({track_id}, 0);
}

void BackendController::play_tracks(const std::vector<std::string>& track_ids, size_t start_index) {
    std::cerr << "BackendController: play_tracks(" << track_ids.size() << " tracks, start=" << start_index << ")" << std::endl;
    if (track_ids.empty()) return;

    queue_manager_->set_queue(track_ids, start_index);
    persist_queue();
    // set_queue() clamps start_index into range; use the clamped cursor so
    // start_queue_index() validates the value that was actually set.
    start_queue_index(queue_manager_->current_index());
}

bool BackendController::start_queue_index(size_t index) {
    if (!queue_manager_->validate_index(index)) return false;

    // Sync the queue manager cursor to the requested index.
    queue_manager_->set_current_index(index);

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        current_track_ = storage_->get_track(queue_manager_->queue()[index]);
    }
    duration_ = current_track_.duration;
    position_ = 0.0;

    if (current_track_.id.empty()) {
        std::cerr << "BackendController: Track not found in queue" << std::endl;
    } else if (current_track_.file_path.empty()) {
        std::cerr << "BackendController: Track has no file path: "
                  << current_track_.title << std::endl;
    } else {
        std::cerr << "BackendController: Loading track: " << current_track_.file_path << std::endl;
        if (audio_engine_->load_track(current_track_.file_path)) {
            std::cerr << "BackendController: Track loaded, playing..." << std::endl;
            audio_engine_->set_volume(volume_);
            audio_engine_->play();
            set_playing(true);
            update_play_statistics();
        } else {
            std::cerr << "BackendController: Failed to load track: "
                      << current_track_.file_path << std::endl;
        }
    }

    emit_track_changed(current_track_);
    emit_position_changed(0.0, duration_);
    // playing_ already emitted via set_playing above.
    emit_queue_changed();

    if (mpris_server_) {
        mpris_server_->update_track(current_track_,
                                    std::to_string(queue_manager_->current_index()));
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
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->update_play_count(current_track_.id);
        storage_->update_last_played(current_track_.id);
    }
    current_track_.play_count++;
    current_track_.last_played = static_cast<int64_t>(std::time(nullptr));
}

void BackendController::toggle_play_pause() {
    if (changing_playback_state_) {
        return;
    }
    if (playing_) {
        pause();
        return;
    }

    if (current_track_.id.empty()) {
        if (!queue_manager_->queue().empty()) start_queue_index(queue_manager_->current_index());
        return;
    }

    const auto state = audio_engine_->get_state();
    if (state == AudioEngine::PlaybackState::Stopped ||
        state == AudioEngine::PlaybackState::Error) {
        start_queue_index(queue_manager_->current_index());
        return;
    }

    audio_engine_->play();
    set_playing(true);
    if (mpris_server_) mpris_server_->update_playback_status(true);
    sync_mpris_capabilities();
}

void BackendController::pause() {
    pausing_ = true;
    set_playing(false);
    audio_engine_->pause();
    sync_mpris_capabilities();
}

void BackendController::stop() {
    position_ = 0.0;
    current_track_ = Track{};
    audio_engine_->stop();
    set_playing(false);
    just_started_ = false;
    pausing_ = false;
    emit_track_changed(current_track_);
    emit_position_changed(0, 0);
    if (mpris_server_) {
        mpris_server_->update_stopped();
        mpris_server_->update_track(Track{});
    }
    sync_mpris_capabilities();
}

void BackendController::next_track() {
    auto next_idx = queue_manager_->next_index();
    if (!next_idx.has_value()) {
        // End of queue with repeat disabled.
        stop();
        return;
    }
    start_queue_index(*next_idx);
}

void BackendController::previous_track() {
    if (position_ > 3.0) {
        // Restart current track if more than 3 seconds in.
        seek(0.0);
        return;
    }

    auto prev_idx = queue_manager_->previous_index();
    if (!prev_idx.has_value()) {
        // Already at the first track: restart it.
        seek(0.0);
        return;
    }
    start_queue_index(*prev_idx);
}

void BackendController::seek(double position_seconds) {
    position_ = std::clamp(position_seconds, 0.0, duration_);
    playback_source_manager_->seek(position_);
    emit_position_changed(position_, duration_);
    if (mpris_server_) {
        mpris_server_->update_position(position_);
        mpris_server_->notify_seeked(position_);
    }
}

void BackendController::set_volume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
    playback_source_manager_->set_volume(volume_);
    if (storage_) {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->set_preference("player.volume", std::to_string(volume_));
    }
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
    const auto& dq = queue_manager_->queue();
    return std::vector<std::string>(dq.begin(), dq.end());
}

size_t BackendController::get_queue_index() const {
    return queue_manager_->current_index();
}

void BackendController::persist_queue() {
    if (!storage_) return;
    std::string encoded;
    for (const auto& id : queue_manager_->queue()) {
        if (id.find_first_of("\n\r") != std::string::npos) continue;
        if (!encoded.empty()) encoded += '\n';
        encoded += id;
    }
    std::lock_guard<std::mutex> lock(storage_mutex_);
    storage_->set_preference("player.queue", encoded);
    storage_->set_preference("player.queue_index",
                             std::to_string(queue_manager_->current_index()));
}

bool BackendController::enqueue_track(const std::string& track_id, bool next) {
    if (track_id.empty()) return false;
    Track track;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        track = storage_->get_track(track_id);
    }
    if (track.id.empty()) return false;
    if (!queue_manager_->enqueue(track_id, next)) return false;
    persist_queue();
    emit_queue_changed();
    return true;
}

bool BackendController::remove_from_queue(size_t index) {
    if (!queue_manager_->remove(index)) return false;
    if (queue_manager_->is_empty()) {
        stop();
    } else if (index == queue_manager_->current_index()) {
        start_queue_index(queue_manager_->current_index());
    }
    persist_queue();
    emit_queue_changed();
    sync_mpris_capabilities();
    return true;
}

bool BackendController::move_queue_item(size_t from, size_t to) {
    if (!queue_manager_->move(from, to)) return false;
    persist_queue();
    emit_queue_changed();
    sync_mpris_capabilities();
    return true;
}

bool BackendController::play_queue_index(size_t index) {
    return start_queue_index(index);
}

void BackendController::clear_queue() {
    queue_manager_->clear();
    persist_queue();
    stop();
    emit_queue_changed();
}

void BackendController::set_playback_source_internal() {
    playback_source_manager_->set_source(PlaybackSource::Internal);
}

void BackendController::set_playback_source_external() {
    playback_source_manager_->set_source(PlaybackSource::External);
}

bool BackendController::is_playback_source_internal() const {
    return playback_source_manager_->source() == PlaybackSource::Internal;
}

void BackendController::set_shuffle(bool enabled) {
    queue_manager_->set_shuffle(enabled);
    if (storage_) {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->set_preference("player.shuffle", enabled ? "1" : "0");
    }
    if (mpris_server_) {
        mpris_server_->update_shuffle(enabled);
    }
    sync_mpris_capabilities();
}

void BackendController::set_repeat(bool enabled) {
    queue_manager_->set_repeat(enabled ? RepeatMode::Playlist :
                                         RepeatMode::None);
    if (enabled) {
        repeat_one_ = false;
    }
    if (storage_) {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->set_preference("player.repeat", enabled ? "1" : "0");
        storage_->set_preference("player.repeat_one",
                                 queue_manager_->repeat_mode() ==
                                     RepeatMode::Track ? "1" : "0");
    }
    if (mpris_server_) {
        mpris_server_->update_loop_state(enabled ? 1 : 0);
    }
    sync_mpris_capabilities();
}

void BackendController::set_repeat_one(bool enabled) {
    queue_manager_->set_repeat(enabled ? RepeatMode::Track :
                                         RepeatMode::None);
    repeat_one_ = enabled;
    if (storage_) {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->set_preference("player.repeat",
                                 queue_manager_->repeat_mode() ==
                                     RepeatMode::Playlist ? "1" : "0");
        storage_->set_preference("player.repeat_one", enabled ? "1" : "0");
    }
    if (mpris_server_) {
        mpris_server_->update_loop_state(enabled ? 2 : 0);
    }
    sync_mpris_capabilities();
}

bool BackendController::get_shuffle() const {
    return queue_manager_->shuffle();
}

bool BackendController::get_repeat() const {
    return queue_manager_->repeat_mode() == RepeatMode::Playlist;
}

bool BackendController::get_repeat_one() const {
    return queue_manager_->repeat_mode() == RepeatMode::Track;
}

bool BackendController::can_seek() const {
    if (!audio_engine_) return false;
    return audio_engine_->can_seek();
}

void BackendController::toggle_like(const std::string& track_id) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    storage_->toggle_like(track_id);
}

void BackendController::toggle_favorite_album(const std::string& album_id) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    storage_->toggle_favorite_album(album_id);
}

void BackendController::sync_mpris_capabilities() {
    if (!mpris_server_) return;

    const bool has_track = !current_track_.id.empty();
    const bool has_queue = !queue_manager_->queue().empty();
    const bool can_play = has_track || has_queue;
    const bool can_go_next =
        has_queue && (queue_manager_->shuffle() ||
                      queue_manager_->repeat_mode() != RepeatMode::None ||
                      queue_manager_->current_index() + 1 < queue_manager_->queue().size());
    const bool can_go_previous =
        has_queue && (queue_manager_->shuffle() ||
                      queue_manager_->repeat_mode() != RepeatMode::None ||
                      queue_manager_->current_index() > 0);
    mpris_server_->update_capabilities(can_go_next, can_go_previous, can_play,
                                       has_track, has_track && duration_ > 0);
}

void BackendController::setup_cava_callbacks() {
    cava_visualizer_->set_points_callback([this](const std::vector<float>& points) {
        emit_visualization_changed(points);
    });
}

void BackendController::setup_mpris_server_callbacks() {
    // Remote MPRIS client (media keys, sound menu) wants to play/pause
    mpris_server_->set_playback_state_changed_callback([this](bool playing) {
        if (playing) {
            // MPRIS Play
            if (playing_) {
                // Already playing: just ensure the engine is running.
                audio_engine_->play();
                return;
            }
            if (current_track_.id.empty()) {
                if (!queue_manager_->queue().empty()) {
                    start_queue_index(queue_manager_->current_index());
                }
                return;
            }
            if (audio_engine_->get_state() == AudioEngine::PlaybackState::Stopped ||
                audio_engine_->get_state() == AudioEngine::PlaybackState::Error) {
                start_queue_index(queue_manager_->current_index());
                return;
            }
            audio_engine_->play();
            set_playing(true);
        } else {
            // MPRIS Pause
            pause();
        }
    });
    mpris_server_->set_play_pause_callback([this] { toggle_play_pause(); });
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

void BackendController::emit_track_changed(const Track& track) {
    for (auto& cb : track_changed_callbacks_) {
        if (cb) cb(track);
    }
}

void BackendController::emit_position_changed(double position, double duration) {
    for (auto& cb : position_changed_callbacks_) {
        if (cb) cb(position, duration);
    }
}

void BackendController::emit_playback_state_changed(bool playing) {
    for (auto& cb : playback_state_changed_callbacks_) {
        if (cb) cb(playing);
    }
}

void BackendController::emit_queue_changed() {
    const auto& dq = queue_manager_->queue();
    std::vector<std::string> queue_ids(dq.begin(), dq.end());
    for (auto& cb : queue_changed_callbacks_) {
        if (cb) cb(queue_ids);
    }
}

void BackendController::emit_visualization_changed(const std::vector<float>& points) {
    for (auto& cb : visualization_callbacks_) {
        if (cb) cb(points);
    }
}

void BackendController::emit_library_changed() {
    for (auto& cb : library_changed_callbacks_) {
        if (cb) cb();
    }
}

} // namespace lollypop

