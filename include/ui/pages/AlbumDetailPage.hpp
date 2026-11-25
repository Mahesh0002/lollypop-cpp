/**
 * Lollypop C++ - Album Detail Page
 *
 * Referenced files (kept as comments so other models can follow the wiring):
 *   frontend/src/ui/pages/AlbumDetailPage.cpp
 *   frontend/src/panels/middle_library.cpp
 *   include/types.hpp
 *   include/services/ILibraryService.hpp
 *   resources/style.css
 *
 * Album artwork, title, artist, genre/year metadata, Play album, Shuffle,
 * and track list using shared TrackRow component.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/TrackRow.hpp"
#include "types.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class ArtworkView;
class MainWindow;

class AlbumDetailPage : public LibraryPage {
public:
    AlbumDetailPage(MainWindow* main_window, ILibraryService* library_service);
    ~AlbumDetailPage();

    AlbumDetailPage(const AlbumDetailPage&) = delete;
    AlbumDetailPage& operator=(const AlbumDetailPage&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "album_detail"; }

    /// Set the album to display.
    void set_album(const std::string& album_id);
    std::string get_current_album_title() const { return current_album_.title; }

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
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> content_box_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> meta_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> track_list_box_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::string current_album_id_;
    Album current_album_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;

    void setup_ui();
    void populate_album(const Album& album);
    void populate_tracks(const std::vector<Track>& tracks);
    static void on_track_played_trampoline(GtkButton* button, gpointer user_data);
    static void on_album_play_trampoline(GtkButton* button, gpointer user_data);
    static void on_album_shuffle_trampoline(GtkButton* button, gpointer user_data);
    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_album_play();
    void on_album_shuffle();
};

} // namespace lollypop
