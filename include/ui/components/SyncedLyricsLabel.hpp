/**
 * Lollypop C++ - Synced Lyrics Label
 * Custom GTK4 widget with progressive karaoke fill effect
 * Uses GtkDrawingArea for full control over Pango/Cairo rendering
 */

#pragma once

#include <gtk/gtk.h>
#include <string>
#include <memory>

namespace lollypop {

class SyncedLyricsLabel {
public:
    SyncedLyricsLabel(const std::string& text = "", double xalign = 0.0);
    ~SyncedLyricsLabel();

    // Non-copyable, movable
    SyncedLyricsLabel(const SyncedLyricsLabel&) = delete;
    SyncedLyricsLabel& operator=(const SyncedLyricsLabel&) = delete;
    SyncedLyricsLabel(SyncedLyricsLabel&&) noexcept;
    SyncedLyricsLabel& operator=(SyncedLyricsLabel&&) noexcept;

    GtkWidget* get_widget() const { return widget_; }

    void set_text(const std::string& text);
    void set_accent(const GdkRGBA& accent);
    void set_active(bool active);
    void set_progress(double fraction);  // 0.0 - 1.0
    void set_gap_phase(double phase);    // For pulsing dots

    void set_xalign(double xalign);
    void set_line_wrap(bool wrap);
    void set_line_wrap_mode(PangoWrapMode mode);

    // For gap lines (instrumental)
    void set_as_gap(bool is_gap);
    void set_gap_markup(const std::string& markup);

    // Sizing
    void set_width_chars(int chars);
    int get_preferred_height_for_width(int width);
    void queue_resize();

private:
    GtkWidget* widget_ = nullptr;
    std::string text_;
    std::string gap_markup_;
    GdkRGBA accent_{0.65, 0.55, 0.98, 1.0};
    bool active_ = false;
    bool is_gap_ = false;
    double progress_ = 0.0;
    double gap_phase_ = 0.0;
    double xalign_ = 0.0;
    bool wrap_ = true;
    PangoWrapMode wrap_mode_ = PANGO_WRAP_WORD_CHAR;
    int width_chars_ = -1;
    int last_width_ = 0;

    PangoLayout* layout_ = nullptr;

    // Widget deleter
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_owner_;

    // Internal methods
    void create_widget();
    void install_draw_func();
    void update_layout();
    void queue_draw();
    int unwrapped_text_width();
    void apply_layout_geometry(int width);
    void draw(cairo_t* cr, int width, int height);
    void draw_gap_dots(cairo_t* cr, int width, int height);
    void draw_fill_overlay(cairo_t* cr, int width, int height);
    static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
    static void measure(GtkWidget* widget, GtkOrientation orientation, int for_size,
                        int* minimum, int* natural, int* minimum_baseline,
                        int* natural_baseline);
    static void allocate(GtkWidget* widget, int width, int height, int baseline);
};

} // namespace lollypop
