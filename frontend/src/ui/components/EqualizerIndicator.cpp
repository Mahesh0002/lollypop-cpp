/**
 * EqualizerIndicator implementation: GtkDrawingArea + frame-clock animation.
 */

#include "ui/components/EqualizerIndicator.hpp"
#include "ui/animation/SpringAnimation.hpp"
#include "ui/components/TrackListMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <gtk/gtk.h>

namespace lollypop {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr double kBarWidth = 2.0;
constexpr double kBarSpacing = 3.2;
}  // namespace

EqualizerIndicator::EqualizerIndicator() {
    widget_.reset(gtk_drawing_area_new());
    g_object_ref_sink(widget_.get());
    gtk_widget_add_css_class(widget_.get(), "track-row-equalizer");
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(widget_.get(),
                                track_list_metrics::kEqualizerSize,
                                track_list_metrics::kEqualizerSize);
    gtk_widget_set_visible(widget_.get(), FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget_.get()), on_draw,
                                   this, nullptr);
}

EqualizerIndicator::~EqualizerIndicator() {
    stop_tick();
}

void EqualizerIndicator::set_playing(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    gtk_widget_set_visible(widget_.get(), playing ? TRUE : FALSE);
    if (playing) {
        start_tick();
    } else {
        stop_tick();
    }
    gtk_widget_queue_draw(widget_.get());
}

void EqualizerIndicator::start_tick() {
    if (tick_id_ != 0) return;
    if (!animations_enabled()) return;
    tick_id_ = gtk_widget_add_tick_callback(widget_.get(), on_tick, this,
                                            nullptr);
}

void EqualizerIndicator::stop_tick() {
    if (tick_id_ == 0) return;
    gtk_widget_remove_tick_callback(widget_.get(), tick_id_);
    tick_id_ = 0;
    phase_ = 0.0;
}

gboolean EqualizerIndicator::on_tick(GtkWidget* widget, GdkFrameClock* clock,
                                      gpointer user_data) {
    (void)widget;
    (void)clock;
    auto* self = static_cast<EqualizerIndicator*>(user_data);
    if (!self || !self->playing_) return G_SOURCE_REMOVE;

    if (!animations_enabled()) {
        self->tick_id_ = 0;
        self->phase_ = 0.0;
        return G_SOURCE_REMOVE;
    }

    self->phase_ += 0.18;
    if (self->phase_ > kTwoPi) self->phase_ -= kTwoPi;
    gtk_widget_queue_draw(self->widget_.get());
    return G_SOURCE_CONTINUE;
}

void EqualizerIndicator::on_draw(GtkDrawingArea* area, cairo_t* cr, int width,
                                 int height, gpointer user_data) {
    auto* self = static_cast<EqualizerIndicator*>(user_data);
    (void)area;
    if (!self || !self->playing_ || width <= 0 || height <= 0) return;

    const double cx = width / 2.0;
    const double cy = height / 2.0;

    // Dark scrim keeps the accent bars readable over bright artwork.
    const double radius = std::min(width, height) / 2.0 - 0.5;
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, std::max(1.0, radius), 0.0, kTwoPi);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.48);
    cairo_fill(cr);

    GdkRGBA accent;
    gtk_widget_get_color(GTK_WIDGET(area), &accent);
    cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue,
                          accent.alpha);
    cairo_set_line_width(cr, kBarWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    const double max_height = height - 4.0;
    for (int i = 0; i < 3; ++i) {
        const double x = cx + (i - 1) * kBarSpacing;
        const double wave =
            0.5 + 0.5 * std::sin(self->phase_ + static_cast<double>(i) * 0.9);
        const double bar_height = 3.0 + wave * (max_height - 3.0);
        cairo_move_to(cr, x, cy - bar_height / 2.0);
        cairo_line_to(cr, x, cy + bar_height / 2.0);
        cairo_stroke(cr);
    }
}

}  // namespace lollypop
