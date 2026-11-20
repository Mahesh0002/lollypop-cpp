/**
 * Lollypop C++ - Backend Controller
 * Core logic: library scanning, playback coordination, queue management
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "types.hpp"
#include "audio_engine.hpp"

namespace lollypop {

class StorageManager;  // Forward declaration

class BackendController {
public:
    BackendController();
    virtual ~BackendController();

    bool initialize();
    void shutdown();

    // Library management
    void scan_library(const std::string& path);
    void add_watch_directory(const std::string& path);
    void remove_watch_directory(const std::string& path);

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

    // Playlist queries
    std::vector<Playlist> get_all_playlists();
    Playlist get_playlist(const std::string& playlist_id);
    void create_playlist(const std::string& name);
    void delete_playlist(const std::string& playlist_id);
    void add_track_to_playlist(const std::string& playlist_id, const std::string& track_id);
    void remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id);

    // Playback control
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

    // Playback state
    bool is_playing() const;
    Track get_current_track() const;
    double get_position() const;
    double get_duration() const;
    std::vector<std::string> get_queue() const;
    size_t get_queue_index() const;

    // Playback modes
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_repeat_one(bool enabled);
    bool get_shuffle() const;
    bool get_repeat() const;
    bool get_repeat_one() const;

    // Favorites / Likes
    void toggle_like(const std::string& track_id);
    void toggle_favorite_album(const std::string& album_id);

    // Audio engine access
    AudioEngine* get_audio_engine() const { return audio_engine_.get(); }

    // Callbacks for UI updates
    using TrackChangedCallback = std::function<void(const Track&)>;
    using PositionChangedCallback = std::function<void(double position, double duration)>;
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using QueueChangedCallback = std::function<void(const std::vector<std::string>&)>;

    void set_track_changed_callback(TrackChangedCallback cb) { track_changed_cb_ = std::move(cb); }
    void set_position_changed_callback(PositionChangedCallback cb) { position_changed_cb_ = std::move(cb); }
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb) { playback_state_changed_cb_ = std::move(cb); }
    void set_queue_changed_callback(QueueChangedCallback cb) { queue_changed_cb_ = std::move(cb); }

private:
    std::unique_ptr<StorageManager> storage_;
    std::unique_ptr<AudioEngine> audio_engine_;

    TrackChangedCallback track_changed_cb_;
    PositionChangedCallback position_changed_cb_;
    PlaybackStateChangedCallback playback_state_changed_cb_;
    QueueChangedCallback queue_changed_cb_;

    // Current playback state
    bool playing_ = false;
    double volume_ = 0.7;
    bool shuffle_ = false;
    bool repeat_ = false;
    bool repeat_one_ = false;
    std::vector<std::string> queue_;
    size_t queue_index_ = 0;
    Track current_track_;
    double position_ = 0.0;
    double duration_ = 0.0;

    void setup_audio_callbacks();

    void emit_track_changed(const Track& track);
    void emit_position_changed(double position, double duration);
    void emit_playback_state_changed(bool playing);
    void emit_queue_changed();
};

} // namespace lollypop