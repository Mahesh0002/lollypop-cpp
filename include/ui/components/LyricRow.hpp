/**
 * Lollypop C++ - Lyric Row
 * Single clickable lyric row with state (future/active/past) and fill progress
 */

#pragma once

#include "ui/components/SyncedLyricsLabel.hpp"
#include "lyrics/LyricTypes.hpp"
#include <gtk/gtk.h>
#include <functional>
#include <memory>

namespace lollypop {

class LyricRow {
public:
    enum class State { Future, Active, Past };

    using SeekCallback = std::function<void(int start_ms)>;

    LyricRow(const SyncedLyricLine& line, const GdkRGBA& accent, SeekCallback on_seek, double xalign = 0.0);
    ~LyricRow();

    GtkWidget* get_widget() const { return widget_; }

    void set_accent(const GdkRGBA& accent);
    void set_state(State state);
    void set_progress(double fraction);  // 0.0 - 1.0 for active line
    void set_gap_phase(double phase);    // For pulsing dots
    void set_prominence(double alpha);   // 0.0 - 1.0 fade based on scroll position

    const SyncedLyricLine& get_line() const { return line_; }
    int get_start_ms() const { return line_.start_ms; }

private:
    SyncedLyricLine line_;
    GdkRGBA accent_;
    SeekCallback on_seek_;
    State state_ = State::Future;
    double prominence_ = 1.0;

    GtkWidget* widget_ = nullptr;
    SyncedLyricsLabel label_;

    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_owner_;

    void create_widget();
    static void on_button_release(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static void on_enter(GtkEventControllerMotion* motion, double x, double y, gpointer user_data);
    static void on_leave(GtkEventControllerMotion* motion, gpointer user_data);
};

} // namespace lollypop