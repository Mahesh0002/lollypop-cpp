/**
 * Lollypop C++ - Backend Controller Implementation
 */

#include "controller.hpp"
#include "manager.hpp"
#include "event_bus.hpp"
#include "external_providers.hpp"
#include <json.hpp>

#include <glib.h>
#include <glibmm/convert.h>
#include <glibmm/dispatcher.h>

#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <condition_variable>
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

using json = nlohmann::json;

std::string metadata_cache_path() {
    const char* home = g_get_home_dir();
    const std::string base = home ? std::string(home) +
                                        "/.local/share/lollypop-cpp"
                                  : std::string("/tmp/lollypop-cpp");
    return base + "/metadata_cache.json";
}

template <typename T>
T json_value(const json& object, const char* key, T fallback) {
    return object.contains(key) ? object.at(key).get<T>() : fallback;
}

json track_to_json(const Track& track) {
    return {
        {"id", track.id},
        {"title", track.title},
        {"artist", track.artist},
        {"album", track.album},
        {"album_artist", track.album_artist},
        {"genre", track.genre},
        {"track_number", track.track_number},
        {"disc_number", track.disc_number},
        {"year", track.year},
        {"duration", track.duration},
        {"file_path", track.file_path},
        {"artwork_path", track.artwork_path},
        {"embedded_lyrics", track.embedded_lyrics},
        {"liked", track.liked},
        {"play_count", track.play_count},
        {"last_played", track.last_played},
        {"date_added", track.date_added},
    };
}

Track track_from_json(const json& value) {
    Track track;
    track.id = json_value<std::string>(value, "id", {});
    track.title = json_value<std::string>(value, "title", {});
    track.artist = json_value<std::string>(value, "artist", {});
    track.album = json_value<std::string>(value, "album", {});
    track.album_artist = json_value<std::string>(value, "album_artist", {});
    track.genre = json_value<std::string>(value, "genre", {});
    track.track_number = json_value<int>(value, "track_number", 0);
    track.disc_number = json_value<int>(value, "disc_number", 0);
    track.year = json_value<int>(value, "year", 0);
    track.duration = json_value<int>(value, "duration", 0);
    track.file_path = json_value<std::string>(value, "file_path", {});
    track.artwork_path = json_value<std::string>(value, "artwork_path", {});
    track.embedded_lyrics = json_value<std::string>(value, "embedded_lyrics", {});
    track.liked = json_value<bool>(value, "liked", false);
    track.play_count = json_value<int>(value, "play_count", 0);
    track.last_played = json_value<int64_t>(value, "last_played", 0);
    track.date_added = json_value<int64_t>(value, "date_added", 0);
    return track;
}

json album_to_json(const Album& album) {
    return {
        {"id", album.id},
        {"title", album.title},
        {"artist", album.artist},
        {"year", album.year},
        {"artwork_path", album.artwork_path},
        {"favorite", album.favorite},
        {"date_added", album.date_added},
        {"track_ids", album.track_ids},
    };
}

Album album_from_json(const json& value) {
    Album album;
    album.id = json_value<std::string>(value, "id", {});
    album.title = json_value<std::string>(value, "title", {});
    album.artist = json_value<std::string>(value, "artist", {});
    album.year = json_value<int>(value, "year", 0);
    album.artwork_path = json_value<std::string>(value, "artwork_path", {});
    album.favorite = json_value<bool>(value, "favorite", false);
    album.date_added = json_value<int64_t>(value, "date_added", 0);
    album.track_ids = json_value<std::vector<std::string>>(value, "track_ids", {});
    return album;
}

json artist_to_json(const Artist& artist) {
    return {
        {"id", artist.id},
        {"name", artist.name},
        {"artwork_path", artist.artwork_path},
        {"date_added", artist.date_added},
        {"album_ids", artist.album_ids},
    };
}

Artist artist_from_json(const json& value) {
    Artist artist;
    artist.id = json_value<std::string>(value, "id", {});
    artist.name = json_value<std::string>(value, "name", {});
    artist.artwork_path = json_value<std::string>(value, "artwork_path", {});
    artist.date_added = json_value<int64_t>(value, "date_added", 0);
    artist.album_ids = json_value<std::vector<std::string>>(value, "album_ids", {});
    return artist;
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

    // Populate read models from the last known-good snapshot before any page
    // asks for them. A fingerprint mismatch simply falls back to SQLite.
    load_metadata_cache();

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

    // Expose a working internal source immediately. Deferred initialization
    // replaces this router once MPRIS becomes available.
    playback_source_manager_ = std::make_unique<PlaybackSourceManager>(
        audio_engine_, nullptr);

    // --- Stage 2b: visualizer engine ---
    visualizer_engine_ = std::make_unique<VisualizerEngine>();
    visualizer_engine_->set_audio_engine(audio_engine_.get());
    visualizer_engine_->set_band_count(64);
    visualizer_engine_->set_enabled(true);
    visualizer_engine_->start();

    // Subscribe to spectrum events on EventBus (published by VisualizerEngine).
    // The controller can re-publish or forward to its own visualization callbacks.
    spectrum_subscription_id_ = EventBus::instance().subscribe("spectrum",
        [this](const Event& event) {
            if (auto* frame = std::get_if<SpectrumFrame>(&event.payload)) {
                emit_visualization_changed(frame->magnitudes);
            }
        });

    // --- Stage 3: library scanner ---
    library_scanner_ = std::make_unique<LibraryScanner>();
    {
        const char* home_dir = g_get_home_dir();
        const std::string cache_root = home_dir
            ? std::string(home_dir) + "/.local/share/lollypop-cpp"
            : std::string("/tmp/lollypop-cpp");
        std::error_code ec;
        std::filesystem::create_directories(cache_root, ec);
        library_scanner_->set_incremental_cache_path(
            cache_root + "/scan_cache.bin");
    }

    // --- Stage 4: file watcher ---
    file_watcher_ = std::make_unique<FileWatcher>();
    file_watcher_->set_event_callback([this](const FileEvent& event) {
        handle_file_event(event);
    });

    // --- Stage 5: LibraryManager (scan coordination + unique work queue) ---
    library_manager_ = std::make_unique<LibraryManager>();
    if (!library_manager_->initialize(storage_.get(), library_scanner_.get())) {
        std::cerr << "BackendController: LibraryManager initialization failed"
                  << std::endl;
        library_manager_.reset();
    } else {
        library_manager_->set_track_found_with_cache_callback(
            [this](const Track& track, bool from_cache) {
            if (track.file_path.empty()) return;
            std::lock_guard<std::mutex> lock(storage_mutex_);

            // Restored metadata is only used to ensure the row still exists.
            // Re-upserting it would erase enrichment (genre, year, artwork)
            // written after the original scan.
            if (from_cache) {
                if (storage_ &&
                    storage_->get_track_by_path(track.file_path).id.empty()) {
                    storage_->add_track(track);
                }
                return;
            }

            if (storage_ && !storage_->add_track(track)) {
                std::cerr << "BackendController: Failed to store track "
                          << track.file_path << std::endl;
            }

            // Enrich missing metadata from iTunes in the background (non-blocking).
            // Fills genre, year, track/disc number, and artwork when tags are absent.
            if (storage_) {
                spawn_background_worker([this, track](const WorkerAliveToken& alive) {
                    if (!alive->load()) return;
                    auto meta = fetch_itunes_metadata(track.artist, track.title,
                                                      track.album);
                    if (!alive->load() || !meta) return;

                    Track updated = track;
                    bool changed = false;

                    if (updated.genre.empty() && !meta->genre.empty()) {
                        updated.genre = meta->genre;
                        changed = true;
                    }
                    if (updated.year == 0 && meta->year != 0) {
                        updated.year = meta->year;
                        changed = true;
                    }
                    if (updated.track_number == 0 && meta->track_number != 0) {
                        updated.track_number = meta->track_number;
                        changed = true;
                    }
                    if (updated.disc_number == 0 && meta->disc_number != 0) {
                        updated.disc_number = meta->disc_number;
                        changed = true;
                    }
                    // Use iTunes high-quality artwork if the track has none.
                    if (updated.artwork_path.empty() && !meta->artwork_url.empty()) {
                        std::string cache_dir;
                        if (const char* home = std::getenv("HOME")) {
                            cache_dir =
                                std::string(home) +
                                "/.local/share/lollypop-cpp/artwork";
                        } else {
                            cache_dir = "/tmp/lollypop-cpp/artwork";
                        }
                        std::string key = "itunes_" + track.id;
                        std::replace(key.begin(), key.end(), '/', '_');
                        std::replace(key.begin(), key.end(), ':', '_');
                        std::string art_path =
                            iTunesProvider::download_artwork(
                                meta->artwork_url, cache_dir, key);
                        if (!art_path.empty()) {
                            updated.artwork_path = art_path;
                            changed = true;
                        }
                    }

                    if (changed && alive->load()) {
                        std::lock_guard<std::mutex> storage_lock(storage_mutex_);
                        if (!alive->load() || !storage_) return;
                        // insert_track upserts on file_path, so re-adding enriches
                        // the existing row while preserving liked/play counts.
                        storage_->add_track(updated);
                    }
                });
            }
        });
        library_manager_->set_error_callback([](const std::string& file,
                                                 const std::string& error) {
            std::cerr << "LibraryScanner: error on " << file << ": "
                      << error << std::endl;
        });

        // Enable periodic incremental scanning (every 30 minutes by default)
        library_manager_->set_periodic_scan_enabled(true);
        library_manager_->set_periodic_scan_interval(std::chrono::minutes(30));
    }

    // Apply queue modes. Deferred services below read these from the manager.
    queue_manager_->set_shuffle(shuffle);
    queue_manager_->set_repeat(repeat_one ? RepeatMode::Track :
                                           (repeat ? RepeatMode::Playlist :
                                                    RepeatMode::None));

    // MPRIS, cache, lyrics, the MPRIS server, and queue restoration are not
    // needed for the first frame. Schedule them after the main loop starts.
    schedule_deferred_initialization();

    std::cout << "BackendController: Initialized successfully" << std::endl;
    return true;
}

void BackendController::schedule_deferred_initialization() {
    cancel_deferred_initialization();
    startup_alive_ = std::make_shared<bool>(true);
    auto* context = new DeferredInitContext{this, startup_alive_};
    startup_idle_context_ = context;
    startup_idle_id_ = g_idle_add_full(
        G_PRIORITY_LOW,
        &BackendController::deferred_init_idle,
        context,
        &BackendController::deferred_init_destroy);
}

void BackendController::cancel_deferred_initialization() {
    if (startup_alive_) *startup_alive_ = false;
    if (startup_idle_id_ != 0) {
        g_source_remove(startup_idle_id_);
        startup_idle_id_ = 0;
    }
    // The destroy notify owns the context and clears startup_idle_context_.
}

void BackendController::deferred_init_destroy(gpointer data) {
    auto* context = static_cast<DeferredInitContext*>(data);
    if (!context) return;
    if (context->self) {
        context->self->startup_idle_id_ = 0;
        if (context->self->startup_idle_context_ == context) {
            context->self->startup_idle_context_ = nullptr;
        }
    }
    delete context;
}

gboolean BackendController::deferred_init_idle(gpointer data) {
    auto* context = static_cast<DeferredInitContext*>(data);
    if (!context || !context->self || !context->alive || !*context->alive) {
        return G_SOURCE_REMOVE;
    }
    context->self->initialize_deferred_services();
    return G_SOURCE_REMOVE;
}

gboolean BackendController::seek_debounce_timeout(gpointer data) {
    auto* self = static_cast<BackendController*>(data);
    if (!self) return G_SOURCE_REMOVE;

    double seek_pos = self->pending_seek_position_;

    self->seek_debounce_timer_id_ = 0;
    self->seek_pending_ = false;

    // Commit the seek. The position_ was already updated immediately for
    // UI responsiveness during scrubbing.
    if (self->playback_source_manager_ && self->audio_engine_) {
        self->playback_source_manager_->seek(seek_pos);
    }
    return G_SOURCE_REMOVE;
}

void BackendController::initialize_deferred_services() {
    if (!storage_ || !audio_engine_) return;

    // External MPRIS players and source routing.
    mpris_manager_ = std::make_shared<MprisManager>();
    if (!mpris_manager_->initialize()) {
        std::cerr << "BackendController: MPRIS unavailable, continuing without it"
                  << std::endl;
        mpris_manager_.reset();
    }
    playback_source_manager_ = std::make_unique<PlaybackSourceManager>(
        audio_engine_, mpris_manager_);
    if (mpris_manager_) setup_mpris_callbacks();

    // Cache and lyrics. Cache must exist before LyricsManager.
    cache_manager_ = std::make_unique<CacheManager>();
    if (!cache_manager_->initialize(
            "/tmp/lollypop-cpp-cache", 64ull * 1024 * 1024)) {
        std::cerr << "BackendController: CacheManager initialization failed"
                  << std::endl;
        cache_manager_.reset();
    }

    // Artwork manager (uses same cache)
    artwork_manager_ = std::make_unique<ArtworkManager>();
    if (!artwork_manager_->initialize(
            "/tmp/lollypop-cpp-cache/artwork", 64ull * 1024 * 1024)) {
        std::cerr << "BackendController: ArtworkManager initialization failed"
                  << std::endl;
        artwork_manager_.reset();
    } else {
        // Set up external artwork fetching
        artwork_manager_->set_extract_fn(make_artwork_extract_fn());
    }

    // Refresh stored album artwork once using the 1200px iTunes images.
    refresh_album_artwork_from_itunes();

    lyrics_manager_ = std::make_unique<LyricsManager>();
    if (!lyrics_manager_->initialize(
            storage_.get(), cache_manager_.get(),
            "/tmp/lollypop-cpp-cache/lyrics")) {
        std::cerr << "BackendController: LyricsManager initialization failed"
                  << std::endl;
        lyrics_manager_.reset();
    } else {
        // Set up external lyrics fetching (LRCLIB)
        lyrics_manager_->set_fetch_fn(make_lyrics_fetch_fn());
    }

    // Export Lollypop as an MPRIS player.
    mpris_server_ = std::make_unique<MprisServer>();
    if (!mpris_server_->initialize()) {
        std::cerr << "BackendController: MPRIS server unavailable" << std::endl;
        mpris_server_.reset();
    } else {
        setup_mpris_server_callbacks();
        mpris_server_->update_volume(volume_);
        mpris_server_->update_shuffle(get_shuffle());
        mpris_server_->update_loop_state(
            get_repeat_one() ? 2 : (get_repeat() ? 1 : 0));
        sync_mpris_capabilities();
    }

    // CAVA visualizer for external/system audio capture (optional).
    cava_visualizer_ = std::make_unique<CavaVisualizer>();
    if (cava_visualizer_->start()) {
        cava_visualizer_->set_points_callback([this](const std::vector<float>& points) {
            emit_visualization_changed(points);
        });
    } else {
        std::cerr << "BackendController: CAVA visualizer unavailable, using internal visualizer only"
                  << std::endl;
        cava_visualizer_.reset();
    }

    // Warm the read models and write a snapshot for the next launch. This runs
    // on a low-priority idle callback, after the first frame is on screen.
    preload_common_queries();
    save_metadata_cache();
    // Backfill artist artwork from local folders/album covers. This is fast
    // and offline; the explicit fetch_artist_artwork() API remains the
    // network-backed path.
    fetch_missing_artist_artwork();

    restore_saved_queue();
}

void BackendController::restore_saved_queue() {
    std::vector<std::string> valid_ids;
    size_t restored_index = 0;

    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        if (!storage_) return;

        const auto saved_queue = storage_->get_preference("player.queue");
        if (!saved_queue) return;

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
        if (ids.empty()) return;

        size_t requested_index = 0;
        if (const auto saved_index = storage_->get_preference("player.queue_index")) {
            try {
                requested_index = static_cast<size_t>(std::stoul(*saved_index));
            } catch (...) {
                // Ignore corrupt preference values.
            }
        }

        // One batched lookup instead of one query per saved track ID.
        const auto tracks = storage_->get_tracks_by_ids(ids);
        size_t matched = 0;
        for (size_t i = 0; i < ids.size() && matched < tracks.size(); ++i) {
            if (tracks[matched].id != ids[i]) continue;
            if (i < requested_index) ++restored_index;
            valid_ids.push_back(ids[i]);
            ++matched;
        }
    }

    if (valid_ids.empty()) return;
    if (restored_index >= valid_ids.size()) {
        restored_index = valid_ids.size() - 1;
    }
    queue_manager_->set_queue(valid_ids, restored_index);
    emit_queue_changed();
}

void BackendController::shutdown() {
    if (shutdown_complete_) return;
    shutdown_complete_ = true;
    std::cout << "BackendController: Shutting down..." << std::endl;

    // Ask every ad-hoc worker to finish before tearing down their dependencies.
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        if (workers_alive_) workers_alive_->store(false);
    }

    // Detach callbacks that capture this before any subsystem can invoke them.
    if (audio_engine_) {
        audio_engine_->set_position_callback(nullptr);
        audio_engine_->set_state_callback(nullptr);
        audio_engine_->set_eos_callback(nullptr);
        audio_engine_->set_error_callback(nullptr);
    }
    if (mpris_server_) {
        mpris_server_->set_playback_state_changed_callback(nullptr);
        mpris_server_->set_track_changed_callback(nullptr);
        mpris_server_->set_seek_callback(nullptr);
        mpris_server_->set_set_position_callback(nullptr);
        mpris_server_->set_volume_changed_callback(nullptr);
        mpris_server_->set_loop_state_changed_callback(nullptr);
        mpris_server_->set_shuffle_changed_callback(nullptr);
        mpris_server_->set_next_callback(nullptr);
        mpris_server_->set_previous_callback(nullptr);
        mpris_server_->set_play_pause_callback(nullptr);
        mpris_server_->set_stop_callback(nullptr);
        mpris_server_->set_open_uri_callback(nullptr);
        mpris_server_->set_raise_callback(nullptr);
    }
    if (mpris_manager_) {
        mpris_manager_->set_active_player_changed_callback(nullptr);
        mpris_manager_->set_playback_state_changed_callback(nullptr);
        mpris_manager_->set_position_changed_callback(nullptr);
    }
    if (library_manager_) {
        library_manager_->set_track_found_callback(nullptr);
        library_manager_->set_track_found_with_cache_callback(nullptr);
        library_manager_->set_scan_progress_callback(nullptr);
        library_manager_->set_scan_finished_callback(nullptr);
        library_manager_->set_error_callback(nullptr);
    }
    if (file_watcher_) file_watcher_->set_event_callback(nullptr);
    if (cava_visualizer_) cava_visualizer_->set_points_callback(nullptr);

    cancel_deferred_initialization();
    cancel_scan_library();
    if (scan_thread_.joinable()) scan_thread_.join();
    join_background_workers();

    // Cancel any pending seek debounce timer.
    if (seek_debounce_timer_id_ != 0) {
        g_source_remove(seek_debounce_timer_id_);
        seek_debounce_timer_id_ = 0;
    }

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
    if (artwork_manager_) {
        artwork_manager_->shutdown();
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
        if (duration > 0.0) duration_ = duration;
        if (!seek_pending_) {
            position_ = position;
        }
        // While a scrub is being debounced, keep reporting the requested
        // position instead of the stale pipeline position.
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
        // Attempt error recovery by rebuilding the pipeline and reloading the track.
        if (audio_engine_->recover_from_error()) {
            std::cerr << "BackendController: Error recovery succeeded, resuming playback" << std::endl;
            audio_engine_->set_volume(volume_);
            audio_engine_->play();
            set_playing(true);
            if (mpris_server_) {
                mpris_server_->update_playback_status(true);
                mpris_server_->update_track(current_track_);
            }
        } else {
            std::cerr << "BackendController: Error recovery failed" << std::endl;
            set_playing(false);
            if (mpris_server_) mpris_server_->update_stopped();
        }
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
        if (success && storage_) {
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                storage_->rebuild_smart_playlists();
            }
            // Refresh the read models and metadata snapshot off the UI thread.
            preload_common_queries();
            save_metadata_cache();
            // New artists may have arrived with no artwork path; backfill from
            // local folder/album art without making the scan wait on network.
            fetch_missing_artist_artwork();
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
    if (library_manager_) {
        library_manager_->add_library_root(path);
    }
}

void BackendController::remove_watch_directory(const std::string& path) {
    if (!file_watcher_) return;
    file_watcher_->remove_watch(path);
    if (library_manager_) {
        library_manager_->remove_library_root(path);
    }
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
    std::vector<PendingLyricsDispatch> dispatches;
    {
        std::lock_guard<std::mutex> lock(pending_lyrics_mutex_);
        dispatches.swap(pending_lyrics_dispatches_);
    }
    for (auto& pending : dispatches) {
        if (!pending.callback) continue;
        try {
            pending.callback(pending.track_id, pending.result);
        } catch (const std::exception& e) {
            std::cerr << "BackendController: lyrics callback threw: "
                      << e.what() << std::endl;
        } catch (...) {
            std::cerr << "BackendController: lyrics callback threw unknown exception"
                      << std::endl;
        }
    }
}

void BackendController::spawn_background_worker(
    std::function<void(const WorkerAliveToken&)> work) {
    const auto alive = workers_alive_;
    if (!alive || !alive->load()) return;

    std::lock_guard<std::mutex> lock(workers_mutex_);
    if (!alive->load()) return;
    background_workers_.emplace_back([alive, work = std::move(work)]() mutable {
        try {
            work(alive);
        } catch (const std::exception& e) {
            std::cerr << "BackendController: background worker threw: "
                      << e.what() << std::endl;
        } catch (...) {
            std::cerr << "BackendController: background worker threw unknown exception"
                      << std::endl;
        }
    });
}

void BackendController::join_background_workers() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers.swap(background_workers_);
    }
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
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
    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    if (!tracks_cache_valid_) {
        tracks_cache_ = storage_->get_all_tracks();
        tracks_cache_valid_ = true;
    }
    return tracks_cache_;
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
    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    if (!albums_cache_valid_) {
        albums_cache_ = storage_->get_all_albums();
        albums_cache_valid_ = true;
    }
    return albums_cache_;
}

std::vector<Album> BackendController::get_albums_by_artist(const std::string& artist_id) {
    return storage_->get_albums_by_artist(artist_id);
}

Album BackendController::get_album(const std::string& album_id) {
    return storage_->get_album(album_id);
}

std::vector<Artist> BackendController::get_all_artists() {
    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    if (!artists_cache_valid_) {
        artists_cache_ = storage_->get_all_artists();
        artists_cache_valid_ = true;
    }
    return artists_cache_;
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
    invalidate_read_caches();
}

void BackendController::delete_playlist(const std::string& playlist_id) {
    storage_->delete_playlist(playlist_id);
    invalidate_read_caches();
}

void BackendController::add_track_to_playlist(const std::string& playlist_id, const std::string& track_id) {
    storage_->add_track_to_playlist(playlist_id, track_id);
    invalidate_read_caches();
}

void BackendController::remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id) {
    storage_->remove_track_from_playlist(playlist_id, track_id);
    invalidate_read_caches();
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
    if (!storage_->create_smart_playlist(name, criteria)) return;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->rebuild_smart_playlists();
    }
    emit_library_changed();
}

void BackendController::update_smart_playlist(const std::string& id,
                                               const std::string& name,
                                               const std::string& criteria) {
    if (!storage_->update_smart_playlist(id, name, criteria)) return;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->rebuild_smart_playlists();
    }
    emit_library_changed();
}

std::optional<Lyrics> BackendController::get_lyrics(const std::string& track_id) {
    return storage_->get_lyrics(track_id);
}

bool BackendController::set_lyrics(const std::string& track_id,
                                   const std::string& text, bool synced,
                                   const std::string& source) {
    bool result = storage_->set_lyrics(track_id, text, synced, source);
    if (result) emit_library_changed();
    return result;
}

void BackendController::request_lyrics(
    const std::string& track_id,
    LegacyLyricsCallback callback) {
    if (!callback) return;

    // Every request owns its callback; results are queued independently so a
    // concurrent request cannot overwrite an earlier one.
    const auto alive = workers_alive_;
    auto enqueue = [this, alive](const std::string& id, const Lyrics& lyrics,
                                 LegacyLyricsCallback cb) {
        if (!alive || !alive->load()) return;
        {
            std::lock_guard<std::mutex> lock(pending_lyrics_mutex_);
            pending_lyrics_dispatches_.push_back(
                PendingLyricsDispatch{id, lyrics, std::move(cb)});
        }
        if (lyrics_dispatcher_) lyrics_dispatcher_->emit();
    };

    if (lyrics_manager_) {
        // Look up the track for artist/title/file_path/embedded_lyrics.
        Track track;
        {
            std::lock_guard<std::mutex> lock(storage_mutex_);
            track = storage_->get_track(track_id);
        }

        // The callback may run synchronously from cache or later on the lyrics
        // worker. The alive token prevents teardown races in both cases.
        lyrics_manager_->request_lyrics(
            track_id,
            track.artist,
            track.title,
            track.file_path,
            track.embedded_lyrics,
            [this, alive, enqueue, cb = std::move(callback)](
                const std::string& fetched_id, const Lyrics& lyrics) mutable {
                if (alive && alive->load() && storage_) {
                    std::lock_guard<std::mutex> lock(storage_mutex_);
                    if (alive->load() && storage_) {
                        storage_->set_lyrics(fetched_id, lyrics.text,
                                             lyrics.synced, lyrics.source);
                    }
                }
                enqueue(fetched_id, lyrics, std::move(cb));
            });
    } else {
        enqueue(track_id, Lyrics{track_id, "", false, "", 0}, std::move(callback));
    }
}

bool BackendController::has_synced_lyrics(const std::string& track_id) {
    if (lyrics_manager_) {
        auto lyrics = lyrics_manager_->get_lyrics(track_id);
        if (lyrics && lyrics->synced && !lyrics->text.empty()) {
            return true;
        }
    }
    // Fallback: check storage directly
    auto opt = storage_->get_lyrics(track_id);
    return opt && opt->synced && !opt->text.empty();
}

std::string BackendController::get_synced_lyrics(const std::string& track_id) {
    if (lyrics_manager_) {
        auto lyrics = lyrics_manager_->get_lyrics(track_id);
        if (lyrics && lyrics->synced) {
            return lyrics->text;
        }
    }
    auto opt = storage_->get_lyrics(track_id);
    if (opt && opt->synced) {
        return opt->text;
    }
    return "";
}

std::string BackendController::get_plain_lyrics(const std::string& track_id) {
    if (lyrics_manager_) {
        auto lyrics = lyrics_manager_->get_lyrics(track_id);
        if (lyrics && !lyrics->synced) {
            return lyrics->text;
        }
    }
    auto opt = storage_->get_lyrics(track_id);
    if (opt && !opt->synced) {
        return opt->text;
    }
    return "";
}

void BackendController::request_synced_lyrics(const std::string& track_id,
                                              ILyricsService::SyncedLyricsReadyCallback cb) {
    // Reuse the existing lyrics request mechanism
    request_lyrics(track_id, [this, track_id, cb = std::move(cb)](
        const std::string& id, const std::optional<Lyrics>& lyrics) {
        if (cb && lyrics) cb(id, lyrics->text);
        else if (cb) cb(id, "");
    });
}

void BackendController::request_plain_lyrics(const std::string& track_id,
                                             ILyricsService::LyricsReadyCallback cb) {
    request_lyrics(track_id, [this, track_id, cb = std::move(cb)](
        const std::string& id, const std::optional<Lyrics>& lyrics) {
        if (cb && lyrics) cb(id, lyrics->text);
        else if (cb) cb(id, "");
    });
}

bool BackendController::save_synced_lyrics(const std::string& track_id, const std::string& lrc_text) {
    return set_lyrics(track_id, lrc_text, true, "user");
}

bool BackendController::save_plain_lyrics(const std::string& track_id, const std::string& lyrics) {
    return set_lyrics(track_id, lyrics, false, "user");
}

std::string BackendController::get_embedded_synced_lyrics(const std::string& track_id) {
    auto track = storage_->get_track(track_id);
    if (!track.embedded_lyrics.empty()) {
        // Check if embedded lyrics are synced (contain LRC timestamps)
        if (track.embedded_lyrics.find('[') != std::string::npos &&
            track.embedded_lyrics.find(']') != std::string::npos) {
            return track.embedded_lyrics;
        }
    }
    return "";
}

std::string BackendController::get_embedded_plain_lyrics(const std::string& track_id) {
    auto track = storage_->get_track(track_id);
    if (!track.embedded_lyrics.empty()) {
        // Return embedded lyrics as plain text (strip timestamps if present)
        if (track.embedded_lyrics.find('[') != std::string::npos) {
            // Simple strip - the LyricsManager can do better parsing
            return track.embedded_lyrics;
        }
        return track.embedded_lyrics;
    }
    return "";
}

std::vector<std::string> BackendController::get_lyrics_providers() {
    return {"lrclib", "embedded", "local"};
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

std::string BackendController::find_local_artist_artwork(const Artist& artist) {
    if (!artist.artwork_path.empty()) return artist.artwork_path;
    if (!storage_ || artist.name.empty()) return "";

    const std::vector<std::string> names = {
        "artist.jpg", "artist.jpeg", "artist.png",
        "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg",
    };

    auto in_directory = [&names](const std::filesystem::path& dir) -> std::string {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return "";
        for (const auto& name : names) {
            const auto candidate = dir / name;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate.string();
            }
        }
        return "";
    };

    // Prefer art next to the artist's tracks, then one folder level up.
    for (const auto& track : storage_->get_tracks_by_artist(artist.name)) {
        if (track.file_path.empty()) continue;
        const std::filesystem::path file(track.file_path);
        if (!file.has_parent_path()) continue;
        if (auto found = in_directory(file.parent_path()); !found.empty()) return found;
        if (auto found = in_directory(file.parent_path().parent_path()); !found.empty()) return found;
    }

    // Fall back to the first album cover associated with this artist.
    for (const auto& album : storage_->get_albums_by_artist(artist.id)) {
        if (!album.artwork_path.empty() &&
            std::filesystem::exists(album.artwork_path)) {
            return album.artwork_path;
        }
    }

    return "";
}

void BackendController::fetch_artist_artwork(
    const std::string& artist_id,
    std::function<void(bool success)> callback) {
    // Run on a tracked background worker to avoid blocking the main thread.
    spawn_background_worker(
        [this, artist_id, callback = std::move(callback)](
            const WorkerAliveToken& alive) mutable {
            if (!alive->load() || !storage_) return;

            Artist artist = storage_->get_artist(artist_id);
            if (artist.name.empty()) {
                if (callback && alive->load()) callback(false);
                return;
            }

            // Local folder/album art resolves instantly and works offline.
            if (auto local = find_local_artist_artwork(artist); !local.empty()) {
                const bool ok = storage_->update_artist_artwork(artist_id, local);
                if (ok) {
                    emit_library_changed();
                    save_metadata_cache();
                }
                if (callback && alive->load()) callback(ok);
                return;
            }

            auto result = ArtworkProvider::fetch_artist_artwork_by_name(artist.name);
            if (!alive->load() || !result) {
                if (callback && alive->load()) callback(false);
                return;
            }

            std::string cache_dir;
            if (const char* home = std::getenv("HOME")) {
                cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
            } else {
                cache_dir = "/tmp/lollypop-cpp/artwork";
            }

            std::string key = "artist_" + artist.name;
            std::replace(key.begin(), key.end(), '/', '_');
            std::replace(key.begin(), key.end(), ':', '_');

            std::string local_path =
                ArtworkProvider::download_artwork(result->url, cache_dir, key);
            if (!alive->load()) return;
            if (local_path.empty()) {
                if (callback) callback(false);
                return;
            }

            bool success = storage_->update_artist_artwork(artist_id, local_path);
            if (success) {
                emit_library_changed();
                save_metadata_cache();
                if (artwork_manager_) {
                    artwork_manager_->invalidate(ArtworkManager::make_key("", artist.name));
                }
            }

            if (callback && alive->load()) callback(success);
        });
}

void BackendController::fetch_missing_artist_artwork() {
    spawn_background_worker([this](const WorkerAliveToken& alive) {
        if (!alive->load() || !storage_) return;

        bool changed = false;
        for (const auto& artist : storage_->get_all_artists()) {
            if (!alive->load()) return;
            if (!artist.artwork_path.empty()) continue;

            const std::string local_path = find_local_artist_artwork(artist);
            if (local_path.empty()) continue;

            if (storage_->update_artist_artwork(artist.id, local_path)) {
                changed = true;
            }
        }

        if (changed && alive->load()) {
            // Refresh both the in-memory read model and the on-disk snapshot
            // so a restart cannot restore stale empty artwork paths.
            emit_library_changed();
            save_metadata_cache();
        }
    });
}

void BackendController::refresh_album_artwork_from_itunes(bool force) {
    if (!storage_) return;

    constexpr const char* kPrefKey = "metadata.itunes_album_artwork_v2_1200";
    if (!force) {
        const auto refresh_done = storage_->get_preference(kPrefKey);
        if (refresh_done && *refresh_done == "1") return;
    }

    std::vector<Album> albums;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        albums = storage_->get_all_albums();
    }
    if (albums.empty()) return;

    spawn_background_worker(
        [this, albums = std::move(albums), kPrefKey](const WorkerAliveToken& alive) mutable {
            std::string cache_dir;
            if (const char* home = std::getenv("HOME")) {
                cache_dir = std::string(home) + "/.local/share/lollypop-cpp/artwork";
            } else {
                cache_dir = "/tmp/lollypop-cpp/artwork";
            }

            int refreshed = 0;
            for (const auto& album : albums) {
                if (!alive->load()) return;
                auto result = iTunesProvider::search_album(album.artist, album.title);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!alive->load()) return;
                if (!result || result->artwork_url.empty()) continue;

                // Include the requested size in the cache key so the one-time
                // refresh replaces 600px files cached by older builds.
                std::string key = "itunes_album_1200_" + album.id;
                std::replace(key.begin(), key.end(), '/', '_');
                std::replace(key.begin(), key.end(), ':', '_');

                const std::string local_path =
                    iTunesProvider::download_artwork(result->artwork_url, cache_dir, key);
                if (!alive->load()) return;
                if (local_path.empty()) continue;

                {
                    std::lock_guard<std::mutex> lock(storage_mutex_);
                    if (!alive->load() || !storage_) return;
                    if (!storage_->update_album_artwork(album.id, local_path)) continue;

                    for (const auto& track : storage_->get_tracks_by_album(album.id)) {
                        if (track.artwork_path != local_path) {
                            storage_->update_track_artwork(track.id, local_path);
                        }
                    }
                }
                ++refreshed;
            }

            if (!alive->load()) return;
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                if (!alive->load() || !storage_) return;
                storage_->set_preference(kPrefKey, "1");
            }
            if (refreshed > 0 && alive->load()) emit_library_changed();
        });
}

namespace {

struct LyricsFetchWait {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool success = false;
};

}  // namespace

void BackendController::fetch_missing_lyrics(
    std::function<void(int fetched, int total)> progress_callback) {
    spawn_background_worker(
        [this, progress_callback = std::move(progress_callback)](
            const WorkerAliveToken& alive) mutable {
            if (!alive->load() || !storage_ || !lyrics_manager_) {
                if (progress_callback && alive->load()) progress_callback(0, 0);
                return;
            }

            std::vector<Track> tracks;
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                tracks = storage_->get_all_tracks();
            }
            const int total = static_cast<int>(tracks.size());
            int fetched = 0;
            if (total == 0) {
                if (progress_callback && alive->load()) progress_callback(0, 0);
                return;
            }

            for (const auto& track : tracks) {
                if (!alive->load()) return;

                bool has_lyrics = false;
                {
                    std::lock_guard<std::mutex> lock(storage_mutex_);
                    auto existing = storage_->get_lyrics(track.id);
                    has_lyrics = existing && !existing->text.empty();
                }
                if (has_lyrics) continue;

                // Shared wait state outlives this worker if shutdown cancels it
                // before the lyrics manager invokes the callback.
                auto wait = std::make_shared<LyricsFetchWait>();
                lyrics_manager_->request_lyrics(
                    track.id, track.artist, track.title, track.file_path,
                    track.embedded_lyrics,
                    [alive, wait](const std::string&, const Lyrics& lyrics) {
                        {
                            std::lock_guard<std::mutex> lock(wait->mutex);
                            wait->done = true;
                            wait->success = !lyrics.text.empty();
                        }
                        wait->cv.notify_one();
                    });

                bool success = false;
                {
                    std::unique_lock<std::mutex> lock(wait->mutex);
                    wait->cv.wait_for(lock, std::chrono::seconds(30), [&] {
                        return !alive->load() || wait->done;
                    });
                    if (!alive->load()) return;
                    success = wait->success;
                }
                if (success) ++fetched;

                if (progress_callback && alive->load()) {
                    progress_callback(fetched, total);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
}

void BackendController::fetch_all_lyrics(
    std::function<void(int fetched, int total)> progress_callback) {
    spawn_background_worker(
        [this, progress_callback = std::move(progress_callback)](
            const WorkerAliveToken& alive) mutable {
            if (!alive->load() || !storage_ || !lyrics_manager_) {
                if (progress_callback && alive->load()) progress_callback(0, 0);
                return;
            }

            std::vector<Track> tracks;
            {
                std::lock_guard<std::mutex> lock(storage_mutex_);
                tracks = storage_->get_all_tracks();
            }
            const int total = static_cast<int>(tracks.size());
            int fetched = 0;
            if (total == 0) {
                if (progress_callback && alive->load()) progress_callback(0, 0);
                return;
            }

            for (const auto& track : tracks) {
                if (!alive->load()) return;

                auto wait = std::make_shared<LyricsFetchWait>();
                lyrics_manager_->request_lyrics(
                    track.id, track.artist, track.title, track.file_path,
                    track.embedded_lyrics,
                    [alive, wait](const std::string&, const Lyrics& lyrics) {
                        {
                            std::lock_guard<std::mutex> lock(wait->mutex);
                            wait->done = true;
                            wait->success = !lyrics.text.empty();
                        }
                        wait->cv.notify_one();
                    });

                bool success = false;
                {
                    std::unique_lock<std::mutex> lock(wait->mutex);
                    wait->cv.wait_for(lock, std::chrono::seconds(30), [&] {
                        return !alive->load() || wait->done;
                    });
                    if (!alive->load()) return;
                    success = wait->success;
                }
                if (success) ++fetched;

                if (progress_callback && alive->load()) {
                    progress_callback(fetched, total);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
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
    return storage_->is_album_favorited(album_id);
}

std::vector<std::string> BackendController::get_sorted_track_ids(
    const std::string& sort_column, bool ascending, const std::string& genre_filter) {
    // Negative limit maps to SQLite LIMIT -1: no artificial 10k truncation.
    std::vector<Track> tracks;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        tracks = storage_->get_tracks_paginated(
            -1, sort_column, "", "", genre_filter);
    }
    std::vector<std::string> ids;
    ids.reserve(tracks.size());
    for (const auto& t : tracks) ids.push_back(t.id);
    // Storage returns ascending order only; reverse for descending sorts.
    if (!ascending) std::reverse(ids.begin(), ids.end());
    return ids;
}

std::vector<Track> BackendController::get_tracks_by_ids(
    const std::vector<std::string>& track_ids) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return storage_->get_tracks_by_ids(track_ids);
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

            // Update visualizer engine with the actual pipeline sample rate
            if (visualizer_engine_) {
                double viz_rate = audio_engine_->get_viz_sample_rate();
                visualizer_engine_->set_sample_rate(viz_rate);
                std::cerr << "BackendController: Visualizer sample rate set to " << viz_rate << " Hz" << std::endl;
            }
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
    invalidate_read_caches();
    current_track_.play_count++;
    current_track_.last_played = static_cast<int64_t>(std::time(nullptr));
}

void BackendController::toggle_play_pause() {
    if (changing_playback_state_) {
        return;
    }

    // Route through PlaybackSourceManager for source-aware behavior.
    // For internal source with no current track, try to start queue.
    if (!playing_ && current_track_.id.empty() && is_playback_source_internal()) {
        if (!queue_manager_->queue().empty()) {
            start_queue_index(queue_manager_->current_index());
            return;
        }
    }

    playback_source_manager_->play_pause();
    // State change will be reflected via callbacks (audio engine or MPRIS).
    sync_mpris_capabilities();
}

void BackendController::pause() {
    // Route through PlaybackSourceManager for source-aware behavior.
    // The pausing_ flag is still needed for internal audio engine state handling.
    if (is_playback_source_internal()) {
        pausing_ = true;
    }
    set_playing(false);
    playback_source_manager_->pause();
    sync_mpris_capabilities();
}

void BackendController::stop() {
    // Route through PlaybackSourceManager for source-aware behavior.
    playback_source_manager_->stop();

    if (is_playback_source_internal()) {
        position_ = 0.0;
        current_track_ = Track{};
        set_playing(false);
        just_started_ = false;
        pausing_ = false;
        emit_track_changed(current_track_);
        emit_position_changed(0, 0);
        if (mpris_server_) {
            mpris_server_->update_stopped();
            mpris_server_->update_track(Track{});
        }
    }
    // For external source, MPRIS manager callbacks will handle state updates.
    sync_mpris_capabilities();
}

void BackendController::next_track() {
    if (!is_playback_source_internal()) {
        // External source: delegate to MPRIS player.
        playback_source_manager_->next();
        return;
    }

    // Internal source: use queue manager.
    auto next_idx = queue_manager_->next_index();
    if (!next_idx.has_value()) {
        // End of queue with repeat disabled.
        stop();
        return;
    }
    start_queue_index(*next_idx);
}

void BackendController::previous_track() {
    if (!is_playback_source_internal()) {
        // External source: delegate to MPRIS player.
        playback_source_manager_->previous();
        return;
    }

    // Internal source: use queue manager with 3-second restart logic.
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
    // Check if the active source can seek; if not, clamp to current position
    // and emit the update without issuing a real seek.
    if (!playback_source_manager_->can_seek()) {
        position_ = std::clamp(position_seconds, 0.0, duration_);
        emit_position_changed(position_, duration_);
        if (mpris_server_) {
            mpris_server_->update_position(position_);
            mpris_server_->notify_seeked(position_);
        }
        return;
    }

    // Debounce: store the pending seek and schedule a timer.
    // 200ms debounce window (within the 150-250ms requirement).
    pending_seek_position_ = std::clamp(position_seconds, 0.0, duration_);

    // Mark a seek as in flight so periodic position callbacks do not
    // overwrite the optimistic scrub position before the debounce commits.
    seek_pending_ = true;

    // Increment generation so any in-flight seek responses are ignored.
    ++seek_generation_;

    // Immediately emit the position update for responsive UI scrubbing.
    position_ = pending_seek_position_;
    emit_position_changed(position_, duration_);
    if (mpris_server_) {
        mpris_server_->update_position(position_);
        mpris_server_->notify_seeked(position_);
    }

    // Schedule the actual seek commit.
    if (seek_debounce_timer_id_ != 0) {
        g_source_remove(seek_debounce_timer_id_);
    }
    seek_debounce_timer_id_ = g_timeout_add(200, &BackendController::seek_debounce_timeout, this);
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

bool BackendController::insert_into_queue(const std::string& track_id,
                                          size_t index) {
    if (track_id.empty()) return false;
    Track track;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        track = storage_->get_track(track_id);
    }
    if (track.id.empty()) return false;
    if (!queue_manager_->insert(track_id, index)) return false;
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
    emit_shuffle_changed(enabled);
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
    emit_repeat_changed(enabled);
    // Also emit repeat_one change since it was reset
    if (enabled) {
        emit_repeat_one_changed(false);
    }
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
    emit_repeat_one_changed(enabled);
    // Also emit repeat change since it was reset
    if (enabled) {
        emit_repeat_changed(false);
    }
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
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        storage_->toggle_like(track_id);
    }
    invalidate_read_caches();
}

void BackendController::toggle_favorite_album(const std::string& album_id) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        changed = storage_->toggle_favorite_album(album_id);
    }
    if (!changed) return;

    // Defer the notification so the tile's clicked handler can finish before
    // pages refresh and destroy/recreate the widget that emitted the change.
    g_idle_add([](gpointer data) -> gboolean {
        static_cast<BackendController*>(data)->emit_library_changed();
        return G_SOURCE_REMOVE;
    }, this);
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
    const bool can_control = has_track || has_queue;  // Can control if there's something to play
    mpris_server_->update_capabilities(can_go_next, can_go_previous, can_play,
                                       has_track, has_track && duration_ > 0,
                                       can_control);
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
    invalidate_read_caches();
    for (auto& cb : library_changed_callbacks_) {
        if (cb) cb();
    }
}

void BackendController::invalidate_read_caches() {
    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    tracks_cache_valid_ = false;
    tracks_cache_.clear();
    albums_cache_valid_ = false;
    albums_cache_.clear();
    artists_cache_valid_ = false;
    artists_cache_.clear();
}

bool BackendController::load_metadata_cache() {
    if (!storage_) return false;

    std::ifstream in(metadata_cache_path());
    if (!in) return false;

    json cache;
    try {
        in >> cache;
    } catch (const std::exception&) {
        return false;
    }
    if (cache.value("version", 0) != 1 || !cache.contains("tracks") ||
        !cache.contains("albums") || !cache.contains("artists") ||
        !cache.at("tracks").is_array() || !cache.at("albums").is_array() ||
        !cache.at("artists").is_array()) {
        return false;
    }

    const json fingerprint = {
        {"tracks", storage_->get_track_count()},
        {"albums", storage_->get_album_count()},
        {"artists", storage_->get_artist_count()},
        {"roots", storage_->get_library_roots()},
    };
    if (!cache.contains("fingerprint") || cache.at("fingerprint") != fingerprint) {
        return false;
    }

    std::vector<Track> tracks;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    tracks.reserve(cache.at("tracks").size());
    albums.reserve(cache.at("albums").size());
    artists.reserve(cache.at("artists").size());
    try {
        for (const auto& item : cache.at("tracks")) {
            tracks.push_back(track_from_json(item));
        }
        for (const auto& item : cache.at("albums")) {
            albums.push_back(album_from_json(item));
        }
        for (const auto& item : cache.at("artists")) {
            artists.push_back(artist_from_json(item));
        }
    } catch (const std::exception&) {
        return false;
    }

    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    tracks_cache_ = std::move(tracks);
    albums_cache_ = std::move(albums);
    artists_cache_ = std::move(artists);
    tracks_cache_valid_ = true;
    albums_cache_valid_ = true;
    artists_cache_valid_ = true;
    return true;
}

void BackendController::save_metadata_cache() {
    if (!storage_) return;
    preload_common_queries();

    std::vector<Track> tracks;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    {
        std::lock_guard<std::mutex> lock(read_caches_mutex_);
        tracks = tracks_cache_;
        albums = albums_cache_;
        artists = artists_cache_;
    }

    json serialized_tracks = json::array();
    json serialized_albums = json::array();
    json serialized_artists = json::array();
    for (const auto& track : tracks) serialized_tracks.push_back(track_to_json(track));
    for (const auto& album : albums) serialized_albums.push_back(album_to_json(album));
    for (const auto& artist : artists) serialized_artists.push_back(artist_to_json(artist));

    const json cache = {
        {"version", 1},
        {"generated_at", static_cast<int64_t>(std::time(nullptr))},
        {"fingerprint",
         {{"tracks", storage_->get_track_count()},
          {"albums", storage_->get_album_count()},
          {"artists", storage_->get_artist_count()},
          {"roots", storage_->get_library_roots()}}},
        {"tracks", std::move(serialized_tracks)},
        {"albums", std::move(serialized_albums)},
        {"artists", std::move(serialized_artists)},
    };

    const std::string path = metadata_cache_path();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path + ".tmp", std::ios::trunc);
    if (!out) return;
    out << cache.dump();
    out.close();
    if (!out) return;
    std::filesystem::rename(path + ".tmp", path, ec);
}

void BackendController::preload_common_queries() {
    if (!storage_) return;
    std::lock_guard<std::mutex> lock(read_caches_mutex_);
    if (!tracks_cache_valid_) {
        tracks_cache_ = storage_->get_all_tracks();
        tracks_cache_valid_ = true;
    }
    if (!albums_cache_valid_) {
        albums_cache_ = storage_->get_all_albums();
        albums_cache_valid_ = true;
    }
    if (!artists_cache_valid_) {
        artists_cache_ = storage_->get_all_artists();
        artists_cache_valid_ = true;
    }
}

void BackendController::emit_shuffle_changed(bool enabled) {
    for (auto& cb : shuffle_changed_callbacks_) {
        if (cb) cb(enabled);
    }
}

void BackendController::emit_repeat_changed(bool enabled) {
    for (auto& cb : repeat_changed_callbacks_) {
        if (cb) cb(enabled);
    }
}

void BackendController::emit_repeat_one_changed(bool enabled) {
    for (auto& cb : repeat_one_changed_callbacks_) {
        if (cb) cb(enabled);
    }
}

} // namespace lollypop

