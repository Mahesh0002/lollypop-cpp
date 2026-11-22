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
    // Animation data structs for staggered spring animations
    struct TrackAnimData {
        GtkWidget* widget;
    };
    
    struct AlbumAnimData {
        GtkWidget* widget;
    };

    HomePage(MainWindow* main_window, ILibraryService* library_service);
    ~HomePage();

    HomePage(const HomePage&) = delete;
    HomePage& operator=(const HomePage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "home"; }

    // Navigation callbacks for "View all" buttons
    void set_recently_added_callback(std::function<void()> cb);
    void set_recently_played_callback(std::function<void()> cb);
    void set_favourite_albums_callback(std::function<void()> cb);

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
        GtkWidget* view_all_button = nullptr;
        std::function<void()> view_all_cb;
        std::vector<std::unique_ptr<TrackRow>> track_rows;
        std::vector<guint> anim_timeout_ids;
    };

    struct AlbumSectionData {
        std::string title;
        std::function<std::vector<Album>()> fetcher;
        std::function<void(const Album&)> play_cb;
        std::function<void(const Album&)> click_cb;
        GtkWidget* grid = nullptr;
        GtkWidget* view_all_button = nullptr;
        std::function<void()> view_all_cb;
        std::vector<std::unique_ptr<AlbumCard>> album_cards;
        std::vector<guint> anim_timeout_ids;
    };

    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
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
    void create_welcome_section();

    GtkWidget* create_section(const std::string& title,
                              std::function<std::vector<Track>()> fetcher,
                              std::function<void(const Track&)> play_cb,
                              std::function<void(const Track&, bool)> like_cb,
                              std::function<void()> view_all_cb = nullptr);

    GtkWidget* create_album_section(const std::string& title,
                                    std::function<std::vector<Album>()> fetcher,
                                    std::function<void(const Album&)> play_cb,
                                    std::function<void(const Album&)> click_cb,
                                    std::function<void()> view_all_cb = nullptr);

    void refresh_track_section(SectionData* data);
    void refresh_album_section(AlbumSectionData* data);

    // Static callbacks for staggered animations
    static gboolean on_track_anim_timeout(gpointer data);
    static gboolean on_album_anim_timeout(gpointer data);

    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_album_played(const Album& album);
    void on_album_clicked(const Album& album);
};

} // namespace lollypop