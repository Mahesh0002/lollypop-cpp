/**
 * Lollypop C++ - Playlists Page
 *
 * Responsive grid of PlaylistItem components. Click opens playlist detail.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/PageStateHost.hpp"
#include "ui/components/PlaylistItem.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class PlaylistsPage : public LibraryPage {
public:
    PlaylistsPage(MainWindow* main_window, ILibraryService* library_service);
    ~PlaylistsPage();

    PlaylistsPage(const PlaylistsPage&) = delete;
    PlaylistsPage& operator=(const PlaylistsPage&) = delete;

    GtkWidget* get_widget() const override { return host_.get_widget(); }
    void refresh() override;
    std::string get_page_id() const override { return "playlists"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    PageStateHost host_;
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<PlaylistItem>> playlist_items_;

    void setup_ui();
    void populate(const std::vector<Playlist>& playlists);
    void on_playlist_clicked(const Playlist& playlist);
    void on_playlist_played(const Playlist& playlist);
};

} // namespace lollypop
