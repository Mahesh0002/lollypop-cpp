/**
 * Lollypop C++ - Volume Dial Component
 *
 * Circular volume dial inspired by MaterialDial.qml from Quickshell.
 * Features:
 * - Circular progress indicator showing volume level
 * - Smooth spring animations for value changes
 * - Click to mute, or to restore audio from a silent state
 * - Scroll to adjust volume
 * - Hover/press spring feedback
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <functional>

namespace lollypop {

class VolumeDial {
public:
    using VolumeChangedCallback = std::function<void(double)>;

    VolumeDial();
    ~VolumeDial();

    VolumeDial(const VolumeDial&) = delete;
    VolumeDial& operator=(const VolumeDial&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    // Get/set volume (0.0 to 1.0)
    double get_volume() const { return volume_; }
    void set_volume(double volume);

    // Mute/unmute. toggle_mute() restores a non-zero level when silent.
    bool is_muted() const { return muted_; }
    void set_muted(bool muted);
    void toggle_mute();

    // Set volume changed callback
    void set_volume_changed_callback(VolumeChangedCallback cb) {
        volume_changed_cb_ = std::move(cb);
    }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            if (gtk_widget_get_parent(w) != nullptr) {
                // GTK parents take ownership of the child's existing reference.
                gtk_widget_unparent(w);
            } else {
                g_object_unref(w);
            }
        }
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> drawing_area_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_popover_;
    std::unique_ptr<GtkWidget, WidgetDeleter> volume_scale_;

    double volume_ = 0.7;
    bool muted_ = false;
    double last_volume_ = 0.7; // For mute toggle

    VolumeChangedCallback volume_changed_cb_;

    void setup_ui();
    static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
    static gboolean on_button_press(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static gboolean on_scroll(GtkEventControllerScroll* controller, double dx, double dy, gpointer user_data);
    static void on_scale_changed(GtkRange* range, gpointer user_data);
    static gboolean on_enter(GtkEventControllerMotion* controller, double x, double y, gpointer user_data);
    static gboolean on_leave(GtkEventControllerMotion* controller, gpointer user_data);
    static gboolean on_pressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);

    void update_icon();
    void emit_volume_changed();
};

} // namespace lollypop