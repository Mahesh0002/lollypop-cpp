/**
 * Lollypop C++ - Favourite Music Page
 *
 * Favourite (liked) tracks using the shared TrackRow component, with a large
 * artwork header and playback controls.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/PageStateHost.hpp"
#include "ui/components/TrackRow.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class ArtworkView;
class MainWindow;

class LikedMusicPage : public LibraryPage {
public:
    LikedMusicPage(MainWindow* main_window, ILibraryService* library_service);
    ~LikedMusicPage();

    LikedMusicPage(const LikedMusicPage&) = delete;
    LikedMusicPage& operator=(const LikedMusicPage&) = delete;

    GtkWidget* get_widget() const override { return host_.get_widget(); }
    void refresh() override;
    std::string get_page_id() const override { return "liked"; }
    std::string palette_artwork_path() const override {
        return palette_artwork_path_;
    }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    PageStateHost host_;
    std::unique_ptr<GtkWidget, WidgetDeleter> root_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> updated_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::string palette_artwork_path_;
    std::vector<std::string> current_track_ids_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;
    GSimpleActionGroup* menu_action_group_ = nullptr;

    void setup_ui();
    void create_menu(GtkWidget* button);
    void update_header(const std::vector<Track>& tracks);
    void populate(const std::vector<Track>& tracks);
    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
    void on_play_all();
    void on_shuffle_all();
    void on_add_to_queue();
    void on_add_to_playlist();
    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_clicked(GtkButton* button, gpointer user_data);
    static void on_menu_action(GSimpleAction* action, GVariant* parameter,
                               gpointer user_data);
};

} // namespace lollypop
