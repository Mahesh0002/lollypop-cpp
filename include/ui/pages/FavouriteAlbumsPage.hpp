/**
 * Lollypop C++ - Favourite Albums Page
 *
 * Apple Music-style grid of favourite albums. Uses the same labeled AlbumCard
 * component and header treatment as the Albums page.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/AlbumCard.hpp"
#include "ui/components/PageStateHost.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class FavouriteAlbumsPage : public LibraryPage {
public:
    FavouriteAlbumsPage(MainWindow* main_window, ILibraryService* library_service);
    ~FavouriteAlbumsPage();

    FavouriteAlbumsPage(const FavouriteAlbumsPage&) = delete;
    FavouriteAlbumsPage& operator=(const FavouriteAlbumsPage&) = delete;

    GtkWidget* get_widget() const override { return host_.get_widget(); }
    void refresh() override;
    std::string get_page_id() const override { return "favorites"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    PageStateHost host_;
    std::unique_ptr<GtkWidget, WidgetDeleter> root_;  // page content, owned by host_
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;

    GtkWidget* subtitle_label_ = nullptr;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<AlbumCard>> album_cards_;

    void setup_ui();
    void populate(const std::vector<Album>& albums);
    void update_header(size_t album_count);
    void on_album_played(const Album& album);
    void on_album_clicked(const Album& album);
};

} // namespace lollypop
