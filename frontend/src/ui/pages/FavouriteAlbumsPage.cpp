/**
 * Lollypop C++ - Favourite Albums Page Implementation
 */

#include "ui/pages/FavouriteAlbumsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

FavouriteAlbumsPage::FavouriteAlbumsPage(
    MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

FavouriteAlbumsPage::~FavouriteAlbumsPage() = default;

void FavouriteAlbumsPage::setup_ui() {
    // Root widget is the FlowBox directly (implements GtkScrollable)
    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "favourite-albums-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_vexpand(grid_.get(), TRUE);
    gtk_widget_set_visible(grid_.get(), TRUE);

    widget_ = grid_.get();
}

void FavouriteAlbumsPage::refresh() {
    GtkWidget* child = gtk_widget_get_first_child(grid_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(grid_.get()), child);
        child = next;
    }
    album_cards_.clear();

    if (library_service_) {
        auto albums = library_service_->get_favourite_albums();
        populate(albums);
    }
}

void FavouriteAlbumsPage::populate(const std::vector<Album>& albums) {
    for (const auto& album : albums) {
        auto card = std::make_unique<AlbumCard>(main_window_, album);
        card->set_play_callback(
            [this](const Album& a) { on_album_played(a); });
        card->set_click_callback(
            [this](const Album& a) { on_album_clicked(a); });

        GtkWidget* w = card->get_widget();
        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), w);
        album_cards_.push_back(std::move(card));
    }
}

void FavouriteAlbumsPage::on_album_played(const Album& album) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(album.track_ids, 0);
        }
    }
}

void FavouriteAlbumsPage::on_album_clicked(const Album& album) {
    std::cout << "[FavouriteAlbumsPage] Open album detail: " << album.title << std::endl;
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->open_album_detail(album.id);
        }
    }
}

} // namespace lollypop