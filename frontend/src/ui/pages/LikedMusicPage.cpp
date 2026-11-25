/**
 * Lollypop C++ - Liked Music Page Implementation
 */

#include "ui/pages/LikedMusicPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

LikedMusicPage::LikedMusicPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

LikedMusicPage::~LikedMusicPage() = default;

void LikedMusicPage::setup_ui() {
    // Root widget is the ListBox directly (implements GtkScrollable)
    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "liked-music-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_vexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);

    widget_ = list_box_.get();
}

void LikedMusicPage::refresh() {
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(list_box_.get()));
    track_rows_.clear();

    if (library_service_) {
        auto tracks = library_service_->get_liked_tracks();
        populate(tracks);
    }
}

void LikedMusicPage::populate(const std::vector<Track>& tracks) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1));
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });
        row->set_liked(true);

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
}

void LikedMusicPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void LikedMusicPage::on_track_liked(const Track& track, bool liked) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
    if (!liked) {
        // Remove from view when unliked
        refresh();
    }
}

} // namespace lollypop