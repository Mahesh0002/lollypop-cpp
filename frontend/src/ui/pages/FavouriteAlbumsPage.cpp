/**
 * Lollypop C++ - Favourite Albums Page Implementation
 */

#include "ui/pages/FavouriteAlbumsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/PlaylistDialogs.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>

namespace lollypop {

FavouriteAlbumsPage::FavouriteAlbumsPage(
    MainWindow* main_window, ILibraryService* library_service)
    : host_("emblem-favorite-symbolic", "No favourite albums yet",
            "Use the heart on an album to keep it here.",
            "Favourite albums unavailable",
            "The library could not be loaded. Try again.",
            [this] { refresh(); }),
      main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

FavouriteAlbumsPage::~FavouriteAlbumsPage() = default;

void FavouriteAlbumsPage::setup_ui() {
    root_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(root_.get());
    gtk_widget_add_css_class(root_.get(), "favourite-albums-page");
    gtk_widget_set_hexpand(root_.get(), TRUE);
    gtk_widget_set_vexpand(root_.get(), TRUE);
    gtk_widget_set_visible(root_.get(), TRUE);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(header, "favourite-albums-page-header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_visible(header, TRUE);

    GtkWidget* title = gtk_label_new("Favourite Albums");
    gtk_widget_add_css_class(title, "page-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(title, TRUE);
    gtk_box_append(GTK_BOX(header), title);

    subtitle_label_ = gtk_label_new("");
    gtk_widget_add_css_class(subtitle_label_, "page-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle_label_), 0.0);
    gtk_widget_set_halign(subtitle_label_, GTK_ALIGN_END);
    gtk_widget_set_valign(subtitle_label_, GTK_ALIGN_END);
    gtk_widget_set_visible(subtitle_label_, TRUE);
    gtk_box_append(GTK_BOX(header), subtitle_label_);

    gtk_box_append(GTK_BOX(root_.get()), header);

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "favourite-albums-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), FALSE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 20);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 16);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_vexpand(grid_.get(), TRUE);
    gtk_widget_set_visible(grid_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_.get()), grid_.get());

    host_.set_content(root_.get());
}

void FavouriteAlbumsPage::refresh() {
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_flow_box_remove_all(GTK_FLOW_BOX(grid_.get()));
    album_cards_.clear();

    if (!library_service_) {
        update_header(0);
        host_.show_error();
        return;
    }

    std::vector<Album> albums = library_service_->get_favourite_albums();
    update_header(albums.size());
    if (albums.empty()) {
        host_.show_empty();
        return;
    }

    host_.show_content();
    populate(albums);
}

void FavouriteAlbumsPage::update_header(size_t album_count) {
    if (!subtitle_label_) return;
    char text[64];
    snprintf(text, sizeof(text), "%zu album%s", album_count,
             album_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(subtitle_label_), text);
}

void FavouriteAlbumsPage::populate(const std::vector<Album>& albums) {
    for (const auto& album : albums) {
        auto card = std::make_unique<AlbumCard>(main_window_, album);
        card->set_play_callback(
            [this](const Album& a) { on_album_played(a); });
        card->set_click_callback(
            [this](const Album& a) { on_album_clicked(a); });
        card->set_album_action_callback([this](const Album& a, const char* action) {
            if (!main_window_) return;
            IPlayerService* backend = main_window_->get_backend();
            if (!backend) return;

            if (std::strcmp(action, "play") == 0) {
                backend->play_tracks(a.track_ids, 0);
            } else if (std::strcmp(action, "shuffle") == 0) {
                std::vector<std::string> shuffled = a.track_ids;
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(shuffled.begin(), shuffled.end(), g);
                backend->play_tracks(shuffled, 0);
            } else if (std::strcmp(action, "add_to_queue") == 0) {
                backend->enqueue_track(a.track_ids[0], false);
                // Add all tracks
                for (size_t i = 1; i < a.track_ids.size(); ++i) {
                    backend->enqueue_track(a.track_ids[i], false);
                }
            } else if (std::strcmp(action, "add_to_playlist") == 0) {
                playlist_dialogs::show_add_to_playlist_dialog(
                    main_window_->get_window(), library_service_, a.track_ids);
            } else if (std::strcmp(action, "toggle_favorite") == 0) {
                if (library_service_) {
                    library_service_->toggle_favorite_album(a.id);
                }
            }
        });

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
