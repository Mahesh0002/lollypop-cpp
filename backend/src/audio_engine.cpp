/**
 * Lollypop C++ - Audio Engine Implementation
 * GStreamer-based audio playback with visualization support.
 *
 * Simple pipeline (no playbin wrapper, which caused heap-use-free crashes):
 *
 *   filesrc "filesrc" -- decodebin "decodebin" -- audioconvert -- audioresample
 *       -- tee "tee"
 *           |-- (src pad 1) -- queue "viz-queue" (leaky=downstream)
 *           |             -- audioconvert -- audioresample -- capsfilter
 *           |             -- appsink "viz-sink" (max-buffers=1, drop=true)
 *           |-- (src pad 2) -- queue "audio-queue"
 *           |             -- volume "audio-volume"
 *           |             -- autoaudiosink "audio-sink"
 *
 * Volume is controlled via a post-tee volume element on the output branch.
 */

#include "audio_engine.hpp"

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>
#include <glib.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>
#include <atomic>

namespace lollypop {

struct AudioEngine::Impl {
    GstElement* pipeline = nullptr;
    GstElement* filesrc = nullptr;
    GstElement* decodebin = nullptr;
    GstElement* audioconvert = nullptr;
    GstElement* audioresample = nullptr;
    GstElement* tee = nullptr;

    // Visualization branch
    GstElement* viz_queue = nullptr;
    GstElement* viz_audioconvert = nullptr;
    GstElement* viz_audioresample = nullptr;
    GstElement* viz_capsfilter = nullptr;
    GstElement* appsink = nullptr;

    // Audio output branch
    GstElement* audio_queue = nullptr;
    GstElement* volume_element = nullptr;
    GstElement* autoaudiosink = nullptr;

    // Requested tee source pads
    GstPad* tee_viz_pad = nullptr;
    GstPad* tee_audio_pad = nullptr;

    guint bus_watch_id = 0;
    guint position_timeout_id = 0;

    // Callback storage (protected by callback_mutex for thread safety)
    std::mutex callback_mutex;
    std::function<void(double position, double duration)> position_cb;
    std::function<void(PlaybackState)> state_cb;
    std::function<void()> eos_cb;
    std::function<void(const std::string&)> error_cb;
    std::function<void(const float*, size_t)> viz_cb;

    // Playback state
    PlaybackState current_state = PlaybackState::Stopped;
    bool buffering = false;
    PlaybackState state_before_buffering = PlaybackState::Stopped;
    double volume_ = 0.7;
    bool initialized = false;

    // Generation token for load/seek operations
    std::atomic<uint64_t> load_generation{0};
    std::atomic<uint64_t> seek_generation{0};
    std::atomic<gint64> pending_seek_position{0};
    std::atomic<bool> seek_pending{false};

    // Audio pad linked flag
    bool audio_pad_linked = false;

    static gboolean bus_callback(GstBus* bus, GstMessage* msg, gpointer user_data);
    static gboolean position_timeout(gpointer user_data);
    void handle_message(GstMessage* msg);
    void update_position();
    bool set_gst_state(PlaybackState state);

    bool build_pipeline(const std::string& uri);
    void destroy_pipeline();
    static void on_decodebin_pad_added(GstElement* decodebin, GstPad* pad,
                                       gpointer user_data);
    GstPad* request_tee_pad(const char* pad_name);
};

// ---------------------------------------------------------------------------
// Playback state transition table
// ---------------------------------------------------------------------------
constexpr bool kAllowedTransitions[5][5] = {
    // from Stopped
    { true,  true,  true,  false, true },
    // from Playing
    { true,  true,  true,  false, true },
    // from Paused
    { true,  true,  true,  false, true },
    // from Buffering
    { true,  true,  true,  false, true },
    // from Error
    { true,  false, false, false, true },
};

bool AudioEngine::Impl::set_gst_state(PlaybackState state) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        const size_t from = static_cast<size_t>(current_state);
        const size_t to = static_cast<size_t>(state);
        if (from >= 5 || to >= 5 || !kAllowedTransitions[from][to]) {
            std::cerr << "AudioEngine: Rejected state transition "
                      << static_cast<int>(current_state) << " -> "
                      << static_cast<int>(state) << std::endl;
            return false;
        }
    }

    GstState gst_state;
    switch (state) {
        case PlaybackState::Playing:
            gst_state = GST_STATE_PLAYING;
            break;
        case PlaybackState::Paused:
            gst_state = GST_STATE_PAUSED;
            break;
        case PlaybackState::Stopped:
            gst_state = GST_STATE_NULL;
            break;
        case PlaybackState::Buffering:
        case PlaybackState::Error:
            return false;
    }

    GstStateChangeReturn ret =
        gst_element_set_state(GST_ELEMENT(pipeline), gst_state);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to set pipeline state to "
                  << static_cast<int>(state) << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        current_state = state;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    Impl* impl = impl_.get();
    if (impl->initialized) {
        return true;
    }

    // Create the pipeline (NOT playbin - playbin wrapper caused crashes).
    impl->pipeline = gst_pipeline_new("audio-pipeline");
    if (!impl->pipeline) {
        std::cerr << "AudioEngine: Failed to create pipeline" << std::endl;
        return false;
    }

    // Bus watch for messages.
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(impl->pipeline));
    impl->bus_watch_id = gst_bus_add_watch(bus, Impl::bus_callback, this);
    gst_object_unref(bus);

    // Position polling timer (100ms = 10fps).
    impl->position_timeout_id = g_timeout_add(100, Impl::position_timeout,
                                              this);

    // Build the pipeline elements.
    if (!impl->build_pipeline("")) {
        return false;
    }

    impl->initialized = true;
    impl->current_state = PlaybackState::Stopped;
    return true;
}

void AudioEngine::shutdown() {
    Impl* impl = impl_.get();
    if (!impl) return;

    // Stop any ongoing playback first
    if (impl->pipeline) {
        gst_element_set_state(GST_ELEMENT(impl->pipeline), GST_STATE_NULL);
        // Wait for the state change to complete (with timeout)
        GstState state, pending;
        gst_element_get_state(GST_ELEMENT(impl->pipeline), &state, &pending, 2 * GST_SECOND);
    }

    // Remove the position timer first so no more callbacks fire.
    if (impl->position_timeout_id) {
        g_source_remove(impl->position_timeout_id);
        impl->position_timeout_id = 0;
    }

    // Remove the bus watch.
    if (impl->bus_watch_id) {
        g_source_remove(impl->bus_watch_id);
        impl->bus_watch_id = 0;
    }

    // Disconnect appsink callbacks before tearing down the pipeline.
    if (impl->appsink) {
        g_signal_handlers_disconnect_by_data(impl->appsink, this);
        g_object_set(impl->appsink, "emit-signals", FALSE, nullptr);
    }

    // Disconnect decodebin pad-added handler
    if (impl->decodebin) {
        g_signal_handlers_disconnect_by_func(impl->decodebin,
                                             reinterpret_cast<gpointer>(
                                                 G_CALLBACK(Impl::on_decodebin_pad_added)),
                                             impl);
    }

    // Destroy the pipeline (releases requested tee pads, unrefs elements).
    impl->destroy_pipeline();

    // Reset callbacks.
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        impl->position_cb = nullptr;
        impl->state_cb = nullptr;
        impl->eos_cb = nullptr;
        impl->error_cb = nullptr;
        impl->viz_cb = nullptr;
    }

    impl->current_state = PlaybackState::Stopped;
    impl->initialized = false;
}

// ---------------------------------------------------------------------------
// Pipeline construction
// ---------------------------------------------------------------------------

void AudioEngine::Impl::on_decodebin_pad_added(GstElement* /*decodebin*/,
                                               GstPad* new_pad,
                                               gpointer user_data) {
    Impl* impl = static_cast<Impl*>(user_data);

    // Check if this pad has audio caps
    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    if (!caps) {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    bool is_audio = false;
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        if (s) {
            const gchar* name = gst_structure_get_name(s);
            if (g_str_has_prefix(name, "audio/")) {
                is_audio = true;
            }
        }
    }

    if (!is_audio) {
        std::cerr << "AudioEngine: Ignoring non-audio pad from decodebin" << std::endl;
        if (caps) gst_caps_unref(caps);
        return;
    }

    GstPad* sink_pad =
        gst_element_get_static_pad(impl->audioconvert, "sink");
    if (!sink_pad) {
        std::cerr << "AudioEngine: Failed to get audioconvert sink pad"
                  << std::endl;
        if (caps) gst_caps_unref(caps);
        return;
    }

    if (gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "AudioEngine: Failed to link decodebin to audioconvert"
                  << std::endl;
    } else {
        impl->audio_pad_linked = true;
    }
    gst_object_unref(sink_pad);
    if (caps) gst_caps_unref(caps);
}

bool AudioEngine::Impl::build_pipeline(const std::string& uri) {
    // filesrc
    filesrc = gst_element_factory_make("filesrc", "filesrc");
    if (!filesrc) {
        std::cerr << "AudioEngine: Failed to create filesrc" << std::endl;
        return false;
    }
    g_object_set(filesrc, "location", uri.c_str(), nullptr);

    // decodebin
    decodebin = gst_element_factory_make("decodebin", "decodebin");
    if (!decodebin) {
        std::cerr << "AudioEngine: Failed to create decodebin" << std::endl;
        return false;
    }
    g_signal_connect(decodebin, "pad-added",
                     G_CALLBACK(on_decodebin_pad_added), this);

    // Common elements
    audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
    audioresample = gst_element_factory_make("audioresample",
                                             "audioresample");
    tee = gst_element_factory_make("tee", "tee");

    // Visualization branch elements
    viz_queue = gst_element_factory_make("queue", "viz-queue");
    viz_audioconvert = gst_element_factory_make("audioconvert",
                                                "viz-audioconvert");
    viz_audioresample = gst_element_factory_make("audioresample",
                                                 "viz-audioresample");
    viz_capsfilter = gst_element_factory_make("capsfilter",
                                              "viz-capsfilter");
    appsink = gst_element_factory_make("appsink", "viz-sink");

    // Audio output branch elements
    audio_queue = gst_element_factory_make("queue", "audio-queue");
    volume_element = gst_element_factory_make("volume", "audio-volume");
    // Use pulsesink directly: autoaudiosink falls back to fakesink in some
    // environments (no real audio device detected), producing silent playback.
    autoaudiosink = gst_element_factory_make("pulsesink", "audio-output");
    if (!autoaudiosink) {
        // Fallback to autoaudiosink if pulsesink is unavailable.
        autoaudiosink = gst_element_factory_make("autoaudiosink",
                                                 "audio-output");
    }

    // Validate all elements
    if (!filesrc || !decodebin || !audioconvert || !audioresample || !tee ||
        !viz_queue || !viz_audioconvert || !viz_audioresample ||
        !viz_capsfilter || !appsink || !audio_queue || !volume_element ||
        !autoaudiosink) {
        std::cerr << "AudioEngine: Failed to create one or more elements"
                  << std::endl;
        return false;
    }

    // Add all elements to the pipeline.
    gst_bin_add_many(GST_BIN(pipeline),
                     filesrc, decodebin, audioconvert, audioresample, tee,
                     viz_queue, viz_audioconvert, viz_audioresample,
                     viz_capsfilter, appsink, audio_queue, volume_element,
                     autoaudiosink, nullptr);

    // Link the static chain: filesrc -> decodebin -> audioconvert ->
    // audioresample -> tee
    // Note: decodebin's source pads are added dynamically, so we link
    // filesrc -> decodebin now, and decodebin -> audioconvert via
    // the pad-added signal.
    if (!gst_element_link(filesrc, decodebin)) {
        std::cerr << "AudioEngine: Failed to link filesrc to decodebin"
                  << std::endl;
        return false;
    }
    if (!gst_element_link(audioconvert, audioresample)) {
        std::cerr << "AudioEngine: Failed to link audioconvert to "
                     "audioresample"
                  << std::endl;
        return false;
    }
    if (!gst_element_link(audioresample, tee)) {
        std::cerr << "AudioEngine: Failed to link audioresample to tee"
                  << std::endl;
        return false;
    }

    // Visualization branch: viz_queue -> viz_audioconvert ->
    // viz_audioresample -> viz_capsfilter -> appsink.
    if (!gst_element_link_many(viz_queue, viz_audioconvert,
                               viz_audioresample, viz_capsfilter,
                               appsink, nullptr)) {
        std::cerr << "AudioEngine: Failed to link visualization branch"
                  << std::endl;
        return false;
    }

    // Audio output branch: audio_queue -> volume_element -> autoaudiosink.
    if (!gst_element_link_many(audio_queue, volume_element,
                               autoaudiosink, nullptr)) {
        std::cerr << "AudioEngine: Failed to link audio output branch"
                  << std::endl;
        return false;
    }

    // Configure the visualization queue to drop buffers when the visualizer
    // lags.
    g_object_set(viz_queue, "leaky", 2, nullptr);

    // Configure appsink: pull-based, emit signals, single buffered, drop.
    g_object_set(appsink, "emit-signals", TRUE, nullptr);
    g_object_set(appsink, "max-buffers", 1, nullptr);
    g_object_set(appsink, "drop", TRUE, nullptr);
    g_object_set(appsink, "sync", FALSE, nullptr);
    g_object_set(appsink, "async", FALSE, nullptr);

    // Set the post-tee volume to the initial level.
    g_object_set(volume_element, "volume", volume_, nullptr);

    // Negotiate visualization caps.
    GstCaps* caps = gst_caps_new_simple(
        "audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "rate", G_TYPE_INT, 44100,
        "channels", G_TYPE_INT, 2,
        "layout", G_TYPE_STRING, "interleaved",
        nullptr);
    if (!caps) {
        std::cerr << "AudioEngine: Failed to create visualization caps"
                  << std::endl;
        return false;
    }
    g_object_set(viz_capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);


    // Request two separate source pads from the tee.
    tee_viz_pad = request_tee_pad("src_%u");
    tee_audio_pad = request_tee_pad("src_%u");
    if (!tee_viz_pad || !tee_audio_pad) {
        std::cerr << "AudioEngine: Failed to request tee source pads"
                  << std::endl;
        return false;
    }

    // Link tee's viz source pad to viz_queue's sink pad.
    GstPad* viz_queue_sink = gst_element_get_static_pad(viz_queue, "sink");
    if (!viz_queue_sink) {
        std::cerr << "AudioEngine: Failed to get viz queue sink pad"
                  << std::endl;
        return false;
    }
    if (gst_pad_link(tee_viz_pad, viz_queue_sink) != GST_PAD_LINK_OK) {
        std::cerr << "AudioEngine: Failed to link tee to viz queue"
                  << std::endl;
        gst_object_unref(viz_queue_sink);
        return false;
    }
    gst_object_unref(viz_queue_sink);

    // Link tee's audio source pad to audio_queue's sink pad.
    GstPad* audio_queue_sink =
        gst_element_get_static_pad(audio_queue, "sink");
    if (!audio_queue_sink) {
        std::cerr << "AudioEngine: Failed to get audio queue sink pad"
                  << std::endl;
        return false;
    }
    if (gst_pad_link(tee_audio_pad, audio_queue_sink) != GST_PAD_LINK_OK) {
        std::cerr << "AudioEngine: Failed to link tee to audio queue"
                  << std::endl;
        gst_object_unref(audio_queue_sink);
        return false;
    }
    gst_object_unref(audio_queue_sink);

    return true;
}

void AudioEngine::Impl::destroy_pipeline() {
    // Release requested tee pads before destroying the pipeline.
    // Requested pads hold an extra reference that must be released.
    if (tee && tee_viz_pad) {
        gst_element_release_request_pad(tee, tee_viz_pad);
        gst_object_unref(tee_viz_pad);
        tee_viz_pad = nullptr;
    }
    if (tee && tee_audio_pad) {
        gst_element_release_request_pad(tee, tee_audio_pad);
        gst_object_unref(tee_audio_pad);
        tee_audio_pad = nullptr;
    }

    if (pipeline) {
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }

    filesrc = nullptr;
    decodebin = nullptr;
    audioconvert = nullptr;
    audioresample = nullptr;
    tee = nullptr;
    viz_queue = nullptr;
    viz_audioconvert = nullptr;
    viz_audioresample = nullptr;
    viz_capsfilter = nullptr;
    appsink = nullptr;
    audio_queue = nullptr;
    volume_element = nullptr;
    autoaudiosink = nullptr;
    tee_viz_pad = nullptr;
    tee_audio_pad = nullptr;
}

GstPad* AudioEngine::Impl::request_tee_pad(const char* pad_name) {
    GstPad* pad = gst_element_request_pad_simple(tee, pad_name);
    if (!pad) {
        pad = gst_element_get_request_pad(tee, pad_name);
    }
    return pad;
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

bool AudioEngine::load_track(const std::string& file_path) {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return false;

    if (file_path.empty()) {
        std::cerr << "AudioEngine: Empty file path" << std::endl;
        return false;
    }

    if (access(file_path.c_str(), F_OK) != 0) {
        std::cerr << "AudioEngine: File does not exist: " << file_path
                  << std::endl;
        return false;
    }

    // Increment load generation to invalidate any pending callbacks
    uint64_t gen = ++impl->load_generation;

    // Stop the pipeline first if it's running - wait for NULL to settle
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(impl->pipeline), GST_STATE_NULL);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to set pipeline to NULL" << std::endl;
        return false;
    }
    // Wait for the NULL state transition to complete
    GstState state, pending;
    ret = gst_element_get_state(GST_ELEMENT(impl->pipeline), &state, &pending, 2 * GST_SECOND);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to reach NULL state" << std::endl;
        return false;
    }

    // Check if generation changed (new load started)
    if (gen != impl->load_generation) {
        return false;
    }

    // Reset audio pad linked flag for new track
    impl->audio_pad_linked = false;

    // Set the file path on filesrc (filesrc expects a local path, not a URI).
    g_object_set(impl->filesrc, "location", file_path.c_str(), nullptr);

    // Move to PAUSED to preroll.
    ret = gst_element_set_state(GST_ELEMENT(impl->pipeline), GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to preroll track: " << file_path
                  << std::endl;
        return false;
    }

    // Wait for preroll to complete (or async)
    ret = gst_element_get_state(GST_ELEMENT(impl->pipeline), &state, &pending, 5 * GST_SECOND);

    // Check if generation changed while waiting
    if (gen != impl->load_generation) {
        return false;
    }

    switch (ret) {
        case GST_STATE_CHANGE_SUCCESS:
            // Preroll completed synchronously
            break;
        case GST_STATE_CHANGE_ASYNC:
            // Still prerolling - will complete via ASYNC_DONE
            break;
        case GST_STATE_CHANGE_NO_PREROLL:
            // Live source - treat as success
            break;
        case GST_STATE_CHANGE_FAILURE:
        default:
            std::cerr << "AudioEngine: Preroll failed for: " << file_path
                      << std::endl;
            return false;
    }

    // Verify we have a valid audio stream (audio pad was linked)
    if (!impl->audio_pad_linked) {
        // Wait a bit more for decodebin to process
        ret = gst_element_get_state(GST_ELEMENT(impl->pipeline), &state, &pending, 2 * GST_SECOND);
        if (!impl->audio_pad_linked) {
            std::cerr << "AudioEngine: No audio stream found in: " << file_path
                      << std::endl;
            return false;
        }
    }

    // Query duration to verify the track is playable
    gint64 duration_ns = 0;
    if (!gst_element_query_duration(GST_ELEMENT(impl->pipeline),
                                    GST_FORMAT_TIME, &duration_ns) || duration_ns <= 0) {
        std::cerr << "AudioEngine: Could not query duration for: " << file_path
                  << std::endl;
        return false;
    }

    StateCallback state_cb_copy;
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        impl->current_state = PlaybackState::Paused;
        state_cb_copy = impl->state_cb;
    }
    if (state_cb_copy) state_cb_copy(impl->current_state);

    return true;
}

void AudioEngine::play() {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return;
    impl->set_gst_state(PlaybackState::Playing);
}

void AudioEngine::pause() {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return;
    impl->set_gst_state(PlaybackState::Paused);
}

void AudioEngine::stop() {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return;
    impl->set_gst_state(PlaybackState::Stopped);
}

void AudioEngine::seek(double position_seconds) {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return;

    GstState state = GST_STATE_NULL;
    gst_element_get_state(GST_ELEMENT(impl->pipeline), &state,
                          nullptr, GST_CLOCK_TIME_NONE);
    if (state != GST_STATE_PAUSED && state != GST_STATE_PLAYING) return;

    gint64 duration_ns = 0;
    if (!gst_element_query_duration(GST_ELEMENT(impl->pipeline),
                                    GST_FORMAT_TIME, &duration_ns)) {
        return;
    }

    if (position_seconds < 0.0) position_seconds = 0.0;
    gint64 target_ns = static_cast<gint64>(position_seconds * GST_SECOND);
    if (target_ns > duration_ns) target_ns = duration_ns;
    position_seconds = static_cast<double>(target_ns) / GST_SECOND;

    // Store pending seek with generation
    uint64_t gen = ++impl->seek_generation;
    impl->pending_seek_position.store(target_ns);
    impl->seek_pending.store(true);

    GstSeekFlags flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_ACCURATE);
    gboolean result = gst_element_seek_simple(
        GST_ELEMENT(impl->pipeline), GST_FORMAT_TIME, flags, target_ns);
    if (!result) {
        std::cerr << "AudioEngine: Seek failed at " << position_seconds
                  << " seconds (state=" << state << ")" << std::endl;
        impl->seek_pending.store(false);
    }
}

bool AudioEngine::can_seek() const {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return false;
    GstState state = GST_STATE_NULL;
    gst_element_get_state(GST_ELEMENT(impl->pipeline), &state,
                          nullptr, GST_CLOCK_TIME_NONE);
    if (state != GST_STATE_PAUSED && state != GST_STATE_PLAYING) return false;
    gint64 duration_ns = 0;
    return gst_element_query_duration(GST_ELEMENT(impl->pipeline),
                                      GST_FORMAT_TIME, &duration_ns);
}

void AudioEngine::set_volume(double volume) {
    Impl* impl = impl_.get();
    if (!impl) return;
    impl->volume_ = std::clamp(volume, 0.0, 1.0);
    if (impl->volume_element) {
        g_object_set(impl->volume_element, "volume", impl->volume_,
                     nullptr);
    }
}

double AudioEngine::get_volume() const {
    return impl_->volume_;
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

AudioEngine::PlaybackState AudioEngine::get_state() const {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    return impl_->current_state;
}

std::optional<double> AudioEngine::get_position() const {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return std::nullopt;

    gint64 pos = 0;
    if (!gst_element_query_position(GST_ELEMENT(impl->pipeline),
                                    GST_FORMAT_TIME, &pos)) {
        return std::nullopt;
    }
    return static_cast<double>(pos) / GST_SECOND;
}

std::optional<double> AudioEngine::get_duration() const {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return std::nullopt;

    gint64 dur = 0;
    if (!gst_element_query_duration(GST_ELEMENT(impl->pipeline),
                                    GST_FORMAT_TIME, &dur)) {
        return std::nullopt;
    }
    return static_cast<double>(dur) / GST_SECOND;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void AudioEngine::set_position_callback(PositionCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->position_cb = std::move(cb);
}

void AudioEngine::set_state_callback(StateCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->state_cb = std::move(cb);
}

void AudioEngine::set_eos_callback(EndOfStreamCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->eos_cb = std::move(cb);
}

void AudioEngine::set_error_callback(ErrorCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->error_cb = std::move(cb);
}

void AudioEngine::set_viz_samples_callback(VizSamplesCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->viz_cb = std::move(cb);
}

// ---------------------------------------------------------------------------
// Internal implementation
// ---------------------------------------------------------------------------

gboolean AudioEngine::Impl::bus_callback(GstBus* bus, GstMessage* msg,
                                         gpointer user_data) {
    (void)bus;
    AudioEngine* self = static_cast<AudioEngine*>(user_data);
    self->impl_->handle_message(msg);
    return TRUE;
}

void AudioEngine::Impl::handle_message(GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(msg, &old_state, &new_state,
                                                &pending_state);

                PlaybackState new_playback_state = PlaybackState::Stopped;
                switch (new_state) {
                    case GST_STATE_PLAYING:
                        new_playback_state = PlaybackState::Playing;
                        break;
                    case GST_STATE_PAUSED:
                        new_playback_state = PlaybackState::Paused;
                        break;
                    case GST_STATE_READY:
                    case GST_STATE_NULL:
                        new_playback_state = PlaybackState::Stopped;
                        break;
                    default:
                        break;
                }

                StateCallback cb;
                PlaybackState emitted_state = PlaybackState::Stopped;
                bool emit = false;
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    if (new_playback_state != current_state) {
                        current_state = new_playback_state;
                        emitted_state = current_state;
                        cb = state_cb;
                        emit = true;
                    }
                }
                if (emit && cb) cb(emitted_state);
            }
            break;
        }
        case GST_MESSAGE_ASYNC_DONE: {
            // Only emit position if this is the current load generation
            double pos = 0.0, dur = 0.0;
            {
                gint64 pos_ns = 0, dur_ns = 0;
                if (gst_element_query_position(
                        GST_ELEMENT(pipeline), GST_FORMAT_TIME, &pos_ns)) {
                    pos = static_cast<double>(pos_ns) / GST_SECOND;
                }
                if (gst_element_query_duration(
                        GST_ELEMENT(pipeline), GST_FORMAT_TIME, &dur_ns)) {
                    dur = static_cast<double>(dur_ns) / GST_SECOND;
                }
            }
            PositionCallback cb;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                cb = position_cb;
            }
            if (cb) cb(pos, dur);

            // If a seek was pending, clear the pending flag
            if (seek_pending.load()) {
                seek_pending.store(false);
            }
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED: {
            double pos = 0.0, dur = 0.0;
            {
                gint64 pos_ns = 0, dur_ns = 0;
                if (gst_element_query_position(
                        GST_ELEMENT(pipeline), GST_FORMAT_TIME, &pos_ns)) {
                    pos = static_cast<double>(pos_ns) / GST_SECOND;
                }
                if (gst_element_query_duration(
                        GST_ELEMENT(pipeline), GST_FORMAT_TIME, &dur_ns)) {
                    dur = static_cast<double>(dur_ns) / GST_SECOND;
                }
            }
            PositionCallback cb;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                cb = position_cb;
            }
            if (cb) cb(pos, dur);
            break;
        }
        case GST_MESSAGE_EOS: {
            EndOfStreamCallback cb;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                cb = eos_cb;
            }
            if (cb) cb();
            break;
        }
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            std::string error = err ? err->message : "Unknown error";
            if (debug) error += " (" + std::string(debug) + ")";
            if (err) g_error_free(err);
            g_free(debug);
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                current_state = PlaybackState::Error;
            }
            ErrorCallback error_cb_copy;
            StateCallback state_cb_copy;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                error_cb_copy = error_cb;
                state_cb_copy = state_cb;
            }
            if (error_cb_copy) error_cb_copy(error);
            if (state_cb_copy)
                state_cb_copy(PlaybackState::Error);
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(msg, &percent);
            StateCallback cb;
            PlaybackState emitted_state = PlaybackState::Stopped;
            bool emit = false;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                if (percent < 100) {
                    if (!buffering) {
                        buffering = true;
                        state_before_buffering = current_state;
                    }
                    current_state = PlaybackState::Buffering;
                    emitted_state = current_state;
                    cb = state_cb;
                    emit = true;
                } else {
                    buffering = false;
                    current_state = state_before_buffering;
                    emitted_state = current_state;
                    cb = state_cb;
                    emit = true;
                }
            }
            if (emit && cb) cb(emitted_state);
            break;
        }
        default:
            break;
    }
}

gboolean AudioEngine::Impl::position_timeout(gpointer user_data) {
    AudioEngine* self = static_cast<AudioEngine*>(user_data);
    self->impl_->update_position();
    return G_SOURCE_CONTINUE;
}

void AudioEngine::Impl::update_position() {
    gint64 pos_ns = 0, dur_ns = 0;
    bool have_pos = pipeline &&
        gst_element_query_position(GST_ELEMENT(pipeline), GST_FORMAT_TIME,
                                   &pos_ns);
    bool have_dur = pipeline &&
        gst_element_query_duration(GST_ELEMENT(pipeline), GST_FORMAT_TIME,
                                   &dur_ns);

    double position =
        have_pos ? static_cast<double>(pos_ns) / GST_SECOND : 0.0;
    double duration =
        have_dur ? static_cast<double>(dur_ns) / GST_SECOND : 0.0;

    PositionCallback cb;
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        cb = position_cb;
    }
    if (cb) cb(position, duration);
}

} // namespace lollypop