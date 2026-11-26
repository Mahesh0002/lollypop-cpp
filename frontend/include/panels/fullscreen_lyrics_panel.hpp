/**
 * Lollypop C++ - Fullscreen Lyrics Panel
 *
 * Owns only the synced lyrics surface: the revealer, framed list, async lyric
 * requests, and its slide transition. It is added as an overlay sibling of the
 * FullscreenPlayerPanel, so revealing it never resizes the player. The
 * coordinator owns the matching player slide animation.
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

    explicit FullscreenLyricsPanel(ILyricsService* lyrics_service);
    ~FullscreenLyricsPanel();

    FullscreenLyricsPanel(const FullscreenLyricsPanel&) = delete;
    FullscreenLyricsPanel& operator=(const FullscreenLyricsPanel&) = delete;

    GtkWidget* get_widget() const { return revealer_; }

    void set_seek_callback(SeekCallback callback) {
        seek_callback_ = std::move(callback);
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
    std::string current_track_id_;

    int last_width_ = 0;
    int last_right_margin_ = -1;
};

} // namespace lollypop
