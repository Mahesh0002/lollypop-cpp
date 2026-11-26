/**
 * Lollypop C++ - Synced Lyrics View
 * Apple Music-style auto-scrolling list of lyric rows with karaoke fill.
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
    GtkWidget* head_spacer_ = nullptr;
    GtkWidget* tail_spacer_ = nullptr;

    std::vector<std::unique_ptr<LyricRow>> rows_;
    std::vector<int> line_starts_;  // start_ms for each line
    SyncedLyrics current_lyrics_;
    bool synced_ = false;
    int active_index_ = -1;
    int duration_ms_ = 0;

    // Continuous-follow state. Anchored scrolling is driven by a persistent
    // tick callback rather than a one-shot animation per line change.
    double follow_target_ = 0.0;
    bool target_valid_ = false;
    double last_frame_time_ = 0.0;
    double follow_paused_until_ = 0.0;
    bool programmatic_scroll_ = false;
    guint tick_id_ = 0;

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
    void reset_follow();
    void update_follow_target();

    double row_content_y(GtkWidget* row_widget) const;
    static double row_height(GtkWidget* row_widget);
    double compute_scroll_target(GtkAdjustment* adj) const;
    void update_head_spacer(GtkAdjustment* adj);
    void update_tail_spacer(GtkAdjustment* adj);
    void update_row_prominence(GtkAdjustment* adj);
    void reset_row_prominence();
    void set_adjustment_value(GtkAdjustment* adj, double value);

    static void on_vadjustment_changed(GtkAdjustment* adjustment, gpointer user_data);
    static gboolean on_follow_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer user_data);
};

} // namespace lollypop
