/**
 * Lollypop C++ - Safe GTK Helpers
 * Prevents double-parenting GTK widget errors.
 * 
 * Strategy: Only check if the NEW child already has a parent.
 * Let GTK handle replacing existing children - it does this correctly.
 * Don't try to unparent existing children as they may be in invalid state.
 */

#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

namespace lollypop {

static inline bool is_valid_widget(GtkWidget* widget) {
    return widget && GTK_IS_WIDGET(widget);
}

void safe_box_append(GtkBox* box, GtkWidget* child) {
    if (!box || !child) return;
    if (!is_valid_widget(child)) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_box_append(box, child);
    }
}

void safe_grid_attach(GtkGrid* grid, GtkWidget* child, gint left, gint top, gint width, gint height) {
    if (!grid || !child) return;
    if (!is_valid_widget(child)) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_grid_attach(grid, child, left, top, width, height);
    }
}

void safe_flow_box_append(GtkFlowBox* flow_box, GtkWidget* child) {
    if (!flow_box || !child) return;
    if (!is_valid_widget(child)) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_flow_box_append(flow_box, child);
    }
}

void safe_list_box_append(GtkListBox* list_box, GtkWidget* child) {
    if (!list_box || !child) return;
    if (!is_valid_widget(child)) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_list_box_append(list_box, child);
    }
}

void safe_scrolled_window_set_child(GtkScrolledWindow* scrolled, GtkWidget* child) {
    if (!scrolled || !child) return;
    if (!is_valid_widget(child)) return;
    // Let GTK handle replacing the current child - it does this correctly
    // Only unparent the new child if it already has a parent
    if (gtk_widget_get_parent(child) != nullptr) {
        gtk_widget_unparent(child);
    }
    gtk_scrolled_window_set_child(scrolled, child);
}

void safe_stack_add_named(GtkStack* stack, GtkWidget* child, const char* name) {
    if (!stack || !child || !name) return;
    if (!is_valid_widget(child)) return;
    // Stack does NOT automatically reparent - check and unparent first
    if (gtk_widget_get_parent(child) != nullptr) {
        gtk_widget_unparent(child);
    }
    gtk_stack_add_named(stack, child, name);
}

void safe_stack_remove(GtkStack* stack, GtkWidget* child) {
    if (!stack || !child) return;
    if (!is_valid_widget(child)) return;
    gtk_stack_remove(stack, child);
}

void safe_box_remove(GtkBox* box, GtkWidget* child) {
    if (!box || !child) return;
    if (!is_valid_widget(child)) return;
    gtk_box_remove(box, child);
}

void safe_button_set_child(GtkButton* button, GtkWidget* child) {
    if (!button || !child) return;
    if (!is_valid_widget(child)) return;
    // Let GTK handle replacing the current child
    if (gtk_widget_get_parent(child) != nullptr) {
        gtk_widget_unparent(child);
    }
    gtk_button_set_child(button, child);
}

void safe_menu_button_set_child(GtkMenuButton* menu_button, GtkWidget* child) {
    if (!menu_button || !child) return;
    if (!is_valid_widget(child)) return;
    // Let GTK handle replacing the current child
    if (gtk_widget_get_parent(child) != nullptr) {
        gtk_widget_unparent(child);
    }
    gtk_menu_button_set_child(menu_button, child);
}

void safe_picture_set_file(GtkPicture* picture, GFile* file) {
    if (!picture) return;
    gtk_picture_set_file(picture, file);
}

void safe_picture_set_paintable(GtkPicture* picture, GdkPaintable* paintable) {
    if (!picture) return;
    gtk_picture_set_paintable(picture, paintable);
}

// Note: GtkContainer is deprecated in GTK4. Use specific container functions instead.
// void safe_container_add(GtkContainer* container, GtkWidget* child) {
//     if (!container || !child) return;
//     if (gtk_widget_get_parent(child) == nullptr) {
//         gtk_container_add(container, child);
//     }
// }

} // namespace lollypop