/**
 * Apple-style source-list sidebar.
 *
 * This is a new implementation kept separate from LeftSidebar. There is no
 * compact mode. The panel fills the full height of the main grid row and is a
 * 1x1 back control above a 9x2 navigation grid: column 0 is an outline icon,
 * column 1 is the page label.
 */

#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace lollypop {

class MainWindow;

class AppleSidebar {
public:
    explicit AppleSidebar(MainWindow* main_window);
    ~AppleSidebar();

    GtkWidget* get_widget() const { return root_; }

    // User-initiated navigation (also drives MiddleLibrary).
    void set_active_item(const std::string& item_id);
    // External sync from MiddleLibrary back/forward history.
    void set_active_item_visual(const std::string& item_id);

private:
    struct NavItem {
        std::string id;
        std::string label;
        std::string icon_name;
        GtkWidget* button = nullptr;
    };

    MainWindow* main_window_ = nullptr;
    GtkWidget* root_ = nullptr;
    GtkWidget* grid_ = nullptr;
    GtkWidget* back_button_ = nullptr;
    std::vector<NavItem> items_;
    std::string active_id_;

    void setup_ui();
    void create_rows();
    void update_active_state();

    static void on_back_clicked(GtkButton* button, gpointer user_data);
    static void on_nav_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
