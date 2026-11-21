/**
 * Lollypop C++ - EmptyState Implementation
 */

#include "ui/components/EmptyState.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

EmptyState::EmptyState() {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 16));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(widget_.get(), "empty-state");
}

EmptyState::~EmptyState() = default;

void EmptyState::setup(const std::string& icon_name,
                       const std::string& title,
                       const std::string& description) {
    // Icon
    auto* icon = gtk_image_new_from_icon_name(icon_name.c_str());
    gtk_widget_add_css_class(icon, "empty-state-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), icon);

    // Title
    auto* title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "empty-state-title");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.5);
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), title_label);

    // Description
    auto* desc_label = gtk_label_new(description.c_str());
    gtk_widget_add_css_class(desc_label, "empty-state-description");
    gtk_label_set_xalign(GTK_LABEL(desc_label), 0.5);
    gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 40);
    gtk_widget_set_halign(desc_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), desc_label);
}

} // namespace lollypop
