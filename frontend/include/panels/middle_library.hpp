/**
 * Lollypop C++ - Middle Library Panel
 * Apple Music-inspired music library browsing experience.
 *
 * Uses GtkOverlay with single scroller and floating glass toolbar.
 * Home page is a real page with library data.
 */

#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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

    /// Recompute the artwork-derived palette for the active page. Called when
    /// the playing track changes so a detail page keeps its own hero tint.
    void update_palette_for_active_page();

    // Detail page navigation
    void open_album_detail(const std::string& album_id);
    void open_artist_detail(const std::string& artist_id);
    void show_genre(const std::string& genre);

    // Callback for page changes (used to sync sidebar)
    using PageChangeCallback = std::function<void(const std::string&)>;
    void set_page_change_callback(PageChangeCallback cb) { page_change_cb_ = std::move(cb); }

    // Connect SongsPage scroll handling to the shared scrolled window
    void connect_scroll_handling();

private:
    MainWindow* main_window_;
    ILibraryService* library_service_;
    GtkWidget* overlay_;           // Root overlay container
    GtkWidget* scrolled_window_;   // Single vertical scroller
    GtkWidget* content_box_;       // Content box inside scroller
    GtkWidget* stack_;             // Page stack
    GtkWidget* sticky_header_;     // Pinned column header for SongsPage

    std::unordered_map<std::string, std::unique_ptr<LibraryPage>> pages_;
    std::string active_page_id_;

    // Search history replay still uses a generation token.
    uint64_t search_generation_ = 0;
    // Lifetime token for timeout callbacks. Plain C++ objects are not
    // GObjects, so GWeakRef must never be used on `this`.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    // Navigation history. Each entry carries the page parameters and scroll
    // offset so Back/Forward restores real state, not just a page id.
    struct HistoryEntry {
        std::string page_id;
        std::string param;
        double scroll_offset = 0.0;
    };
    std::vector<HistoryEntry> history_;
    size_t history_index_ = 0;

    PageChangeCallback page_change_cb_;

    void setup_ui();
    void create_content_area();
    void create_placeholder_pages();
    void ensure_page(const std::string& page_id);
    void ensure_search_page();
    void update_sticky_header();
    void navigate_to_page(const std::string& page_id, bool add_to_history,
                          const std::string& param = std::string());
    void apply_history_entry(const HistoryEntry& entry, bool restore_scroll);
    void schedule_scroll(double offset);
    double current_scroll() const;

    static gboolean on_scroll_restore_idle(gpointer data);
    static void on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data);
    void sync_sticky_header_margin();
    static gboolean apply_sticky_header_margin_idle(gpointer data);
    static void on_sticky_header_height_changed(GObject* object,
                                                GParamSpec* pspec,
                                                gpointer user_data);
};

} // namespace lollypop
