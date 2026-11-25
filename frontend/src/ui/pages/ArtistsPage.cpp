/**
 * Lollypop C++ - Artists Page Implementation
 */

#include "ui/pages/ArtistsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <cstdio>
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
    root_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(root_.get());
    gtk_widget_add_css_class(root_.get(), "artists-page");
    gtk_widget_set_hexpand(root_.get(), TRUE);
    gtk_widget_set_vexpand(root_.get(), FALSE);
    gtk_widget_set_visible(root_.get(), TRUE);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(header, "artists-page-header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_visible(header, TRUE);

    GtkWidget* title = gtk_label_new("Artists");
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
    gtk_widget_add_css_class(grid_.get(), "artists-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_halign(grid_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(grid_.get(), FALSE);
    gtk_widget_set_visible(grid_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_.get()), grid_.get());

    empty_state_ = std::make_unique<EmptyState>();
    empty_state_->setup("avatar-default-symbolic", "No artists yet",
                        "Add music to your library to see artists here.");
    gtk_widget_set_visible(empty_state_->get_widget(), FALSE);
    gtk_box_append(GTK_BOX(root_.get()), empty_state_->get_widget());

    error_state_ = std::make_unique<ErrorState>();
    error_state_->setup("Artists unavailable",
                        "The library could not be loaded. Try again.",
                        [this] { refresh(); });
    gtk_widget_set_visible(error_state_->get_widget(), FALSE);
    gtk_box_append(GTK_BOX(root_.get()), error_state_->get_widget());
}

void ArtistsPage::refresh() {
    // Use safe helper to avoid invalid sibling pointers after unparenting.
    safe_flow_box_remove_all(GTK_FLOW_BOX(grid_.get()));
    artist_items_.clear();

    if (!library_service_) {
        update_header(0);
        gtk_widget_set_visible(grid_.get(), FALSE);
        gtk_widget_set_visible(empty_state_->get_widget(), FALSE);
        gtk_widget_set_visible(error_state_->get_widget(), TRUE);
        return;
    }

    std::vector<Artist> artists = library_service_->get_all_artists();
    const bool has_artists = !artists.empty();
    update_header(artists.size());
    gtk_widget_set_visible(grid_.get(), has_artists);
    gtk_widget_set_visible(empty_state_->get_widget(), !has_artists);
    gtk_widget_set_visible(error_state_->get_widget(), FALSE);
    if (!has_artists) return;

    populate(artists);
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

void ArtistsPage::update_header(size_t artist_count) {
    if (!subtitle_label_) return;
    char text[64];
    snprintf(text, sizeof(text), "%zu artist%s", artist_count,
             artist_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(subtitle_label_), text);
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
