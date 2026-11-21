/**
 * Lollypop C++ - Safe GTK Helpers
 * Prevents double-parenting GTK widget errors.
 */

#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

namespace lollypop {

void safe_box_append(GtkBox* box, GtkWidget* child) {
    if (!box || !child) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_box_append(box, child);
    }
}

void safe_grid_attach(GtkGrid* grid, GtkWidget* child, gint left, gint top, gint width, gint height) {
    if (!grid || !child) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_grid_attach(grid, child, left, top, width, height);
    }
}

void safe_flow_box_append(GtkFlowBox* flow_box, GtkWidget* child) {
    if (!flow_box || !child) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_flow_box_append(flow_box, child);
    }
}

void safe_scrolled_window_set_child(GtkScrolledWindow* scrolled, GtkWidget* child) {
    if (!scrolled || !child) return;
    // For scrolled window, we need to check if child already has a parent
    // If it does, unparent it first
    GtkWidget* current = gtk_scrolled_window_get_child(scrolled);
    if (current) {
        gtk_widget_unparent(current);
    }
    if (gtk_widget_get_parent(child) != nullptr) {
        gtk_widget_unparent(child);
    }
    gtk_scrolled_window_set_child(scrolled, child);
}

void safe_stack_add_named(GtkStack* stack, GtkWidget* child, const char* name) {
    if (!stack || !child || !name) return;
    // Stack handles reparenting automatically, but we can check
    gtk_stack_add_named(stack, child, name);
}

void safe_container_add(GtkContainer* container, GtkWidget* child) {
    if (!container || !child) return;
    if (gtk_widget_get_parent(child) == nullptr) {
        gtk_container_add(container, child);
    }
}

} // namespace lollypop