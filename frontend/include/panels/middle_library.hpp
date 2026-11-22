/**
 * Lollypop C++ - Middle Library Panel
 * Apple Music-inspired music library browsing experience.
 *
 * Uses GtkStack for page switching with safe navigation.
 * Home page is a real page with library data (Phase 4.6).
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "ui/components/LibraryPage.hpp"

namespace lollypop {

class MainWindow;
class ILibraryService;

class MiddleLibrary {
public:
    MiddleLibrary(MainWindow* main_window, ILibraryService* library_service);
    ~MiddleLibrary();

    GtkWidget* get_widget() const { return main_box_; }

    void set_active_page(const std::string& page_id);
    void register_page(std::unique_ptr<LibraryPage> page);
    // Rebuild the currently visible page from the library service. Called
    // after a background scan or file-watcher change.
    void refresh_active_page();

    // Detail page navigation
    void open_album_detail(const std::string& album_id);
    void open_artist_detail(const std::string& artist_id);
    void show_genre(const std::string& genre);

private:
    MainWindow* main_window_;
    ILibraryService* library_service_;
    GtkWidget* main_box_;
    GtkWidget* header_bar_;
    GtkWidget* search_entry_;
    GtkWidget* stack_;
    GtkWidget* scrolled_window_;

    std::unordered_map<std::string, std::unique_ptr<LibraryPage>> pages_;
    std::string active_page_id_;

    // Search
    guint search_timeout_id_ = 0;
    uint64_t search_generation_ = 0;

    void setup_ui();
    void create_header_bar();
    void create_placeholder_pages();

    static void on_search_changed(GtkEditable* editable, gpointer user_data);
    void perform_search(const std::string& query, uint64_t generation);
};

} // namespace lollypop
