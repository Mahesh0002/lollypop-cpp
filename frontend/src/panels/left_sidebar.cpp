/**
 * Lollypop C++ - Left Sidebar Implementation
 *
 * Compact mode: 42px wide - only the '>' toggle button and 8 nav icons.
 * Expanded mode: 220px wide - '<' toggle + settings icon + fullscreen icon
 *   side by side in the header, then nav items with icon + text label.
 * Toggle: '>' (pan-end-symbolic) to expand, '<' (pan-start-symbolic) to collapse.
 * No branding, no greetings - pure navigation.
 *
 * Every button is centered with identical geometry:
 *   - Size locked at 36x36px (never changes between modes)
 *   - 10px margin on all four sides
 *   - Icon centered inside button (halign CENTER, valign CENTER)
 *
 * Only text labels appear/disappear via the GTK revealer widget.
 * Icons stay in the exact same position in both modes.
 */

#include "panels/left_sidebar.hpp"
#include "main_window.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

// ---------------------------------------------------------------------------
// Helper: create a sidebar action button with identical geometry
// ---------------------------------------------------------------------------
// All sidebar buttons (collapse, settings, fullscreen) use this helper so
// every icon ends up on the same straight line with the same size and spacing.
static GtkWidget* create_sidebar_action_button(const char* icon_name,
                                               GCallback clicked_cb,
                                               gpointer user_data,
                                               const char* tooltip,
                                               const char* accessible_label,
                                               bool visible) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "sidebar-action-button");
    // One uniform control for every sidebar action: 40x45 in both modes.
    gtk_widget_set_size_request(button, LeftSidebar::kButtonWidth,
                                LeftSidebar::kButtonHeight);
    gtk_widget_set_hexpand(button, FALSE);
    gtk_widget_set_vexpand(button, FALSE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

    GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), LeftSidebar::kIconSize);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon, TRUE);
    safe_button_set_child(GTK_BUTTON(button), icon);

    g_signal_connect(button, "clicked", clicked_cb, user_data);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   accessible_label, -1);
    gtk_widget_set_visible(button, visible);
    return button;
}

// ---------------------------------------------------------------------------
// LeftSidebar
// ---------------------------------------------------------------------------

LeftSidebar::LeftSidebar(MainWindow* main_window)
    : main_window_(main_window),
      sidebar_box_(nullptr),
      header_box_(nullptr),
      collapse_button_(nullptr),
      collapse_icon_(nullptr),
      settings_button_(nullptr),
      fullscreen_button_(nullptr),
      nav_list_box_(nullptr) {
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
    gtk_widget_set_visible(sidebar_box_, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(sidebar_box_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Library navigation", -1);

    // Header box: holds collapse toggle + settings + fullscreen icons (expanded only)
    header_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_box_, "sidebar-header");
    gtk_box_set_homogeneous(GTK_BOX(header_box_), TRUE);
    gtk_widget_set_hexpand(header_box_, TRUE);
    gtk_widget_set_halign(header_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(header_box_, TRUE);

    // Collapse toggle button - always visible
    collapse_button_ = create_sidebar_action_button(
        expanded_ ? "pan-start-symbolic" : "pan-end-symbolic",
        G_CALLBACK(on_collapse_clicked), this,
        expanded_ ? "Collapse sidebar" : "Expand sidebar",
        expanded_ ? "Collapse sidebar" : "Expand sidebar",
        true);
    collapse_icon_ = gtk_image_new_from_icon_name(
        expanded_ ? "pan-start-symbolic" : "pan-end-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(collapse_icon_), kIconSize);
    gtk_widget_set_halign(collapse_icon_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(collapse_icon_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(collapse_icon_, TRUE);
    // The displayed icon must be the one update_toggle_icon() swaps.
    safe_button_set_child(GTK_BUTTON(collapse_button_), collapse_icon_);

    // Settings icon button - shown only in expanded mode
    settings_button_ = create_sidebar_action_button(
        "preferences-system-symbolic",
        G_CALLBACK(on_settings_clicked), this,
        "Settings", "Settings", expanded_);

    // Fullscreen icon button - shown only in expanded mode
    fullscreen_button_ = create_sidebar_action_button(
        "view-fullscreen-symbolic",
        G_CALLBACK(on_fullscreen_clicked), this,
        "Fullscreen", "Fullscreen", expanded_);

    safe_box_append(GTK_BOX(header_box_), collapse_button_);
    safe_box_append(GTK_BOX(header_box_), settings_button_);
    safe_box_append(GTK_BOX(header_box_), fullscreen_button_);

    // Navigation container. A plain box means each item is one button with no
    // GtkListBoxRow wrapper around it.
    nav_list_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(nav_list_box_, "sidebar-nav");
    gtk_widget_set_hexpand(nav_list_box_, TRUE);
    gtk_widget_set_halign(nav_list_box_, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(nav_list_box_, TRUE);
    gtk_widget_set_valign(nav_list_box_, GTK_ALIGN_START);
    gtk_widget_set_visible(nav_list_box_, TRUE);

    // Assemble sidebar
    safe_box_append(GTK_BOX(sidebar_box_), header_box_);
    safe_box_append(GTK_BOX(sidebar_box_), nav_list_box_);
}

void LeftSidebar::create_navigation_items() {
    nav_items_ = {
        {"home", "Home", "go-home-symbolic"},
        {"albums", "Albums", "media-optical-symbolic"},
        {"artists", "Artists", "system-users-symbolic"},
        {"playlists", "Playlists", "folder-music-symbolic"},
        {"genres", "Genres", "audio-x-generic-symbolic"},
        {"songs", "Songs", "media-playlist-repeat-symbolic"},
        {"favorites", "Favourite Albums", "starred-symbolic"},
        {"liked", "Liked Music", "star-new-symbolic"},
    };

    for (const auto& item : nav_items_) {
        create_nav_button(item);
    }

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
    // Height never changes; width is switched between 40 and 135 in
    // update_sidebar_width(). One button is the entire hit target.
    gtk_widget_set_size_request(button, expanded_ ? kExpandedWidth : kCompactWidth,
                                kButtonHeight);
    gtk_widget_set_hexpand(button, FALSE);
    gtk_widget_set_vexpand(button, FALSE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(button, TRUE);

    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   item.label.c_str(), -1);

    // Icon and label live directly inside the button. The icon occupies a
    // fixed 40x45 slot so it stays on the exact same x/y in both modes; the
    // label simply slides in to the right when the sidebar expands.
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(box, TRUE);

    GtkWidget* icon = gtk_image_new_from_icon_name(item.icon_name.c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(icon), kIconSize);
    gtk_widget_add_css_class(icon, "sidebar-nav-icon");
    gtk_widget_set_size_request(icon, kButtonWidth, kButtonHeight);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon, TRUE);

    GtkWidget* label = gtk_label_new(item.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "sidebar-nav-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(label, 4);
    gtk_widget_set_visible(label, TRUE);

    GtkWidget* revealer = gtk_revealer_new();
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), expanded_);
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 180);
    gtk_revealer_set_child(GTK_REVEALER(revealer), label);
    gtk_widget_set_visible(revealer, TRUE);

    safe_box_append(GTK_BOX(box), icon);
    safe_box_append(GTK_BOX(box), revealer);
    safe_button_set_child(GTK_BUTTON(button), box);

    safe_box_append(GTK_BOX(nav_list_box_), button);
    nav_buttons_.push_back(button);

    g_object_set_data_full(G_OBJECT(button), "nav-item-id",
                           g_strdup(item.id.c_str()), g_free);
    g_object_set_data(G_OBJECT(button), "revealer", revealer);

    g_signal_connect(button, "clicked", G_CALLBACK(on_nav_item_clicked), this);
}

void LeftSidebar::set_active_item(const std::string& item_id) {
    active_item_id_ = item_id;
    update_active_state();

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
    save_sidebar_state();
}

void LeftSidebar::apply_expanded_state() {
    if (expanded_) {
        gtk_widget_remove_css_class(sidebar_box_, "compact");
        gtk_widget_set_visible(settings_button_, TRUE);
        gtk_widget_set_visible(fullscreen_button_, TRUE);
    } else {
        gtk_widget_add_css_class(sidebar_box_, "compact");
        gtk_widget_set_visible(settings_button_, FALSE);
        gtk_widget_set_visible(fullscreen_button_, FALSE);
    }

    update_sidebar_width();
    update_toggle_icon();
    animate_labels();
}

void LeftSidebar::update_toggle_icon() {
    const char* icon_name = expanded_ ? "pan-start-symbolic" : "pan-end-symbolic";
    if (collapse_icon_) {
        gtk_image_set_from_icon_name(GTK_IMAGE(collapse_icon_), icon_name);
    }

    gtk_widget_set_tooltip_text(collapse_button_, expanded_
                                ? "Collapse sidebar"
                                : "Expand sidebar");
}

void LeftSidebar::animate_labels() {
    for (GtkWidget* button : nav_buttons_) {
        auto* revealer = static_cast<GtkWidget*>(
            g_object_get_data(G_OBJECT(button), "revealer"));
        if (revealer) {
            gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), expanded_);
        }
    }
}

void LeftSidebar::update_sidebar_width() {
    const int width = expanded_ ? kExpandedWidth : kCompactWidth;
    gtk_widget_set_size_request(sidebar_box_, width, -1);
    for (GtkWidget* button : nav_buttons_) {
        gtk_widget_set_size_request(button, width, kButtonHeight);
    }
}

void LeftSidebar::update_active_state() {
    for (GtkWidget* button : nav_buttons_) {
        const char* item_id = static_cast<const char*>(
            g_object_get_data(G_OBJECT(button), "nav-item-id"));
        if (item_id && std::string(item_id) == active_item_id_) {
            gtk_widget_add_css_class(button, "active");
        } else {
            gtk_widget_remove_css_class(button, "active");
        }
    }
}

void LeftSidebar::load_sidebar_state() {
    expanded_ = true;
    update_sidebar_width();
}

void LeftSidebar::save_sidebar_state() {
    std::cout << "[LeftSidebar] Sidebar state: "
              << (expanded_ ? "expanded" : "compact") << std::endl;
}

void LeftSidebar::on_collapse_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    self->toggle_expanded();
}

void LeftSidebar::on_settings_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    std::cout << "Settings clicked" << std::endl;
}

void LeftSidebar::on_fullscreen_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    GtkWindow* window = self->main_window_->get_window();
    if (gtk_window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}

void LeftSidebar::on_nav_item_clicked(GtkButton* button, gpointer user_data) {
    LeftSidebar* self = static_cast<LeftSidebar*>(user_data);
    const char* item_id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "nav-item-id"));
    if (item_id) {
        self->set_active_item(item_id);
    }
}

} // namespace lollypop
