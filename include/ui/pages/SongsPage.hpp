/**
 * Lollypop C++ - Songs Page
 *
 * Sortable multi-column song table using GtkListBox with TrackRow components.
 * Sorting is database-backed via ILibraryService (plan Section 5.7).
 * Lazy loading with batched retrieval for large libraries.
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

class SongsPage : public LibraryPage {
public:
    SongsPage(MainWindow* main_window, ILibraryService* library_service);
    ~SongsPage();

    SongsPage(const SongsPage&) = delete;
    SongsPage& operator=(const SongsPage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "songs"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrolled_window_;
    std::unique_ptr<GtkWidget, WidgetDeleter> header_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrolled_list_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;
    std::vector<std::string> sorted_ids_;
    std::string sort_column_ = "title";
    bool sort_ascending_ = true;
    uint64_t generation_ = 0;
    bool loading_ = false;
    size_t loaded_count_ = 0;
    size_t total_count_ = 0;
    static constexpr size_t PAGE_SIZE = 100;
    static constexpr size_t PREFETCH_THRESHOLD = 50;

    std::mutex load_mutex_;
    std::mutex ids_mutex_;

    void setup_ui();
    void create_header(GtkWidget* main_box);
    void reload();
    void load_page(size_t start);
    void append_tracks(std::vector<Track>&& tracks);

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);

    static void on_sort_header_clicked(GtkButton* button, gpointer user_data);
    static void on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data);
};

} // namespace lollypop