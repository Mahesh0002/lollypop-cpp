/**
 * Lollypop C++ - Albums Page
 *
 * Responsive grid of AlbumCard components. Single click opens album detail.
 * Hover and keyboard focus reveal Play button.
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

class AlbumsPage : public LibraryPage {
public:
    AlbumsPage(MainWindow* main_window, ILibraryService* library_service);
    ~AlbumsPage();

    AlbumsPage(const AlbumsPage&) = delete;
    AlbumsPage& operator=(const AlbumsPage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "albums"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> scrolled_window_;
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
