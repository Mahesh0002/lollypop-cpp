/**
 * Lollypop C++ - Left Sidebar Implementation
 *
 * The sidebar uses custom layout managers so all geometry comes from one
 * source: the allocated panel height. Rows are 4.5% of that height, icons are
 * square tiles with 5px gaps on every side, compact width is the tile plus its
 * two side gaps, and expanded width is exactly three times compact width.
 * Labels are shown/hidden directly so a hidden label never contributes to the
 * fixed compact width. Active rows use color and weight only, with no tile.
 */

#include "panels/left_sidebar.hpp"
#include "main_window.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace lollypop {
namespace {

constexpr const char* kRowHeightKey = "sidebar-row-height";
constexpr const char* kExpandedKey = "sidebar-expanded";
constexpr const char* kNavListKey = "sidebar-nav-list";
constexpr const char* kIconSlotKey = "sidebar-icon-slot";
constexpr const char* kIconKey = "sidebar-icon";
constexpr const char* kLabelKey = "sidebar-label";
constexpr const char* kScaledRowKey = "sidebar-scaled-row";

int row_height_for(int panel_height) {
    if (panel_height <= 0) return LeftSidebar::kFallbackRowHeight;
    const double scaled = panel_height * LeftSidebar::kRowHeightFraction;
    return std::max(LeftSidebar::kMinRowHeight,
                    static_cast<int>(scaled + 0.5));
}

// Square tile side. The 5px gaps live inside the row and the compact width,
// so the tile is the row height minus those two gaps.
int icon_size_for(int row_height) {
    return std::max(LeftSidebar::kMinIconSize,
                    row_height - 2 * LeftSidebar::kIconGap);
}

int compact_width_for(int row_height) {
    return icon_size_for(row_height) + 2 * LeftSidebar::kIconGap;
}

int expanded_width_for(int row_height) {
    return 3 * compact_width_for(row_height);
}

int label_font_for(int row_height) {
    const int scaled = static_cast<int>(row_height * 0.20 + 0.5);
    return std::max(LeftSidebar::kMinLabelFont,
                    scaled + LeftSidebar::kLabelFontBoost);
}

int child_count(GtkWidget* widget) {
    int count = 0;
    for (GtkWidget* child = widget ? gtk_widget_get_first_child(widget)
                                   : nullptr;
         child; child = gtk_widget_get_next_sibling(child)) {
        ++count;
    }
    return count;
}

using ActivateCallback = void (*)(GtkWidget*, gpointer);

struct ActivateContext {
    GtkWidget* widget;
    ActivateCallback callback;
    gpointer user_data;
};

void on_activate_released(GtkGestureClick*, gint, gdouble, gdouble,
                          gpointer data) {
    auto* context = static_cast<ActivateContext*>(data);
    if (context && context->callback) {
        context->callback(context->widget, context->user_data);
    }
}

gboolean on_activate_key_pressed(GtkEventControllerKey*, guint keyval,
                                 guint, GdkModifierType, gpointer data) {
    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter &&
        keyval != GDK_KEY_space) {
        return FALSE;
    }
    auto* context = static_cast<ActivateContext*>(data);
    if (context && context->callback) {
        context->callback(context->widget, context->user_data);
    }
    return TRUE;
}

// Use plain boxes for sidebar rows: GTK theme buttons add internal padding
// that cannot be removed reliably and breaks the strict row/tile geometry.
void make_activatable(GtkWidget* widget, ActivateCallback callback,
                      gpointer user_data) {
    auto* context = new ActivateContext{widget, callback, user_data};
    g_object_set_data_full(G_OBJECT(widget), "sidebar-activate-context",
                           context, [](gpointer data) {
                               delete static_cast<ActivateContext*>(data);
                           });
    gtk_widget_set_focusable(widget, TRUE);

    auto* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
                                  GDK_BUTTON_PRIMARY);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "released", G_CALLBACK(on_activate_released),
                     context);

    auto* key = gtk_event_controller_key_new();
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(key));
    g_signal_connect(key, "key-pressed",
                     G_CALLBACK(on_activate_key_pressed), context);
}

int sidebar_row_count(GtkWidget* widget) {
    int count = 0;
    for (GtkWidget* child = widget ? gtk_widget_get_first_child(widget)
                                   : nullptr;
         child; child = gtk_widget_get_next_sibling(child)) {
        if (g_object_get_data(G_OBJECT(child), kNavListKey) != nullptr) {
            count += std::max(1, child_count(child));
        } else {
            ++count;
        }
    }
    return std::max(1, count);
}

void set_geometry_data(GtkWidget* widget, int row_height) {
    if (!widget) return;
    g_object_set_data(G_OBJECT(widget), kRowHeightKey,
                      GINT_TO_POINTER(row_height));
}

int row_for_widget(GtkWidget* widget) {
    const int stored = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(widget), kRowHeightKey));
    if (stored > 0) return stored;
    return row_height_for(gtk_widget_get_height(widget));
}

bool is_expanded(GtkWidget* widget) {
    return g_object_get_data(G_OBJECT(widget), kExpandedKey) != nullptr;
}

int sidebar_width_for(GtkWidget* widget) {
    const int row = row_for_widget(widget);
    return is_expanded(widget) ? expanded_width_for(row)
                               : compact_width_for(row);
}

// Scale the square icon tile, its glyph and the label from one row height.
void scale_button_children(GtkWidget* button, int row_height) {
    if (!button) return;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), kScaledRowKey)) ==
        row_height) {
        return;
    }

    g_object_set_data(G_OBJECT(button), kScaledRowKey,
                      GINT_TO_POINTER(row_height));

    auto* icon_slot = static_cast<GtkWidget*>(
        g_object_get_data(G_OBJECT(button), kIconSlotKey));
    auto* icon = static_cast<GtkWidget*>(
        g_object_get_data(G_OBJECT(button), kIconKey));
    auto* label = static_cast<GtkWidget*>(
        g_object_get_data(G_OBJECT(button), kLabelKey));

    const int tile = icon_size_for(row_height);
    const int glyph = std::max(LeftSidebar::kMinIconSize, tile * 3 / 5);

    if (icon_slot) {
        gtk_widget_set_size_request(icon_slot, tile, tile);
        gtk_widget_set_margin_start(icon_slot, LeftSidebar::kIconGap);
        gtk_widget_set_margin_end(icon_slot, LeftSidebar::kIconGap);
        gtk_widget_set_margin_top(icon_slot, LeftSidebar::kIconGap);
        gtk_widget_set_margin_bottom(icon_slot, LeftSidebar::kIconGap);
    }
    if (icon) {
        gtk_image_set_pixel_size(GTK_IMAGE(icon), glyph);
    }
    if (label) {
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(
            attrs, pango_attr_size_new_absolute(label_font_for(row_height) *
                                                PANGO_SCALE));
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
        pango_attr_list_unref(attrs);
        gtk_widget_set_margin_start(label, LeftSidebar::kIconGap);
    }
}

GtkSizeRequestMode sidebar_request_mode(GtkWidget*) {
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

void set_baselines(int* minimum_baseline, int* natural_baseline) {
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

void sidebar_measure(GtkWidget* widget, GtkOrientation orientation, int,
                     int* minimum, int* natural, int* minimum_baseline,
                     int* natural_baseline) {
    const int row = row_for_widget(widget);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        const int width = sidebar_width_for(widget);
        if (minimum) *minimum = width;
        if (natural) *natural = width;
    } else {
        const int total = row * sidebar_row_count(widget);
        if (minimum) *minimum = total;
        if (natural) *natural = total;
    }
    set_baselines(minimum_baseline, natural_baseline);
}

void sidebar_allocate(GtkWidget* widget, int width, int height, int) {
    const int row = row_height_for(height);
    set_geometry_data(widget, row);
    gtk_widget_set_size_request(widget, sidebar_width_for(widget), -1);

    GtkWidget* header = nullptr;
    GtkWidget* nav = nullptr;
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        if (g_object_get_data(G_OBJECT(child), kNavListKey) != nullptr) {
            nav = child;
        } else if (!header) {
            header = child;
        }
    }
    set_geometry_data(nav, row);

    if (header) {
        graphene_point_t position = {0.0f, 0.0f};
        gtk_widget_allocate(header, width, row, -1,
                            gsk_transform_translate(nullptr, &position));
        for (GtkWidget* child = gtk_widget_get_first_child(header); child;
             child = gtk_widget_get_next_sibling(child)) {
            scale_button_children(child, row);
        }
    }

    if (nav) {
        const int nav_height = row * std::max(1, child_count(nav));
        graphene_point_t position = {0.0f, static_cast<float>(row)};
        gtk_widget_allocate(nav, width, nav_height, -1,
                            gsk_transform_translate(nullptr, &position));
    }
}

void nav_measure(GtkWidget* widget, GtkOrientation orientation, int,
                 int* minimum, int* natural, int* minimum_baseline,
                 int* natural_baseline) {
    const int row = row_for_widget(widget);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        const int width = sidebar_width_for(widget);
        if (minimum) *minimum = width;
        if (natural) *natural = width;
    } else {
        const int total = row * std::max(1, child_count(widget));
        if (minimum) *minimum = total;
        if (natural) *natural = total;
    }
    set_baselines(minimum_baseline, natural_baseline);
}

void nav_allocate(GtkWidget* widget, int width, int, int) {
    const int row = row_for_widget(widget);
    int y = 0;
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        graphene_point_t position = {0.0f, static_cast<float>(y)};
        gtk_widget_allocate(child, width, row, -1,
                            gsk_transform_translate(nullptr, &position));
        scale_button_children(child, row);
        y += row;
    }
}

std::filesystem::path state_path() {
    return std::filesystem::path(g_get_user_config_dir())
        / "lollypop-cpp" / "sidebar-expanded";
}

// Detail and transient pages have no sidebar row of their own. Map them to
// the section they belong to so the visual highlight never points at nothing.
std::string sidebar_page_for(const std::string& page_id) {
    if (page_id == "album_detail" || page_id == "album") return "albums";
    if (page_id == "artist_detail" || page_id == "artist") return "artists";
    if (page_id == "search") return "songs";
    if (page_id == "genre_detail" || page_id == "genre") return "genres";
    if (page_id == "playlist_detail" || page_id == "playlist") return "playlists";
    if (page_id == "favourites" || page_id == "favorite_albums") return "favorites";
    if (page_id == "liked_music") return "liked";
    return page_id;
}

LeftSidebar::ActionButton create_action_button(const char* icon_name,
                                               const char* accessible_label,
                                               GCallback callback,
                                               gpointer user_data,
                                               bool visible) {
    LeftSidebar::ActionButton action;
    action.button = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(action.button, "sidebar-action-button");
    gtk_widget_set_halign(action.button, GTK_ALIGN_FILL);
    gtk_widget_set_valign(action.button, GTK_ALIGN_CENTER);
    make_activatable(action.button,
                     reinterpret_cast<ActivateCallback>(callback), user_data);

    action.icon_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(action.icon_slot, "sidebar-icon-square");
    gtk_widget_set_halign(action.icon_slot, GTK_ALIGN_START);
    gtk_widget_set_valign(action.icon_slot, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(action.icon_slot, TRUE);
    g_object_set_data(G_OBJECT(action.button), kIconSlotKey, action.icon_slot);

    action.icon = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(action.icon, "sidebar-nav-icon");
    // Fill the square tile so the symbolic glyph is centred inside it.
    gtk_widget_set_hexpand(action.icon, TRUE);
    gtk_widget_set_vexpand(action.icon, TRUE);
    gtk_widget_set_halign(action.icon, GTK_ALIGN_FILL);
    gtk_widget_set_valign(action.icon, GTK_ALIGN_FILL);
    gtk_widget_set_visible(action.icon, TRUE);
    g_object_set_data(G_OBJECT(action.button), kIconKey, action.icon);

    gtk_box_append(GTK_BOX(action.icon_slot), action.icon);
    safe_box_append(GTK_BOX(action.button), action.icon_slot);

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
    // Visual-only: MainWindow owns the initial page navigation. Calling
    // set_active_item() here would feed a navigation event back into it.
    set_active_item_visual("home");
}

LeftSidebar::~LeftSidebar() = default;

void LeftSidebar::setup_ui() {
    sidebar_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_layout_manager(
        sidebar_box_,
        gtk_custom_layout_new(sidebar_request_mode, sidebar_measure,
                              sidebar_allocate));
    gtk_widget_add_css_class(sidebar_box_, "sidebar");
    gtk_widget_set_vexpand(sidebar_box_, TRUE);
    gtk_widget_set_visible(sidebar_box_, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(sidebar_box_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Library navigation", -1);

    // Only the collapse control lives in the header; its icon uses the same
    // square tile as every navigation row.
    header_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_box_, "sidebar-header");
    gtk_box_set_homogeneous(GTK_BOX(header_box_), FALSE);
    gtk_widget_set_hexpand(header_box_, TRUE);
    gtk_widget_set_halign(header_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(header_box_, TRUE);

    collapse_ = create_action_button("pan-start-symbolic", "Collapse sidebar",
                                     G_CALLBACK(on_collapse_clicked), this,
                                     TRUE);
    collapse_button_ = collapse_.button;
    safe_box_append(GTK_BOX(header_box_), collapse_.button);

    nav_list_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_layout_manager(
        nav_list_box_,
        gtk_custom_layout_new(sidebar_request_mode, nav_measure, nav_allocate));
    gtk_widget_add_css_class(nav_list_box_, "sidebar-nav");
    g_object_set_data(G_OBJECT(nav_list_box_), kNavListKey,
                      GINT_TO_POINTER(1));
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

    row.button = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row.button, "sidebar-nav-item");
    gtk_widget_set_hexpand(row.button, TRUE);
    gtk_widget_set_vexpand(row.button, FALSE);
    gtk_widget_set_halign(row.button, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row.button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row.button, TRUE);
    make_activatable(row.button, on_nav_item_clicked, this);
    gtk_accessible_update_property(GTK_ACCESSIBLE(row.button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   item.label.c_str(), -1);

    row.content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(row.content, TRUE);
    gtk_widget_set_halign(row.content, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row.content, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row.content, TRUE);

    GtkWidget* icon_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(icon_slot, "sidebar-icon-square");
    gtk_widget_set_halign(icon_slot, GTK_ALIGN_START);
    gtk_widget_set_valign(icon_slot, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon_slot, TRUE);
    g_object_set_data(G_OBJECT(row.button), kIconSlotKey, icon_slot);

    GtkWidget* icon = gtk_image_new_from_icon_name(item.icon_name.c_str());
    gtk_widget_add_css_class(icon, "sidebar-nav-icon");
    // Fill the square tile so the symbolic glyph is centred inside it.
    gtk_widget_set_hexpand(icon, TRUE);
    gtk_widget_set_vexpand(icon, TRUE);
    gtk_widget_set_halign(icon, GTK_ALIGN_FILL);
    gtk_widget_set_valign(icon, GTK_ALIGN_FILL);
    gtk_widget_set_visible(icon, TRUE);
    g_object_set_data(G_OBJECT(row.button), kIconKey, icon);

    GtkWidget* label = gtk_label_new(item.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    // Keep the label's minimum width tiny so it never inflates the fixed
    // compact/expanded sidebar width. It expands to the full row in practice.
    gtk_label_set_width_chars(GTK_LABEL(label), 1);
    gtk_widget_set_size_request(label, 0, -1);
    gtk_widget_add_css_class(label, "sidebar-nav-label");
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(label, TRUE);
    g_object_set_data(G_OBJECT(row.button), kLabelKey, label);
    row.label = label;

    gtk_box_append(GTK_BOX(icon_slot), icon);
    safe_box_append(GTK_BOX(row.content), icon_slot);
    safe_box_append(GTK_BOX(row.content), label);
    safe_box_append(GTK_BOX(row.button), row.content);
    safe_box_append(GTK_BOX(nav_list_box_), row.button);

    g_object_set_data_full(G_OBJECT(row.button), "nav-item-id",
                           g_strdup(item.id.c_str()), g_free);

    nav_rows_.push_back(row);
}

void LeftSidebar::set_active_item(const std::string& item_id) {
    const std::string nav_id = sidebar_page_for(item_id);
    active_item_id_ = nav_id;
    update_active_state();

    if (main_window_) {
        if (MiddleLibrary* middle = main_window_->get_middle_library()) {
            middle->set_active_page(nav_id);
        }
    }
}

void LeftSidebar::set_active_item_visual(const std::string& item_id) {
    active_item_id_ = sidebar_page_for(item_id);
    update_active_state();
}

void LeftSidebar::toggle_expanded() {
    expanded_ = !expanded_;
    apply_expanded_state();
    save_sidebar_state();
}

void LeftSidebar::apply_expanded_state() {
    g_object_set_data(G_OBJECT(sidebar_box_), kExpandedKey,
                      GINT_TO_POINTER(expanded_ ? 1 : 0));
    g_object_set_data(G_OBJECT(nav_list_box_), kExpandedKey,
                      GINT_TO_POINTER(expanded_ ? 1 : 0));
    if (expanded_) {
        gtk_widget_remove_css_class(sidebar_box_, "compact");
    } else {
        gtk_widget_add_css_class(sidebar_box_, "compact");
    }
    update_sidebar_width();
    update_toggle_icon();
    update_label_visibility();
}

void LeftSidebar::update_sidebar_width() {
    const int panel_height = gtk_widget_get_height(sidebar_box_);
    const int row = row_height_for(panel_height > 0
                                       ? panel_height
                                       : kFallbackRowHeight);
    set_geometry_data(sidebar_box_, row);
    set_geometry_data(nav_list_box_, row);
    gtk_widget_set_size_request(sidebar_box_, sidebar_width_for(sidebar_box_),
                                -1);
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
        gtk_widget_set_visible(row.label, expanded_);
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

void LeftSidebar::on_collapse_clicked(GtkWidget*, gpointer user_data) {
    static_cast<LeftSidebar*>(user_data)->toggle_expanded();
}

void LeftSidebar::on_nav_item_clicked(GtkWidget* button, gpointer user_data) {
    auto* self = static_cast<LeftSidebar*>(user_data);
    const char* id = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "nav-item-id"));
    if (id) self->set_active_item(id);
}

} // namespace lollypop
