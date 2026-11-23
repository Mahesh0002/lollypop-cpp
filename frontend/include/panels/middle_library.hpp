/**
 * Lollypop C++ - Middle Library Panel
 * Apple Music-inspired music library browsing experience.
 *
 * Uses GtkOverlay with single scroller and floating glass toolbar.
 * Home page is a real page with library data.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/components/LibraryPage.hpp"

namespace lollypop {

class MainWindow;
class ILibraryService;

class MiddleLibrary {
public:
    MiddleLibrary(MainWindow* main_window, ILibraryService* library_service);
    ~MiddleLibrary();

    GtkWidget* get_widget() const { return overlay_; }

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
    GtkWidget* overlay_;           // Root overlay container
    GtkWidget* scrolled_window_;   // Single vertical scroller
    GtkWidget* content_box_;       // Content box inside scroller
    GtkWidget* stack_;             // Page stack
    GtkWidget* glass_toolbar_;     // Floating glass toolbar (overlay)
    GtkWidget* search_entry_;
    GtkWidget* menu_button_;
    GtkWidget* back_button_;
    GtkWidget* forward_button_;
    GtkWidget* title_label_;

    std::unordered_map<std::string, std::unique_ptr<LibraryPage>> pages_;
    std::string active_page_id_;

    // Search
    guint search_timeout_id_ = 0;
    uint64_t search_generation_ = 0;
    GWeakRef self_weak_ref_; // For timeout callback lifetime safety

    // Navigation history
    std::vector<std::string> history_;
    size_t history_index_ = 0;

    void setup_ui();
    void create_glass_toolbar();
    void create_content_area();
    void create_placeholder_pages();
    void update_toolbar_state();
    void navigate_to_page(const std::string& page_id, bool add_to_history);

    static void on_search_changed(GtkEditable* editable, gpointer user_data);
    static void on_back_clicked(GtkButton*, gpointer user_data);
    static void on_forward_clicked(GtkButton*, gpointer user_data);
    void perform_search(const std::string& query, uint64_t generation);
};

} // namespace lollypop
