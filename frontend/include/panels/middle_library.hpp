/**
 * Lollypop C++ - Middle Library Panel
 * Apple Music-inspired music library browsing experience
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace lollypop {

class MainWindow;

class LibraryPage {
public:
    virtual ~LibraryPage() = default;
    virtual GtkWidget* get_widget() = 0;
    virtual void refresh() = 0;
    virtual std::string get_page_id() const = 0;
};

class MiddleLibrary {
public:
    explicit MiddleLibrary(MainWindow* main_window);
    ~MiddleLibrary();

    GtkWidget* get_widget() const { return main_box_; }

    void set_active_page(const std::string& page_id);
    void register_page(std::unique_ptr<LibraryPage> page);

private:
    MainWindow* main_window_;
    GtkWidget* main_box_;
    GtkWidget* header_bar_;
    GtkWidget* search_entry_;
    GtkWidget* stack_;
    GtkWidget* scrolled_window_;

    std::unordered_map<std::string, std::unique_ptr<LibraryPage>> pages_;
    std::string active_page_id_;

    void setup_ui();
    void create_header_bar();
    void create_placeholder_pages();
};

} // namespace lollypop