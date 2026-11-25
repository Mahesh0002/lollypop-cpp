/**
 * Small animated equalizer badge overlaid on a playing track's artwork.
 * Uses the widget's CSS `color` (bound to --accent-color) and respects the
 * GTK reduced-motion setting.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>

namespace lollypop {

class EqualizerIndicator {
public:
    EqualizerIndicator();
    ~EqualizerIndicator();

    EqualizerIndicator(const EqualizerIndicator&) = delete;
    EqualizerIndicator& operator=(const EqualizerIndicator&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void set_playing(bool playing);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    guint tick_id_ = 0;
    double phase_ = 0.0;
    bool playing_ = false;

    void start_tick();
    void stop_tick();

    static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width,
                        int height, gpointer user_data);
    static gboolean on_tick(GtkWidget* widget, GdkFrameClock* clock,
                            gpointer user_data);
};

}  // namespace lollypop
