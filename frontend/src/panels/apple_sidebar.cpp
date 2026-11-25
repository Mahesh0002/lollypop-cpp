/**
 * Apple-style source-list sidebar implementation.
 *
 * Kept in its own file so the previous LeftSidebar implementation is not
 * modified by this redesign.
 */

#include "panels/apple_sidebar.hpp"
#include "main_window.hpp"
#include "panels/middle_library.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>

namespace lollypop {
namespace {

constexpr const char* kCssResource = "/com/mahesh/lollypop/apple_ui.css";
constexpr const char* kNavIdKey = "apple-nav-id";

void ensure_stylesheet(GtkWidget* widget) {
    static bool installed = false;
    if (installed) return;

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, kCssResource);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    installed = true;
}

std::string sidebar_page_for(const std::string& page_id) {
    if (page_id == "album_detail" || page_id == "album") return "albums";
    if (page_id == "artist_detail" || page_id == "artist") return "artists";
    if (page_id == "genre_detail" || page_id == "genre") return "genres";
    if (page_id == "playlist_detail" || page_id == "playlist") return "playlists";
    if (page_id == "favourites" || page_id == "favorite_albums") return "favorites";
    if (page_id == "liked_music") return "liked";
    return page_id;
}

GtkWidget* create_icon(const char* icon_name) {
    GtkWidget* image = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(image, "apple-nav-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(image), 22);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    return image;
}

} // namespace

AppleSidebar::AppleSidebar(MainWindow* main_window)
    : main_window_(main_window) {
    setup_ui();
    create_rows();
    set_active_item_visual("home");
}

AppleSidebar::~AppleSidebar() = default;

void AppleSidebar::setup_ui() {
    root_ = gtk_grid_new();
    gtk_widget_add_css_class(root_, "apple-sidebar");
    gtk_widget_set_hexpand(root_, FALSE);
    gtk_widget_set_vexpand(root_, TRUE);
    gtk_widget_set_size_request(root_, kPanelWidth, -1);
    gtk_widget_set_visible(root_, TRUE);

    grid_ = gtk_grid_new();
    gtk_widget_add_css_class(grid_, "apple-sidebar-grid");
    gtk_grid_set_row_homogeneous(GTK_GRID(grid_), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid_), FALSE);
    gtk_grid_set_row_spacing(GTK_GRID(grid_), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid_), 0);
    gtk_widget_set_hexpand(grid_, TRUE);
    gtk_widget_set_vexpand(grid_, FALSE);
    gtk_widget_set_valign(grid_, GTK_ALIGN_START);
    gtk_widget_set_visible(grid_, TRUE);
    gtk_grid_attach(GTK_GRID(root_), grid_, 0, 0, 1, 1);

    ensure_stylesheet(root_);
}

void AppleSidebar::create_rows() {
    // 1x1 grid: back symbol only.
    GtkWidget* back_grid = gtk_grid_new();
    gtk_widget_add_css_class(back_grid, "apple-back-grid");
    gtk_grid_set_row_homogeneous(GTK_GRID(back_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(back_grid), TRUE);
    gtk_widget_set_hexpand(back_grid, TRUE);
    gtk_widget_set_vexpand(back_grid, FALSE);
    gtk_widget_set_visible(back_grid, TRUE);

    back_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(back_button_), FALSE);
    gtk_widget_add_css_class(back_button_, "apple-back-button");
    gtk_widget_set_halign(back_button_, GTK_ALIGN_START);
    gtk_widget_set_valign(back_button_, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(back_button_, TRUE);
    gtk_widget_set_vexpand(back_button_, FALSE);
    gtk_widget_set_tooltip_text(back_button_, "Back to Home");
    gtk_accessible_update_property(GTK_ACCESSIBLE(back_button_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Back to Home", -1);
    g_signal_connect(back_button_, "clicked",
                     G_CALLBACK(on_back_clicked), this);

    GtkWidget* back_icon_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(back_icon_slot, "apple-icon-slot");
    gtk_widget_set_margin_start(back_icon_slot, 5);
    gtk_widget_set_margin_end(back_icon_slot, 5);
    gtk_widget_set_halign(back_icon_slot, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(back_icon_slot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(back_icon_slot), create_icon("go-previous-symbolic"));
    gtk_button_set_child(GTK_BUTTON(back_button_), back_icon_slot);
    gtk_widget_set_visible(back_button_, TRUE);
    gtk_grid_attach(GTK_GRID(back_grid), back_button_, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_), back_grid, 0, 0, 2, 1);

    items_ = {
        {"home",      "Home",             "go-home-symbolic"},
        {"albums",    "Albums",           "media-optical-symbolic"},
        {"artists",   "Artists",          "system-users-symbolic"},
        {"playlists", "Playlists",        "view-list-symbolic"},
        {"genres",    "Genres",           "audio-x-generic-symbolic"},
        {"songs",     "Songs",            "media-playlist-repeat-symbolic"},
        {"search",    "Search",           "system-search-symbolic"},
        {"favorites", "Favourite Albums", "non-starred-symbolic"},
        {"liked",     "Liked Music",      "star-new-symbolic"},
    };

    int row = 1;
    for (const NavItem& source : items_) {
        NavItem item = source;

        item.button = gtk_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(item.button), FALSE);
        gtk_widget_add_css_class(item.button, "apple-nav-row");
        gtk_widget_set_hexpand(item.button, TRUE);
        gtk_widget_set_vexpand(item.button, FALSE);
        gtk_widget_set_halign(item.button, GTK_ALIGN_FILL);
        gtk_widget_set_valign(item.button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(item.button, item.label.c_str());
        gtk_accessible_update_property(GTK_ACCESSIBLE(item.button),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       item.label.c_str(), -1);
        g_object_set_data_full(G_OBJECT(item.button), kNavIdKey,
                               g_strdup(item.id.c_str()), g_free);
        g_signal_connect(item.button, "clicked",
                         G_CALLBACK(on_nav_clicked), this);

        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(content, TRUE);
        gtk_widget_set_vexpand(content, TRUE);
        gtk_widget_set_halign(content, GTK_ALIGN_FILL);
        gtk_widget_set_valign(content, GTK_ALIGN_CENTER);

        GtkWidget* icon_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(icon_slot, "apple-icon-slot");
        gtk_widget_set_margin_start(icon_slot, 5);
        gtk_widget_set_margin_end(icon_slot, 5);
        gtk_widget_set_halign(icon_slot, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(icon_slot, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(icon_slot), create_icon(item.icon_name.c_str()));

        GtkWidget* label = gtk_label_new(item.label.c_str());
        gtk_widget_add_css_class(label, "apple-nav-label");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_FILL);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(label, 6);

        gtk_box_append(GTK_BOX(content), icon_slot);
        gtk_box_append(GTK_BOX(content), label);
        gtk_button_set_child(GTK_BUTTON(item.button), content);
        gtk_widget_set_visible(item.button, TRUE);

        gtk_grid_attach(GTK_GRID(grid_), item.button, 0, row, 2, 1);
        items_[static_cast<size_t>(row - 1)] = item;
        ++row;
    }
}

void AppleSidebar::set_active_item(const std::string& item_id) {
    active_id_ = sidebar_page_for(item_id);
    update_active_state();

    if (main_window_) {
        if (MiddleLibrary* middle = main_window_->get_middle_library()) {
            middle->set_active_page(active_id_);
        }
    }
}

void AppleSidebar::set_active_item_visual(const std::string& item_id) {
    active_id_ = sidebar_page_for(item_id);
    update_active_state();
}

void AppleSidebar::update_active_state() {
    for (NavItem& item : items_) {
        if (item.id == active_id_) {
            gtk_widget_add_css_class(item.button, "active");
        } else {
            gtk_widget_remove_css_class(item.button, "active");
        }
    }
}

void AppleSidebar::on_back_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<AppleSidebar*>(user_data);
    self->set_active_item("home");
}

void AppleSidebar::on_nav_clicked(GtkButton* button, gpointer user_data) {
    auto* self = static_cast<AppleSidebar*>(user_data);
    const char* id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), kNavIdKey));
    if (id) self->set_active_item(id);
}

} // namespace lollypop
