/**
 * Lollypop C++ - ArtworkView
 *
 * Reusable artwork display component (plan Section 6.6).
 * Responsibilities:
 * - Display album or track artwork.
 * - Preserve aspect ratio.
 * - Support rounded corners.
 * - Show fallback artwork when missing.
 * - Load asynchronously via a shared, size-bucketed thumbnail cache.
 */

#pragma once

#include <gtk/gtk.h>
#include <cstdint>
#include <memory>
#include <string>

namespace lollypop {

class ArtworkCache;
class ThumbnailGenerator;

class ArtworkView {
public:
    ArtworkView();
    ~ArtworkView();

    ArtworkView(const ArtworkView&) = delete;
    ArtworkView& operator=(const ArtworkView&) = delete;

    /// Install the shared thumbnail cache and generator used by all views.
    /// Call before constructing views; nullptr restores the legacy loader.
    static void configure_cache(ArtworkCache* cache,
                                ThumbnailGenerator* generator);

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
    std::unique_ptr<GtkWidget, WidgetDeleter> artwork_image_;
    std::unique_ptr<GtkWidget, WidgetDeleter> fallback_image_;
    GTask* decode_task_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    // Async decode callbacks capture this flag; the destructor clears it so a
    // late callback can never touch a destroyed ArtworkView.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::string current_path_;
    std::string pending_path_;
    uint64_t load_generation_ = 0;
    int cache_bucket_ = 0;
    int preferred_width_ = 56;
    int preferred_height_ = 56;
    bool has_artwork_ = false;
    double corner_radius_ = 12.0;

    static ArtworkCache* cache_;
    static ThumbnailGenerator* generator_;

    void start_pending_load();
    void show_fallback();
    void update_visibility();

    static void on_decode_done(GObject* source, GAsyncResult* res, gpointer user_data);
    static void decode_thread_func(GTask* task, gpointer source_object, gpointer task_data, GCancellable* cancellable);
    static void on_map(GtkWidget* widget, gpointer user_data);
};

} // namespace lollypop
