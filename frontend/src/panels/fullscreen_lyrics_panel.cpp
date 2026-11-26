/**
 * Lollypop C++ - Fullscreen Lyrics Panel Implementation
 */

#include "panels/fullscreen_lyrics_panel.hpp"

#include "lyrics/LyricTypes.hpp"
#include "services/ILyricsService.hpp"
#include "ui/components/SyncedLyricsView.hpp"

#include <gtk/gtk.h>

#include <algorithm>

namespace lollypop {
namespace {

constexpr double kLyricsAnimMs = 340.0;

} // namespace

FullscreenLyricsPanel::FullscreenLyricsPanel(ILyricsService* lyrics_service)
    : lyrics_service_(lyrics_service),
      revealer_(gtk_revealer_new()),
      frame_(gtk_frame_new(nullptr)) {
    lyrics_view_ = std::make_unique<SyncedLyricsView>(
        [this](int position_ms) {
            if (seek_callback_) seek_callback_(position_ms);
        },
        true);

    gtk_widget_add_css_class(frame_, "fullscreen-lyrics-frame");
    gtk_widget_set_hexpand(frame_, TRUE);
    gtk_widget_set_vexpand(frame_, TRUE);
    gtk_widget_set_size_request(frame_, 480, -1);
    gtk_widget_set_visible(frame_, TRUE);
    last_width_ = 480;
    gtk_frame_set_child(GTK_FRAME(frame_), lyrics_view_->get_widget());

    gtk_widget_add_css_class(revealer_, "fullscreen-lyrics");
    gtk_revealer_set_transition_type(
        GTK_REVEALER(revealer_), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(
        GTK_REVEALER(revealer_), static_cast<guint>(kLyricsAnimMs));
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer_), FALSE);
    gtk_widget_set_hexpand(revealer_, FALSE);
    gtk_widget_set_vexpand(revealer_, TRUE);
    gtk_widget_set_halign(revealer_, GTK_ALIGN_END);
    gtk_widget_set_valign(revealer_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(revealer_, TRUE);
    gtk_revealer_set_child(GTK_REVEALER(revealer_), frame_);
}

FullscreenLyricsPanel::~FullscreenLyricsPanel() {
    *alive_ = false;
}

void FullscreenLyricsPanel::set_accent(const GdkRGBA& accent) {
    if (lyrics_view_) lyrics_view_->set_accent(accent);
}

void FullscreenLyricsPanel::set_visible(bool visible) {
    reveal_from_ = reveal_;
    reveal_target_ = visible ? 1.0 : 0.0;
    anim_start_ = g_get_monotonic_time();

    if (revealer_) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(revealer_), visible);
        if (anim_source_ != 0) {
            gtk_widget_remove_tick_callback(revealer_, anim_source_);
            anim_source_ = 0;
        }
        anim_source_ = gtk_widget_add_tick_callback(
            revealer_, on_animation_tick, this, nullptr);
    }
}

void FullscreenLyricsPanel::set_width(int width) {
    if (!frame_ || width <= 0 || width == last_width_) return;
    last_width_ = width;
    gtk_widget_set_size_request(frame_, width, -1);
}

void FullscreenLyricsPanel::set_right_margin(int margin) {
    if (!revealer_ || margin < 0 || margin == last_right_margin_) return;
    last_right_margin_ = margin;
    gtk_widget_set_margin_end(revealer_, margin);
}

void FullscreenLyricsPanel::request_lyrics_for_track(
    const std::string& track_id) {
    current_track_id_ = track_id;
    if (lyrics_view_) {
        lyrics_view_->set_message(track_id.empty()
            ? "No lyrics available."
            : "Loading lyrics…");
    }
    if (!lyrics_service_ || track_id.empty()) return;

    std::shared_ptr<bool> alive = alive_;
    lyrics_service_->request_synced_lyrics(
        track_id,
        [this, alive, track_id](const std::string& id,
                                const std::string& lrc_text) {
            if (!*alive || id != current_track_id_) return;
            if (!lyrics_view_) return;

            if (lrc_text.empty()) {
                lyrics_view_->set_message("No lyrics available.");
                return;
            }
            if (lrc_text.find('[') != std::string::npos &&
                lrc_text.find(']') != std::string::npos) {
                SyncedLyrics parsed = LrcParser::parse(lrc_text);
                if (parsed.valid()) {
                    lyrics_view_->set_lyrics(parsed);
                    return;
                }
            }
            lyrics_view_->set_message(lrc_text);
        });
}

void FullscreenLyricsPanel::set_position(int position_ms, int duration_ms) {
    if (lyrics_view_) lyrics_view_->set_position(position_ms, duration_ms);
}

gboolean FullscreenLyricsPanel::on_animation_tick(GtkWidget* widget,
                                                  GdkFrameClock*,
                                                  gpointer user_data) {
    auto* self = static_cast<FullscreenLyricsPanel*>(user_data);
    const gint64 elapsed_us = g_get_monotonic_time() - self->anim_start_;
    const double t = std::clamp(
        static_cast<double>(elapsed_us) / (kLyricsAnimMs * 1000.0), 0.0, 1.0);
    // Quintic ease-in-out: slow departure and arrival, faster middle.
    const double eased =
        t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    self->reveal_ = self->reveal_from_ +
        (self->reveal_target_ - self->reveal_from_) * eased;

    if (self->reveal_callback_) self->reveal_callback_(self->reveal_);
    gtk_widget_queue_resize(widget);

    if (t >= 1.0) {
        self->reveal_ = self->reveal_target_;
        self->anim_source_ = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

} // namespace lollypop
