/**
 * Lollypop C++ - Artists Page
 *
 * Apple Music-style library page: a large title with artist count, a working
 * sort control, and a responsive 6-column grid of overlay-labeled artist
 * tiles. Single click opens artist detail; double click or the play button
 * starts playback.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/AppleArtistTile.hpp"
#include "ui/components/EmptyState.hpp"
#include "ui/components/ErrorState.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class ArtistsPage : public LibraryPage {
public:
    enum class SortMode {
        Name,
        RecentlyAdded,
    };

    ArtistsPage(MainWindow* main_window, ILibraryService* library_service);
    ~ArtistsPage();

    ArtistsPage(const ArtistsPage&) = delete;
    ArtistsPage& operator=(const ArtistsPage&) = delete;

    GtkWidget* get_widget() const override { return root_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "artists"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> root_;
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;

    GtkWidget* subtitle_label_ = nullptr;
    GtkWidget* sort_button_ = nullptr;
    GSimpleActionGroup* sort_action_group_ = nullptr;
    std::unique_ptr<EmptyState> empty_state_;
    std::unique_ptr<ErrorState> error_state_;
    GtkAdjustment* artwork_scroll_adjustment_ = nullptr;
    gulong artwork_scroll_value_changed_id_ = 0;
    gulong artwork_scroll_changed_id_ = 0;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<AppleArtistTile>> artist_tiles_;
    SortMode sort_mode_ = SortMode::Name;

    void setup_ui();
    void populate(const std::vector<Artist>& artists);
    void update_header(size_t artist_count);
    void create_sort_menu();
    void update_sort_action_states();
    void set_sort_mode(SortMode mode);
    int visible_columns() const;
    void sorted_artists(std::vector<Artist>& artists) const;
    void connect_scroll_adjustment();
    void update_visible_artwork();

    static void on_sort_action(GSimpleAction* action, GVariant* parameter,
                               gpointer user_data);
    static void on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data);
    void on_artist_played(const Artist& artist);
    void on_artist_clicked(const Artist& artist);
};

} // namespace lollypop
