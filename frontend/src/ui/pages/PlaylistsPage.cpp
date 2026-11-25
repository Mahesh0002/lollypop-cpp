/**
 * Lollypop C++ - Playlists Page Implementation
 */

#include "ui/pages/PlaylistsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <cstring>
#include <iostream>
#include <random>
#include <algorithm>

namespace lollypop {

PlaylistsPage::PlaylistsPage(MainWindow* main_window, ILibraryService* library_service)
    : host_("view-list-symbolic", "No playlists yet",
            "Create a playlist to group songs you listen to together.",
            "Playlists unavailable",
            "The library could not be loaded. Try again.",
            [this] { refresh(); }),
      main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

PlaylistsPage::~PlaylistsPage() = default;

void PlaylistsPage::setup_ui() {
    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "playlists-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_vexpand(grid_.get(), TRUE);
    gtk_widget_set_visible(grid_.get(), TRUE);
    host_.set_content(grid_.get());
}

void PlaylistsPage::refresh() {
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_flow_box_remove_all(GTK_FLOW_BOX(grid_.get()));
    playlist_items_.clear();

    if (!library_service_) {
        host_.show_error();
        return;
    }

    auto playlists = library_service_->get_all_playlists();
    if (playlists.empty()) {
        host_.show_empty();
        return;
    }

    host_.show_content();
    populate(playlists);
}

void PlaylistsPage::populate(const std::vector<Playlist>& playlists) {
    for (const auto& playlist : playlists) {
        auto item = std::make_unique<PlaylistItem>(main_window_, playlist);
        item->set_click_callback(
            [this](const Playlist& p) { on_playlist_clicked(p); });
        item->set_play_callback(
            [this](const Playlist& p) { on_playlist_played(p); });
        item->set_playlist_action_callback([this](const Playlist& p, const char* action) {
            if (!main_window_) return;
            IPlayerService* backend = main_window_->get_backend();
            if (!backend) return;

            if (std::strcmp(action, "play") == 0) {
                backend->play_tracks(p.track_ids, 0);
            } else if (std::strcmp(action, "shuffle") == 0) {
                std::vector<std::string> shuffled = p.track_ids;
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled.begin(), shuffled.end(), g);
                backend->play_tracks(shuffled, 0);
            } else if (std::strcmp(action, "add_to_queue") == 0) {
                for (const auto& track_id : p.track_ids) {
                    backend->enqueue_track(track_id, false);
                }
            } else if (std::strcmp(action, "rename") == 0) {
                // TODO: Show rename dialog
                std::cout << "[PlaylistsPage] Rename playlist: " << p.name << std::endl;
            } else if (std::strcmp(action, "delete") == 0) {
                // TODO: Implement delete_playlist in ILibraryService
                std::cout << "[PlaylistsPage] Delete playlist: " << p.id << std::endl;
            }
        });

        GtkWidget* w = item->get_widget();
        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), w);
        playlist_items_.push_back(std::move(item));
    }
}

void PlaylistsPage::on_playlist_clicked(const Playlist& playlist) {
    std::cout << "[PlaylistsPage] Open playlist: " << playlist.name << std::endl;
}

void PlaylistsPage::on_playlist_played(const Playlist& playlist) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(playlist.track_ids, 0);
        }
    }
}

} // namespace lollypop
