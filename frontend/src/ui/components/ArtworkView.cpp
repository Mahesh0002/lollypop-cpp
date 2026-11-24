/**
 * Lollypop C++ - ArtworkView Implementation
 * Async artwork loading with a shared, size-bucketed thumbnail cache.
 */

#include "ui/components/ArtworkView.hpp"

#include "data/ArtworkCache.hpp"
#include "data/ThumbnailGenerator.hpp"

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
};

void free_decode_data(gpointer data) {
    delete static_cast<DecodeData*>(data);
}
} // namespace

ArtworkCache* ArtworkView::cache_ = nullptr;
ThumbnailGenerator* ArtworkView::generator_ = nullptr;

void ArtworkView::configure_cache(ArtworkCache* cache,
                                  ThumbnailGenerator* generator) {
    cache_ = cache;
    generator_ = generator;
}

ArtworkView::ArtworkView()
    : decode_task_(nullptr), cancellable_(nullptr), has_artwork_(false) {
    widget_.reset(gtk_overlay_new());
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), FALSE);
    gtk_widget_set_vexpand(widget_.get(), FALSE);
    gtk_widget_add_css_class(widget_.get(), "artwork-view");
    gtk_widget_set_overflow(widget_.get(), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_visible(widget_.get(), TRUE);
    g_signal_connect(widget_.get(), "map", G_CALLBACK(on_map), this);

    // The main child is an invisible sizer. As an overlay child, the picture's
    // natural size would otherwise inflate the tile.
    GtkWidget* sizer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_visible(sizer, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(widget_.get()), sizer);

    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_picture_set_can_shrink(GTK_PICTURE(artwork_image_.get()), TRUE);
    gtk_widget_add_css_class(artwork_image_.get(), "artwork-view-image");
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(artwork_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(artwork_image_.get(), TRUE);
    gtk_widget_set_vexpand(artwork_image_.get(), TRUE);
    gtk_widget_set_visible(artwork_image_.get(), FALSE);

    fallback_image_.reset(gtk_image_new_from_icon_name("audio-x-generic-symbolic"));
    g_object_ref_sink(fallback_image_.get());
    gtk_widget_add_css_class(fallback_image_.get(), "artwork-view-fallback");
    gtk_widget_set_halign(fallback_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(fallback_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(fallback_image_.get(), TRUE);
    gtk_widget_set_vexpand(fallback_image_.get(), TRUE);
    gtk_image_set_pixel_size(GTK_IMAGE(fallback_image_.get()), 32);
    gtk_widget_set_visible(fallback_image_.get(), TRUE);

    gtk_overlay_add_overlay(GTK_OVERLAY(widget_.get()), artwork_image_.get());
    gtk_overlay_add_overlay(GTK_OVERLAY(widget_.get()), fallback_image_.get());

    update_visibility();
}

ArtworkView::~ArtworkView() {
    // Invalidate queued callbacks before releasing widget/task references.
    if (alive_) *alive_ = false;
    pending_path_.clear();
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
    // Cancel previous decode task if any.
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
    ++load_generation_;
    pending_path_.clear();

    if (!path.empty()) {
        has_artwork_ = false;
        update_visibility();
        pending_path_ = path;
        cache_bucket_ = ArtworkCache::select_bucket(
            std::max(preferred_width_, preferred_height_),
            gtk_widget_get_scale_factor(widget_.get()));
        // Off-screen tiles are not mapped; their load starts on the "map"
        // signal so a page only decodes artwork it actually displays.
        if (gtk_widget_get_mapped(widget_.get())) start_pending_load();
    } else {
        gtk_picture_set_paintable(GTK_PICTURE(artwork_image_.get()), nullptr);
        has_artwork_ = false;
        update_visibility();
    }
}

void ArtworkView::start_pending_load() {
    if (pending_path_.empty() || !*alive_) return;

    const std::string path = pending_path_;
    const uint64_t generation = load_generation_;
    pending_path_.clear();

    if (cache_ && generator_) {
        const std::string key = path;
        if (auto hit = cache_->lookup(key, cache_bucket_)) {
            if (generation == load_generation_ && current_path_ == path && hit->texture) {
                gtk_picture_set_paintable(GTK_PICTURE(artwork_image_.get()),
                                          GDK_PAINTABLE(hit->texture));
                has_artwork_ = true;
                update_visibility();
            }
            return;
        }

        auto alive = alive_;
        generator_->queue_job(
            key, path, cache_bucket_,
            [this, alive, generation, path](GdkTexture* texture, const std::string&) {
                if (!*alive || generation != load_generation_ || current_path_ != path) return;
                if (texture) {
                    gtk_picture_set_paintable(GTK_PICTURE(artwork_image_.get()),
                                              GDK_PAINTABLE(texture));
                    has_artwork_ = true;
                    update_visibility();
                } else {
                    show_fallback();
                }
            });
        return;
    }

    // Legacy path: no shared cache configured (used by standalone tests).
    cancellable_ = g_cancellable_new();
    auto* data = new DecodeData{this, alive_, path};
    decode_task_ = g_task_new(nullptr, cancellable_, on_decode_done, nullptr);
    g_task_set_task_data(decode_task_, data, free_decode_data);
    g_task_run_in_thread(decode_task_, decode_thread_func);
}

void ArtworkView::on_map(GtkWidget*, gpointer user_data) {
    static_cast<ArtworkView*>(user_data)->start_pending_load();
}

void ArtworkView::decode_thread_func(GTask* task, gpointer /*source_object*/,
                                     gpointer /*task_data*/, GCancellable* cancellable) {
    auto* data = static_cast<DecodeData*>(g_task_get_task_data(task));
    if (!data) return;

    GError* error = nullptr;
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_error(task, g_error_new(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));
        return;
    }

    GdkPixbuf* source = gdk_pixbuf_new_from_file(data->path.c_str(), &error);
    if (error || !source) {
        if (error) {
            g_task_return_error(task, error);
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
    GdkTexture* texture = gdk_texture_new_for_pixbuf(square);

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
    gtk_picture_set_paintable(GTK_PICTURE(artwork_image_.get()), nullptr);
    has_artwork_ = false;
    update_visibility();
}

void ArtworkView::set_corner_radius(double radius) {
    corner_radius_ = radius;
}

void ArtworkView::set_preferred_size(int width, int height) {
    preferred_width_ = width;
    preferred_height_ = height;

    GtkWidget* sizer = gtk_overlay_get_child(GTK_OVERLAY(widget_.get()));
    if (sizer) {
        gtk_widget_set_size_request(sizer, width, height);
    }
    gtk_widget_set_size_request(widget_.get(), width, height);
    gtk_widget_set_size_request(artwork_image_.get(), width, height);
    gtk_widget_set_size_request(fallback_image_.get(), width, height);

    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(artwork_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_halign(fallback_image_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_valign(fallback_image_.get(), GTK_ALIGN_FILL);
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_image_set_pixel_size(GTK_IMAGE(fallback_image_.get()),
                             std::min(48, std::max(16, std::min(width, height))));
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
