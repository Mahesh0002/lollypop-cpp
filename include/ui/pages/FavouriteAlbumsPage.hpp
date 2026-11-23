/**
 * Lollypop C++ - Favourite Albums Page
 *
 * Grid of AlbumCard components showing only favourite albums.
 * Uses the same AlbumCard component as the Albums page
 * (plan Section 5.8).
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/AlbumCard.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class FavouriteAlbumsPage : public LibraryPage {
public:
    FavouriteAlbumsPage(MainWindow* main_window, ILibraryService* library_service);
    ~FavouriteAlbumsPage();

    FavouriteAlbumsPage(const FavouriteAlbumsPage&) = delete;
    FavouriteAlbumsPage& operator=(const FavouriteAlbumsPage&) = delete;

    GtkWidget* get_widget() const { return widget_; }
    void refresh() override;
    std::string get_page_id() const override { return "favorites"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    GtkWidget* widget_ = nullptr;  // FlowBox (content), owned by grid_
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<AlbumCard>> album_cards_;

    void setup_ui();
    void populate(const std::vector<Album>& albums);
    void on_album_played(const Album& album);
    void on_album_clicked(const Album& album);
};

} // namespace lollypop
