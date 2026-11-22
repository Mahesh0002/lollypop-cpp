/**
 * Lollypop C++ - ErrorState Implementation
 */

#include "ui/components/ErrorState.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

namespace {

// Static trampoline for retry button callback.
void retry_button_clicked(GtkButton* button, gpointer user_data) {
    auto* cb = static_cast<std::function<void()>*>(user_data);
    if (cb && *cb) {
        (*cb)();
    }
}

} // namespace

ErrorState::ErrorState() {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 16));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(widget_.get(), "error-state");
}

ErrorState::~ErrorState() = default;

void ErrorState::setup(const std::string& title,
                       const std::string& description,
                       std::function<void()> retry_callback) {
    // Error icon
    auto* icon = gtk_image_new_from_icon_name("dialog-error-symbolic");
    gtk_widget_add_css_class(icon, "error-state-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), icon);

    // Title
    auto* title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "error-state-title");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.5);
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), title_label);

    // Description
    auto* desc_label = gtk_label_new(description.c_str());
    gtk_widget_add_css_class(desc_label, "error-state-description");
    gtk_label_set_xalign(GTK_LABEL(desc_label), 0.5);
    gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 40);
    gtk_widget_set_halign(desc_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), desc_label);

    // Retry button
    if (retry_callback) {
        // Heap-allocate the callback so it outlives this function.
        auto* cb = new std::function<void()>(std::move(retry_callback));
        auto* retry_button = gtk_button_new_with_label("Retry");
        gtk_widget_add_css_class(retry_button, "error-state-retry");
        gtk_widget_set_halign(retry_button, GTK_ALIGN_CENTER);
        // Consistent spacing on all four sides.
        gtk_widget_set_margin_start(retry_button, 10);
        gtk_widget_set_margin_end(retry_button, 10);
        gtk_widget_set_margin_top(retry_button, 10);
        gtk_widget_set_margin_bottom(retry_button, 10);
        g_signal_connect(retry_button, "clicked",
                         G_CALLBACK(retry_button_clicked), cb);
        // Note: cb is intentionally leaked; the button's lifetime is tied to
        // the widget, and the callback is only invoked while the widget lives.
        gtk_box_append(GTK_BOX(widget_.get()), retry_button);
    }
}

} // namespace lollypop
