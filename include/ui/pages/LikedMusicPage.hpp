/**
 * Lollypop C++ - Liked Music Page
 *
 * List of tracks marked as liked, using the shared TrackRow component
 * (plan Section 5.9).
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

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    PageStateHost host_;
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<TrackRow>> track_rows_;

    void setup_ui();
    void populate(const std::vector<Track>& tracks);
    void on_track_played(const Track& track);
    void on_track_liked(const Track& track, bool liked);
};

} // namespace lollypop
