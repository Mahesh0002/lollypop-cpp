/**
 * Lollypop C++ - Artists Page Implementation
 */

#include "ui/pages/ArtistsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <cstring>
#include <iostream>
#include <random>
#include <algorithm>

namespace lollypop {

ArtistsPage::ArtistsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

ArtistsPage::~ArtistsPage() = default;

void ArtistsPage::setup_ui() {
    // Root widget is the FlowBox directly (implements GtkScrollable)
    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "artists-grid");
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

void ArtistsPage::refresh() {
    GtkWidget* child = gtk_widget_get_first_child(grid_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(grid_.get()), child);
        child = next;
    }
    artist_items_.clear();

    if (library_service_) {
        auto artists = library_service_->get_all_artists();
        populate(artists);
    }
}

void ArtistsPage::populate(const std::vector<Artist>& artists) {
    for (const auto& artist : artists) {
        auto item = std::make_unique<ArtistItem>(main_window_, artist);
        item->set_click_callback(
            [this](const Artist& a) { on_artist_clicked(a); });
        item->set_artist_action_callback([this](const Artist& a, const char* action) {
            if (!main_window_) return;
            IPlayerService* backend = main_window_->get_backend();
            if (!backend) return;

            if (std::strcmp(action, "play") == 0) {
                // Get artist tracks and play
                if (library_service_) {
                    auto tracks = library_service_->get_tracks_by_artist(a.id);
                    std::vector<std::string> track_ids;
                    for (const auto& track : tracks) {
                        track_ids.push_back(track.id);
                    }
                    if (!track_ids.empty()) {
                        backend->play_tracks(track_ids, 0);
                    }
                }
            } else if (std::strcmp(action, "shuffle") == 0) {
                if (library_service_) {
                    auto tracks = library_service_->get_tracks_by_artist(a.id);
                    std::vector<std::string> track_ids;
                    for (const auto& track : tracks) {
                        track_ids.push_back(track.id);
                    }
                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::shuffle(track_ids.begin(), track_ids.end(), g);
                    if (!track_ids.empty()) {
                        backend->play_tracks(track_ids, 0);
                    }
                }
            } else if (std::strcmp(action, "add_to_queue") == 0) {
                if (library_service_) {
                    auto tracks = library_service_->get_tracks_by_artist(a.id);
                    for (const auto& track : tracks) {
                        backend->enqueue_track(track.id, false);
                    }
                }
            } else if (std::strcmp(action, "add_to_playlist") == 0) {
                // TODO: Show playlist selection dialog
                // For now, add to queue as fallback
                if (library_service_) {
                    auto tracks = library_service_->get_tracks_by_artist(a.id);
                    for (const auto& track : tracks) {
                        backend->enqueue_track(track.id, false);
                    }
                }
            }
        });

        GtkWidget* w = item->get_widget();
        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), w);
        artist_items_.push_back(std::move(item));
    }
}

void ArtistsPage::on_artist_clicked(const Artist& artist) {
    std::cout << "[ArtistsPage] Open artist detail: " << artist.name << std::endl;
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->open_artist_detail(artist.id);
        }
    }
}

} // namespace lollypop