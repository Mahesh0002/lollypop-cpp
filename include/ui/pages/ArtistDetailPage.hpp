/**
 * Lollypop C++ - Artist Detail Page
 *
 * Artist artwork, name, albums, and tracks (plan Section 5.11).
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/EmptyState.hpp"
#include "types.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class ArtistDetailPage : public LibraryPage {
public:
    ArtistDetailPage(MainWindow* main_window, ILibraryService* library_service);
    ~ArtistDetailPage();

    ArtistDetailPage(const ArtistDetailPage&) = delete;
    ArtistDetailPage& operator=(const ArtistDetailPage&) = delete;

    GtkWidget* get_widget() const override { return widget_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "artist_detail"; }

    void set_artist(const std::string& artist_id);
    std::string get_current_artist_name() const { return current_artist_.name; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> state_container_;
    std::unique_ptr<GtkWidget, WidgetDeleter> content_box_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> name_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> track_list_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> no_tracks_label_;
    std::unique_ptr<EmptyState> artist_missing_state_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::string current_artist_id_;
    Artist current_artist_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;

    void setup_ui();
    void populate_artist(const Artist& artist);
    void populate_tracks(const std::vector<Track>& tracks);
    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
};

} // namespace lollypop
