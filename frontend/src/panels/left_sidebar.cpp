/**
 * Lollypop C++ - Left Sidebar Implementation
 *
 * Compact mode: 42px wide (icon only, 22px icon + 10px left/right padding)
 * Expanded mode: 220px wide (icon + label)
 * Toggle: '>' (pan-end-symbolic) to expand, '<' (pan-start-symbolic) to collapse
 * No branding, no greetings - pure navigation.
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
      collapse_button_(nullptr),
      collapse_icon_(nullptr),
      menu_button_(nullptr),
      settings_button_(nullptr),
      fullscreen_button_(nullptr),
      nav_list_box_(nullptr),
      menu_popover_(nullptr) {
    // Default to expanded
    expanded_ = true;
    setup_ui();
    create_navigation_items();
    load_sidebar_state();
    apply_expanded_state();
}

LeftSidebar::~LeftSidebar() = default;

void LeftSidebar::setup_ui() {
    // Main sidebar container
    sidebar_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box_, "sidebar");
    gtk_widget_set_vexpand(sidebar_box_, TRUE);

    // Header with collapse button and menu button (hamburger hidden in compact)
    header_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_box_, "sidebar-header");
    gtk_box_set_homogeneous(GTK_BOX(header_box_), FALSE);
    gtk_widget_set_margin_top(header_box_, 4);
    gtk_widget_set_margin_bottom(header_box_, 4);
    gtk_widget_set_margin_start(header_box_, 0);
    gtk_widget_set_margin_end(header_box_, 0);

    // Collapse button (chevron) - toggles sidebar width
    collapse_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(collapse_button_), FALSE);
    gtk_widget_add_css_class(collapse_button_, "sidebar-collapse-button");
    collapse_icon_ = gtk_image_new_from_icon_name(
        expanded_ ? "pan-start-symbolic" : "pan-end-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(collapse_icon_), kIconSize);
    gtk_button_set_child(GTK_BUTTON(collapse_button_), collapse_icon_);
    g_signal_connect(collapse_button_, "clicked", G_CALLBACK(on_collapse_clicked), this);
    gtk_widget_set_tooltip_text(collapse_button_, expanded_
                                ? "Collapse sidebar"
                                : "Expand sidebar");

    // Menu button (hamburger) - opens popover with Settings/Fullscreen
    // Hidden in compact mode
    menu_button_ = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(menu_button_), FALSE);
    gtk_widget_add_css_class(menu_button_, "sidebar-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), kIconSize);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button_), menu_icon);
    gtk_widget_set_tooltip_text(menu_button_, "More");

    // Popover menu
    menu_popover_ = gtk_popover_new();
    GtkWidget* popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(popover_box, 8);
    gtk_widget_set_margin_bottom(popover_box, 8);
    gtk_widget_set_margin_start(popover_box, 8);
    gtk_widget_set_margin_end(popover_box, 8);

    settings_button_ = gtk_button_new_with_label("Settings");
    gtk_button_set_has_frame(GTK_BUTTON(settings_button_), FALSE);
    gtk_widget_set_halign(settings_button_, GTK_ALIGN_START);
    gtk_widget_set_hexpand(settings_button_, TRUE);
    g_signal_connect(settings_button_, "clicked", G_CALLBACK(on_settings_clicked), this);

    fullscreen_button_ = gtk_button_new_with_label("Fullscreen");
    gtk_button_set_has_frame(GTK_BUTTON(fullscreen_button_), FALSE);
    gtk_widget_set_halign(fullscreen_button_, GTK_ALIGN_START);
    gtk_widget_set_hexpand(fullscreen_button_, TRUE);
    g_signal_connect(fullscreen_button_, "clicked", G_CALLBACK(on_fullscreen_clicked), this);

    gtk_box_append(GTK_BOX(popover_box), settings_button_);
    gtk_box_append(GTK_BOX(popover_box), fullscreen_button_);
    gtk_popover_set_child(GTK_POPOVER(menu_popover_), popover_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button_), menu_popover_);

    gtk_box_append(GTK_BOX(header_box_), collapse_button_);
    gtk_box_append(GTK_BOX(header_box_), menu_button_);

    // Navigation list
    nav_list_box_ = gtk_list_box_new();
    gtk_widget_add_css_class(nav_list_box_, "sidebar-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav_list_box_), GTK_SELECTION_SINGLE);
    gtk_widget_set_vexpand(nav_list_box_, TRUE);
    gtk_widget_set_valign(nav_list_box_, GTK_ALIGN_START);

    // Assemble sidebar
    gtk_box_append(GTK_BOX(sidebar_box_), header_box_);
    gtk_box_append(GTK_BOX(sidebar_box_), nav_list_box_);
}

void LeftSidebar::create_navigation_items() {
    nav_items_ = {
        {"home", "Home", "go-home-symbolic"},
        {"albums", "Albums", "media-optical-symbolic"},
        {"artists", "Artists", "user-group-symbolic"},
        {"playlists", "Playlists", "playlist-symbolic"},
        {"genres", "Genres", "music-symbolic"},
        {"songs", "Songs", "media-playlist-symbolic"},
        {"favorites", "Favourite Albums", "favorite-symbolic"},
        {"liked", "Liked Music", "heart-symbolic"},
    };

    for (const auto& item : nav_items_) {
        create_nav_button(item);
    }

    // Set first item as active by default (or restored from persistence)
    if (!nav_items_.empty() && active_item_id_.empty()) {
        set_active_item(nav_items_[0].id);
    } else if (!active_item_id_.empty()) {
        set_active_item(active_item_id_);
    }
}

void LeftSidebar::create_nav_button(const NavigationItem& item) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "sidebar-nav-item");
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);

    // Inner box - compact mode: no extra spacing/margins, expanded: icon + label
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, expanded_ ? 12 : 0);
    gtk_widget_set_margin_start(box, expanded_ ? 12 : 10);
    gtk_widget_set_margin_end(box, expanded_ ? 12 : 10);
    gtk_widget_set_margin_top(box, expanded_ ? 8 : 0);
    gtk_widget_set_margin_bottom(box, expanded_ ? 8 : 0);

    // Icon (always visible, 22px)
    GtkWidget* icon = gtk_image_new_from_icon_name(item.icon_name.c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(icon), kIconSize);
    gtk_widget_add_css_class(icon, "sidebar-nav-icon");

    // Label inside a revealer for animated show/hide
    GtkWidget* label = gtk_label_new(item.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "sidebar-nav-label");
    gtk_widget_set_hexpand(label, TRUE);

    GtkWidget* revealer = gtk_revealer_new();
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), expanded_);
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 180);
    gtk_revealer_set_child(GTK_REVEALER(revealer), label);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), revealer);
    gtk_button_set_child(GTK_BUTTON(button), box);

    // Store item ID and revealer in button data for later access
    g_object_set_data_full(G_OBJECT(button), "nav-item-id",
                           g_strdup(item.id.c_str()), g_free);
    g_object_set_data(G_OBJECT(button), "revealer", revealer);

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
    apply_expanded_state();
    animate_labels();
    save_sidebar_state();
}

void LeftSidebar::apply_expanded_state() {
    // Update sidebar width
    update_sidebar_width();

    // Update collapse icon
    update_toggle_icon();

    // Update header: show/hide menu button in compact mode
    if (expanded_) {
        gtk_widget_set_visible(menu_button_, TRUE);
        gtk_widget_remove_css_class(header_box_, "compact");
    } else {
        gtk_widget_set_visible(menu_button_, FALSE);
        gtk_widget_add_css_class(header_box_, "compact");
    }

    // Update nav items: change inner box spacing/margins
    GtkWidget* child = gtk_widget_get_first_child(nav_list_box_);
    while (child) {
        GtkWidget* row = child;
        GtkWidget* button = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* box = gtk_button_get_child(GTK_BUTTON(button));
        
        if (box) {
            // Update box spacing and margins
            gtk_orientable_set_orientation(GTK_ORIENTABLE(box), GTK_ORIENTATION_HORIZONTAL);
            if (expanded_) {
                // Expanded: spacing 12, margins 12/12/8/8
                gtk_widget_set_margin_start(box, 12);
                gtk_widget_set_margin_end(box, 12);
                gtk_widget_set_margin_top(box, 8);
                gtk_widget_set_margin_bottom(box, 8);
                // Can't easily change box spacing after creation, so we'll handle via CSS
                gtk_widget_remove_css_class(box, "compact");
            } else {
                // Compact: no spacing, margins 10/10/0/0
                gtk_widget_set_margin_start(box, 10);
                gtk_widget_set_margin_end(box, 10);
                gtk_widget_set_margin_top(box, 0);
                gtk_widget_set_margin_bottom(box, 0);
                gtk_widget_add_css_class(box, "compact");
            }
        }
        
        child = gtk_widget_get_next_sibling(child);
    }
}

void LeftSidebar::update_toggle_icon() {
    const char* icon_name = expanded_ ? "pan-start-symbolic" : "pan-end-symbolic";
    GtkWidget* new_icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(new_icon), kIconSize);
    GtkWidget* old_child = gtk_button_get_child(GTK_BUTTON(collapse_button_));
    gtk_button_set_child(GTK_BUTTON(collapse_button_), new_icon);
    collapse_icon_ = new_icon;  // Keep reference
    (void)old_child;

    gtk_widget_set_tooltip_text(collapse_button_, expanded_
                                ? "Collapse sidebar"
                                : "Expand sidebar");
}

void LeftSidebar::animate_labels() {
    GtkWidget* child = gtk_widget_get_first_child(nav_list_box_);
    while (child) {
        GtkWidget* row = child;
        GtkWidget* button = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        GtkWidget* revealer = static_cast<GtkWidget*>(
            g_object_get_data(G_OBJECT(button), "revealer"));
        if (revealer) {
            gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), expanded_);
        }
        child = gtk_widget_get_next_sibling(child);
    }
}

void LeftSidebar::update_sidebar_width() {
    int target_width = expanded_ ? kExpandedWidth : kCompactWidth;
    gtk_widget_set_size_request(sidebar_box_, target_width, -1);
}

void LeftSidebar::update_active_state() {
    GtkWidget* child = gtk_widget_get_first_child(nav_list_box_);
    while (child) {
        GtkWidget* row = child;
        GtkWidget* button = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
        const char* item_id = static_cast<const char*>(
            g_object_get_data(G_OBJECT(button), "nav-item-id"));
        if (item_id && std::string(item_id) == active_item_id_) {
            gtk_widget_add_css_class(button, "active");
            gtk_list_box_select_row(GTK_LIST_BOX(nav_list_box_),
                                    GTK_LIST_BOX_ROW(row));
        } else {
            gtk_widget_remove_css_class(button, "active");
        }
        child = gtk_widget_get_next_sibling(child);
    }
}

void LeftSidebar::load_sidebar_state() {
    // TODO: Load from GSettings or settings layer (Phase 13 / persistence)
    // For now, default to expanded
    expanded_ = true;
    update_sidebar_width();
}

void LeftSidebar::save_sidebar_state() {
    // TODO: Save to GSettings or settings layer (Phase 13 / persistence)
    std::cout << "[LeftSidebar] Sidebar state: "
              << (expanded_ ? "expanded" : "compact") << std::endl;
}

void LeftSidebar::on_collapse_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    self->toggle_expanded();
}

void LeftSidebar::on_menu_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    gtk_popover_popup(GTK_POPOVER(self->menu_popover_));
}

void LeftSidebar::on_nav_item_clicked(GtkButton* button, gpointer user_data) {
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    const char* item_id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "nav-item-id"));
    if (item_id) {
        self->set_active_item(item_id);
    }
}

void LeftSidebar::on_settings_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    gtk_popover_popdown(GTK_POPOVER(self->menu_popover_));
    // TODO: Open settings dialog (Phase 13)
    std::cout << "Settings clicked" << std::endl;
}

void LeftSidebar::on_fullscreen_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    gtk_popover_popdown(GTK_POPOVER(self->menu_popover_));
    GtkWindow* window = self->main_window_->get_window();
    if (gtk_window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}

} // namespace lollypop