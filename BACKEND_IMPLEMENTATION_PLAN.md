# Backend Implementation Plan

## Current Status

| Component | Status | Notes |
|-----------|--------|-------|
| Storage (SQLite) | ✅ Complete | Full schema, CRUD, FTS5 |
| BackendController API | ✅ Defined | All methods declared |
| Query delegation | ✅ Done | Passes through to StorageManager |
| Playback state machine | 🟡 Stub | Callbacks wired, no audio |
| Audio engine | ❌ Missing | GStreamer/PipeWire needed |
| Library scanning | ❌ Missing | taglib integration needed |
| File watching | ❌ Missing | inotify/fanotify |
| MPRIS integration | ❌ Missing | Gio::DBusProxy |
| CAVA visualizer | ❌ Missing | Subprocess + ASCII parsing |
| Background position updates | ❌ Missing | Timer/thread for position polling |

---

## Phase 1: Audio Engine (GStreamer)

### 1.1 Dependencies
```cmake
# CMakeLists.txt additions
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-audio-1.0 gstreamer-tag-1.0)
pkg_check_modules(GST_PLUGINS_BASE REQUIRED gstreamer-pbutils-1.0)
```

### 1.2 AudioEngine Class Design

```cpp
// backend/include/audio_engine.hpp
#pragma once

#include <string>
#include <functional>
#include <memory>

namespace lollypop {

class AudioEngine {
public:
    using PositionCallback = std::function<void(double position, double duration)>;
    using StateCallback = std::function<void(PlaybackState state)>;
    using EndOfStreamCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
        Buffering,
        Error
    };

    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void shutdown();

    // Playback control
    bool load_track(const std::string& file_path);
    void play();
    void pause();
    void stop();
    void seek(double position_seconds);
    void set_volume(double volume);  // 0.0 - 1.0
    double get_volume() const;

    // State queries
    PlaybackState get_state() const;
    double get_position() const;
    double get_duration() const;

    // Callbacks
    void set_position_callback(PositionCallback cb);
    void set_state_callback(StateCallback cb);
    void set_eos_callback(EndOfStreamCallback cb);
    void set_error_callback(ErrorCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
```

### 1.3 GStreamer Pipeline

```cpp
// backend/src/audio_engine.cpp
#include "audio_engine.hpp"
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>
#include <iostream>

namespace lollypop {

struct AudioEngine::Impl {
    GstElement* pipeline = nullptr;
    GstElement* playbin = nullptr;
    GstElement* volume = nullptr;
    GstElement* equalizer = nullptr;
    GstElement* appsink = nullptr;  // For visualization data
    
    PositionCallback position_cb;
    StateCallback state_cb;
    EndOfStreamCallback eos_cb;
    ErrorCallback error_cb;
    
    PlaybackState current_state = PlaybackState::Stopped;
    double volume_ = 0.7;
    
    guint bus_watch_id = 0;
    guint position_timeout_id = 0;
    
    static gboolean bus_callback(GstBus* bus, GstMessage* msg, gpointer user_data);
    static gboolean position_timeout(gpointer user_data);
    void handle_message(GstMessage* msg);
    void update_position();
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    
    impl_->pipeline = gst_pipeline_new("audio-pipeline");
    
    // Create playbin for automatic format handling
    impl_->playbin = gst_element_factory_make("playbin", "player");
    if (!impl_->playbin) {
        std::cerr << "Failed to create playbin" << std::endl;
        return false;
    }
    
    // Volume element for software volume control
    impl_->volume = gst_element_factory_make("volume", "volume");
    g_object_set(impl_->volume, "volume", impl_->volume_, nullptr);
    
    // Audio sink with appsink for visualization
    GstElement* audio_sink = gst_bin_new("audio-sink");
    GstElement* convert = gst_element_factory_make("audioconvert", "convert");
    GstElement* resample = gst_element_factory_make("audioresample", "resample");
    
    // Tee for splitting audio to output + visualization
    GstElement* tee = gst_element_factory_make("tee", "tee");
    
    // Output branch
    GstElement* queue_out = gst_element_factory_make("queue", "queue-out");
    GstElement* auto_sink = gst_element_factory_make("autoaudiosink", "auto-sink");
    
    // Visualization branch
    GstElement* queue_vis = gst_element_factory_make("queue", "queue-vis");
    impl_->appsink = gst_element_factory_make("appsink", "vis-sink");
    g_object_set(impl_->appsink,
        "emit-signals", TRUE,
        "sync", FALSE,
        "max-buffers", 1,
        "drop", TRUE,
        "caps", gst_caps_new_simple("audio/x-raw",
            "format", G_TYPE_STRING, "F32LE",
            "layout", G_TYPE_STRING, "interleaved",
            "rate", G_TYPE_INT, 44100,
            "channels", G_TYPE_INT, 2,
            nullptr),
        nullptr);
    
    // Build audio sink bin
    gst_bin_add_many(GST_BIN(audio_sink), tee, queue_out, auto_sink, queue_vis, impl_->appsink, nullptr);
    gst_element_link_many(queue_out, auto_sink, nullptr);
    gst_element_link_many(queue_vis, impl_->appsink, nullptr);
    
    GstPad* tee_src = gst_element_get_request_pad(tee, "src_%u");
    GstPad* queue_out_sink = gst_element_get_static_pad(queue_out, "sink");
    GstPad* queue_vis_sink = gst_element_get_static_pad(queue_vis, "sink");
    gst_pad_link(tee_src, queue_out_sink);
    gst_pad_link(tee_src, queue_vis_sink);
    gst_object_unref(tee_src);
    gst_object_unref(queue_out_sink);
    gst_object_unref(queue_vis_sink);
    
    GstPad* audio_sink_ghost = gst_ghost_pad_new("sink", gst_element_get_static_pad(tee, "sink"));
    gst_element_add_pad(audio_sink, audio_sink_ghost);
    
    // Set audio sink on playbin
    g_object_set(impl_->playbin, "audio-sink", audio_sink, nullptr);
    
    // Add playbin to pipeline
    gst_bin_add(GST_BIN(impl_->pipeline), impl_->playbin);
    
    // Bus watch for messages
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(impl_->pipeline));
    impl_->bus_watch_id = gst_bus_add_watch(bus, Impl::bus_callback, this);
    gst_object_unref(bus);
    
    // Position polling timer (100ms = 10fps)
    impl_->position_timeout_id = g_timeout_add(100, Impl::position_timeout, this);
    
    gst_element_set_state(impl_->pipeline, GST_STATE_READY);
    
    return true;
}

void AudioEngine::shutdown() {
    if (impl_->position_timeout_id) {
        g_source_remove(impl_->position_timeout_id);
        impl_->position_timeout_id = 0;
    }
    if (impl_->bus_watch_id) {
        g_source_remove(impl_->bus_watch_id);
        impl_->bus_watch_id = 0;
    }
    if (impl_->pipeline) {
        gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
        gst_object_unref(impl_->pipeline);
        impl_->pipeline = nullptr;
    }
}

bool AudioEngine::load_track(const std::string& file_path) {
    gchar* uri = gst_filename_to_uri(file_path.c_str(), nullptr);
    g_object_set(impl_->playbin, "uri", uri, nullptr);
    g_free(uri);
    
    GstStateChangeReturn ret = gst_element_set_state(impl_->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to load track" << std::endl;
        return false;
    }
    return true;
}

void AudioEngine::play() {
    gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING);
}

void AudioEngine::pause() {
    gst_element_set_state(impl_->pipeline, GST_STATE_PAUSED);
}

void AudioEngine::stop() {
    gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
    gst_element_set_state(impl_->pipeline, GST_STATE_READY);
}

void AudioEngine::seek(double position_seconds) {
    gst_element_seek_simple(impl_->pipeline, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        static_cast<gint64>(position_seconds * GST_SECOND));
}

void AudioEngine::set_volume(double volume) {
    impl_->volume_ = std::clamp(volume, 0.0, 1.0);
    if (impl_->volume) {
        g_object_set(impl_->volume, "volume", impl_->volume_, nullptr);
    }
}

double AudioEngine::get_volume() const {
    return impl_->volume_;
}

AudioEngine::PlaybackState AudioEngine::get_state() const {
    return impl_->current_state;
}

double AudioEngine::get_position() const {
    gint64 pos = 0;
    gst_element_query_position(impl_->pipeline, GST_FORMAT_TIME, &pos);
    return pos / 1e9;
}

double AudioEngine::get_duration() const {
    gint64 dur = 0;
    gst_element_query_duration(impl_->pipeline, GST_FORMAT_TIME, &dur);
    return dur / 1e9;
}

void AudioEngine::set_position_callback(PositionCallback cb) {
    impl_->position_cb = std::move(cb);
}

void AudioEngine::set_state_callback(StateCallback cb) {
    impl_->state_cb = std::move(cb);
}

void AudioEngine::set_eos_callback(EndOfStreamCallback cb) {
    impl_->eos_cb = std::move(cb);
}

void AudioEngine::set_error_callback(ErrorCallback cb) {
    impl_->error_cb = std::move(cb);
}

gboolean AudioEngine::Impl::bus_callback(GstBus* bus, GstMessage* msg, gpointer user_data) {
    AudioEngine* self = static_cast<AudioEngine*>(user_data);
    self->impl_->handle_message(msg);
    return TRUE;
}

void AudioEngine::Impl::handle_message(GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                
                PlaybackState new_playback_state = PlaybackState::Stopped;
                switch (new_state) {
                    case GST_STATE_PLAYING: new_playback_state = PlaybackState::Playing; break;
                    case GST_STATE_PAUSED: new_playback_state = PlaybackState::Paused; break;
                    case GST_STATE_READY:
                    case GST_STATE_NULL: new_playback_state = PlaybackState::Stopped; break;
                    default: break;
                }
                
                if (new_playback_state != current_state) {
                    current_state = new_playback_state;
                    if (state_cb) state_cb(current_state);
                }
            }
            break;
        }
        case GST_MESSAGE_EOS: {
            if (eos_cb) eos_cb();
            break;
        }
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            std::string error = err ? err->message : "Unknown error";
            if (debug) error += " (" + std::string(debug) + ")";
            g_error_free(err);
            g_free(debug);
            if (error_cb) error_cb(error);
            current_state = PlaybackState::Error;
            if (state_cb) state_cb(current_state);
            break;
        }
        case GST_MESSAGE_TAG: {
            // Metadata tags available
            break;
        }
        default:
            break;
    }
}

gboolean AudioEngine::Impl::position_timeout(gpointer user_data) {
    AudioEngine* self = static_cast<AudioEngine*>(user_data);
    self->impl_->update_position();
    return TRUE;  // Continue timer
}

void AudioEngine::Impl::update_position() {
    if (current_state != PlaybackState::Playing) return;
    
    double position = 0, duration = 0;
    
    gint64 pos_ns = 0, dur_ns = 0;
    if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos_ns)) {
        position = pos_ns / 1e9;
    }
    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur_ns)) {
        duration = dur_ns / 1e9;
    }
    
    if (position_cb) position_cb(position, duration);
}

} // namespace lollypop
```

---

## Phase 2: MPRIS Integration

### 2.1 MprisManager Class

```cpp
// backend/include/mpris_manager.hpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace lollypop {

struct MprisPlayerInfo {
    std::string identity;
    std::string desktop_entry;
    std::string dbus_name;
    bool is_playing = false;
    std::string track_title;
    std::string track_artist;
    std::string track_album;
    std::string art_url;
    double position = 0;
    double duration = 0;
    double volume = 1.0;
    bool can_seek = false;
    bool can_control = false;
    int loop_state = 0;  // 0=none, 1=playlist, 2=track
    bool shuffle = false;
};

class MprisManager {
public:
    using PlayerListChangedCallback = std::function<void()>;
    using ActivePlayerChangedCallback = std::function<void(const MprisPlayerInfo&)>;
    using PlaybackStateChangedCallback = std::function<void(bool playing)>;
    using TrackChangedCallback = std::function<void(const MprisPlayerInfo&)>;
    using PositionChangedCallback = std::function<void(double position, double duration)>;
    using VolumeChangedCallback = std::function<void(double volume)>;
    using LoopStateChangedCallback = std::function<void(int loop_state)>;
    using ShuffleChangedCallback = std::function<void(bool shuffle)>;

    MprisManager();
    ~MprisManager();

    bool initialize();
    void shutdown();

    // Player management
    std::vector<MprisPlayerInfo> get_players() const;
    MprisPlayerInfo get_active_player() const;
    void set_active_player(const std::string& dbus_name);
    void set_preferred_player(const std::string& name);

    // Playback control (forwarded to active player)
    void play_pause();
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(double position);
    void set_volume(double volume);
    void set_loop_state(int state);  // 0=none, 1=playlist, 2=track
    void set_shuffle(bool shuffle);

    // Callbacks
    void set_player_list_changed_callback(PlayerListChangedCallback cb);
    void set_active_player_changed_callback(ActivePlayerChangedCallback cb);
    void set_playback_state_changed_callback(PlaybackStateChangedCallback cb);
    void set_track_changed_callback(TrackChangedCallback cb);
    void set_position_changed_callback(PositionChangedCallback cb);
    void set_volume_changed_callback(VolumeChangedCallback cb);
    void set_loop_state_changed_callback(LoopStateChangedCallback cb);
    void set_shuffle_changed_callback(ShuffleChangedCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
```

### 2.2 MprisManager Implementation (Gio::DBusProxy)

```cpp
// backend/src/mpris_manager.cpp
#include "mpris_manager.hpp"
#include <giomm.h>
#include <iostream>
#include <algorithm>

namespace lollypop {

struct MprisManager::Impl {
    Glib::RefPtr<Gio::DBusConnection> session_bus;
    std::vector<MprisPlayerInfo> players;
    std::string active_player_dbus_name;
    std::string preferred_player_name;
    Glib::RefPtr<Gio::DBusProxy> active_player_proxy;
    Glib::RefPtr<Gio::DBusProxy> active_player_properties;
    
    // Signal handlers
    guint name_appeared_id = 0;
    guint name_vanished_id = 0;
    gulong properties_changed_id = 0;
    gulong seeked_id = 0;
    
    // Callbacks
    MprisManager::PlayerListChangedCallback player_list_cb;
    MprisManager::ActivePlayerChangedCallback active_player_cb;
    MprisManager::PlaybackStateChangedCallback playback_state_cb;
    MprisManager::TrackChangedCallback track_cb;
    MprisManager::PositionChangedCallback position_cb;
    MprisManager::VolumeChangedCallback volume_cb;
    MprisManager::LoopStateChangedCallback loop_state_cb;
    MprisManager::ShuffleChangedCallback shuffle_cb;
    
    void refresh_players();
    void on_name_appeared(const Glib::ustring& name);
    void on_name_vanished(const Glib::ustring& name);
    void on_properties_changed(Glib::RefPtr<Gio::DBusProxy> proxy, const Glib::VariantContainerBase& changed, const std::vector<Glib::ustring>& invalidated);
    void update_player_info(const Glib::ustring& dbus_name);
    void set_active_proxy(const Glib::ustring& dbus_name);
    void emit_player_list_changed();
    void emit_active_player_changed();
    static void on_name_appeared_static(Gio::DBusConnection::NameAppearedCallback callback, const Glib::ustring& name, gpointer user_data);
    static void on_name_vanished_static(Gio::DBusConnection::NameVanishedCallback callback, const Glib::ustring& name, gpointer user_data);
};

MprisManager::MprisManager() : impl_(std::make_unique<Impl>()) {}

MprisManager::~MprisManager() {
    shutdown();
}

bool MprisManager::initialize() {
    try {
        impl_->session_bus = Gio::DBusConnection::get_sync(Gio::DBusConnection::BUS_TYPE_SESSION);
        
        // Watch for MPRIS players appearing/disappearing
        impl_->name_appeared_id = impl_->session_bus->signal_subscribe(
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "NameOwnerChanged",
            "",
            nullptr,
            Gio::DBusSignalFlags::NONE,
            sigc::mem_fun(*impl_, &Impl::on_name_appeared)
        );
        
        impl_->name_vanished_id = impl_->session_bus->signal_subscribe(
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "NameOwnerChanged",
            "",
            nullptr,
            Gio::DBusSignalFlags::NONE,
            sigc::mem_fun(*impl_, &Impl::on_name_vanished)
        );
        
        // Initial scan
        impl_->refresh_players();
        
        return true;
    } catch (const Glib::Error& e) {
        std::cerr << "MPRIS initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void MprisManager::shutdown() {
    if (impl_->name_appeared_id) {
        impl_->session_bus->signal_unsubscribe(impl_->name_appeared_id);
    }
    if (impl_->name_vanished_id) {
        impl_->session_bus->signal_unsubscribe(impl_->name_vanished_id);
    }
    if (impl_->properties_changed_id) {
        impl_->active_player_properties->signal_unsubscribe(impl_->properties_changed_id);
    }
    if (impl_->seeked_id) {
        // Unsubscribe from seeked signal
    }
    impl_->active_player_proxy = nullptr;
    impl_->active_player_properties = nullptr;
}

void MprisManager::Impl::refresh_players() {
    try {
        auto names = session_bus->list_names_sync();
        players.clear();
        
        for (const auto& name : names) {
            if (name.starts_with("org.mpris.MediaPlayer2.")) {
                on_name_appeared(name);
            }
        }
        emit_player_list_changed();
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to list MPRIS players: " << e.what() << std::endl;
    }
}

void MprisManager::Impl::on_name_appeared(const Glib::ustring& name) {
    if (!name.starts_with("org.mpris.MediaPlayer2.")) return;
    
    // Filter out duplicates (playerctld, plasma-browser-integration browsers)
    // ... filtering logic from Quickshell MprisController
    
    try {
        auto proxy = Gio::DBusProxy::create_sync(
            session_bus,
            Gio::DBusProxyFlags::NONE,
            {},  // interface info - use introspection
            name,
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            nullptr
        );
        
        auto properties = Gio::DBusProxy::create_sync(
            session_bus,
            Gio::DBusProxyFlags::NONE,
            {},
            name,
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            nullptr
        );
        
        // Get identity and desktop entry
        auto identity_variant = proxy->get_cached_property("Identity");
        auto desktop_entry_variant = proxy->get_cached_property("DesktopEntry");
        
        std::string identity = identity_variant ? identity_variant->get_string() : "";
        std::string desktop_entry = desktop_entry_variant ? desktop_entry_variant->get_string() : "";
        
        // Store player info
        MprisPlayerInfo info;
        info.identity = identity;
        info.dbus_name = name;
        info.desktop_entry = desktop_entry;
        
        players.push_back(info);
        
        // Connect to properties changed
        properties_changed_id = properties->signal_properties_changed().connect(
            sigc::bind(sigc::mem_fun(*this, &Impl::on_properties_changed), name)
        );
        
        // If this is our preferred player or first player, make it active
        if (active_player_dbus_name.empty() || 
            (preferred_player_name.length() > 0 && 
             (identity.find(preferred_player_name) != std::string::npos || 
              desktop_entry.find(preferred_player_name) != std::string::npos))) {
            set_active_proxy(name);
        }
        
        emit_player_list_changed();
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to connect to MPRIS player " << name << ": " << e.what() << std::endl;
    }
}

void MprisManager::Impl::on_name_vanished(const Glib::ustring& name) {
    if (!name.starts_with("org.mpris.MediaPlayer2.")) return;
    
    auto it = std::find_if(players.begin(), players.end(),
        [&name](const MprisPlayerInfo& p) { return p.dbus_name == name; });
    
    if (it != players.end()) {
        players.erase(it);
        
        // If this was the active player, select another
        if (active_player_dbus_name == name) {
            active_player_dbus_name.clear();
            active_player_proxy = nullptr;
            active_player_properties = nullptr;
            
            if (!players.empty()) {
                set_active_proxy(players[0].dbus_name);
            }
        }
        
        emit_player_list_changed();
        emit_active_player_changed();
    }
}

void MprisManager::Impl::on_properties_changed(Glib::RefPtr<Gio::DBusProxy> proxy, 
                                               const Glib::VariantContainerBase& changed, 
                                               const std::vector<Glib::ustring>& invalidated) {
    Glib::VariantDict dict(changed);
    
    // Parse changed properties and emit appropriate callbacks
    // PlaybackStatus, Metadata, Volume, Position, LoopStatus, Shuffle
    
    if (dict.contains("PlaybackStatus")) {
        Glib::ustring status;
        dict.lookup("PlaybackStatus", status);
        bool playing = (status == "Playing");
        if (playback_state_cb) playback_state_cb(playing);
    }
    
    if (dict.contains("Metadata")) {
        // Parse metadata and emit track changed
        if (track_cb) {
            MprisPlayerInfo info;
            // ... parse metadata
            track_cb(info);
        }
    }
    
    if (dict.contains("Volume")) {
        double volume;
        dict.lookup("Volume", volume);
        if (volume_cb) volume_cb(volume);
    }
    
    if (dict.contains("LoopStatus")) {
        Glib::ustring loop_status;
        dict.lookup("LoopStatus", loop_status);
        int loop_state = 0;  // 0=none, 1=playlist, 2=track
        if (loop_status == "Playlist") loop_state = 1;
        else if (loop_status == "Track") loop_state = 2;
        if (loop_state_cb) loop_state_cb(loop_state);
    }
    
    if (dict.contains("Shuffle")) {
        bool shuffle;
        dict.lookup("Shuffle", shuffle);
        if (shuffle_cb) shuffle_cb(shuffle);
    }
}

void MprisManager::Impl::set_active_proxy(const Glib::ustring& dbus_name) {
    active_player_dbus_name = dbus_name;
    
    try {
        active_player_proxy = Gio::DBusProxy::create_sync(
            session_bus,
            Gio::DBusProxyFlags::NONE,
            {},
            dbus_name,
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            nullptr
        );
        
        active_player_properties = Gio::DBusProxy::create_sync(
            session_bus,
            Gio::DBusProxyFlags::NONE,
            {},
            dbus_name,
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            nullptr
        );
        
        properties_changed_id = active_player_properties->signal_properties_changed().connect(
            sigc::mem_fun(*this, &Impl::on_properties_changed)
        );
        
        // Connect to Seeked signal
        // seeked_id = active_player_proxy->signal_signal().connect(...)
        
        emit_active_player_changed();
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to set active MPRIS player: " << e.what() << std::endl;
    }
}

void MprisManager::Impl::emit_player_list_changed() {
    if (player_list_cb) player_list_cb();
}

void MprisManager::Impl::emit_active_player_changed() {
    if (active_player_cb) {
        // Find active player info
        auto it = std::find_if(players.begin(), players.end(),
            [this](const MprisPlayerInfo& p) { return p.dbus_name == active_player_dbus_name; });
        if (it != players.end()) {
            active_player_cb(*it);
        }
    }
}

// MprisManager method implementations
std::vector<MprisPlayerInfo> MprisManager::get_players() const {
    return impl_->players;
}

MprisPlayerInfo MprisManager::get_active_player() const {
    auto it = std::find_if(impl_->players.begin(), impl_->players.end(),
        [this](const MprisPlayerInfo& p) { return p.dbus_name == impl_->active_player_dbus_name; });
    if (it != impl_->players.end()) return *it;
    return {};
}

void MprisManager::set_active_player(const std::string& dbus_name) {
    impl_->set_active_proxy(dbus_name);
}

void MprisManager::set_preferred_player(const std::string& name) {
    impl_->preferred_player_name = name;
    // Re-evaluate active player
    impl_->refresh_players();
}

void MprisManager::play_pause() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("PlayPause");
    }
}

void MprisManager::play() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("Play");
    }
}

void MprisManager::pause() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("Pause");
    }
}

void MprisManager::stop() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("Stop");
    }
}

void MprisManager::next() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("Next");
    }
}

void MprisManager::previous() {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->call_sync("Previous");
    }
}

void MprisManager::seek(double position) {
    if (impl_->active_player_proxy) {
        int64_t pos_us = static_cast<int64_t>(position * 1'000'000);
        impl_->active_player_proxy->call_sync("Seek", pos_us);
    }
}

void MprisManager::set_volume(double volume) {
    if (impl_->active_player_proxy) {
        double vol = std::clamp(volume, 0.0, 1.0);
        impl_->active_player_proxy->call_sync("SetVolume", vol);
    }
}

void MprisManager::set_loop_state(int state) {
    if (impl_->active_player_proxy) {
        Glib::ustring loop_status = "None";
        if (state == 1) loop_status = "Playlist";
        else if (state == 2) loop_status = "Track";
        impl_->active_player_proxy->set_property("LoopStatus", Glib::Variant<std::string>::create(loop_status));
    }
}

void MprisManager::set_shuffle(bool shuffle) {
    if (impl_->active_player_proxy) {
        impl_->active_player_proxy->set_property("Shuffle", Glib::Variant<bool>::create(shuffle));
    }
}

// Callback setters
void MprisManager::set_player_list_changed_callback(PlayerListChangedCallback cb) {
    impl_->player_list_cb = std::move(cb);
}
void MprisManager::set_active_player_changed_callback(ActivePlayerChangedCallback cb) {
    impl_->active_player_cb = std::move(cb);
}
void MprisManager::set_playback_state_changed_callback(PlaybackStateChangedCallback cb) {
    impl_->playback_state_cb = std::move(cb);
}
void MprisManager::set_track_changed_callback(TrackChangedCallback cb) {
    impl_->track_cb = std::move(cb);
}
void MprisManager::set_position_changed_callback(PositionChangedCallback cb) {
    impl_->position_cb = std::move(cb);
}
void MprisManager::set_volume_changed_callback(VolumeChangedCallback cb) {
    impl_->volume_cb = std::move(cb);
}
void MprisManager::set_loop_state_changed_callback(LoopStateChangedCallback cb) {
    impl_->loop_state_cb = std::move(cb);
}
void MprisManager::set_shuffle_changed_callback(ShuffleChangedCallback cb) {
    impl_->shuffle_cb = std::move(cb);
}

} // namespace lollypop
```

---

## Phase 3: Library Scanning (taglib)

### 3.1 Scanner Class

```cpp
// backend/include/library_scanner.hpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "types.hpp"

namespace lollypop {

struct ScanProgress {
    size_t files_found = 0;
    size_t files_processed = 0;
    std::string current_file;
    bool cancelled = false;
};

class LibraryScanner {
public:
    using ProgressCallback = std::function<void(const ScanProgress&)>;
    using TrackFoundCallback = std::function<void(const Track&)>;
    using ErrorCallback = std::function<void(const std::string& file, const std::string& error)>;

    LibraryScanner();
    ~LibraryScanner();

    void scan_directory(const std::string& path, bool recursive = true);
    void cancel();

    void set_progress_callback(ProgressCallback cb);
    void set_track_found_callback(TrackFoundCallback cb);
    void set_error_callback(ErrorCallback cb);

    static bool is_audio_file(const std::string& path);
    static std::vector<std::string> get_audio_extensions();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
```

### 3.2 Scanner Implementation

```cpp
// backend/src/library_scanner.cpp
#include "library_scanner.hpp"
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <random>

namespace lollypop {

struct LibraryScanner::Impl {
    ScanProgress progress;
    ProgressCallback progress_cb;
    TrackFoundCallback track_cb;
    ErrorCallback error_cb;
    bool cancelled = false;

    void scan_recursive(const std::filesystem::path& path) {
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (cancelled) break;
                if (!entry.is_regular_file()) continue;
                
                const auto& file_path = entry.path();
                std::string ext = file_path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (!is_audio_file(ext)) continue;
                
                progress.files_found++;
                progress.current_file = file_path.string();
                if (progress_cb) progress_cb(progress);
                
                process_file(file_path.string());
                progress.files_processed++;
                if (progress_cb) progress_cb(progress);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Filesystem error: " << e.what() << std::endl;
        }
    }

    void process_file(const std::string& file_path) {
        TagLib::FileRef file(file_path.c_str());
        if (file.isNull() || !file.tag()) {
            if (error_cb) error_cb(file_path, "Failed to read file or no tag");
            return;
        }

        TagLib::Tag* tag = file.tag();
        TagLib::AudioProperties* props = file.audioProperties();

        Track track;
        track.id = generate_id();
        track.title = tag->title().to8Bit(true);
        track.artist = tag->artist().to8Bit(true);
        track.album = tag->album().to8Bit(true);
        track.album_artist = "";  // TagLib doesn't expose album artist directly
        track.genre = tag->genre().to8Bit(true);
        track.track_number = tag->track();
        track.year = tag->year();
        track.duration = props ? props->lengthInSeconds() : 0;
        track.file_path = file_path;
        track.date_added = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Extract artwork
        if (auto* mpeg_file = dynamic_cast<TagLib::MPEG::File*>(file.file())) {
            if (auto* id3v2_tag = mpeg_file->ID3v2Tag()) {
                auto frames = id3v2_tag->frameListMap()["APIC"];
                if (!frames.isEmpty()) {
                    auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                    if (pic) {
                        // Save artwork to file
                        std::string art_path = save_artwork(pic->picture().data(), pic->picture().size(), pic->mimeType());
                        track.artwork_path = art_path;
                    }
                }
            }
        } else if (auto* flac_file = dynamic_cast<TagLib::FLAC::File*>(file.file())) {
            // FLAC artwork extraction
        } else if (auto* ogg_file = dynamic_cast<TagLib::Ogg::File*>(file.file())) {
            // Ogg/Opus artwork extraction
        }

        if (track_cb) track_cb(track);
    }

    std::string generate_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        for (int i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                ss << '-';
            } else {
                ss << "0123456789abcdef"[dis(gen)];
            }
        }
        return ss.str();
    }

    std::string save_artwork(const char* data, size_t size, const std::string& mime_type) {
        // Save to ~/.local/share/lollypop-cpp/artwork/{id}.{ext}
        // Return path
        return "";
    }
};

LibraryScanner::LibraryScanner() : impl_(std::make_unique<Impl>()) {}
LibraryScanner::~LibraryScanner() = default;

void LibraryScanner::scan_directory(const std::string& path, bool recursive) {
    impl_->cancelled = false;
    impl_->scan_recursive(path);
}

void LibraryScanner::cancel() {
    impl_->cancelled = true;
}

void LibraryScanner::set_progress_callback(ProgressCallback cb) {
    impl_->progress_cb = std::move(cb);
}

void LibraryScanner::set_track_found_callback(TrackFoundCallback cb) {
    impl_->track_cb = std::move(cb);
}

void LibraryScanner::set_error_callback(ErrorCallback cb) {
    impl_->error_cb = std::move(cb);
}

bool LibraryScanner::is_audio_file(const std::string& path) {
    static const std::vector<std::string> extensions = get_audio_extensions();
    std::string ext = path;
    size_t pos = ext.rfind('.');
    if (pos != std::string::npos) {
        ext = ext.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
    }
    return false;
}

std::vector<std::string> LibraryScanner::get_audio_extensions() {
    return {".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac", ".wv", ".ape", ".mpc"};
}

} // namespace lollypop
```

---

## Phase 4: File Watching (inotify)

```cpp
// backend/include/file_watcher.hpp
#pragma once

#include <string>
#include <functional>
#include <vector>

namespace lollypop {

enum class FileEventType {
    Created,
    Modified,
    Deleted,
    Moved
};

struct FileEvent {
    FileEventType type;
    std::string path;
    std::string dest_path;  // For move events
};

class FileWatcher {
public:
    using EventCallback = std::function<void(const FileEvent&)>;

    FileWatcher();
    ~FileWatcher();

    bool add_watch(const std::string& path, bool recursive = true);
    void remove_watch(const std::string& path);
    void remove_all_watches();

    void set_event_callback(EventCallback cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
```

```cpp
// backend/src/file_watcher.cpp
#include "file_watcher.hpp"
#include <sys/inotify.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <filesystem>

namespace lollypop {

struct FileWatcher::Impl {
    int inotify_fd = -1;
    std::thread watch_thread;
    std::atomic<bool> running{false};
    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;
    EventCallback event_cb;
    std::mutex mutex;

    void run() {
        char buffer[4096];
        while (running) {
            ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
            if (len <= 0) continue;

            size_t i = 0;
            while (i < static_cast<size_t>(len)) {
                const inotify_event* event = reinterpret_cast<const inotify_event*>(buffer + i);
                
                std::string path;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it = wd_to_path.find(event->wd);
                    if (it != wd_to_path.end()) {
                        path = it->second;
                    }
                }

                if (!path.empty()) {
                    std::string full_path = path + "/" + event->name;
                    
                    FileEventType type = FileEventType::Created;
                    if (event->mask & IN_DELETE) type = FileEventType::Deleted;
                    else if (event->mask & IN_MODIFY) type = FileEventType::Modified;
                    else if (event->mask & IN_MOVED_FROM) type = FileEventType::Moved;
                    else if (event->mask & IN_MOVED_TO) type = FileEventType::Moved;
                    else if (event->mask & IN_CREATE) type = FileEventType::Created;

                    FileEvent fe{type, full_path};
                    if (event_cb) event_cb(fe);
                }

                i += sizeof(inotify_event) + event->len;
            }
        }
    }
};

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {
    impl_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->inotify_fd < 0) {
        throw std::runtime_error("Failed to initialize inotify");
    }
    impl_->running = true;
    impl_->watch_thread = std::thread(&Impl::run, impl_.get());
}

FileWatcher::~FileWatcher() {
    impl_->running = false;
    if (impl_->inotify_fd >= 0) {
        close(impl_->inotify_fd);
    }
    if (impl_->watch_thread.joinable()) {
        impl_->watch_thread.join();
    }
}

bool FileWatcher::add_watch(const std::string& path, bool recursive) {
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE | IN_ATTRIB;
    int wd = inotify_add_watch(impl_->inotify_fd, path.c_str(), mask);
    if (wd < 0) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->wd_to_path[wd] = path;
    impl_->path_to_wd[path] = wd;

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_directory()) {
                int sub_wd = inotify_add_watch(impl_->inotify_fd, entry.path().c_str(), mask);
                if (sub_wd >= 0) {
                    impl_->wd_to_path[sub_wd] = entry.path().string();
                    impl_->path_to_wd[entry.path().string()] = sub_wd;
                }
            }
        }
    }
    return true;
}

void FileWatcher::remove_watch(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->path_to_wd.find(path);
    if (it != impl_->path_to_wd.end()) {
        inotify_rm_watch(impl_->inotify_fd, it->second);
        impl_->wd_to_path.erase(it->second);
        impl_->path_to_wd.erase(it);
    }
}

void FileWatcher::remove_all_watches() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& [wd, _] : impl_->wd_to_path) {
        inotify_rm_watch(impl_->inotify_fd, wd);
    }
    impl_->wd_to_path.clear();
    impl_->path_to_wd.clear();
}

void FileWatcher::set_event_callback(EventCallback cb) {
    impl_->event_cb = std::move(cb);
}

} // namespace lollypop
```

---

## Phase 5: CAVA Visualizer Integration

```cpp
// backend/include/cava_visualizer.hpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

namespace lollypop {

class CavaVisualizer {
public:
    using PointsCallback = std::function<void(const std::vector<float>&)>;

    CavaVisualizer();
    ~CavaVisualizer();

    bool start(const std::string& config_path = "");
    void stop();

    void set_points_callback(PointsCallback cb);

    static bool is_cava_available();
    static std::string generate_default_config(int bars = 50, int framerate = 60);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lollypop
```

```cpp
// backend/src/cava_visualizer.cpp
#include "cava_visualizer.hpp"
#include <giomm.h>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace lollypop {

struct CavaVisualizer::Impl {
    Glib::RefPtr<Gio::Subprocess> cava_process;
    std::thread reader_thread;
    std::atomic<bool> running{false};
    PointsCallback points_cb;
    std::string config_path;

    void read_output() {
        try {
            auto stdout_pipe = cava_process->get_stdout_pipe();
            auto input_stream = Gio::DataInputStream::create(stdout_pipe);
            
            while (running) {
                Glib::ustring line;
                if (!input_stream->read_line(line)) break;
                
                if (line.empty()) continue;
                
                // Parse "123;456;789;" format
                std::vector<float> points;
                std::stringstream ss(line.raw());
                std::string token;
                while (std::getline(ss, token, ';')) {
                    if (!token.empty()) {
                        try {
                            points.push_back(std::stof(token));
                        } catch (...) {}
                    }
                }
                
                if (!points.empty() && points_cb) {
                    points_cb(points);
                }
            }
        } catch (const Glib::Error& e) {
            std::cerr << "CAVA reader error: " << e.what() << std::endl;
        }
    }
};

CavaVisualizer::CavaVisualizer() : impl_(std::make_unique<Impl>()) {}

CavaVisualizer::~CavaVisualizer() {
    stop();
}

bool CavaVisualizer::start(const std::string& config_path) {
    if (impl_->running) return true;

    // Use default config if none provided
    if (config_path.empty()) {
        impl_->config_path = generate_default_config();
    } else {
        impl_->config_path = config_path;
    }

    try {
        impl_->cava_process = Gio::Subprocess::create_sync(
            {"cava", "-p", impl_->config_path},
            Gio::SUBPROCESS_FLAGS_STDOUT_PIPE | Gio::SUBPROCESS_FLAGS_STDERR_PIPE
        );

        impl_->running = true;
        impl_->reader_thread = std::thread(&Impl::read_output, impl_.get());
        return true;
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to start CAVA: " << e.what() << std::endl;
        return false;
    }
}

void CavaVisualizer::stop() {
    impl_->running = false;
    if (impl_->cava_process) {
        impl_->cava_process->force_exit();
        impl_->cava_process = nullptr;
    }
    if (impl_->reader_thread.joinable()) {
        impl_->reader_thread.join();
    }
}

void CavaVisualizer::set_points_callback(PointsCallback cb) {
    impl_->points_cb = std::move(cb);
}

bool CavaVisualizer::is_cava_available() {
    // Check if cava is in PATH
    return !Glib::find_program_in_path("cava").empty();
}

std::string CavaVisualizer::generate_default_config(int bars, int framerate) {
    std::string config = 
        "[general]\n"
        "mode = waves\n"
        "framerate = " + std::to_string(framerate) + "\n"
        "autosens = 1\n"
        "bars = " + std::to_string(bars) + "\n\n"
        "[output]\n"
        "method = raw\n"
        "raw_target = /dev/stdout\n"
        "data_format = ascii\n"
        "channels = mono\n"
        "mono_option = average\n\n"
        "[smoothing]\n"
        "noise_reduction = 20\n";
    
    // Write to temp file
    std::string path = "/tmp/lollypop-cava-config-" + std::to_string(getpid()) + ".txt";
    std::ofstream file(path);
    file << config;
    file.close();
    return path;
}

} // namespace lollypop
```

---

## Phase 6: BackendController Integration

### 6.1 Updated BackendController with Real Implementations

```cpp
// backend/include/controller.hpp - Additions
class BackendController {
    // ... existing methods ...
private:
    std::unique_ptr<AudioEngine> audio_engine_;
    std::unique_ptr<MprisManager> mpris_manager_;
    std::unique_ptr<LibraryScanner> library_scanner_;
    std::unique_ptr<FileWatcher> file_watcher_;
    std::unique_ptr<CavaVisualizer> cava_visualizer_;
    
    // Connect audio engine callbacks
    void setup_audio_callbacks();
    void setup_mpris_callbacks();
    void setup_cava_callbacks();
    
    // Handle end of stream
    void on_eos();
};
```

```cpp
// backend/src/controller.cpp - Key additions
bool BackendController::initialize() {
    std::cout << "BackendController: Initializing..." << std::endl;

    // Initialize storage
    storage_ = std::make_unique<StorageManager>();
    if (!storage_->initialize()) {
        return false;
    }

    // Initialize audio engine
    audio_engine_ = std::make_unique<AudioEngine>();
    if (!audio_engine_->initialize()) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        return false;
    }
    setup_audio_callbacks();

    // Initialize MPRIS
    mpris_manager_ = std::make_unique<MprisManager>();
    mpris_manager_->initialize();
    setup_mpris_callbacks();

    // Initialize CAVA visualizer
    cava_visualizer_ = std::make_unique<CavaVisualizer>();
    cava_visualizer_->start();
    setup_cava_callbacks();

    // Initialize file watcher
    file_watcher_ = std::make_unique<FileWatcher>();
    file_watcher_->set_event_callback([this](const FileEvent& event) {
        handle_file_event(event);
    });

    std::cout << "BackendController: Initialized successfully" << std::endl;
    return true;
}

void BackendController::setup_audio_callbacks() {
    audio_engine_->set_position_callback([this](double pos, double dur) {
        position_ = pos;
        duration_ = dur;
        emit_position_changed(pos, dur);
    });

    audio_engine_->set_state_callback([this](AudioEngine::PlaybackState state) {
        bool playing = (state == AudioEngine::PlaybackState::Playing);
        playing_ = playing;
        emit_playback_state_changed(playing);
    });

    audio_engine_->set_eos_callback([this]() {
        on_eos();
    });

    audio_engine_->set_error_callback([this](const std::string& error) {
        std::cerr << "Audio engine error: " << error << std::endl;
    });
}

void BackendController::setup_mpris_callbacks() {
    mpris_manager_->set_playback_state_changed_callback([this](bool playing) {
        playing_ = playing;
        emit_playback_state_changed(playing);
    });

    mpris_manager_->set_track_changed_callback([this](const MprisPlayerInfo& info) {
        // Convert to Track and emit
        Track track;
        track.title = info.track_title;
        track.artist = info.track_artist;
        track.album = info.track_album;
        track.artwork_path = info.art_url;
        track.duration = static_cast<int>(info.duration);
        current_track_ = track;
        emit_track_changed(track);
    });

    mpris_manager_->set_position_changed_callback([this](double pos, double dur) {
        position_ = pos;
        duration_ = dur;
        emit_position_changed(pos, dur);
    });
}

void BackendController::setup_cava_callbacks() {
    cava_visualizer_->set_points_callback([this](const std::vector<float>& points) {
        // Emit to UI via callback or signal
        if (visualization_cb_) visualization_cb_(points);
    });
}

void BackendController::on_eos() {
    // Handle end of stream - next track or stop based on repeat mode
    if (repeat_one_) {
        // Replay current track
        audio_engine_->seek(0);
        audio_engine_->play();
    } else {
        next_track();
    }
}

void BackendController::play_tracks(const std::vector<std::string>& track_ids, size_t start_index) {
    if (track_ids.empty()) return;

    queue_ = track_ids;
    queue_index_ = std::min(start_index, queue_.size() - 1);

    // Load track metadata
    current_track_ = get_track(queue_[queue_index_]);
    duration_ = current_track_.duration;
    position_ = 0.0;

    // Load and play via audio engine
    if (audio_engine_->load_track(current_track_.file_path)) {
        audio_engine_->set_volume(volume_);
        audio_engine_->play();
        playing_ = true;
    }

    emit_track_changed(current_track_);
    emit_playback_state_changed(true);
    emit_queue_changed();
}

void BackendController::seek(double position_seconds) {
    audio_engine_->seek(position_seconds);
    position_ = std::clamp(position_seconds, 0.0, duration_);
    emit_position_changed(position_, duration_);
}

void BackendController::set_volume(double volume) {
    volume_ = std::clamp(volume, 0.0, 1.0);
    audio_engine_->set_volume(volume_);
}
```

---

## CMakeLists.txt Updates

```cmake
# backend/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-audio-1.0 gstreamer-tag-1.0 gstreamer-pbutils-1.0)
pkg_check_modules(GIO REQUIRED gio-2.0 glib-2.0 gobject-2.0)
pkg_check_modules(TAGLIB REQUIRED taglib)
pkg_check_modules(GIO REQUIRED gio-2.0)

# ... existing ...

target_link_libraries(lollypop-backend PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GIO_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    ${GLIB_LIBRARIES}
    lollypop-storage
)

target_include_directories(lollypop-backend PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

---

## Implementation Priority

| Priority | Component | Effort | Dependencies |
|----------|-----------|--------|--------------|
| 1 | AudioEngine (GStreamer) | High | gstreamer, gstreamer-audio |
| 2 | MprisManager | High | gio, glib |
| 3 | CavaVisualizer | Medium | cava, gio |
| 4 | LibraryScanner | Medium | taglib |
| 5 | FileWatcher | Low | inotify (Linux) |
| 6 | BackendController integration | High | All above |

---

## Testing Strategy

```cpp
// tests/test_audio_engine.cpp
TEST(AudioEngineTest, LoadAndPlay) {
    AudioEngine engine;
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.load_track("/path/to/test.mp3"));
    engine.play();
    // ... verify state changes
}

// tests/test_mpris.cpp
TEST(MprisTest, PlayerDetection) {
    MprisManager mpris;
    ASSERT_TRUE(mpris.initialize());
    auto players = mpris.get_players();
    // ... verify player list
}

// tests/test_library_scanner.cpp
TEST(LibraryScannerTest, ScanDirectory) {
    LibraryScanner scanner;
    scanner.set_track_found_callback([](const Track& t) {
        EXPECT_FALSE(t.title.empty());
    });
    scanner.scan_directory("/test/music");
}
```

---

## Next Steps

1. **Week 1**: Implement `AudioEngine` with GStreamer playbin
2. **Week 2**: Implement `MprisManager` with Gio::DBusProxy
3. **Week 3**: Integrate both into `BackendController`, test playback
4. **Week 4**: Add `CavaVisualizer` and `LibraryScanner`
5. **Week 5**: Add `FileWatcher`, complete integration testing