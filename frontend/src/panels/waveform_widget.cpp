/**
 * Lollypop C++ - Waveform Widget Implementation
 */

#include "panels/waveform_widget.hpp"

#include <algorithm>
#include <cmath>
#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

WaveformWidget::WaveformWidget(GtkWidget* parent) {
    drawing_area_ = gtk_drawing_area_new();
    gtk_widget_add_css_class(drawing_area_, "waveform-widget");
    gtk_widget_set_size_request(drawing_area_, -1, 40);
    gtk_widget_set_hexpand(drawing_area_, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area_),
                                   draw_callback, this, nullptr);

    if (parent) {
        gtk_box_append(GTK_BOX(parent), drawing_area_);
    }
}

WaveformWidget::~WaveformWidget() {
    stop_animation();
}

void WaveformWidget::set_points(const std::vector<float>& points) {
    points_ = points;
    gtk_widget_queue_draw(drawing_area_);
}

void WaveformWidget::set_progress(double progress) {
    progress_ = std::clamp(progress, 0.0, 1.0);
    gtk_widget_queue_draw(drawing_area_);
}

void WaveformWidget::set_accent_color(double r, double g, double b, double a) {
    accent_r_ = std::clamp(r, 0.0, 1.0);
    accent_g_ = std::clamp(g, 0.0, 1.0);
    accent_b_ = std::clamp(b, 0.0, 1.0);
    accent_a_ = std::clamp(a, 0.0, 1.0);
    gtk_widget_queue_draw(drawing_area_);
}

void WaveformWidget::set_scheme(bool dark) {
    dark_ = dark;
    gtk_widget_queue_draw(drawing_area_);
}

void WaveformWidget::set_playing(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    if (playing) {
        start_animation();
    } else {
        stop_animation();
    }
    gtk_widget_queue_draw(drawing_area_);
}

void WaveformWidget::start_animation() {
    if (animation_timeout_id_) return;
    animation_phase_ = 0.0;
    animation_timeout_id_ = g_timeout_add(16, animation_tick, this); // ~60fps
}

void WaveformWidget::stop_animation() {
    if (animation_timeout_id_) {
        g_source_remove(animation_timeout_id_);
        animation_timeout_id_ = 0;
    }
}

gboolean WaveformWidget::animation_tick(gpointer user_data) {
    WaveformWidget* self = static_cast<WaveformWidget*>(user_data);
    self->animation_phase_ += 0.15;
    if (self->animation_phase_ > 6.28) self->animation_phase_ -= 6.28;
    gtk_widget_queue_draw(self->drawing_area_);
    return G_SOURCE_CONTINUE;
}

void WaveformWidget::draw_callback(GtkDrawingArea* area, cairo_t* cr,
                                   int width, int height, gpointer user_data) {
    WaveformWidget* self = static_cast<WaveformWidget*>(user_data);
    // Chrome follows the scheme; only the played bars carry accent.
    if (self->dark_) {
        cairo_set_source_rgba(cr, 0.11, 0.11, 0.14, 0.60);
    } else {
        cairo_set_source_rgba(cr, 0.96, 0.96, 0.97, 0.80);
    }
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (self->points_.empty()) {
        // No audio data - draw placeholder bars based on progress
        int bar_count = 80;
        double bar_width = width / static_cast<double>(bar_count * 2);
        double spacing = bar_width;

        for (int i = 0; i < bar_count; ++i) {
            double x = i * (bar_width + spacing) + spacing / 2;
            double bar_height = (std::sin(i * 0.3 + self->animation_phase_) * 0.5 + 0.5)
                                * height * 0.5;
            double y = (height - bar_height) / 2;

            if (i < self->progress_ * bar_count) {
                cairo_set_source_rgba(cr, self->accent_r_, self->accent_g_,
                                      self->accent_b_, self->accent_a_);
            } else {
                const double ink = self->dark_ ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, ink, ink, ink, 0.30);
            }
            cairo_rectangle(cr, x, y, bar_width, bar_height);
            cairo_fill(cr);
        }
    } else {
        // Draw CAVA audio data as bars
        size_t bar_count = self->points_.size();
        double bar_width = width / static_cast<double>(bar_count * 2);
        double spacing = bar_width;

        for (size_t i = 0; i < bar_count; ++i) {
            double x = i * (bar_width + spacing) + spacing / 2;
            double bar_height = self->points_[i] * height * 0.9;
            double y = (height - bar_height) / 2;

            if (static_cast<double>(i) / bar_count < self->progress_) {
                cairo_set_source_rgba(cr, self->accent_r_, self->accent_g_,
                                      self->accent_b_, self->accent_a_);
            } else {
                const double ink = self->dark_ ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, ink, ink, ink, 0.35);
            }
            cairo_rectangle(cr, x, y, bar_width, bar_height);
            cairo_fill(cr);
        }
    }

    // Progress indicator line
    double progress_x = self->progress_ * width;
    const double ink = self->dark_ ? 1.0 : 0.0;
    cairo_set_source_rgba(cr, ink, ink, ink, 0.85);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, progress_x, 0);
    cairo_line_to(cr, progress_x, height);
    cairo_stroke(cr);
}

} // namespace lollypop
