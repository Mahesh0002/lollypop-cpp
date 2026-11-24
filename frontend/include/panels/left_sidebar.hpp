/**
 * Lollypop C++ - Left Sidebar Panel
 *
 * Geometry is derived at layout time from the panel's allocated height:
 *   - every row is 4.5% of that height,
 *   - each icon sits in a square tile with a 5px gap on all four sides,
 *   - compact width = tile width + the two 5px side gaps,
 *   - expanded width = 3 * compact width.
 *
 * Label text is scaled from the row height with a 5px boost. Only semibold
 * and normal font weights are used. Labels are hidden outright in compact
 * mode instead of being left inside a revealer, where they still contribute
 * a minimum width and fight the fixed compact width.
 *
 * Active and hover states change text/icon color only; there is no tile
 * background and no accent color in the sidebar.
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
    static constexpr double kRowHeightFraction = 0.045; // 4.5% of panel height
    static constexpr int kIconGap = 5;                  // all four sides
    // Used only before the first allocation. It matches the row height derived
    // from the window's default size (4.5% of ~760px, clamped to the minimum),
    // so children and the fixed width request start from the same geometry.
    static constexpr int kFallbackRowHeight = 40;
    static constexpr int kMinRowHeight = 40;
    static constexpr int kMinIconSize = 16;
    static constexpr int kMinLabelFont = 11;
    static constexpr int kLabelFontBoost = 5;

    explicit LeftSidebar(MainWindow* main_window);
    ~LeftSidebar();

    GtkWidget* get_widget() const { return sidebar_box_; }
    GtkWidget* get_collapse_button() const { return collapse_button_; }

    // User-initiated navigation (triggers MiddleLibrary).
    void set_active_item(const std::string& item_id);
    // External sync (e.g. from MiddleLibrary back/forward) - visual only.
    void set_active_item_visual(const std::string& item_id);
    void toggle_expanded();
    bool is_expanded() const { return expanded_; }

    void load_sidebar_state();
    void save_sidebar_state();

    struct ActionButton {
        GtkWidget* button = nullptr;
        GtkWidget* icon = nullptr;
        GtkWidget* icon_slot = nullptr;
    };

    struct NavRow {
        std::string id;
        GtkWidget* button = nullptr;
        GtkWidget* content = nullptr;
        GtkWidget* label = nullptr;
    };

private:
    MainWindow* main_window_ = nullptr;
    GtkWidget* sidebar_box_ = nullptr;
    GtkWidget* header_box_ = nullptr;
    GtkWidget* collapse_button_ = nullptr;
    ActionButton collapse_;
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

    static void on_collapse_clicked(GtkWidget* widget, gpointer user_data);
    static void on_nav_item_clicked(GtkWidget* widget, gpointer user_data);
};

} // namespace lollypop
