/**
 * Lollypop C++ - Left Sidebar Panel
 *
 * Compact mode: 50px wide, icon-only navigation.
 * Expanded mode: 156px wide, icon + label navigation.
 *
 * All rows are 50x52px. The icon column is anchored at the left edge and
 * never moves between modes; only the label revealer changes.
 */

#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace lollypop {

class MainWindow;

struct NavigationItem {
    std::string id;
    std::string label;
    std::string icon_name;
    bool is_active = false;
};

class LeftSidebar {
public:
    static constexpr int kIconSize = 22;
    static constexpr int kButtonWidth = 50;
    static constexpr int kButtonHeight = 52;
    static constexpr int kCompactWidth = kButtonWidth;
    static constexpr int kExpandedWidth = 3 * kButtonHeight; // 156px

    explicit LeftSidebar(MainWindow* main_window);
    ~LeftSidebar();

    GtkWidget* get_widget() const { return sidebar_box_; }
    GtkWidget* get_collapse_button() const { return collapse_button_; }

    // User-initiated navigation (triggers MiddleLibrary)
    void set_active_item(const std::string& item_id);
    // External sync (e.g., from MiddleLibrary back/forward) - updates visual only
    void set_active_item_visual(const std::string& item_id);
    void toggle_expanded();
    bool is_expanded() const { return expanded_; }

    void load_sidebar_state();
    void save_sidebar_state();

public:
    struct ActionButton {
        GtkWidget* button = nullptr;
        GtkWidget* icon = nullptr;
    };

    struct NavRow {
        std::string id;
        GtkWidget* button = nullptr;
        GtkWidget* content = nullptr;
        GtkWidget* revealer = nullptr;
    };

private:
    MainWindow* main_window_ = nullptr;
    GtkWidget* sidebar_box_ = nullptr;
    GtkWidget* header_box_ = nullptr;
    GtkWidget* collapse_button_ = nullptr;
    ActionButton collapse_;
    ActionButton about_;
    ActionButton fullscreen_;
    GtkWidget* nav_list_box_ = nullptr;

    bool expanded_ = true;
    std::string active_item_id_;
    std::vector<NavigationItem> nav_items_;
    std::vector<NavRow> nav_rows_;

    void setup_ui();
    void create_navigation_items();
    void add_nav_row(const NavigationItem& item);
    void apply_expanded_state();
    void update_sidebar_width();
    void update_toggle_icon();
    void update_label_visibility();
    void update_active_state();
    void set_row_width(NavRow& row, int width);

    static void on_collapse_clicked(GtkButton* button, gpointer user_data);
    static void on_about_clicked(GtkButton* button, gpointer user_data);
    static void on_fullscreen_clicked(GtkButton* button, gpointer user_data);
    static void on_nav_item_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
