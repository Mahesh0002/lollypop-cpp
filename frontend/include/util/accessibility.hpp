/**
 * Lollypop C++ - Shared accessibility helpers
 *
 * Custom widgets should pair every visible tooltip with an accessible name
 * that includes the object they act on. This keeps screen-reader naming from
 * being reimplemented differently in each panel.
 */

#pragma once

#include <gtk/gtk.h>

namespace lollypop {
namespace accessibility {

inline void set_label(GtkWidget* widget, const char* label) {
    if (!widget || !label) return;
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
}

} // namespace accessibility
} // namespace lollypop
