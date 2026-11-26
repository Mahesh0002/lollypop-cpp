/**
 * Lollypop C++ - Artist Detail Page
 *
 * Album-style detail hero: square artwork on the top-left, artist metadata
 * and actions on the right, with the artist's tracks below.
 */

#pragma once

#include <gtk/gtk.h>
#include <cstdint>
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
    std::string palette_artwork_path() const override {
        return current_artist_.artwork_path;
    }

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
    std::unique_ptr<GtkWidget, WidgetDeleter> biography_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> albums_fact_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> songs_fact_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> genres_fact_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> years_fact_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> albums_flow_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> no_albums_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> genres_flow_box_;
    std::unique_ptr<GtkWidget, WidgetDeleter> no_genres_label_;
    std::vector<std::unique_ptr<ArtworkView>> related_artwork_views_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::string current_artist_id_;
    Artist current_artist_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    uint64_t info_generation_ = 0;

    void setup_ui();
    void populate_artist(const Artist& artist);
    void populate_tracks(const std::vector<Track>& tracks);
    void populate_about(const std::vector<Track>& tracks,
                        const std::vector<Album>& albums,
                        const std::vector<std::string>& genres);
    void populate_related_albums(const std::vector<Album>& albums);
    void populate_genres(const std::vector<std::string>& genres);
    void request_biography();
    std::string library_biography(const std::vector<Track>& tracks,
                                  const std::vector<Album>& albums,
                                  const std::vector<std::string>& genres) const;
    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_play_artist();
    void on_shuffle_artist();
    void on_album_open(const std::string& album_id);
    void on_genre_open(const std::string& genre);

    static void on_play_artist_trampoline(GtkButton* button, gpointer user_data);
    static void on_shuffle_artist_trampoline(GtkButton* button, gpointer user_data);
    static void on_album_button_trampoline(GtkButton* button, gpointer user_data);
    static void on_genre_button_trampoline(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
