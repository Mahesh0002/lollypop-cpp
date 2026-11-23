/**
 * Lollypop C++ - Left Sidebar Implementation
 *
 * One button is one complete hit target. No ListBoxRow, no nested controls.
 * Geometry is fixed: 50x52px rows, 156x52px expanded rows, and a 22px icon
 * column that never changes position.
 */

#include "panels/left_sidebar.hpp"
#include "main_window.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace lollypop {
namespace {

// GTK/Adwaita reserves this much space around a flat button's child. The
// content widgets below subtract it so the requested outer size is exact.
constexpr int kThemePaddingH = 24;
constexpr int kThemePaddingV = 10;
constexpr int kContentHeight = LeftSidebar::kButtonHeight - kThemePaddingV; // 42

int content_width(int outer_width) {
    return outer_width - kThemePaddingH;
}

std::filesystem::path state_path() {
    return std::filesystem::path(g_get_user_config_dir())
        / "lollypop-cpp" / "sidebar-expanded";
}

LeftSidebar::ActionButton create_action_button(const char* icon_name,
                                               const char* accessible_label,
                                               GCallback callback,
                                               gpointer user_data,
                                               bool visible) {
    LeftSidebar::ActionButton action;
    action.button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(action.button), FALSE);
    gtk_widget_add_css_class(action.button, "sidebar-action-button");
    gtk_widget_set_size_request(action.button, LeftSidebar::kButtonWidth,
                                LeftSidebar::kButtonHeight);
    gtk_widget_set_hexpand(action.button, FALSE);
    gtk_widget_set_vexpand(action.button, FALSE);
    gtk_widget_set_halign(action.button, GTK_ALIGN_START);
    gtk_widget_set_valign(action.button, GTK_ALIGN_CENTER);

    action.icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(action.icon), LeftSidebar::kIconSize);
    gtk_widget_set_size_request(action.icon,
                                content_width(LeftSidebar::kButtonWidth),
                                kContentHeight);
    gtk_widget_set_halign(action.icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(action.icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(action.icon, TRUE);
    safe_button_set_child(GTK_BUTTON(action.button), action.icon);

    g_signal_connect(action.button, "clicked", callback, user_data);
    gtk_widget_set_tooltip_text(action.button, accessible_label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(action.button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   accessible_label, -1);
    gtk_widget_set_visible(action.button, visible);
    return action;
}

} // namespace

LeftSidebar::LeftSidebar(MainWindow* main_window)
    : main_window_(main_window) {
    setup_ui();
    create_navigation_items();
    load_sidebar_state();
    apply_expanded_state();
    set_active_item("home");
}

LeftSidebar::~LeftSidebar() = default;

void LeftSidebar::setup_ui() {
    sidebar_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box_, "sidebar");
    gtk_widget_set_margin_top(sidebar_box_, 2);
    gtk_widget_set_vexpand(sidebar_box_, TRUE);
    gtk_widget_set_visible(sidebar_box_, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(sidebar_box_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Library navigation", -1);

    // Header: three fixed-width cells. Settings/fullscreen are hidden in
    // compact mode, so the collapse control stays in the same position.
    header_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_box_, "sidebar-header");
    gtk_box_set_homogeneous(GTK_BOX(header_box_), FALSE);
    gtk_widget_set_hexpand(header_box_, TRUE);
    gtk_widget_set_halign(header_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(header_box_, TRUE);

    collapse_ = create_action_button("pan-start-symbolic", "Collapse sidebar",
                                     G_CALLBACK(on_collapse_clicked), this,
                                     TRUE);
    settings_ = create_action_button("preferences-system-symbolic", "Settings",
                                     G_CALLBACK(on_settings_clicked), this,
                                     FALSE);
    fullscreen_ = create_action_button("view-fullscreen-symbolic", "Fullscreen",
                                       G_CALLBACK(on_fullscreen_clicked), this,
                                       FALSE);
    collapse_button_ = collapse_.button;

    safe_box_append(GTK_BOX(header_box_), collapse_.button);
    safe_box_append(GTK_BOX(header_box_), settings_.button);
    safe_box_append(GTK_BOX(header_box_), fullscreen_.button);

    nav_list_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(nav_list_box_, "sidebar-nav");
    gtk_widget_set_hexpand(nav_list_box_, TRUE);
    gtk_widget_set_halign(nav_list_box_, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(nav_list_box_, TRUE);
    gtk_widget_set_valign(nav_list_box_, GTK_ALIGN_START);
    gtk_widget_set_visible(nav_list_box_, TRUE);

    safe_box_append(GTK_BOX(sidebar_box_), header_box_);
    safe_box_append(GTK_BOX(sidebar_box_), nav_list_box_);
}

void LeftSidebar::create_navigation_items() {
    nav_items_ = {
        {"home",      "Home",             "go-home-symbolic"},
        {"albums",    "Albums",           "media-optical-symbolic"},
        {"artists",   "Artists",          "system-users-symbolic"},
        {"playlists", "Playlists",        "folder-music-symbolic"},
        {"genres",    "Genres",           "audio-x-generic-symbolic"},
        {"songs",     "Songs",            "media-playlist-repeat-symbolic"},
        {"favorites", "Favourite Albums", "starred-symbolic"},
        {"liked",     "Liked Music",      "star-new-symbolic"},
    };
    for (const auto& item : nav_items_) {
        add_nav_row(item);
    }
}

void LeftSidebar::add_nav_row(const NavigationItem& item) {
    NavRow row;
    row.id = item.id;

    row.button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(row.button), FALSE);
    gtk_widget_add_css_class(row.button, "sidebar-nav-item");
    gtk_widget_set_size_request(row.button, kCompactWidth, kButtonHeight);
    gtk_widget_set_hexpand(row.button, TRUE);
    gtk_widget_set_vexpand(row.button, FALSE);
    gtk_widget_set_halign(row.button, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row.button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row.button, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(row.button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   item.label.c_str(), -1);

    row.content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(row.content,
                                content_width(kCompactWidth), kContentHeight);
    gtk_widget_set_hexpand(row.content, FALSE);
    gtk_widget_set_halign(row.content, GTK_ALIGN_START);
    gtk_widget_set_valign(row.content, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row.content, TRUE);

    GtkWidget* icon = gtk_image_new_from_icon_name(item.icon_name.c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(icon), kIconSize);
    gtk_widget_add_css_class(icon, "sidebar-nav-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_START);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon, TRUE);

    GtkWidget* label = gtk_label_new(item.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 9);
    gtk_widget_add_css_class(label, "sidebar-nav-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(label, 4);
    gtk_widget_set_visible(label, TRUE);

    row.revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(row.revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(row.revealer), 180);
    gtk_revealer_set_child(GTK_REVEALER(row.revealer), label);
    gtk_widget_set_visible(row.revealer, TRUE);

    safe_box_append(GTK_BOX(row.content), icon);
    safe_box_append(GTK_BOX(row.content), row.revealer);
    safe_button_set_child(GTK_BUTTON(row.button), row.content);
    safe_box_append(GTK_BOX(nav_list_box_), row.button);

    g_object_set_data_full(G_OBJECT(row.button), "nav-item-id",
                           g_strdup(item.id.c_str()), g_free);
    g_signal_connect(row.button, "clicked",
                     G_CALLBACK(on_nav_item_clicked), this);

    nav_rows_.push_back(row);
}

void LeftSidebar::set_active_item(const std::string& item_id) {
    active_item_id_ = item_id;
    update_active_state();

    if (main_window_) {
        if (MiddleLibrary* middle = main_window_->get_middle_library()) {
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
    } else {
        gtk_widget_add_css_class(sidebar_box_, "compact");
    }
    gtk_widget_set_visible(settings_.button, expanded_);
    gtk_widget_set_visible(fullscreen_.button, expanded_);

    update_sidebar_width();
    update_toggle_icon();
    update_label_visibility();
}

void LeftSidebar::set_row_width(NavRow& row, int width) {
    gtk_widget_set_size_request(row.button, width, kButtonHeight);
    gtk_widget_set_size_request(row.content, content_width(width),
                                kContentHeight);
}

void LeftSidebar::update_sidebar_width() {
    const int width = expanded_ ? kExpandedWidth : kCompactWidth;
    gtk_widget_set_size_request(sidebar_box_, width, -1);
    for (NavRow& row : nav_rows_) {
        set_row_width(row, width);
    }
}

void LeftSidebar::update_toggle_icon() {
    const char* icon = expanded_ ? "pan-start-symbolic" : "pan-end-symbolic";
    const char* label = expanded_ ? "Collapse sidebar" : "Expand sidebar";
    gtk_image_set_from_icon_name(GTK_IMAGE(collapse_.icon), icon);
    gtk_widget_set_tooltip_text(collapse_.button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(collapse_.button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
}

void LeftSidebar::update_label_visibility() {
    for (NavRow& row : nav_rows_) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(row.revealer), expanded_);
    }
}

void LeftSidebar::update_active_state() {
    for (NavRow& row : nav_rows_) {
        if (row.id == active_item_id_) {
            gtk_widget_add_css_class(row.button, "active");
        } else {
            gtk_widget_remove_css_class(row.button, "active");
        }
    }
}

void LeftSidebar::load_sidebar_state() {
    std::ifstream in(state_path());
    std::string value;
    expanded_ = !(in && std::getline(in, value) && value == "0");
}

void LeftSidebar::save_sidebar_state() {
    const auto path = state_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (out) out << (expanded_ ? "1" : "0") << '\n';
}

void LeftSidebar::on_collapse_clicked(GtkButton*, gpointer user_data) {
    static_cast<LeftSidebar*>(user_data)->toggle_expanded();
}

void LeftSidebar::on_settings_clicked(GtkButton*, gpointer) {
    std::cout << "Settings clicked" << std::endl;
}

void LeftSidebar::on_fullscreen_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<LeftSidebar*>(user_data);
    if (!self->main_window_) return;
    GtkWindow* window = self->main_window_->get_window();
    if (!window) return;
    if (gtk_window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}

void LeftSidebar::on_nav_item_clicked(GtkButton* button, gpointer user_data) {
    auto* self = static_cast<LeftSidebar*>(user_data);
    const char* id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "nav-item-id"));
    if (id) self->set_active_item(id);
}

} // namespace lollypop
