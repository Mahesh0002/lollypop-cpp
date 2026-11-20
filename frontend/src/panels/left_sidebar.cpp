/**
 * Lollypop C++ - Left Sidebar Implementation
 */

#include "panels/left_sidebar.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

LeftSidebar::LeftSidebar(MainWindow* main_window)
    : main_window_(main_window),
      sidebar_box_(nullptr),
      header_box_(nullptr),
      menu_button_(nullptr),
      settings_button_(nullptr),
      fullscreen_button_(nullptr),
      nav_list_box_(nullptr),
      settings_revealer_(nullptr) {
    setup_ui();
    create_navigation_items();
}

LeftSidebar::~LeftSidebar() = default;

void LeftSidebar::setup_ui() {
    // Main sidebar container
    sidebar_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box_, "sidebar");
    gtk_widget_set_vexpand(sidebar_box_, TRUE);

    // Header with menu button
    header_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header_box_, "sidebar-header");
    gtk_box_set_homogeneous(GTK_BOX(header_box_), FALSE);
    gtk_widget_set_margin_start(header_box_, 8);
    gtk_widget_set_margin_end(header_box_, 8);
    gtk_widget_set_margin_top(header_box_, 8);
    gtk_widget_set_margin_bottom(header_box_, 8);

    // Menu button (hamburger)
    menu_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(menu_button_), FALSE);
    gtk_widget_add_css_class(menu_button_, "sidebar-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 24);
    gtk_button_set_child(GTK_BUTTON(menu_button_), menu_icon);
    g_signal_connect(menu_button_, "clicked", G_CALLBACK(on_menu_clicked), this);

    // Settings and fullscreen buttons (hidden initially, shown when menu clicked)
    settings_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(settings_button_), FALSE);
    gtk_widget_add_css_class(settings_button_, "sidebar-action-button");
    GtkWidget* settings_icon = gtk_image_new_from_icon_name("preferences-system-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(settings_icon), 20);
    gtk_button_set_child(GTK_BUTTON(settings_button_), settings_icon);
    g_signal_connect(settings_button_, "clicked", G_CALLBACK(on_settings_clicked), this);

    fullscreen_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(fullscreen_button_), FALSE);
    gtk_widget_add_css_class(fullscreen_button_, "sidebar-action-button");
    GtkWidget* fullscreen_icon = gtk_image_new_from_icon_name("view-fullscreen-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(fullscreen_icon), 20);
    gtk_button_set_child(GTK_BUTTON(fullscreen_button_), fullscreen_icon);
    g_signal_connect(fullscreen_button_, "clicked", G_CALLBACK(on_fullscreen_clicked), this);

    // Revealer for settings/fullscreen buttons
    settings_revealer_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(settings_revealer_), GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(settings_revealer_), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(settings_revealer_), FALSE);

    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(action_box), settings_button_);
    gtk_box_append(GTK_BOX(action_box), fullscreen_button_);
    gtk_revealer_set_child(GTK_REVEALER(settings_revealer_), action_box);

    gtk_box_append(GTK_BOX(header_box_), menu_button_);
    gtk_box_append(GTK_BOX(header_box_), settings_revealer_);

    // Navigation list
    nav_list_box_ = gtk_list_box_new();
    gtk_widget_add_css_class(nav_list_box_, "sidebar-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav_list_box_), GTK_SELECTION_SINGLE);
    gtk_widget_set_vexpand(nav_list_box_, TRUE);
    gtk_widget_set_valign(nav_list_box_, GTK_ALIGN_START);

    // Assemble
    gtk_box_append(GTK_BOX(sidebar_box_), header_box_);
    gtk_box_append(GTK_BOX(sidebar_box_), nav_list_box_);
}

void LeftSidebar::create_navigation_items() {
    nav_items_ = {
        {"favorites", "Favourite Albums", "favorite-symbolic"},
        {"albums", "Albums", "media-optical-symbolic"},
        {"artists", "Artists", "user-group-symbolic"},
        {"playlists", "Playlists", "playlist-symbolic"},
        {"genres", "Genres", "music-symbolic"},
        {"songs", "Songs", "media-playlist-symbolic"},
        {"liked", "Liked Music", "heart-symbolic"},
    };

    for (const auto& item : nav_items_) {
        create_nav_button(item);
    }

    // Set first item as active by default
    if (!nav_items_.empty()) {
        set_active_item(nav_items_[0].id);
    }
}

void LeftSidebar::create_nav_button(const NavigationItem& item) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "sidebar-nav-item");
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    // Icon
    GtkWidget* icon = gtk_image_new_from_icon_name(item.icon_name.c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 22);
    gtk_widget_add_css_class(icon, "sidebar-nav-icon");

    // Label (hidden in compact mode)
    GtkWidget* label = gtk_label_new(item.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "sidebar-nav-label");
    gtk_widget_set_visible(label, expanded_);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_button_set_child(GTK_BUTTON(button), box);

    // Store item ID in button data (cast to GObject*)
    g_object_set_data_full(G_OBJECT(button), "nav-item-id", g_strdup(item.id.c_str()), g_free);

    g_signal_connect(button, "clicked", G_CALLBACK(on_nav_item_clicked), this);

    gtk_list_box_append(GTK_LIST_BOX(nav_list_box_), button);
}

void LeftSidebar::set_active_item(const std::string& item_id) {
    active_item_id_ = item_id;
    update_active_state();

    // Notify middle library to switch page
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->set_active_page(item_id);
        }
    }
}

void LeftSidebar::toggle_expanded() {
    expanded_ = !expanded_;

    // Update label visibility by iterating through list box rows
    GtkWidget* child = gtk_widget_get_first_child(nav_list_box_);
    while (child) {
        GtkWidget* row = child;
        GtkWidget* button = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* box = gtk_button_get_child(GTK_BUTTON(button));
        
        // Get the second child of the box (the label)
        GtkWidget* box_child = gtk_widget_get_first_child(box);
        if (box_child) {
            box_child = gtk_widget_get_next_sibling(box_child);  // Skip icon, get label
            if (box_child) {
                gtk_widget_set_visible(box_child, expanded_);
            }
        }
        
        child = gtk_widget_get_next_sibling(child);
    }

    // Animate width change
    int target_width = expanded_ ? 220 : 72;
    gtk_widget_set_size_request(sidebar_box_, target_width, -1);
}

void LeftSidebar::update_active_state() {
    GtkWidget* child = gtk_widget_get_first_child(nav_list_box_);
    while (child) {
        GtkWidget* row = child;
        GtkWidget* button = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        const char* item_id = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "nav-item-id"));
        if (item_id && std::string(item_id) == active_item_id_) {
            gtk_widget_add_css_class(button, "active");
            gtk_list_box_select_row(GTK_LIST_BOX(nav_list_box_), GTK_LIST_BOX_ROW(row));
        } else {
            gtk_widget_remove_css_class(button, "active");
        }
        child = gtk_widget_get_next_sibling(child);
    }
}

void LeftSidebar::on_menu_clicked(GtkButton* button, gpointer user_data) {
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    gboolean revealed = gtk_revealer_get_reveal_child(GTK_REVEALER(self->settings_revealer_));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->settings_revealer_), !revealed);
}

void LeftSidebar::on_nav_item_clicked(GtkButton* button, gpointer user_data) {
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    const char* item_id = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "nav-item-id"));
    if (item_id) {
        self->set_active_item(item_id);
    }
}

void LeftSidebar::on_settings_clicked(GtkButton* button, gpointer user_data) {
    // TODO: Open settings dialog
    std::cout << "Settings clicked" << std::endl;
}

void LeftSidebar::on_fullscreen_clicked(GtkButton* button, gpointer user_data) {
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    GtkWindow* window = self->main_window_->get_window();
    if (gtk_window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}

} // namespace lollypop