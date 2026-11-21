/**
 * Lollypop C++ - Audio Engine Implementation
 * GStreamer-based audio playback with visualization support
 *
 * Pipeline layout (custom audio sink wrapped in a GstBin):
 *
 *   playbin --(audio-sink)-->
 *     +-- bin "audio-sink-bin"
 *         +-- tee "tee"
 *             |-- (src pad 1) -- queue "viz-queue" (leaky=downstream)
 *             |             -- audioconvert -- audioresample -- capsfilter
 *             |             -- appsink "viz-sink" (max-buffers=1, drop=true, sync=false)
 *             |-- (src pad 2) -- queue "audio-queue"
 *             |             -- volume "audio-volume" (post-tee, so the
 *             |                visualizer always sees raw unscaled audio)
 *             |             -- autoaudiosink "audio-sink"
 *
 * Volume is controlled via a post-tee volume element on the output branch,
 * not via playbin's volume property. That keeps the visualization branch
 * fed with full-scale audio even at low playback volume.
 */

#include "audio_engine.hpp"

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>
#include <glib.h>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

namespace lollypop {

struct AudioEngine::Impl {
    GstElement* pipeline = nullptr;
    GstElement* playbin = nullptr;

    // Custom audio sink bin elements
    GstElement* sink_bin = nullptr;
    GstElement* tee = nullptr;
    GstElement* viz_queue = nullptr;
    GstElement* audio_queue = nullptr;
    GstElement* audioconvert = nullptr;
    GstElement* audioresample = nullptr;
    GstElement* capsfilter = nullptr;
    GstElement* appsink = nullptr;
    GstElement* volume_element = nullptr;
    GstElement* autoaudiosink = nullptr;

    // Requested tee source pads (released during shutdown)
    GstPad* tee_viz_pad = nullptr;
    GstPad* tee_audio_pad = nullptr;

    guint bus_watch_id = 0;
    guint position_timeout_id = 0;

    // Callback storage (protected by callback_mutex for thread safety)
    std::mutex callback_mutex;
    std::function<void(double position, double duration)> position_cb;
    std::function<void(AudioEngine::PlaybackState)> state_cb;
    std::function<void()> eos_cb;
    std::function<void(const std::string&)> error_cb;
    std::function<void(const float*, size_t)> viz_cb;

    // Playback state machine. Seeking and Buffering are orthogonal flags,
    // not states. Allowed transitions are enforced via kAllowed below.
    // current_state and the buffering bookkeeping are guarded by callback_mutex
    // (callbacks are always invoked *without* holding the lock to avoid
    // deadlocks when a callback re-enters a locking API).
    AudioEngine::PlaybackState current_state = AudioEngine::PlaybackState::Stopped;
    bool seeking = false;
    bool buffering = false;
    // The meaningful state (Playing/Paused) to restore to when buffering ends.
    AudioEngine::PlaybackState state_before_buffering = AudioEngine::PlaybackState::Stopped;
    double volume_ = 0.7;
    bool initialized = false;

    // Pipeline edges declared explicitly so every link is checked in a loop
    // and reachability can be verified before PLAYING.
    struct Edge { GstElement* src; GstElement* dst; };

    static gboolean bus_callback(GstBus* bus, GstMessage* msg, gpointer user_data);
    static gboolean position_timeout(gpointer user_data);
    void handle_message(GstMessage* msg);
    void update_position();
    bool set_gst_state(AudioEngine::PlaybackState state);

    bool build_pipeline();
    void destroy_pipeline();
    GstPad* request_tee_pad(const char* pad_name);
    bool verify_reachability();
};

// ---------------------------------------------------------------------------
// Playback state transition table (Bug 8)
//
// Rows/cols: Stopped, Playing, Paused, Buffering, Error
// Seeking and Buffering are flags, not states.
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

bool AudioEngine::Impl::set_gst_state(AudioEngine::PlaybackState state) {
    // Enforce the transition table. Buffering/Seeking are handled as flags
    // elsewhere and never transition through here as states.
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
        case AudioEngine::PlaybackState::Playing:
            gst_state = GST_STATE_PLAYING;
            break;
        case AudioEngine::PlaybackState::Paused:
            gst_state = GST_STATE_PAUSED;
            break;
        case AudioEngine::PlaybackState::Stopped:
            gst_state = GST_STATE_NULL;
            break;
        case AudioEngine::PlaybackState::Buffering:
        case AudioEngine::PlaybackState::Error:
            // These are flag-driven or terminal; do not drive the pipeline
            // directly from here.
            return false;
    }
    // Release callback_mutex before driving the pipeline: gst_element_set_state
    // can synchronously emit GST_MESSAGE_STATE_CHANGED, which re-acquires
    // callback_mutex in handle_message(). Holding it here would deadlock.
    GstStateChangeReturn ret =
        gst_element_set_state(pipeline, gst_state);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to set pipeline state" << std::endl;
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

    if (impl_->initialized) {
        return true;
    }

    impl_->pipeline = gst_pipeline_new("audio-pipeline");
    if (!impl_->pipeline) {
        std::cerr << "AudioEngine: Failed to create pipeline" << std::endl;
        return false;
    }

    impl_->playbin = gst_element_factory_make("playbin", "player");
    if (!impl_->playbin) {
        std::cerr << "AudioEngine: Failed to create playbin" << std::endl;
        gst_object_unref(impl_->pipeline);
        impl_->pipeline = nullptr;
        return false;
    }

    // Add playbin to the pipeline.
    gst_bin_add(GST_BIN(impl_->pipeline), impl_->playbin);

    // Build the custom audio sink bin (tee + queues + appsink + autoaudiosink).
    if (!impl_->build_pipeline()) {
        std::cerr << "AudioEngine: Failed to build custom audio sink pipeline"
                  << std::endl;
        impl_->destroy_pipeline();
        return false;
    }

    // Set playbin's audio-sink to our custom bin.
    g_object_set(impl_->playbin, "audio-sink", impl_->sink_bin, nullptr);

    // Bus watch for messages.
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(impl_->pipeline));
    impl_->bus_watch_id = gst_bus_add_watch(bus, Impl::bus_callback, this);
    gst_object_unref(bus);

    // Position polling timer (100ms = 10fps). Runs regardless of state so we
    // can report progress while loading, paused, or transitioning.
    impl_->position_timeout_id = g_timeout_add(100, Impl::position_timeout, this);

    // Set pipeline to READY state. In headless/CI environments without an
    // audio device this may fail; treat it as a warning rather than a hard
    // failure so the engine can still be constructed and tested. The actual
    // playback state transition is validated when load_track()/play() runs.
    GstStateChangeReturn ret =
        gst_element_set_state(impl_->pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Warning: could not set pipeline to READY "
                     "(no audio device available). Playback may fail."
                  << std::endl;
    }

    impl_->initialized = true;
    impl_->current_state = AudioEngine::PlaybackState::Stopped;
    return true;
}

void AudioEngine::shutdown() {
    Impl* impl = impl_.get();
    if (!impl) return;

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

    // Release requested tee pads.
    if (impl->tee_viz_pad) {
        gst_object_unref(impl->tee_viz_pad);
        impl->tee_viz_pad = nullptr;
    }
    if (impl->tee_audio_pad) {
        gst_object_unref(impl->tee_audio_pad);
        impl->tee_audio_pad = nullptr;
    }

    // Detach the sink bin from playbin. playbin owns a reference to the sink
    // bin via the "audio-sink" property, so we must NOT unref the bin itself —
    // playbin will handle cleanup when the pipeline is set to NULL and playbin
    // is unref'd.
    if (impl->playbin) {
        g_object_set(impl->playbin, "audio-sink", nullptr, nullptr);
    }

    // Set pipeline to NULL and unref. This will also unref playbin, which in
    // turn unrefs the sink bin and all its children.
    if (impl->pipeline) {
        gst_element_set_state(impl->pipeline, GST_STATE_NULL);
        gst_object_unref(impl->pipeline);
        impl->pipeline = nullptr;
    }

    impl->playbin = nullptr;
    impl->tee = nullptr;
    impl->viz_queue = nullptr;
    impl->audio_queue = nullptr;
    impl->audioconvert = nullptr;
    impl->audioresample = nullptr;
    impl->capsfilter = nullptr;
    impl->appsink = nullptr;
    impl->autoaudiosink = nullptr;

    // Reset callbacks.
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        impl->position_cb = nullptr;
        impl->state_cb = nullptr;
        impl->eos_cb = nullptr;
        impl->error_cb = nullptr;
        impl->viz_cb = nullptr;
    }

    impl->current_state = AudioEngine::PlaybackState::Stopped;
    impl->initialized = false;
}

// ---------------------------------------------------------------------------
// Pipeline construction
// ---------------------------------------------------------------------------

bool AudioEngine::Impl::build_pipeline() {
    // Create the bin that will serve as playbin's audio-sink.
    sink_bin = gst_bin_new("audio-sink-bin");
    if (!sink_bin) {
        std::cerr << "AudioEngine: Failed to create sink bin" << std::endl;
        return false;
    }

    tee = gst_element_factory_make("tee", "tee");
    viz_queue = gst_element_factory_make("queue", "viz-queue");
    audio_queue = gst_element_factory_make("queue", "audio-queue");
    volume_element = gst_element_factory_make("volume", "audio-volume");
    audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
    audioresample = gst_element_factory_make("audioresample", "audioresample");
    capsfilter = gst_element_factory_make("capsfilter", "viz-capsfilter");
    appsink = gst_element_factory_make("appsink", "viz-sink");
    autoaudiosink = gst_element_factory_make("autoaudiosink", "audio-sink");

    // Validate every element before proceeding.
    if (!tee) {
        std::cerr << "AudioEngine: Failed to create tee" << std::endl;
        return false;
    }
    if (!viz_queue) {
        std::cerr << "AudioEngine: Failed to create viz queue" << std::endl;
        return false;
    }
    if (!audio_queue) {
        std::cerr << "AudioEngine: Failed to create audio queue" << std::endl;
        return false;
    }
    if (!volume_element) {
        std::cerr << "AudioEngine: Failed to create volume element" << std::endl;
        return false;
    }
    if (!audioconvert) {
        std::cerr << "AudioEngine: Failed to create audioconvert" << std::endl;
        return false;
    }
    if (!audioresample) {
        std::cerr << "AudioEngine: Failed to create audioresample" << std::endl;
        return false;
    }
    if (!capsfilter) {
        std::cerr << "AudioEngine: Failed to create capsfilter" << std::endl;
        return false;
    }
    if (!appsink) {
        std::cerr << "AudioEngine: Failed to create appsink" << std::endl;
        return false;
    }
    if (!autoaudiosink) {
        std::cerr << "AudioEngine: Failed to create autoaudiosink" << std::endl;
        return false;
    }

    // Add all elements to the bin.
    gst_bin_add_many(GST_BIN(sink_bin), tee, viz_queue, audio_queue,
                     volume_element, audioconvert, audioresample, capsfilter,
                     appsink, autoaudiosink, nullptr);

    // Link the visualization branch: viz_queue -> audioconvert ->
    // audioresample -> capsfilter -> appsink.
    if (!gst_element_link_many(viz_queue, audioconvert, audioresample,
                               capsfilter, appsink, nullptr)) {
        std::cerr << "AudioEngine: Failed to link visualization branch"
                  << std::endl;
        return false;
    }

    // Link the audio output branch: audio_queue -> volume -> autoaudiosink.
    // Volume lives after the tee so the visualization branch sees raw,
    // unscaled audio even at low playback volume.
    if (!gst_element_link_many(audio_queue, volume_element, autoaudiosink,
                               nullptr)) {
        std::cerr << "AudioEngine: Failed to link audio output branch"
                  << std::endl;
        return false;
    }

    // Configure the visualization queue to drop buffers when the visualizer
    // lags, so playback is never blocked by visualization.
    g_object_set(viz_queue, "leaky", 2, nullptr);  // leaky=downstream

    // Configure appsink: pull-based, emit signals, single buffered, drop.
    g_object_set(appsink, "emit-signals", TRUE, nullptr);
    g_object_set(appsink, "max-buffers", 1, nullptr);
    g_object_set(appsink, "drop", TRUE, nullptr);
    g_object_set(appsink, "sync", FALSE, nullptr);
    g_object_set(appsink, "async", FALSE, nullptr);

    // Configure tee: no special properties needed; we request source pads.

    // Set the post-tee volume to the initial level.
    g_object_set(volume_element, "volume", volume_, nullptr);

    // Negotiate visualization caps: raw audio that appsink can consume.
    // We request S16LE stereo at 44100 Hz as a common supported format.
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
    g_object_set(capsfilter, "caps", caps, nullptr);
    // Release our reference; capsfilter takes its own.
    gst_caps_unref(caps);

    // Connect the appsink new-sample callback before linking pads.
    // Runs on the GStreamer streaming thread: pull the sample, convert to
    // mono float, and hand it to the viz callback. No GTK, no blocking,
    // no dynamic allocation after warmup (thread-local buffer reuse).
    g_signal_connect(appsink, "new-sample",
                     G_CALLBACK(+[](GstAppSink* sink, gpointer user_data) {
                         AudioEngine* self = static_cast<AudioEngine*>(user_data);
                         GstSample* sample = gst_app_sink_pull_sample(sink);
                         if (!sample) return GST_FLOW_OK;

                         GstBuffer* buffer = gst_sample_get_buffer(sample);
                         if (buffer) {
                             GstMapInfo map;
                             if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                                 // Interpret as interleaved S16LE samples.
                                 const gint16* samples =
                                     reinterpret_cast<const gint16*>(map.data);
                                 guint num_samples = map.size / sizeof(gint16);

                                 // Convert interleaved S16LE to mono float.
                                 // Reuse a thread-local buffer so the streaming
                                 // callback does not malloc after warmup.
                                 thread_local std::vector<float> floats;
                                 floats.clear();
                                 floats.reserve(num_samples / 2);
                                 for (guint i = 0; i + 1 < num_samples; i += 2) {
                                     float l = samples[i] / 32768.0f;
                                     float r = samples[i + 1] / 32768.0f;
                                     floats.push_back((l + r) * 0.5f);
                                 }

                                 std::lock_guard<std::mutex> lock(
                                     self->impl_->callback_mutex);
                                 if (self->impl_->viz_cb) {
                                     self->impl_->viz_cb(floats.data(),
                                                         floats.size());
                                 }
                                 gst_buffer_unmap(buffer, &map);
                             }
                         }
                         gst_sample_unref(sample);
                         return GST_FLOW_OK;
                     }),
                     this);

    // Create a ghost pad for the bin's sink pad, linking it to the tee's sink
    // pad. Store the tee sink pad, create the ghost pad, then release the
    // original reference.
    GstPad* tee_sink_pad = gst_element_get_static_pad(tee, "sink");
    if (!tee_sink_pad) {
        std::cerr << "AudioEngine: Failed to get tee sink pad" << std::endl;
        return false;
    }
    GstPad* ghost_pad = gst_ghost_pad_new("sink", tee_sink_pad);
    if (!ghost_pad) {
        std::cerr << "AudioEngine: Failed to create ghost pad" << std::endl;
        gst_object_unref(tee_sink_pad);
        return false;
    }
    if (!gst_element_add_pad(sink_bin, ghost_pad)) {
        std::cerr << "AudioEngine: Failed to add ghost pad to sink bin"
                  << std::endl;
        gst_object_unref(ghost_pad);
        gst_object_unref(tee_sink_pad);
        return false;
    }
    gst_object_unref(ghost_pad);
    gst_object_unref(tee_sink_pad);

    // Request two separate source pads from the tee, one for each branch.
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
    GstPad* audio_queue_sink = gst_element_get_static_pad(audio_queue, "sink");
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

    // Verify the pipeline graph is fully connected from tee source to both
    // sinks before returning. Missing edges are a common defect class.
    if (!verify_reachability()) {
        std::cerr << "AudioEngine: Pipeline reachability check failed"
                  << std::endl;
        return false;
    }

    return true;
}

bool AudioEngine::Impl::verify_reachability() {
    // Build an explicit edge list for the bin and check that every element
    // reachable from the tee's source pads can reach a sink.
    std::vector<Edge> edges;
    auto add_edge = [&](GstElement* a, GstElement* b) {
        if (a && b) edges.push_back({a, b});
    };

    // Tee source -> branch heads (via requested pads). We can't easily
    // introspect pad links here, so we declare the intended topology.
    add_edge(tee, viz_queue);
    add_edge(tee, audio_queue);
    add_edge(viz_queue, audioconvert);
    add_edge(audioconvert, audioresample);
    add_edge(audioresample, capsfilter);
    add_edge(capsfilter, appsink);
    add_edge(audio_queue, volume_element);
    add_edge(volume_element, autoaudiosink);

    if (edges.empty()) return false;

    // Reachability: mark everything reachable from tee, then confirm the two
    // terminal sinks are reached.
    std::vector<GstElement*> reachable;
    reachable.push_back(tee);
    size_t idx = 0;
    while (idx < reachable.size()) {
        GstElement* cur = reachable[idx++];
        for (const auto& e : edges) {
            if (e.src == cur) {
                bool already = false;
                for (GstElement* r : reachable) {
                    if (r == e.dst) { already = true; break; }
                }
                if (!already) reachable.push_back(e.dst);
            }
        }
    }

    auto is_reached = [&](GstElement* e) {
        for (GstElement* r : reachable) {
            if (r == e) return true;
        }
        return false;
    };

    return is_reached(appsink) && is_reached(autoaudiosink);
}

void AudioEngine::Impl::destroy_pipeline() {
    // Release requested tee pads.
    if (tee_viz_pad) {
        gst_object_unref(tee_viz_pad);
        tee_viz_pad = nullptr;
    }
    if (tee_audio_pad) {
        gst_object_unref(tee_audio_pad);
        tee_audio_pad = nullptr;
    }

    // Detach the sink bin from playbin. playbin owns a reference to the sink
    // bin via the "audio-sink" property, so we must NOT unref the bin itself.
    if (sink_bin && playbin) {
        g_object_set(playbin, "audio-sink", nullptr, nullptr);
    }

    tee = nullptr;
    viz_queue = nullptr;
    audio_queue = nullptr;
    audioconvert = nullptr;
    audioresample = nullptr;
    capsfilter = nullptr;
    appsink = nullptr;
    volume_element = nullptr;
    autoaudiosink = nullptr;
}

GstPad* AudioEngine::Impl::request_tee_pad(const char* pad_name) {
    GstPad* pad = gst_element_request_pad_simple(tee, pad_name);
    if (!pad) {
        // Fall back to the older API if request_pad_simple is unavailable.
        pad = gst_element_get_request_pad(tee, pad_name);
    }
    return pad;
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

bool AudioEngine::load_track(const std::string& file_path) {
    Impl* impl = impl_.get();
    if (!impl || !impl->playbin) return false;

    // Validate the file path exists and is readable.
    if (file_path.empty()) {
        std::cerr << "AudioEngine: Empty file path" << std::endl;
        return false;
    }

    // Convert to a URI, validating the result.
    gchar* uri = gst_filename_to_uri(file_path.c_str(), nullptr);
    if (!uri) {
        std::cerr << "AudioEngine: Failed to convert path to URI: "
                  << file_path << std::endl;
        return false;
    }

    // First ensure pipeline is in a clean state before setting URI.
    gst_element_set_state(impl->pipeline, GST_STATE_NULL);

    // Set the URI on playbin.
    g_object_set(impl->playbin, "uri", uri, nullptr);
    g_free(uri);

    // Move to PAUSED to preroll the new track.
    GstStateChangeReturn ret =
        gst_element_set_state(impl->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "AudioEngine: Failed to preroll track: " << file_path
                  << std::endl;
        return false;
    }

    // Update state tracking.
    StateCallback state_cb_copy;
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        impl->current_state = AudioEngine::PlaybackState::Paused;
        state_cb_copy = impl->state_cb;
    }
    // Invoke outside the lock to avoid deadlock if the callback re-enters a
    // locking API.
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

    // Validate the requested position.
    if (position_seconds < 0.0) position_seconds = 0.0;

    // Clamp to duration if known.
    gint64 duration_ns = 0;
    if (gst_element_query_duration(impl->pipeline, GST_FORMAT_TIME,
                                   &duration_ns)) {
        gint64 max_ns = duration_ns;
        gint64 target_ns =
            static_cast<gint64>(position_seconds * GST_SECOND);
        if (target_ns > max_ns) target_ns = max_ns;
        position_seconds = static_cast<double>(target_ns) / GST_SECOND;
    }

    GstSeekFlags flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT);
    gboolean result = gst_element_seek_simple(impl->pipeline, GST_FORMAT_TIME,
                                              flags,
                                              static_cast<gint64>(
                                                  position_seconds * GST_SECOND));
    if (!result) {
        std::cerr << "AudioEngine: Seek failed at " << position_seconds
                  << " seconds" << std::endl;
    }
}

void AudioEngine::set_volume(double volume) {
    Impl* impl = impl_.get();
    if (!impl) return;
    impl->volume_ = std::clamp(volume, 0.0, 1.0);
    // Volume is applied post-tee on the output branch so the visualizer
    // branch always sees full-scale audio.
    if (impl->volume_element) {
        g_object_set(impl->volume_element, "volume", impl->volume_, nullptr);
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
    if (!gst_element_query_position(impl->pipeline, GST_FORMAT_TIME, &pos)) {
        return std::nullopt;
    }
    return static_cast<double>(pos) / GST_SECOND;
}

std::optional<double> AudioEngine::get_duration() const {
    Impl* impl = impl_.get();
    if (!impl || !impl->pipeline) return std::nullopt;

    gint64 dur = 0;
    if (!gst_element_query_duration(impl->pipeline, GST_FORMAT_TIME, &dur)) {
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

                AudioEngine::PlaybackState new_playback_state =
                    AudioEngine::PlaybackState::Stopped;
                switch (new_state) {
                    case GST_STATE_PLAYING:
                        new_playback_state = AudioEngine::PlaybackState::Playing;
                        break;
                    case GST_STATE_PAUSED:
                        new_playback_state = AudioEngine::PlaybackState::Paused;
                        break;
                    case GST_STATE_READY:
                    case GST_STATE_NULL:
                        new_playback_state =
                            AudioEngine::PlaybackState::Stopped;
                        break;
                    default:
                        break;
                }

                AudioEngine::StateCallback cb;
                AudioEngine::PlaybackState emitted_state =
                    AudioEngine::PlaybackState::Stopped;
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
                // Invoke outside the lock to avoid deadlock if the callback
                // re-enters a locking API (e.g. set_state_callback).
                if (emit && cb) cb(emitted_state);
            }
            break;
        }
        case GST_MESSAGE_ASYNC_DONE: {
            // An asynchronous state change (e.g. preroll) has completed.
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED: {
            // Duration may become known after preroll; notify position
            // listeners so they can refresh duration.
            double pos = 0.0, dur = 0.0;
            {
                gint64 pos_ns = 0, dur_ns = 0;
                if (gst_element_query_position(pipeline, GST_FORMAT_TIME,
                                               &pos_ns)) {
                    pos = static_cast<double>(pos_ns) / GST_SECOND;
                }
                if (gst_element_query_duration(pipeline, GST_FORMAT_TIME,
                                               &dur_ns)) {
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
                current_state = AudioEngine::PlaybackState::Error;
            }
            ErrorCallback error_cb_copy;
            StateCallback state_cb_copy;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                error_cb_copy = error_cb;
                state_cb_copy = state_cb;
            }
            // Invoke outside the lock.
            if (error_cb_copy) error_cb_copy(error);
            if (state_cb_copy) state_cb_copy(AudioEngine::PlaybackState::Error);
            break;
        }
        case GST_MESSAGE_TAG: {
            // Metadata tags available (handled elsewhere via TagLib).
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(msg, &percent);
            AudioEngine::StateCallback cb;
            AudioEngine::PlaybackState emitted_state =
                AudioEngine::PlaybackState::Stopped;
            bool emit = false;
            {
                std::lock_guard<std::mutex> lock(callback_mutex);
                if (percent < 100) {
                    if (!buffering) {
                        buffering = true;
                        state_before_buffering = current_state;
                    }
                    current_state = AudioEngine::PlaybackState::Buffering;
                    emitted_state = current_state;
                    cb = state_cb;
                    emit = true;
                } else {
                    // Buffering complete: restore the previous meaningful
                    // state (Playing or Paused) so the controller, which
                    // ignores Buffering, can advance playback.
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
        case GST_MESSAGE_CLOCK_LOST:
        case GST_MESSAGE_CLOCK_PROVIDE:
        case GST_MESSAGE_LATENCY:
        case GST_MESSAGE_STREAM_START:
        case GST_MESSAGE_STREAM_COLLECTION:
            // Informational messages; no action required.
            break;
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
    // Report position regardless of state so the UI can show progress while
    // loading, paused, or transitioning.
    gint64 pos_ns = 0, dur_ns = 0;
    bool have_pos =
        pipeline && gst_element_query_position(pipeline, GST_FORMAT_TIME,
                                               &pos_ns);
    bool have_dur =
        pipeline && gst_element_query_duration(pipeline, GST_FORMAT_TIME,
                                               &dur_ns);

    double position = have_pos ? static_cast<double>(pos_ns) / GST_SECOND : 0.0;
    double duration = have_dur ? static_cast<double>(dur_ns) / GST_SECOND : 0.0;

    PositionCallback cb;
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        cb = position_cb;
    }
    if (cb) cb(position, duration);
}

} // namespace lollypop
