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
void safe_list_box_append(GtkListBox* list_box, GtkWidget* child);
void safe_scrolled_window_set_child(GtkScrolledWindow* scrolled, GtkWidget* child);
void safe_stack_add_named(GtkStack* stack, GtkWidget* child, const char* name);
void safe_stack_remove(GtkStack* stack, GtkWidget* child);
void safe_box_remove(GtkBox* box, GtkWidget* child);
void safe_button_set_child(GtkButton* button, GtkWidget* child);
void safe_menu_button_set_child(GtkMenuButton* menu_button, GtkWidget* child);
void safe_picture_set_file(GtkPicture* picture, GFile* file);
void safe_picture_set_paintable(GtkPicture* picture, GdkPaintable* paintable);
// Note: GtkContainer is deprecated in GTK4. Use specific container functions instead.

} // namespace lollypop