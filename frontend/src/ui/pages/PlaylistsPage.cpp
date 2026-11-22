/**
 * Lollypop C++ - Playlists Page Implementation
 */

#include "ui/pages/PlaylistsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

PlaylistsPage::PlaylistsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

PlaylistsPage::~PlaylistsPage() = default;

void PlaylistsPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "playlists-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_widget_set_hexpand(grid_.get(), TRUE);

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), grid_.get());
}

void PlaylistsPage::refresh() {
    // Clear existing - use gtk_flow_box_remove for proper cleanup
    GtkWidget* child = gtk_widget_get_first_child(grid_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(grid_.get()), child);
        child = next;
    }
    playlist_items_.clear();

    if (library_service_) {
        auto playlists = library_service_->get_all_playlists();
        populate(playlists);
    }
}

void PlaylistsPage::populate(const std::vector<Playlist>& playlists) {
    for (const auto& playlist : playlists) {
        auto item = std::make_unique<PlaylistItem>(main_window_, playlist);
        item->set_click_callback(
            [this](const Playlist& p) { on_playlist_clicked(p); });
        item->set_play_callback(
            [this](const Playlist& p) { on_playlist_played(p); });

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
