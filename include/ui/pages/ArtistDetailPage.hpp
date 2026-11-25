/**
 * Lollypop C++ - Artist Detail Page
 *
 * Album-style detail hero: square artwork on the top-left, artist metadata
 * and actions on the right, with the artist's tracks below.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/PageStateHost.hpp"
#include "ui/components/TrackRow.hpp"
#include "types.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class ArtworkView;
class MainWindow;

class ArtistDetailPage : public LibraryPage {
public:
    ArtistDetailPage(MainWindow* main_window, ILibraryService* library_service);
    ~ArtistDetailPage();

    ArtistDetailPage(const ArtistDetailPage&) = delete;
    ArtistDetailPage& operator=(const ArtistDetailPage&) = delete;

    GtkWidget* get_widget() const override { return host_.get_widget(); }
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
    PageStateHost host_;
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;  // page content, owned by host_
    std::unique_ptr<GtkWidget, WidgetDeleter> content_box_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> secondary_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> meta_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> track_list_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> no_tracks_label_;

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
    void on_play_artist();
    void on_shuffle_artist();

    static void on_play_artist_trampoline(GtkButton* button, gpointer user_data);
    static void on_shuffle_artist_trampoline(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
