/**
 * Lollypop C++ - Left Sidebar Panel
 * Compact/expandable navigation sidebar with icons and labels.
 *
 * Per plan Section 4:
 *   - Collapse button (chevron) is SEPARATE from menu button (hamburger).
 *   - Collapse button toggles sidebar width (220px <-> 72px).
 *   - Menu button opens a popover with Settings and Fullscreen.
 *   - GtkRevealer used for label reveal/hide (Section 4.6).
 *   - Sidebar state and last page persisted (Section 4.7).
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
    GtkWidget* menu_button_;          // hamburger - opens popover
    GtkWidget* settings_button_;      // inside popover
    GtkWidget* fullscreen_button_;    // inside popover
    GtkWidget* nav_list_box_;
    GtkWidget* menu_popover_;

    bool expanded_ = false;
    std::vector<NavigationItem> nav_items_;
    std::string active_item_id_;

    void setup_ui();
    void create_navigation_items();
    void create_nav_button(const NavigationItem& item);
    void update_active_state();
    void update_label_visibility();
    void update_sidebar_width();
    void animate_labels();

    // Callbacks
    static void on_collapse_clicked(GtkButton* button, gpointer user_data);
    static void on_menu_clicked(GtkButton* button, gpointer user_data);
    static void on_nav_item_clicked(GtkButton* button, gpointer user_data);
    static void on_settings_clicked(GtkButton* button, gpointer user_data);
    static void on_fullscreen_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
