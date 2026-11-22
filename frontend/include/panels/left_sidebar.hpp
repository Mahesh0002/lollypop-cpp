/**
 * Lollypop C++ - Left Sidebar Panel
 * Compact/expandable navigation sidebar with icons and labels.
 *
 * Compact mode: 42px wide (icon only, 22px icon + 10px left/right padding)
 * Expanded mode: 220px wide (icon + label)
 * Toggle: '>' (pan-end-symbolic) to expand, '<' (pan-start-symbolic) to collapse
 * No branding, no greetings - pure navigation.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <vector>
#include <string>

namespace lollypop {

class MainWindow;

struct NavigationItem {
    std::string id;
    std::string label;
    std::string icon_name;  // Icon name for GTK icon theme
    bool is_active = false;
};

class LeftSidebar {
public:
    // Constants
    static constexpr int kIconSize = 22;
    static constexpr int kCompactWidth = 42;   // 22px icon + 10px left + 10px right
    static constexpr int kExpandedWidth = 220;

    explicit LeftSidebar(MainWindow* main_window);
    ~LeftSidebar();

    GtkWidget* get_widget() const { return sidebar_box_; }

    void set_active_item(const std::string& item_id);
    void toggle_expanded();
    bool is_expanded() const { return expanded_; }

    // Persistence
    void load_sidebar_state();
    void save_sidebar_state();

private:
    MainWindow* main_window_;
    GtkWidget* sidebar_box_;
    GtkWidget* header_box_;
    GtkWidget* collapse_button_;      // chevron - toggles width
    GtkWidget* collapse_icon_;        // icon inside collapse_button_ (so we can swap it)
    GtkWidget* menu_button_;          // hamburger - opens popover (hidden in compact)
    GtkWidget* settings_button_;      // inside popover
    GtkWidget* fullscreen_button_;    // inside popover
    GtkWidget* nav_list_box_;
    GtkWidget* menu_popover_;

    bool expanded_ = true;  // Default to expanded
    std::vector<NavigationItem> nav_items_;
    std::string active_item_id_;

    void setup_ui();
    void create_navigation_items();
    void create_nav_button(const NavigationItem& item);
    void update_active_state();
    void update_sidebar_width();
    void update_toggle_icon();
    void apply_expanded_state();
    void animate_labels();

    // Callbacks
    static void on_collapse_clicked(GtkButton* button, gpointer user_data);
    static void on_menu_clicked(GtkButton* button, gpointer user_data);
    static void on_nav_item_clicked(GtkButton* button, gpointer user_data);
    static void on_settings_clicked(GtkButton* button, gpointer user_data);
    static void on_fullscreen_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
