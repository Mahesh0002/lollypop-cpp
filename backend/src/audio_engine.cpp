/**
 * Lollypop C++ - Audio Engine Implementation
 * GStreamer-based audio playback with visualization support
 */

#include "audio_engine.hpp"

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>
#include <glib.h>
#include <iostream>
#include <algorithm>

namespace lollypop {

struct AudioEngine::Impl {
    GstElement* pipeline = nullptr;
    GstElement* playbin = nullptr;
    GstElement* volume = nullptr;
    GstElement* audio_sink = nullptr;
    GstElement* tee = nullptr;
    GstElement* appsink = nullptr;

    // Use full std::function types to avoid nested class typedef issues
    std::function<void(double position, double duration)> position_cb;
    std::function<void(AudioEngine::PlaybackState)> state_cb;
    std::function<void()> eos_cb;
    std::function<void(const std::string&)> error_cb;

    AudioEngine::PlaybackState current_state = AudioEngine::PlaybackState::Stopped;
    double volume_ = 0.7;

    guint bus_watch_id = 0;
    guint position_timeout_id = 0;

    static gboolean bus_callback(GstBus* bus, GstMessage* msg, gpointer user_data);
    static gboolean position_timeout(gpointer user_data);
    void handle_message(GstMessage* msg);
    void update_position();
    void set_gst_state(AudioEngine::PlaybackState state);
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
    if (!impl_->pipeline) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // Create playbin for automatic format handling
    impl_->playbin = gst_element_factory_make("playbin", "player");
    if (!impl_->playbin) {
        std::cerr << "Failed to create playbin" << std::endl;
        return false;
    }

    // Create custom audio sink with tee for visualization
    impl_->audio_sink = gst_bin_new("audio-sink");
    if (!impl_->audio_sink) {
        std::cerr << "Failed to create audio-sink bin" << std::endl;
        return false;
    }

    // Elements for audio sink bin
    GstElement* convert = gst_element_factory_make("audioconvert", "convert");
    GstElement* resample = gst_element_factory_make("audioresample", "resample");
    impl_->tee = gst_element_factory_make("tee", "tee");

    // Output branch
    GstElement* queue_out = gst_element_factory_make("queue", "queue-out");
    GstElement* auto_sink = gst_element_factory_make("autoaudiosink", "auto-sink");

    // Visualization branch
    GstElement* queue_vis = gst_element_factory_make("queue", "queue-vis");
    impl_->appsink = gst_element_factory_make("appsink", "vis-sink");
    if (!impl_->appsink) {
        std::cerr << "Failed to create appsink" << std::endl;
        return false;
    }

    GstCaps* caps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "F32LE",
        "layout", G_TYPE_STRING, "interleaved",
        "rate", G_TYPE_INT, 44100,
        "channels", G_TYPE_INT, 2,
        nullptr);
    g_object_set(impl_->appsink,
        "emit-signals", TRUE,
        "sync", FALSE,
        "max-buffers", 1,
        "drop", TRUE,
        "caps", caps,
        nullptr);
    gst_caps_unref(caps);

    // Build audio sink bin
    gst_bin_add_many(GST_BIN(impl_->audio_sink), 
        convert, resample, impl_->tee, 
        queue_out, auto_sink, 
        queue_vis, impl_->appsink, 
        nullptr);

    // Link output branch: convert -> resample -> tee -> queue_out -> auto_sink
    gst_element_link_many(convert, resample, impl_->tee, nullptr);
    gst_element_link_many(queue_out, auto_sink, nullptr);

    // Link visualization branch: tee -> queue_vis -> appsink
    gst_element_link_many(queue_vis, impl_->appsink, nullptr);

    // Link tee pads
    GstPad* tee_src_out = gst_element_get_request_pad(impl_->tee, "src_%u");
    GstPad* queue_out_sink = gst_element_get_static_pad(queue_out, "sink");
    GstPad* tee_src_vis = gst_element_get_request_pad(impl_->tee, "src_%u");
    GstPad* queue_vis_sink = gst_element_get_static_pad(queue_vis, "sink");

    gst_pad_link(tee_src_out, queue_out_sink);
    gst_pad_link(tee_src_vis, queue_vis_sink);

    gst_object_unref(tee_src_out);
    gst_object_unref(queue_out_sink);
    gst_object_unref(tee_src_vis);
    gst_object_unref(queue_vis_sink);

    // Create ghost pad for audio sink bin
    GstPad* sink_pad = gst_element_get_static_pad(convert, "sink");
    GstPad* ghost_pad = gst_ghost_pad_new("sink", sink_pad);
    gst_element_add_pad(impl_->audio_sink, ghost_pad);
    gst_object_unref(sink_pad);

    // Set audio sink on playbin
    g_object_set(impl_->playbin, "audio-sink", impl_->audio_sink, nullptr);

    // Volume control
    impl_->volume = gst_element_factory_make("volume", "volume");
    if (impl_->volume) {
        g_object_set(impl_->volume, "volume", impl_->volume_, nullptr);
        // Insert volume before tee in the chain
        // We'll set it on playbin's volume property instead for simplicity
        g_object_set(impl_->playbin, "volume", impl_->volume_, nullptr);
    }

    // Add playbin to pipeline
    gst_bin_add(GST_BIN(impl_->pipeline), impl_->playbin);

    // Bus watch for messages
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(impl_->pipeline));
    impl_->bus_watch_id = gst_bus_add_watch(bus, Impl::bus_callback, this);
    gst_object_unref(bus);

    // Position polling timer (100ms = 10fps)
    impl_->position_timeout_id = g_timeout_add(100, Impl::position_timeout, this);

    // Set pipeline to READY state
    GstStateChangeReturn ret = gst_element_set_state(impl_->pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to set pipeline to READY state" << std::endl;
        return false;
    }

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
        std::cerr << "Failed to load track: " << file_path << std::endl;
        return false;
    }
    return true;
}

void AudioEngine::play() {
    impl_->set_gst_state(PlaybackState::Playing);
}

void AudioEngine::pause() {
    impl_->set_gst_state(PlaybackState::Paused);
}

void AudioEngine::stop() {
    impl_->set_gst_state(AudioEngine::PlaybackState::Stopped);
    // Reset to READY for next track
    gst_element_set_state(impl_->pipeline, GST_STATE_READY);
}

void AudioEngine::seek(double position_seconds) {
    gst_element_seek_simple(impl_->pipeline, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        static_cast<gint64>(position_seconds * GST_SECOND));
}

void AudioEngine::set_volume(double volume) {
    impl_->volume_ = std::clamp(volume, 0.0, 1.0);
    if (impl_->playbin) {
        g_object_set(impl_->playbin, "volume", impl_->volume_, nullptr);
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
    if (impl_->pipeline) {
        gst_element_query_position(impl_->pipeline, GST_FORMAT_TIME, &pos);
    }
    return pos / 1e9;
}

double AudioEngine::get_duration() const {
    gint64 dur = 0;
    if (impl_->pipeline) {
        gst_element_query_duration(impl_->pipeline, GST_FORMAT_TIME, &dur);
    }
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

void AudioEngine::Impl::set_gst_state(AudioEngine::PlaybackState state) {
    GstState gst_state;
    switch (state) {
        case AudioEngine::PlaybackState::Playing: gst_state = GST_STATE_PLAYING; break;
        case AudioEngine::PlaybackState::Paused: gst_state = GST_STATE_PAUSED; break;
        case AudioEngine::PlaybackState::Stopped: gst_state = GST_STATE_NULL; break;
        default: return;
    }
    gst_element_set_state(pipeline, gst_state);
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

                AudioEngine::PlaybackState new_playback_state = AudioEngine::PlaybackState::Stopped;
                switch (new_state) {
                    case GST_STATE_PLAYING: new_playback_state = AudioEngine::PlaybackState::Playing; break;
                    case GST_STATE_PAUSED: new_playback_state = AudioEngine::PlaybackState::Paused; break;
                    case GST_STATE_READY:
                    case GST_STATE_NULL: new_playback_state = AudioEngine::PlaybackState::Stopped; break;
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
            current_state = AudioEngine::PlaybackState::Error;
            if (state_cb) state_cb(current_state);
            break;
        }
        case GST_MESSAGE_TAG: {
            // Metadata tags available
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(msg, &percent);
            if (percent < 100) {
                current_state = AudioEngine::PlaybackState::Buffering;
                if (state_cb) state_cb(current_state);
            }
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