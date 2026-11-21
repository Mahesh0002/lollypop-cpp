/**
 * Lollypop C++ - Search Results Page
 * Displays search results with lazy loading.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

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

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrolled_window_;
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> header_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> empty_state_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;
    std::string current_query_;
    uint64_t current_generation_ = 0;
    bool loading_ = false;
    size_t loaded_count_ = 0;
    size_t total_count_ = 0;
    size_t PAGE_SIZE = 50;
    std::vector<Track> remaining_tracks_;

    std::mutex load_mutex_;

    void setup_ui();
    void create_header(GtkWidget* main_box);
    void do_search(const std::string& query, uint64_t generation);
    void append_tracks(std::vector<Track>&& tracks);
    void show_empty_state(const std::string& message);
    void clear_results();
    void load_more();

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);

    static void on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data);
};

} // namespace lollypop