/**
 * Lollypop C++ - ArtworkView
 *
 * Reusable artwork display component (plan Section 6.6).
 * Responsibilities:
 * - Display album or track artwork.
 * - Preserve aspect ratio.
 * - Support rounded corners.
 * - Show fallback artwork when missing.
 * - Load asynchronously via GdkTexture from file with GCancellable support.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class ArtworkView {
public:
    ArtworkView();
    ~ArtworkView();

    ArtworkView(const ArtworkView&) = delete;
    ArtworkView& operator=(const ArtworkView&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    /// Set artwork from a file path. Empty path shows fallback.
    /// Loads asynchronously; cancels any previous load.
    void set_artwork_path(const std::string& path);

    /// Set the corner radius in pixels.
    void set_corner_radius(double radius);

    /// Set the preferred display size.
    void set_preferred_size(int width, int height);

    /// Whether artwork is currently loaded (not fallback).
    bool has_artwork() const { return has_artwork_; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artwork_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> fallback_image_;
    GTask* decode_task_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    std::string current_path_;
    int preferred_width_ = 56;
    int preferred_height_ = 56;
    bool has_artwork_ = false;
    double corner_radius_ = 12.0;

    static void on_decode_done(GObject* source, GAsyncResult* res, gpointer user_data);
    static void decode_thread_func(GTask* task, gpointer source_object, gpointer task_data, GCancellable* cancellable);
    void show_fallback();
    void update_visibility();
};

} // namespace lollypop