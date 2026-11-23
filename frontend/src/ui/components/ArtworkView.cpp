/**
 * Lollypop C++ - ArtworkView Implementation
 * Async artwork loading with GTask, GCancellable support, and fallback.
 */

#include "ui/components/ArtworkView.hpp"

#include <gtk/gtk.h>
#include <glib.h>
#include <algorithm>
#include <iostream>

namespace lollypop {

namespace {
struct DecodeData {
    ArtworkView* view;
    std::shared_ptr<bool> alive;
    std::string path;
    int width;
    int height;
};

void free_decode_data(gpointer data) {
    delete static_cast<DecodeData*>(data);
}
} // namespace

ArtworkView::ArtworkView()
    : decode_task_(nullptr), cancellable_(nullptr), has_artwork_(false) {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), FALSE);
    gtk_widget_set_vexpand(widget_.get(), FALSE);
    gtk_widget_add_css_class(widget_.get(), "artwork-view");
    gtk_widget_set_visible(widget_.get(), TRUE);

    // Artwork image
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "artwork-view-image");
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(artwork_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(artwork_image_.get(), FALSE);

    // Fallback (music note symbol)
    fallback_image_.reset(gtk_image_new_from_icon_name("audio-x-generic-symbolic"));
    g_object_ref_sink(fallback_image_.get());
    gtk_widget_add_css_class(fallback_image_.get(), "artwork-view-fallback");
    gtk_widget_set_halign(fallback_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(fallback_image_.get(), GTK_ALIGN_CENTER);
    gtk_image_set_pixel_size(GTK_IMAGE(fallback_image_.get()), 32);
    gtk_widget_set_visible(fallback_image_.get(), TRUE);

    gtk_box_append(GTK_BOX(widget_.get()), artwork_image_.get());
    gtk_box_append(GTK_BOX(widget_.get()), fallback_image_.get());

    update_visibility();
}

ArtworkView::~ArtworkView() {
    // The completion callback runs on the main loop. The shared flag makes it
    // a no-op before any widget or task ref is released.
    if (alive_) *alive_ = false;
    if (cancellable_) {
        g_cancellable_cancel(cancellable_);
        g_object_unref(cancellable_);
        cancellable_ = nullptr;
    }
    if (decode_task_) {
        g_object_unref(decode_task_);
        decode_task_ = nullptr;
    }
}

void ArtworkView::set_artwork_path(const std::string& path) {
    // Cancel previous decode task if any
    if (cancellable_) {
        g_cancellable_cancel(cancellable_);
        g_object_unref(cancellable_);
        cancellable_ = nullptr;
    }
    if (decode_task_) {
        g_object_unref(decode_task_);
        decode_task_ = nullptr;
    }

    current_path_ = path;

    if (!path.empty()) {
        // Show loading state (fallback) immediately
        has_artwork_ = false;
        update_visibility();

        // Create new cancellable for this load
        cancellable_ = g_cancellable_new();

        // The task data captures a shared liveness flag. A late callback is a
        // no-op after destruction, without needing to touch a widget ref.
        auto* data = new DecodeData{this,
                                    alive_,
                                    path,
                                    preferred_width_,
                                    preferred_height_};
        decode_task_ = g_task_new(nullptr, cancellable_, on_decode_done, nullptr);
        g_task_set_task_data(decode_task_, data, free_decode_data);
        g_task_run_in_thread(decode_task_, decode_thread_func);
    } else {
        // No path - show fallback immediately
        gtk_picture_set_file(GTK_PICTURE(artwork_image_.get()), nullptr);
        has_artwork_ = false;
        update_visibility();
    }
}

void ArtworkView::decode_thread_func(GTask* task, gpointer /*source_object*/, gpointer /*task_data*/, GCancellable* cancellable) {
    auto* data = static_cast<DecodeData*>(g_task_get_task_data(task));
    if (!data) return;

    GError* error = nullptr;
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_error(task, g_error_new(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));
        return;
    }

    // Decode to an exact-size texture. GtkPicture advertises a texture's
    // natural size, so loading a full-resolution image made every container
    // that displayed it grow. Crop to a square and scale before handing it
    // over to the UI thread.
    GdkPixbuf* source = gdk_pixbuf_new_from_file(data->path.c_str(), &error);
    if (error || !source) {
        if (error) {
            g_task_return_error(task, error);  // takes ownership
        } else {
            g_task_return_error(task, g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                                  "Could not decode artwork: %s",
                                                  data->path.c_str()));
        }
        return;
    }

    const int width = gdk_pixbuf_get_width(source);
    const int height = gdk_pixbuf_get_height(source);
    const int side = std::min(width, height);
    if (side <= 0) {
        g_object_unref(source);
        g_task_return_error(task, g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                              "Invalid artwork: %s",
                                              data->path.c_str()));
        return;
    }

    GdkPixbuf* square = gdk_pixbuf_new_subpixbuf(source,
                                                 (width - side) / 2,
                                                 (height - side) / 2,
                                                 side, side);
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(square,
                                                std::max(1, data->width),
                                                std::max(1, data->height),
                                                GDK_INTERP_BILINEAR);
    GdkTexture* texture = gdk_texture_new_for_pixbuf(scaled);

    g_object_unref(scaled);
    g_object_unref(square);
    g_object_unref(source);

    g_task_return_pointer(task, texture, [](gpointer p) { g_object_unref(G_OBJECT(p)); });
}

void ArtworkView::on_decode_done(GObject* /*source*/, GAsyncResult* res, gpointer /*user_data*/) {
    auto* task = G_TASK(res);
    auto* data = static_cast<DecodeData*>(g_task_get_task_data(task));
    if (!data || !data->alive || !*data->alive) return;

    ArtworkView* self = data->view;
    const bool current = self->decode_task_ == task;

    GError* error = nullptr;
    GdkTexture* texture = GDK_TEXTURE(g_task_propagate_pointer(task, &error));

    if (current) {
        if (error) {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                std::cerr << "[ArtworkView] Decode failed: " << error->message << std::endl;
            }
            self->show_fallback();
        } else if (texture) {
            gtk_picture_set_paintable(GTK_PICTURE(self->artwork_image_.get()),
                                      GDK_PAINTABLE(texture));
            self->has_artwork_ = true;
            self->update_visibility();
        } else {
            self->show_fallback();
        }

        // Release references held for this load only if it is still current.
        if (self->decode_task_) {
            g_object_unref(self->decode_task_);
            self->decode_task_ = nullptr;
        }
        if (self->cancellable_) {
            g_object_unref(self->cancellable_);
            self->cancellable_ = nullptr;
        }
    }
    if (error) g_error_free(error);
    if (texture) g_object_unref(texture);
}

void ArtworkView::show_fallback() {
    gtk_picture_set_file(GTK_PICTURE(artwork_image_.get()), nullptr);
    has_artwork_ = false;
    update_visibility();
}

void ArtworkView::set_corner_radius(double radius) {
    corner_radius_ = radius;
    // Corner radius is applied via the artwork-view CSS class in styles.css.
    // Size is set exclusively by set_preferred_size(); do not request a size
    // here, or the artwork gets forced to radius*2 (e.g. 24x24).
}

void ArtworkView::set_preferred_size(int width, int height) {
    preferred_width_ = width;
    preferred_height_ = height;

    // Constrain the wrapper too: GtkPicture advertises the texture's natural
    // size, which otherwise grows its parent container.
    gtk_widget_set_size_request(widget_.get(), width, height);
    gtk_widget_set_size_request(artwork_image_.get(), width, height);
    gtk_widget_set_size_request(fallback_image_.get(),
                                std::max(16, width / 3),
                                std::max(16, height / 3));
}

void ArtworkView::update_visibility() {
    if (has_artwork_) {
        gtk_widget_set_visible(artwork_image_.get(), TRUE);
        gtk_widget_set_visible(fallback_image_.get(), FALSE);
    } else {
        gtk_widget_set_visible(artwork_image_.get(), FALSE);
        gtk_widget_set_visible(fallback_image_.get(), TRUE);
    }
}

} // namespace lollypop