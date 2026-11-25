/**
 * Lollypop C++ - Synced Lyrics View
 * Scrollable list of lyric rows with auto-scroll and progressive fill
 */

#pragma once

#include "lyrics/LyricTypes.hpp"
#include "ui/components/LyricRow.hpp"
#include <gtk/gtk.h>
#include <vector>
#include <memory>
#include <functional>

namespace lollypop {

class SyncedLyricsView {
public:
    using SeekCallback = std::function<void(int position_ms)>;

    SyncedLyricsView(SeekCallback on_seek = nullptr, bool fullscreen = false);
    ~SyncedLyricsView();

    GtkWidget* get_widget() const { return widget_; }

    void set_accent(const GdkRGBA& accent);
    void set_lyrics(const SyncedLyrics& lyrics);
    void set_message(const std::string& message);
    void set_position(int position_ms, int duration_ms = 0);
    void scroll_to_current(bool animate = false);

    bool has_lyrics() const { return synced_; }

private:
    SeekCallback on_seek_;
    bool fullscreen_;
    GdkRGBA accent_{0.65, 0.55, 0.98, 1.0};

    GtkWidget* widget_ = nullptr;
    GtkWidget* scroller_ = nullptr;
    GtkWidget* list_box_ = nullptr;
    GtkWidget* heading_ = nullptr;
    GtkWidget* message_label_ = nullptr;

    std::vector<std::unique_ptr<LyricRow>> rows_;
    std::vector<int> line_starts_;  // start_ms for each line
    SyncedLyrics current_lyrics_;
    bool synced_ = false;
    int active_index_ = -1;
    int duration_ms_ = 0;
    bool first_sync_ = true;
    double follow_paused_until_ = 0.0;
    int scroll_token_ = 0;
    bool programmatic_scroll_ = false;
    guint scroll_source_ = 0;
    guint anim_source_ = 0;
    gpointer scroll_data_ = nullptr;
    gpointer anim_data_ = nullptr;

    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_owner_;

    void create_widget();
    void clear();
    void build_rows();
    void set_active_index(int index);
    void schedule_scroll(int index, bool animate);
    void schedule_top(bool animate);
    void animate_to(double target);
    void cancel_scroll();
    void set_adjustment_value(GtkAdjustment* adj, double value);
    void update_scroll_position();
    static void on_vadjustment_changed(GtkAdjustment* adjustment, gpointer user_data);
    static gboolean on_scroll_measure(gpointer user_data);
    static gboolean on_animate_step(gpointer user_data);
};

} // namespace lollypop