/**
 * Lollypop C++ - ArtworkView Implementation
 * Async artwork loading with GTask, cancellation support, and fallback.
 */

#include "ui/components/ArtworkView.hpp"

#include <gtk/gtk.h>
#include <glib.h>
#include <iostream>

namespace lollypop {

ArtworkView::ArtworkView()
    : decode_task_(nullptr), has_artwork_(false) {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), FALSE);
    gtk_widget_set_vexpand(widget_.get(), FALSE);
    gtk_widget_add_css_class(widget_.get(), "artwork-view");

    // Artwork image
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "artwork-view-image");
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(artwork_image_.get(), GTK_ALIGN_CENTER);

    // Fallback (music note symbol)
    fallback_image_.reset(gtk_image_new_from_icon_name("audio-x-generic-symbolic"));
    g_object_ref_sink(fallback_image_.get());
    gtk_widget_add_css_class(fallback_image_.get(), "artwork-view-fallback");
    gtk_widget_set_halign(fallback_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(fallback_image_.get(), GTK_ALIGN_CENTER);
    gtk_image_set_pixel_size(GTK_IMAGE(fallback_image_.get()), 64);

    gtk_box_append(GTK_BOX(widget_.get()), artwork_image_.get());
    gtk_box_append(GTK_BOX(widget_.get()), fallback_image_.get());

    update_visibility();
}

ArtworkView::~ArtworkView() {
    // Cancel any pending decode task
    if (decode_task_) {
        GError* error = g_error_new(G_IO_ERROR, G_IO_ERROR_CANCELLED, "ArtworkView destroyed");
        g_task_return_error(decode_task_, error);
        g_object_unref(decode_task_);
        decode_task_ = nullptr;
    }
}

void ArtworkView::set_artwork_path(const std::string& path) {
    // Cancel previous decode task if any
    if (decode_task_) {
        GError* error = g_error_new(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Superseded by new request");
        g_task_return_error(decode_task_, error);
        g_object_unref(decode_task_);
        decode_task_ = nullptr;
    }

    current_path_ = path;

    if (!path.empty()) {
        // Show loading state (fallback) immediately
        has_artwork_ = false;
        update_visibility();

        // Start async decode in thread pool
        decode_task_ = g_task_new(nullptr, nullptr, on_decode_done, this);
        g_task_set_task_data(decode_task_, g_strdup(path.c_str()), g_free);
        g_task_run_in_thread(decode_task_, decode_thread_func);
    } else {
        // No path - show fallback immediately
        gtk_picture_set_file(GTK_PICTURE(artwork_image_.get()), nullptr);
        has_artwork_ = false;
        update_visibility();
    }
}

void ArtworkView::decode_thread_func(GTask* task, gpointer /*source_object*/, gpointer /*task_data*/, GCancellable* /*cancellable*/) {
    const char* path = static_cast<const char*>(g_task_get_task_data(task));
    GError* error = nullptr;
    GdkTexture* texture = gdk_texture_new_from_filename(path, &error);
    if (error) {
        g_task_return_error(task, error);
        g_error_free(error);
    } else {
        g_task_return_pointer(task, texture, [](gpointer p) { g_object_unref(G_OBJECT(p)); });
    }
}

void ArtworkView::on_decode_done(GObject* /*source*/, GAsyncResult* res, gpointer user_data) {
    ArtworkView* self = static_cast<ArtworkView*>(user_data);
    GError* error = nullptr;
    GdkTexture* texture = GDK_TEXTURE(g_task_propagate_pointer(G_TASK(res), &error));

    if (error) {
        // Decode failed - show fallback
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            std::cerr << "[ArtworkView] Decode failed: " << error->message << std::endl;
        }
        g_error_free(error);
        self->show_fallback();
    } else if (texture) {
        // Success - set texture on picture
        gtk_picture_set_paintable(GTK_PICTURE(self->artwork_image_.get()), GDK_PAINTABLE(texture));
        self->has_artwork_ = true;
        self->update_visibility();
        g_object_unref(texture);
    } else {
        self->show_fallback();
    }

    // Clean up task reference
    if (self->decode_task_) {
        g_object_unref(self->decode_task_);
        self->decode_task_ = nullptr;
    }
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
    gtk_widget_set_size_request(artwork_image_.get(), width, height);
    gtk_widget_set_size_request(fallback_image_.get(),
                                width / 2, height / 2);
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