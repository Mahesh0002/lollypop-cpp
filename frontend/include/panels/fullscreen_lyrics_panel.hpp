/**
 * Lollypop C++ - Fullscreen Lyrics Panel
 *
 * Owns only the synced lyrics surface: the revealer, framed list, async lyric
 * requests, and its slide animation. It is added as an overlay sibling of the
 * FullscreenPlayerPanel, so revealing it never resizes the player. A reveal
 * callback lets the coordinator slide the player by the same eased amount.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace lollypop {

class ILyricsService;
class SyncedLyricsView;

class FullscreenLyricsPanel {
public:
    using SeekCallback = std::function<void(int position_ms)>;
    using RevealCallback = std::function<void(double reveal)>;

    explicit FullscreenLyricsPanel(ILyricsService* lyrics_service);
    ~FullscreenLyricsPanel();

    FullscreenLyricsPanel(const FullscreenLyricsPanel&) = delete;
    FullscreenLyricsPanel& operator=(const FullscreenLyricsPanel&) = delete;

    GtkWidget* get_widget() const { return revealer_; }
    double get_reveal() const { return reveal_; }

    void set_seek_callback(SeekCallback callback) {
        seek_callback_ = std::move(callback);
    }
    void set_reveal_callback(RevealCallback callback) {
        reveal_callback_ = std::move(callback);
    }

    void set_accent(const GdkRGBA& accent);
    void set_visible(bool visible);
    void set_width(int width);
    void set_right_margin(int margin);

    void request_lyrics_for_track(const std::string& track_id);
    void set_position(int position_ms, int duration_ms);

private:
    ILyricsService* lyrics_service_ = nullptr;
    GtkWidget* revealer_ = nullptr;
    GtkWidget* frame_ = nullptr;
    std::unique_ptr<SyncedLyricsView> lyrics_view_;

    // Async lyric requests may outlive the panel; callbacks check this flag.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    SeekCallback seek_callback_;
    RevealCallback reveal_callback_;
    std::string current_track_id_;

    double reveal_ = 0.0;
    double reveal_from_ = 0.0;
    double reveal_target_ = 0.0;
    gint64 anim_start_ = 0;
    guint anim_source_ = 0;
    int last_width_ = 0;
    int last_right_margin_ = -1;

    static gboolean on_animation_tick(GtkWidget* widget,
                                      GdkFrameClock* clock,
                                      gpointer user_data);
};

} // namespace lollypop
