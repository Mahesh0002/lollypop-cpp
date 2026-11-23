/**
 * Lollypop C++ - Backend Controller
 * Core logic: library scanning, playback coordination, queue management
 *
 * Architecture:
 *   - PlaybackController (this class) owns the authoritative playback state.
 *   - PlaybackSourceManager routes commands to the active source
 *     (internal AudioEngine or external MPRIS player).
 *   - QueueManager owns next/previous/shuffle/repeat decisions.
 *   - Only PlaybackController sets `playing_`.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>
#include <cstddef>

#include <glib.h>

#include "types.hpp"

#include "services/IPlayerService.hpp"

namespace Glib {
class Dispatcher;
}

#include "audio_engine.hpp"
#include "mpris_manager.hpp"
#include "mpris_server.hpp"
#include "library_scanner.hpp"
#include "library_manager.hpp"
#include "lyrics_manager.hpp"
#include "file_watcher.hpp"
#include "cava_visualizer.hpp"
#include "visualizer_engine.hpp"
#include "cache_manager.hpp"
#include "queue_manager.hpp"
#include "playback_source_manager.hpp"

#include "services/ILibraryService.hpp"
#include "services/ILyricsService.hpp"

namespace lollypop {

class StorageManager;  // Forward declaration

class BackendController : public IPlayerService, public ILibraryService, public ILyricsService {
public:
    BackendController();
    virtual ~BackendController();

    // Staged initialization: returns false if any stage fails, with rollback.
    bool initialize();
    void shutdown();

    // Library management
    using ScanProgressCallback = LibraryScanner::ProgressCallback;
    using ScanFinishedCallback = std::function<void(bool success, bool cancelled)>;
    void scan_library(const std::string& path);
    bool start_scan_library(const std::string& path);
    // Scan the user's XDG Music directory (falling back to ~/Music) and
    // persist it as a library root.
    bool scan_default_music_library();
    void cancel_scan_library();
    bool is_scan_running() const;
    void set_scan_progress_callback(ScanProgressCallback cb) {
        scan_progress_cb_ = std::move(cb);
    }
    void set_scan_finished_callback(ScanFinishedCallback cb) {
        scan_finished_cb_ = std::move(cb);
    }
    void add_watch_directory(const std::string& path);
    void remove_watch_directory(const std::string& path);
    void open_uri(const std::string& uri);

    // Track queries
    std::vector<Track> get_all_tracks();
    std::vector<Track> get_recently_added(int limit = 20);
    std::vector<Track> get_recently_played(int limit = 20);
    std::vector<Track> get_liked_tracks();
    std::vector<Track> search_tracks(const std::string& query);
    Track get_track(const std::string& track_id);

    // Album queries
    std::vector<Album> get_all_albums();
    std::vector<Album> get_albums_by_artist(const std::string& artist_id);
    Album get_album(const std::string& album_id);

    // Artist queries
    std::vector<Artist> get_all_artists();
    Artist get_artist(const std::string& artist_id);
    std::vector<Track> get_tracks_by_artist(const std::string& artist_name);

    // Genre queries
    std::vector<std::string> get_all_genres();
    std::vector<Track> get_tracks_by_genre(const std::string& genre);

    // Artwork paths
    std::string get_artwork_path_for_track(const std::string& track_id);
    std::string get_artwork_path_for_album(const std::string& album_id);
    std::string get_artwork_path_for_artist(const std::string& artist_id);

    // Favourite albums
    std::vector<Album> get_favourite_albums();
    bool is_track_liked(const std::string& track_id);
    bool is_album_favorited(const std::string& album_id);

    // Database-backed sorting (keyset pagination)
    std::vector<std::string> get_sorted_track_ids(
        const std::string& sort_column, bool ascending, const std::string& genre_filter = "");
    std::vector<Track> get_tracks_by_ids(
        const std::vector<std::string>& track_ids);

    // Playlist queries
    std::vector<Playlist> get_all_playlists();
    Playlist get_playlist(const std::string& playlist_id);
    std::vector<Track> get_playlist_tracks(const std::string& playlist_id);
    std::vector<Track> get_smart_playlist_tracks(const std::string& playlist_id);
    void create_playlist(const std::string& name);
    void create_smart_playlist(const std::string& name, const std::string& criteria);
    void update_smart_playlist(const std::string& id, const std::string& name,
                               const std::string& criteria);
    void delete_playlist(const std::string& playlist_id);
    void add_track_to_playlist(const std::string& playlist_id, const std::string& track_id);
    void remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id);

    // Lyrics
    std::optional<Lyrics> get_lyrics(const std::string& track_id);
    void set_lyrics(const std::string& track_id, const std::string& text,
                    bool synced = false, const std::string& source = {});
    void request_lyrics(
        const std::string& track_id,
        ILyricsService::LyricsCallback callback) override;

    // Library changed notification
    // (overrides both IPlayerService and ILibraryService, defined below)

    // Queue editing (all indices refer to the live queue).
    bool enqueue_track(const std::string& track_id, bool next = false);
    bool remove_from_queue(size_t index);
    bool move_queue_item(size_t from, size_t to);
    bool play_queue_index(size_t index);
    void clear_queue();

    // Playback control (routed through the active playback source).
    void play_track(const std::string& track_id);
    void play_tracks(const std::vector<std::string>& track_ids, size_t start_index = 0);
    void toggle_play_pause();
    void pause();
    void stop();
    void next_track();
    void previous_track();
    void seek(double position_seconds);
    void set_volume(double volume);  // 0.0 - 1.0
    double get_volume() const;

    // Playback state - `playing_` is the single authoritative source of truth.
    bool is_playing() const;
    Track get_current_track() const;
    double get_position() const;
    double get_duration() const;
    std::vector<std::string> get_queue() const;
    size_t get_queue_index() const;

    // Playback source selection
    void set_playback_source_internal();
    void set_playback_source_external();
    bool is_playback_source_internal() const;

    // Playback modes
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_repeat_one(bool enabled);
    bool get_shuffle() const;
    bool get_repeat() const;
    bool get_repeat_one() const;

    // Capability queries
    bool can_seek() const;

    // Favorites / Likes
    void toggle_like(const std::string& track_id);
    void toggle_favorite_album(const std::string& album_id);

    // Subsystem access
    AudioEngine* get_audio_engine() const { return audio_engine_.get(); }
    MprisManager* get_mpris_manager() const { return mpris_manager_.get(); }
    MprisServer* get_mpris_server() const { return mpris_server_.get(); }
    LibraryScanner* get_library_scanner() const { return library_scanner_.get(); }
    LibraryManager* get_library_manager() const { return library_manager_.get(); }
    LyricsManager* get_lyrics_manager() const { return lyrics_manager_.get(); }
    FileWatcher* get_file_watcher() const { return file_watcher_.get(); }
    CavaVisualizer* get_cava_visualizer() const { return cava_visualizer_.get(); }
    VisualizerEngine* get_visualizer_engine() const { return visualizer_engine_.get(); }
    CacheManager* get_cache_manager() const { return cache_manager_.get(); }
    QueueManager* get_queue_manager() const { return queue_manager_.get(); }
    PlaybackSourceManager* get_playback_source_manager() const { return playback_source_manager_.get(); }

    // Callbacks for UI updates (multicast - multiple observers supported)
    using TrackChangedCallback = std::function<void(const Track&)>;
    using PositionChangedCallback = std::function<void(double position, double duration)>;
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using QueueChangedCallback = std::function<void(const std::vector<std::string>&)>;
    using VisualizationCallback = std::function<void(const std::vector<float>&)>;

    void add_track_changed_callback(TrackChangedCallback cb) {
        track_changed_callbacks_.push_back(std::move(cb));
    }
    void add_position_changed_callback(PositionChangedCallback cb) {
        position_changed_callbacks_.push_back(std::move(cb));
    }
    void add_playback_state_changed_callback(PlaybackStateChangedCallback cb) {
        playback_state_changed_callbacks_.push_back(std::move(cb));
    }
    void add_queue_changed_callback(QueueChangedCallback cb) {
        queue_changed_callbacks_.push_back(std::move(cb));
    }
    void add_visualization_changed_callback(VisualizationCallback cb) {
        visualization_callbacks_.push_back(std::move(cb));
    }
    void add_library_changed_callback(std::function<void()> cb) {
        library_changed_callbacks_.push_back(std::move(cb));
    }

    // Legacy single-callback setters (for backward compatibility with IPlayerService)
    void set_track_changed_callback(TrackChangedCallback cb) override {
        track_changed_callbacks_.clear();
        if (cb) track_changed_callbacks_.push_back(std::move(cb));
    }
    void set_position_changed_callback(PositionChangedCallback cb) override {
        position_changed_callbacks_.clear();
        if (cb) position_changed_callbacks_.push_back(std::move(cb));
    }
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb) override {
        playback_state_changed_callbacks_.clear();
        if (cb) playback_state_changed_callbacks_.push_back(std::move(cb));
    }
    void set_queue_changed_callback(QueueChangedCallback cb) override {
        queue_changed_callbacks_.clear();
        if (cb) queue_changed_callbacks_.push_back(std::move(cb));
    }
    void set_visualization_changed_callback(VisualizationCallback cb) override {
        visualization_callbacks_.clear();
        if (cb) visualization_callbacks_.push_back(std::move(cb));
    }
    void set_library_changed_callback(std::function<void()> cb) override {
        library_changed_callbacks_.clear();
        if (cb) library_changed_callbacks_.push_back(std::move(cb));
    }

private:
    std::unique_ptr<StorageManager> storage_;
    std::shared_ptr<AudioEngine> audio_engine_;
    std::shared_ptr<MprisManager> mpris_manager_;
    std::unique_ptr<MprisServer> mpris_server_;
    std::unique_ptr<LibraryScanner> library_scanner_;
    std::unique_ptr<FileWatcher> file_watcher_;
    std::unique_ptr<CavaVisualizer> cava_visualizer_;
    std::unique_ptr<VisualizerEngine> visualizer_engine_;
    std::unique_ptr<CacheManager> cache_manager_;
    std::unique_ptr<LibraryManager> library_manager_;
    std::unique_ptr<LyricsManager> lyrics_manager_;

    // EventBus subscription ID for spectrum events (unsubscribed on shutdown).
    uint64_t spectrum_subscription_id_ = 0;

    // Non-essential startup work is scheduled on the GLib main loop so the
    // first window frame is not blocked by D-Bus, cache, or queue validation.
    struct DeferredInitContext {
        BackendController* self = nullptr;
        std::shared_ptr<bool> alive;
    };
    unsigned int startup_idle_id_ = 0;
    DeferredInitContext* startup_idle_context_ = nullptr;
    std::shared_ptr<bool> startup_alive_;

    std::unique_ptr<QueueManager> queue_manager_;
    std::unique_ptr<PlaybackSourceManager> playback_source_manager_;

    std::vector<TrackChangedCallback> track_changed_callbacks_;
    std::vector<PositionChangedCallback> position_changed_callbacks_;
    std::vector<PlaybackStateChangedCallback> playback_state_changed_callbacks_;
    std::vector<QueueChangedCallback> queue_changed_callbacks_;
    std::vector<VisualizationCallback> visualization_callbacks_;
    std::vector<std::function<void()>> library_changed_callbacks_;
    ILyricsService::LyricsCallback lyrics_callback_;

    // Lyrics result dispatcher (marshals async lyrics to GTK main thread).
    std::unique_ptr<Glib::Dispatcher> lyrics_dispatcher_;
    std::optional<Lyrics> pending_lyrics_result_;
    std::string pending_lyrics_track_id_;

    // File watcher events arrive on its own thread; queue them and hand them
    // to the GLib main loop where storage and UI callbacks are safe to touch.
    std::unique_ptr<Glib::Dispatcher> file_event_dispatcher_;
    std::mutex file_events_mutex_;
    std::vector<FileEvent> pending_file_events_;

    struct ScanResult { bool success = false; bool cancelled = false; };
    std::thread scan_thread_;
    std::unique_ptr<LibraryScanner> active_scan_scanner_;
    std::atomic<bool> scan_running_{false};
    mutable std::mutex scan_mutex_;
    std::vector<Track> scan_tracks_;
    std::vector<Track> pending_scan_tracks_;
    std::string pending_scan_path_;
    std::atomic<bool> scan_cancel_requested_{false};
    ScanProgress scan_progress_;
    ScanProgressCallback scan_progress_cb_;
    ScanFinishedCallback scan_finished_cb_;
    std::unique_ptr<Glib::Dispatcher> scan_dispatcher_;
    std::optional<ScanResult> pending_scan_result_;

    // Serialize storage operations across the scan worker and GLib thread.
    mutable std::mutex storage_mutex_;
    std::vector<std::string> shuffle_history_;
    size_t shuffle_history_index_ = 0;
    bool restoring_playback_ = false;

    // Current playback state - owned solely by PlaybackController (this class).
    // AudioEngine and MPRISManager callbacks must NOT write to this directly;
    // they inform the controller, which updates playing_ through set_playing().
    bool playing_ = false;
    // Guard against re-entrancy: set when BackendController itself changes
    // playing_ (via set_playing), so the MPRIS callback doesn't toggle it back.
    bool changing_playback_state_ = false;
    // Set to true when set_playing(true) is called, so toggle_play_pause()
    // knows not to pause immediately after start_queue_index() started playback.
    bool just_started_ = false;
    // Set to true during pause() to prevent the audio engine state callback
    // from re-setting playing_=true on a transient Playing state during the
    // PLAYING->PAUSED pipeline transition.
    bool pausing_ = false;
    double volume_ = 0.7;
    bool repeat_one_ = false;
    Track current_track_;
    double position_ = 0.0;
    double duration_ = 0.0;

    // Seek debounce and generation tracking.
    std::atomic<uint64_t> seek_generation_{0};
    guint seek_debounce_timer_id_ = 0;
    double pending_seek_position_ = 0.0;
    bool seek_pending_ = false;

    static gboolean seek_debounce_timeout(gpointer data);

    void set_playing(bool playing);

    void schedule_deferred_initialization();
    void cancel_deferred_initialization();
    void initialize_deferred_services();
    void restore_saved_queue();
    static gboolean deferred_init_idle(gpointer data);
    static void deferred_init_destroy(gpointer data);

    void setup_audio_callbacks();
    void setup_mpris_callbacks();

    void handle_file_event(const FileEvent& event);
    void dispatch_file_events();
    void dispatch_scan_updates();
    void dispatch_lyrics_result();

    void process_file_event(const FileEvent& event);

    void setup_cava_callbacks();

    void setup_mpris_server_callbacks();
    void sync_mpris_capabilities();

    bool start_queue_index(size_t index);
    void update_play_statistics();
    void persist_queue();

    void emit_track_changed(const Track& track);
    void emit_position_changed(double position, double duration);
    void emit_playback_state_changed(bool playing);
    void emit_queue_changed();
    void emit_visualization_changed(const std::vector<float>& points);
    void emit_library_changed();
};

} // namespace lollypop
