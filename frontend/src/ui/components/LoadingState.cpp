/**
 * Lollypop C++ - LoadingState Implementation
 */

#include "ui/components/LoadingState.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

LoadingState::LoadingState() {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 16));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_halign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(widget_.get(), GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(widget_.get(), "loading-state");
}

LoadingState::~LoadingState() = default;

void LoadingState::setup(const std::string& message) {
    auto* spinner = gtk_spinner_new();
    gtk_widget_add_css_class(spinner, "loading-state-spinner");
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_append(GTK_BOX(widget_.get()), spinner);

    auto* label = gtk_label_new(message.c_str());
    gtk_widget_add_css_class(label, "loading-state-message");
    gtk_label_set_xalign(GTK_LABEL(label), 0.5);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(widget_.get()), label);
}

} // namespace lollypop
