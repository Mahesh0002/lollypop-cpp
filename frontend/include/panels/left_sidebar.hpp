/**
 * Lollypop C++ - Left Sidebar Panel
 * Compact/expandable navigation sidebar with icons and labels
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

private:
    MainWindow* main_window_;
    GtkWidget* sidebar_box_;
    GtkWidget* header_box_;
    GtkWidget* menu_button_;
    GtkWidget* settings_button_;
    GtkWidget* fullscreen_button_;
    GtkWidget* nav_list_box_;
    GtkWidget* settings_revealer_;

    bool expanded_ = false;
    std::vector<NavigationItem> nav_items_;
    std::string active_item_id_;

    void setup_ui();
    void create_navigation_items();
    void create_nav_button(const NavigationItem& item);
    void update_active_state();

    // Callbacks
    static void on_menu_clicked(GtkButton* button, gpointer user_data);
    static void on_nav_item_clicked(GtkButton* button, gpointer user_data);
    static void on_settings_clicked(GtkButton* button, gpointer user_data);
    static void on_fullscreen_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop