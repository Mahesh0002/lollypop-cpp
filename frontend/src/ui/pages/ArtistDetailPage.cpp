/**
 * Lollypop C++ - Artist Detail Page Implementation
 */

#include "ui/pages/ArtistDetailPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

ArtistDetailPage::ArtistDetailPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

ArtistDetailPage::~ArtistDetailPage() = default;

void ArtistDetailPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");
    gtk_widget_set_visible(widget_.get(), TRUE);

    state_container_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(state_container_.get());
    gtk_widget_set_hexpand(state_container_.get(), TRUE);
    gtk_widget_set_vexpand(state_container_.get(), TRUE);
    gtk_widget_set_visible(state_container_.get(), TRUE);

    content_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "artist-detail-content");
    gtk_widget_set_margin_top(content_box_.get(), 24);
    gtk_widget_set_margin_bottom(content_box_.get(), 24);
    gtk_widget_set_margin_start(content_box_.get(), 24);
    gtk_widget_set_margin_end(content_box_.get(), 24);
    gtk_widget_set_visible(content_box_.get(), TRUE);
    safe_box_append(GTK_BOX(state_container_.get()), content_box_.get());

    // Artwork uses the shared cached view, with a visible fallback when the
    // library has no artist image yet.
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(300, 300);
    gtk_widget_add_css_class(artwork_view_->get_widget(), "artist-artwork");
    gtk_widget_add_css_class(artwork_view_->get_widget(), "detail-artwork");
    gtk_widget_set_halign(artwork_view_->get_widget(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(artwork_view_->get_widget(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), artwork_view_->get_widget());

    // Name
    name_label_.reset(gtk_label_new(""));
    g_object_ref_sink(name_label_.get());
    gtk_widget_add_css_class(name_label_.get(), "title-2");
    gtk_widget_add_css_class(name_label_.get(), "detail-title");
    gtk_label_set_xalign(GTK_LABEL(name_label_.get()), 0.5);
    gtk_widget_set_visible(name_label_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), name_label_.get());

    // Track list
    track_list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(track_list_box_.get());
    gtk_widget_add_css_class(track_list_box_.get(), "artist-detail-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(track_list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(track_list_box_.get(), TRUE);
    gtk_widget_set_visible(track_list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), track_list_box_.get());

    // An inline state keeps artwork and the artist name visible when there
    // are no tracks to show.
    no_tracks_label_.reset(gtk_label_new("No tracks available for this artist."));
    g_object_ref_sink(no_tracks_label_.get());
    gtk_widget_add_css_class(no_tracks_label_.get(), "empty-state-description");
    gtk_label_set_wrap(GTK_LABEL(no_tracks_label_.get()), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(no_tracks_label_.get()), 40);
    gtk_label_set_xalign(GTK_LABEL(no_tracks_label_.get()), 0.5);
    gtk_widget_set_halign(no_tracks_label_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(no_tracks_label_.get(), 24);
    gtk_widget_set_margin_bottom(no_tracks_label_.get(), 24);
    gtk_widget_set_visible(no_tracks_label_.get(), FALSE);
    safe_box_append(GTK_BOX(content_box_.get()), no_tracks_label_.get());

    artist_missing_state_ = std::make_unique<EmptyState>();
    artist_missing_state_->setup("avatar-default-symbolic", "Artist not found",
                                 "This artist is no longer in your library.");
    gtk_widget_set_visible(artist_missing_state_->get_widget(), FALSE);
    safe_box_append(GTK_BOX(state_container_.get()), artist_missing_state_->get_widget());

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()),
                                   state_container_.get());
}

void ArtistDetailPage::refresh() {
    if (!library_service_ || current_artist_id_.empty()) {
        current_artist_ = Artist{};
        gtk_widget_set_visible(content_box_.get(), FALSE);
        gtk_widget_set_visible(artist_missing_state_->get_widget(), TRUE);
        return;
    }

    auto artist = library_service_->get_artist(current_artist_id_);
    if (artist.id.empty()) {
        current_artist_ = Artist{};
        gtk_widget_set_visible(content_box_.get(), FALSE);
        gtk_widget_set_visible(artist_missing_state_->get_widget(), TRUE);
        return;
    }

    current_artist_ = artist;
    gtk_widget_set_visible(content_box_.get(), TRUE);
    gtk_widget_set_visible(artist_missing_state_->get_widget(), FALSE);
    populate_artist(artist);
}

void ArtistDetailPage::set_artist(const std::string& artist_id) {
    current_artist_id_ = artist_id;
    refresh();
}

void ArtistDetailPage::populate_artist(const Artist& artist) {
    gtk_label_set_text(GTK_LABEL(name_label_.get()), artist.name.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artist.artwork_path);
    }

    // Load tracks - use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(track_list_box_.get()));
    track_rows_.clear();

    std::vector<Track> tracks;
    if (library_service_) {
        tracks = library_service_->get_tracks_by_artist(artist.name);
    }
    const bool has_tracks = !tracks.empty();
    gtk_widget_set_visible(track_list_box_.get(), has_tracks);
    gtk_widget_set_visible(no_tracks_label_.get(), !has_tracks);
    populate_tracks(tracks);
}

void ArtistDetailPage::populate_tracks(const std::vector<Track>& tracks) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1));
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(track_list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
}

void ArtistDetailPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void ArtistDetailPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

} // namespace lollypop
