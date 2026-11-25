/**
 * Lollypop C++ - Home Page
 *
 * Real Home page with library sections: Recently Played, Recently Added,
 * Favourite Albums, etc. Uses actual library data via ILibraryService.
 * Displays proper empty state when no data exists.
 *
 * No nested scroller - content is meant to be placed inside the
 * middle panel's single scroller.
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
#include "ui/components/AppleAlbumTile.hpp"

namespace lollypop {

class MainWindow;

class HomePage : public LibraryPage {
public:
    HomePage(MainWindow* main_window, ILibraryService* library_service);
    ~HomePage();

    HomePage(const HomePage&) = delete;
    HomePage& operator=(const HomePage&) = delete;

    GtkWidget* get_widget() const { return main_box_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "home"; }

    // Navigation callbacks retained for future section navigation.
    void set_recently_added_callback(std::function<void()> cb);
    void set_recently_played_callback(std::function<void()> cb);
    void set_favourite_albums_callback(std::function<void()> cb);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };

    struct SectionData {
        std::string title;
        std::function<std::vector<Track>()> fetcher;
        std::function<void(const Track&)> play_cb;
        std::function<void(const Track&, bool)> like_cb;
        GtkWidget* list_box = nullptr;
        std::function<void()> view_all_cb;
        std::vector<std::unique_ptr<TrackRow>> track_rows;
    };

    struct AlbumSectionData {
        std::string title;
        std::function<std::vector<Album>()> fetcher;
        std::function<void(const Album&)> play_cb;
        std::function<void(const Album&)> click_cb;
        GtkWidget* grid = nullptr;
        std::function<void()> view_all_cb;
        std::vector<std::unique_ptr<AppleAlbumTile>> album_tiles;
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> main_box_;

    MainWindow* main_window_ = nullptr;
    ILibraryService* library_service_ = nullptr;
    bool initialized_ = false;

    std::function<void()> recently_added_cb_;
    std::function<void()> recently_played_cb_;
    std::function<void()> favourite_albums_cb_;

    std::vector<std::unique_ptr<SectionData>> sections_data_;
    std::vector<std::unique_ptr<AlbumSectionData>> album_sections_data_;

    void setup_ui();

    GtkWidget* create_section(const std::string& title,
                              std::function<std::vector<Track>()> fetcher,
                              std::function<void(const Track&)> play_cb,
                              std::function<void(const Track&, bool)> like_cb,
                              std::function<void()> view_all_cb = nullptr);

    GtkWidget* create_album_section(const std::string& title,
                                    std::function<std::vector<Album>()> fetcher,
                                    std::function<void(const Album&)> play_cb,
                                    std::function<void(const Album&)> click_cb,
                                    std::function<void()> view_all_cb = nullptr,
                                    int max_per_row = 4);

    void refresh_track_section(SectionData* data);
    void refresh_album_section(AlbumSectionData* data);

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_album_played(const Album& album);
    void on_album_clicked(const Album& album);
};

} // namespace lollypop