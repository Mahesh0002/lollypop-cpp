/**
 * Lollypop C++ - Fullscreen Close Button Implementation
 */

#include "panels/fullscreen_close_button.hpp"

#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

namespace lollypop {

FullscreenCloseButton::FullscreenCloseButton() {
    button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button_), FALSE);
    gtk_widget_add_css_class(button_, "fullscreen-control-button");
    gtk_widget_add_css_class(button_, "fullscreen-close-button");
    gtk_widget_set_size_request(button_, 44, 44);
    gtk_widget_set_tooltip_text(button_, "Leave fullscreen");
    accessibility::set_label(button_, "Leave fullscreen player");

    GtkWidget* icon =
        gtk_image_new_from_icon_name("window-close-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
    safe_button_set_child(GTK_BUTTON(button_), icon);

    g_signal_connect(button_, "clicked", G_CALLBACK(on_clicked), this);
    gtk_widget_set_visible(button_, TRUE);
}

FullscreenCloseButton::~FullscreenCloseButton() = default;

void FullscreenCloseButton::on_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenCloseButton*>(user_data);
    if (self->callback_) self->callback_();
}

} // namespace lollypop
