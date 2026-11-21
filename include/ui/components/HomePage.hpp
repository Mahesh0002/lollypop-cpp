/**
 * Lollypop C++ - Home Page
 *
 * Real Home page with library sections: Recently Played, Recently Added,
 * Favourite Albums, etc. Uses actual library data via ILibraryService.
 * Displays proper empty state when no data exists.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "ui/components/LibraryPage.hpp"
#include "services/ILibraryService.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/components/AlbumCard.hpp"

namespace lollypop {

class MainWindow;

class HomePage : public LibraryPage {
public:
    HomePage(MainWindow* main_window, ILibraryService* library_service);
    ~HomePage();

    HomePage(const HomePage&) = delete;
    HomePage& operator=(const HomePage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "home"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    struct SectionData {
        std::string title;
        std::function<std::vector<Track>()> fetcher;
        std::function<void(const Track&)> play_cb;
        std::function<void(const Track&, bool)> like_cb;
        GtkWidget* list_box = nullptr;
        std::vector<std::unique_ptr<TrackRow>> track_rows;
    };

    struct AlbumSectionData {
        std::string title;
        std::function<std::vector<Album>()> fetcher;
        std::function<void(const Album&)> play_cb;
        std::function<void(const Album&)> click_cb;
        GtkWidget* grid = nullptr;
        std::vector<std::unique_ptr<AlbumCard>> album_cards;
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> main_box_;

    MainWindow* main_window_ = nullptr;
    ILibraryService* library_service_ = nullptr;
    bool initialized_ = false;

    std::vector<std::unique_ptr<SectionData>> sections_data_;
    std::vector<std::unique_ptr<AlbumSectionData>> album_sections_data_;

    void setup_ui();
    void create_welcome_section();

    GtkWidget* create_section(const std::string& title,
                              std::function<std::vector<Track>()> fetcher,
                              std::function<void(const Track&)> play_cb,
                              std::function<void(const Track&, bool)> like_cb);

    GtkWidget* create_album_section(const std::string& title,
                                    std::function<std::vector<Album>()> fetcher,
                                    std::function<void(const Album&)> play_cb,
                                    std::function<void(const Album&)> click_cb);

    void refresh_track_section(SectionData* data);
    void refresh_album_section(AlbumSectionData* data);

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_album_played(const Album& album);
    void on_album_clicked(const Album& album);
};

} // namespace lollypop