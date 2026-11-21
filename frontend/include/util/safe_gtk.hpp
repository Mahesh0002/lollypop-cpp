/**
 * Lollypop C++ - Safe GTK Helpers
 * Prevents double-parenting GTK widget errors.
 */

#pragma once

#include <gtk/gtk.h>

namespace lollypop {

void safe_box_append(GtkBox* box, GtkWidget* child);
void safe_grid_attach(GtkGrid* grid, GtkWidget* child, gint left, gint top, gint width, gint height);
void safe_flow_box_append(GtkFlowBox* flow_box, GtkWidget* child);
void safe_scrolled_window_set_child(GtkScrolledWindow* scrolled, GtkWidget* child);
void safe_stack_add_named(GtkStack* stack, GtkWidget* child, const char* name);
void safe_container_add(GtkContainer* container, GtkWidget* child);

} // namespace lollypop