/**
 * Lollypop C++ - Synced Lyrics Label Implementation
 * Custom widget using GtkDrawingArea for progressive karaoke fill
 * Word-wrap aware clipping per visual line (Apple Music style)
 */

#include "ui/components/SyncedLyricsLabel.hpp"

#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>

namespace lollypop {

namespace {
constexpr int kVerticalPadding = 8;
constexpr int kHorizontalPadding = 8;
constexpr const char* kLabelKey = "lollypop-synced-lyrics-label";
} // namespace

void SyncedLyricsLabel::on_draw(GtkDrawingArea*, cairo_t* cr, int width,
                                int height, gpointer user_data) {
    static_cast<SyncedLyricsLabel*>(user_data)->draw(cr, width, height);
}

void SyncedLyricsLabel::install_draw_func() {
    // Re-register on every move. The draw callback carries `this`, and a
    // moved-from temporary must never remain as the callback's user_data.
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(widget_), &SyncedLyricsLabel::on_draw, this, nullptr);
    g_object_set_data(G_OBJECT(widget_), kLabelKey, this);
}

void SyncedLyricsLabel::create_widget() {
    widget_ = gtk_drawing_area_new();
    g_object_ref_sink(widget_);
    gtk_widget_set_hexpand(widget_, TRUE);
    gtk_widget_add_css_class(widget_, "lyrics-line");

    // A GtkDrawingArea has a zero content size by default. Give it a layout
    // manager that derives its height from the wrapped text so lyric rows
    // actually have an allocation (and can be scrolled/clicked).
    GtkLayoutManager* layout_manager = gtk_custom_layout_new(
        nullptr, &SyncedLyricsLabel::measure, &SyncedLyricsLabel::allocate);
    // gtk_widget_set_layout_manager() takes ownership of the manager.
    gtk_widget_set_layout_manager(widget_, layout_manager);

    install_draw_func();

    layout_ = gtk_widget_create_pango_layout(widget_, nullptr);
    pango_layout_set_ellipsize(layout_, PANGO_ELLIPSIZE_NONE);

    widget_owner_.reset(widget_);
    update_layout();
}

SyncedLyricsLabel::SyncedLyricsLabel(const std::string& text, double xalign)
    : text_(text), xalign_(xalign) {
    create_widget();
}

SyncedLyricsLabel::~SyncedLyricsLabel() {
    if (layout_) g_object_unref(layout_);
}

SyncedLyricsLabel::SyncedLyricsLabel(SyncedLyricsLabel&& other) noexcept
    : widget_(other.widget_), text_(std::move(other.text_)), gap_markup_(std::move(other.gap_markup_)),
      accent_(other.accent_), active_(other.active_), is_gap_(other.is_gap_),
      progress_(other.progress_), gap_phase_(other.gap_phase_), xalign_(other.xalign_),
      wrap_(other.wrap_), wrap_mode_(other.wrap_mode_), width_chars_(other.width_chars_),
      last_width_(other.last_width_), layout_(other.layout_),
      widget_owner_(std::move(other.widget_owner_)) {
    other.widget_ = nullptr;
    other.layout_ = nullptr;
    if (widget_) {
        install_draw_func();
        gtk_widget_queue_resize(widget_);
    }
}

SyncedLyricsLabel& SyncedLyricsLabel::operator=(SyncedLyricsLabel&& other) noexcept {
    if (this != &other) {
        if (layout_) g_object_unref(layout_);

        widget_ = other.widget_;
        text_ = std::move(other.text_);
        gap_markup_ = std::move(other.gap_markup_);
        accent_ = other.accent_;
        active_ = other.active_;
        is_gap_ = other.is_gap_;
        progress_ = other.progress_;
        gap_phase_ = other.gap_phase_;
        xalign_ = other.xalign_;
        wrap_ = other.wrap_;
        wrap_mode_ = other.wrap_mode_;
        width_chars_ = other.width_chars_;
        last_width_ = other.last_width_;
        layout_ = other.layout_;
        widget_owner_ = std::move(other.widget_owner_);

        other.widget_ = nullptr;
        other.layout_ = nullptr;
        if (widget_) {
            install_draw_func();
            gtk_widget_queue_resize(widget_);
        }
    }
    return *this;
}

void SyncedLyricsLabel::apply_layout_geometry(int width) {
    if (!layout_) return;

    // Reset to the widget's CSS font so theme/accent and active-state styling
    // are honoured, then wrap to the actual allocation width.
    pango_layout_set_font_description(layout_, nullptr);
    pango_layout_set_text(layout_, text_.c_str(), -1);
    pango_layout_set_wrap(layout_, wrap_ ? wrap_mode_ : PANGO_WRAP_WORD);
    pango_layout_set_width(layout_, wrap_ && width > 0 ? width * PANGO_SCALE : -1);
    pango_layout_set_alignment(layout_,
        xalign_ <= 0.0 ? PANGO_ALIGN_LEFT :
        xalign_ >= 1.0 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_CENTER);
    pango_layout_context_changed(layout_);
}

void SyncedLyricsLabel::update_layout() {
    if (!layout_) return;
    apply_layout_geometry(-1);
    queue_draw();
    queue_resize();
}

void SyncedLyricsLabel::queue_draw() {
    if (widget_) gtk_widget_queue_draw(widget_);
}

void SyncedLyricsLabel::queue_resize() {
    if (widget_) gtk_widget_queue_resize(widget_);
}

int SyncedLyricsLabel::unwrapped_text_width() {
    if (!layout_) return 0;
    apply_layout_geometry(-1);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout_, &width, &height);
    if (width_chars_ > 0) {
        width = std::min(width, width_chars_ * 8);
    }
    return width;
}

int SyncedLyricsLabel::get_preferred_height_for_width(int width) {
    if (!layout_) return 0;
    if (is_gap_) return 24;

    apply_layout_geometry(width);
    int height = 0;
    pango_layout_get_pixel_size(layout_, nullptr, &height);
    return (height > 0 ? height : 20) + kVerticalPadding;
}

void SyncedLyricsLabel::measure(GtkWidget* widget, GtkOrientation orientation,
                                int for_size, int* minimum, int* natural,
                                int* minimum_baseline, int* natural_baseline) {
    auto* self = static_cast<SyncedLyricsLabel*>(
        g_object_get_data(G_OBJECT(widget), kLabelKey));

    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;

    if (!self || !self->layout_) {
        if (minimum) *minimum = 0;
        if (natural) *natural = 0;
        return;
    }

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        const int width = self->is_gap_ ? 44 : self->unwrapped_text_width();
        if (minimum) *minimum = 0;
        if (natural) *natural = width + kHorizontalPadding * 2;
        return;
    }

    // GtkBox measures children with for_size == -1. Use the parent's current
    // width (minus the lyric row's horizontal CSS padding) so a wrapped line
    // reports its multi-line height instead of a single unwrapped line.
    int width = for_size;
    if (width <= 0) {
        GtkWidget* parent = gtk_widget_get_parent(widget);
        if (parent) {
            width = gtk_widget_get_width(parent)
                    - gtk_widget_get_margin_start(widget)
                    - gtk_widget_get_margin_end(widget)
                    - 24; // .lyrics-row padding (12px each side)
        }
        if (width <= 0) width = self->unwrapped_text_width();
    }
    const int height = self->get_preferred_height_for_width(width);
    if (minimum) *minimum = height;
    if (natural) *natural = height;
}

void SyncedLyricsLabel::allocate(GtkWidget* widget, int width, int height,
                                 int baseline) {
    // A GtkDrawingArea is a leaf widget: GTK applies the allocation before
    // calling the layout manager, and there are no children to place. If the
    // width changed, remeasure so wrapped lines get the correct row height.
    (void)height;
    (void)baseline;
    auto* self = static_cast<SyncedLyricsLabel*>(
        g_object_get_data(G_OBJECT(widget), kLabelKey));
    if (self && self->last_width_ != width) {
        self->last_width_ = width;
        gtk_widget_queue_resize(widget);
    }
}

void SyncedLyricsLabel::set_text(const std::string& text) {
    text_ = text;
    progress_ = 0.0;
    update_layout();
}

void SyncedLyricsLabel::set_accent(const GdkRGBA& accent) {
    accent_ = accent;
    queue_draw();
}

void SyncedLyricsLabel::set_active(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (!active) progress_ = 0.0;
    queue_draw();
    queue_resize();
}

void SyncedLyricsLabel::set_progress(double fraction) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (std::abs(progress_ - fraction) < 0.001) return;
    progress_ = fraction;
    queue_draw();
}

void SyncedLyricsLabel::set_gap_phase(double phase) {
    gap_phase_ = phase;
    if (is_gap_) queue_draw();
}

void SyncedLyricsLabel::set_xalign(double xalign) {
    xalign_ = xalign;
    update_layout();
}

void SyncedLyricsLabel::set_line_wrap(bool wrap) {
    wrap_ = wrap;
    update_layout();
}

void SyncedLyricsLabel::set_line_wrap_mode(PangoWrapMode mode) {
    wrap_mode_ = mode;
    update_layout();
}

void SyncedLyricsLabel::set_as_gap(bool is_gap) {
    is_gap_ = is_gap;
    queue_draw();
    queue_resize();
}

void SyncedLyricsLabel::set_gap_markup(const std::string& markup) {
    gap_markup_ = markup;
    queue_draw();
}

void SyncedLyricsLabel::set_width_chars(int chars) {
    width_chars_ = chars;
    update_layout();
}

void SyncedLyricsLabel::draw(cairo_t* cr, int width, int height) {
    if (!layout_ || width <= 0 || height <= 0) return;

    if (is_gap_) {
        draw_gap_dots(cr, width, height);
        return;
    }

    apply_layout_geometry(width);

    GdkRGBA base_color;
    gtk_widget_get_color(widget_, &base_color);
    base_color.alpha *= active_ ? 0.42 : 0.72;

    gdk_cairo_set_source_rgba(cr, &base_color);
    pango_cairo_show_layout(cr, layout_);

    if (active_ && progress_ > 0.0) {
        draw_fill_overlay(cr, width, height);
    }
}

void SyncedLyricsLabel::draw_gap_dots(cairo_t* cr, int width, int height) {
    // Three pulsing dots while the music is instrumental, like Apple Music.
    constexpr double dot_size = 8.0;
    constexpr double spacing = 12.0;
    const double total_width = 3 * dot_size + 2 * spacing;

    double start_x;
    if (xalign_ <= 0.0) {
        start_x = kHorizontalPadding;
    } else if (xalign_ >= 1.0) {
        start_x = std::max(0.0, static_cast<double>(width) - total_width - kHorizontalPadding);
    } else {
        start_x = (width - total_width) / 2.0;
    }

    for (int i = 0; i < 3; ++i) {
        const double wave = 0.5 + 0.5 * std::sin(gap_phase_ - i * 1.7);
        GdkRGBA dot_color = accent_;
        dot_color.alpha *= 0.22 + 0.78 * wave;

        const double x = start_x + i * (dot_size + spacing);
        const double y = (height - dot_size) / 2.0;

        gdk_cairo_set_source_rgba(cr, &dot_color);
        cairo_arc(cr, x + dot_size / 2.0, y + dot_size / 2.0,
                  dot_size / 2.0, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

void SyncedLyricsLabel::draw_fill_overlay(cairo_t* cr, int width, int height) {
    if (!layout_) return;
    (void)width;
    (void)height;

    const int total_chars = g_utf8_strlen(text_.c_str(), -1);
    if (total_chars <= 0) return;

    const int fill_chars = std::min(total_chars,
        static_cast<int>(std::round(progress_ * total_chars)));
    if (fill_chars <= 0) return;

    const char* text_ptr = text_.c_str();
    const int fill_bytes = g_utf8_offset_to_pointer(text_ptr, fill_chars) - text_ptr;

    const int line_count = pango_layout_get_line_count(layout_);
    if (line_count <= 0) return;

    // Apple Music-style karaoke: redraw the same Pango layout in the accent
    // colour, clipped to the sung glyphs of each visual line. Filling a
    // rectangle here would produce a solid bar behind the text instead.
    cairo_save(cr);
    gdk_cairo_set_source_rgba(cr, &accent_);

    for (int i = 0; i < line_count; ++i) {
        PangoLayoutLine* line = pango_layout_get_line(layout_, i);
        if (!line) continue;

        const int line_start = line->start_index;
        const int line_end = line_start + line->length;
        if (fill_bytes <= line_start) continue;

        PangoRectangle pos{};
        pango_layout_index_to_pos(layout_, line_start, &pos);
        const double line_x = pos.x / static_cast<double>(PANGO_SCALE);
        const double line_y = pos.y / static_cast<double>(PANGO_SCALE);
        const double line_height = pos.height / static_cast<double>(PANGO_SCALE) + 2;

        int end_x = 0;
        pango_layout_line_index_to_x(line, line->length, FALSE, &end_x);
        const double line_width = end_x / static_cast<double>(PANGO_SCALE);

        double fill_x;
        if (fill_bytes >= line_end) {
            fill_x = line_x + line_width;
        } else {
            int x_pos = 0;
            pango_layout_line_index_to_x(line, fill_bytes - line_start,
                                         FALSE, &x_pos);
            fill_x = line_x + x_pos / static_cast<double>(PANGO_SCALE);
        }

        double clip_start = line_x;
        double clip_end = fill_x;
        if (pango_layout_line_get_resolved_direction(line) == PANGO_DIRECTION_RTL) {
            clip_start = fill_x;
            clip_end = line_x + line_width;
        }
        if (clip_end <= clip_start) continue;

        cairo_save(cr);
        cairo_rectangle(cr, clip_start, line_y - 2,
                        clip_end - clip_start, line_height + 4);
        cairo_clip(cr);
        pango_cairo_show_layout(cr, layout_);
        cairo_restore(cr);
    }

    cairo_restore(cr);
}

} // namespace lollypop
