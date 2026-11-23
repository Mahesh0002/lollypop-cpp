/**
 * Lollypop C++ - Search Results Page
 * Displays search results with lazy loading.
 *
 * The page owns no scrolled window of its own: MiddleLibrary provides one
 * shared scroller for the whole middle panel. Results are fetched on a worker
 * thread and applied on the main thread only when the generation still
 * matches, so fast typing can never flash stale results.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/TrackRow.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class SearchResultsPage : public LibraryPage {
public:
    SearchResultsPage(MainWindow* main_window, ILibraryService* library_service);
    ~SearchResultsPage();

    SearchResultsPage(const SearchResultsPage&) = delete;
    SearchResultsPage& operator=(const SearchResultsPage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "search"; }

    void search(const std::string& query, uint64_t generation);
    // Called by MiddleLibrary when the shared scroller moves.
    void handle_scroll(GtkAdjustment* adjustment);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> header_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> content_area_;
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> query_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> empty_state_;
    std::unique_ptr<GtkWidget, WidgetDeleter> empty_desc_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;
    std::string current_query_;
    uint64_t current_generation_ = 0;
    size_t loaded_count_ = 0;
    size_t total_count_ = 0;
    static constexpr size_t PAGE_SIZE = 50;
    std::vector<Track> remaining_tracks_;

    // Plain C++ object: GWeakRef must not be used on `this`. Main-thread
    // callbacks test the token before touching the page.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    guint idle_source_id_ = 0;

    void setup_ui();
    void set_content(GtkWidget* child);
    void apply_search_results(uint64_t generation, std::vector<Track>&& tracks);
    void append_tracks(std::vector<Track>&& tracks);
    void clear_rows();
    void show_empty_state(const std::string& message);
    void load_more();

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
};

} // namespace lollypop
