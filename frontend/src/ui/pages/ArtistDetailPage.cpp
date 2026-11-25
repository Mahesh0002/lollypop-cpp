/**
 * Lollypop C++ - Artist Detail Page Implementation
 *
 * Mirrors AlbumDetailPage: a fixed square artwork tile on the top-left,
 * vertically centered metadata and actions on the right, and the track list
 * below. MiddleLibrary owns the outer scroller, so this page stays a plain box.
 */

#include "ui/pages/ArtistDetailPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>

namespace lollypop {

namespace {

constexpr int kDetailArtworkSize = 240;
constexpr int kDetailHeaderHeight = 240;

} // namespace

ArtistDetailPage::ArtistDetailPage(MainWindow* main_window,
                                   ILibraryService* library_service)
    : host_("avatar-default-symbolic", "Artist not found",
            "This artist is no longer in your library.",
            "Artist unavailable",
            "The library could not be loaded. Try again.",
            [this] { refresh(); }),
      main_window_(main_window),
      library_service_(library_service) {
    setup_ui();
}

ArtistDetailPage::~ArtistDetailPage() = default;

void ArtistDetailPage::setup_ui() {
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "artist-detail-page");
    gtk_widget_set_visible(widget_.get(), TRUE);

    content_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "artist-detail-content");
    gtk_widget_set_vexpand(content_box_.get(), FALSE);
    gtk_widget_set_margin_top(content_box_.get(), 24);
    gtk_widget_set_margin_bottom(content_box_.get(), 24);
    gtk_widget_set_margin_start(content_box_.get(), 24);
    gtk_widget_set_margin_end(content_box_.get(), 24);
    gtk_widget_set_visible(content_box_.get(), TRUE);

    // Header: artwork top-left, information column on the right.
    GtkWidget* artwork_data_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_halign(artwork_data_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(artwork_data_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(artwork_data_box, TRUE);
    gtk_widget_set_vexpand(artwork_data_box, FALSE);
    gtk_widget_set_size_request(artwork_data_box, -1, kDetailHeaderHeight);
    gtk_widget_set_visible(artwork_data_box, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), artwork_data_box);

    // Fixed square wrapper; hidden overflow clips the picture to its radius.
    GtkWidget* artwork_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_ref_sink(artwork_wrapper);
    gtk_widget_add_css_class(artwork_wrapper, "detail-artwork");
    gtk_widget_set_size_request(artwork_wrapper, kDetailArtworkSize,
                                kDetailArtworkSize);
    gtk_widget_set_overflow(artwork_wrapper, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_hexpand(artwork_wrapper, FALSE);
    gtk_widget_set_vexpand(artwork_wrapper, FALSE);
    gtk_widget_set_halign(artwork_wrapper, GTK_ALIGN_START);
    gtk_widget_set_valign(artwork_wrapper, GTK_ALIGN_START);
    gtk_widget_set_visible(artwork_wrapper, TRUE);
    safe_box_append(GTK_BOX(artwork_data_box), artwork_wrapper);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(kDetailArtworkSize, kDetailArtworkSize);
    artwork_view_->set_corner_radius(12.0);
    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "detail-artwork-image");
    gtk_widget_set_visible(artwork, TRUE);
    safe_box_append(GTK_BOX(artwork_wrapper), artwork);

    // Information column matches the artwork height. Metadata is centered;
    // actions are pinned to the bottom of the column.
    GtkWidget* data_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(data_box, GTK_ALIGN_START);
    gtk_widget_set_valign(data_box, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(data_box, TRUE);
    gtk_widget_set_vexpand(data_box, FALSE);
    gtk_widget_set_size_request(data_box, -1, kDetailHeaderHeight);
    gtk_widget_set_visible(data_box, TRUE);
    safe_box_append(GTK_BOX(artwork_data_box), data_box);

    GtkWidget* metadata_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(metadata_box, GTK_ALIGN_START);
    gtk_widget_set_valign(metadata_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(metadata_box, TRUE);
    gtk_widget_set_vexpand(metadata_box, TRUE);
    gtk_widget_set_visible(metadata_box, TRUE);
    safe_box_append(GTK_BOX(data_box), metadata_box);

    title_label_.reset(gtk_label_new(""));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "detail-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(title_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), title_label_.get());

    secondary_label_.reset(gtk_label_new("Artist"));
    g_object_ref_sink(secondary_label_.get());
    gtk_widget_add_css_class(secondary_label_.get(), "body");
    gtk_label_set_xalign(GTK_LABEL(secondary_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(secondary_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(secondary_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(secondary_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), secondary_label_.get());

    meta_label_.reset(gtk_label_new(""));
    g_object_ref_sink(meta_label_.get());
    gtk_widget_add_css_class(meta_label_.get(), "caption");
    gtk_widget_add_css_class(meta_label_.get(), "detail-meta");
    gtk_label_set_xalign(GTK_LABEL(meta_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(meta_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(meta_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(meta_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), meta_label_.get());

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(actions, "album-detail-actions");
    gtk_widget_set_halign(actions, GTK_ALIGN_START);
    gtk_widget_set_valign(actions, GTK_ALIGN_END);
    gtk_widget_set_visible(actions, TRUE);
    safe_box_append(GTK_BOX(data_box), actions);

    GtkWidget* play_btn = gtk_button_new_with_label("Play Artist");
    gtk_widget_add_css_class(play_btn, "album-detail-action-button");
    gtk_widget_add_css_class(play_btn, "album-detail-play-button");
    g_signal_connect(play_btn, "clicked",
                     G_CALLBACK(on_play_artist_trampoline), this);
    gtk_widget_set_visible(play_btn, TRUE);
    safe_box_append(GTK_BOX(actions), play_btn);

    GtkWidget* shuffle_btn = gtk_button_new_with_label("Shuffle");
    gtk_widget_add_css_class(shuffle_btn, "album-detail-action-button");
    g_signal_connect(shuffle_btn, "clicked",
                     G_CALLBACK(on_shuffle_artist_trampoline), this);
    gtk_widget_set_visible(shuffle_btn, TRUE);
    safe_box_append(GTK_BOX(actions), shuffle_btn);

    // Music section uses the same treatment as the album detail page.
    GtkWidget* music_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_halign(music_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(music_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(music_box, TRUE);
    gtk_widget_set_vexpand(music_box, FALSE);
    gtk_widget_set_visible(music_box, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), music_box);

    GtkWidget* songs_header = gtk_label_new("Songs");
    gtk_widget_add_css_class(songs_header, "section-title");
    gtk_widget_add_css_class(songs_header, "album-detail-songs-title");
    gtk_label_set_xalign(GTK_LABEL(songs_header), 0.0);
    gtk_widget_set_halign(songs_header, GTK_ALIGN_START);
    gtk_widget_set_visible(songs_header, TRUE);
    safe_box_append(GTK_BOX(music_box), songs_header);

    track_list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(track_list_box_.get());
    gtk_widget_add_css_class(track_list_box_.get(), "album-detail-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(track_list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(track_list_box_.get(), TRUE);
    gtk_widget_set_visible(track_list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(music_box), track_list_box_.get());

    no_tracks_label_.reset(
        gtk_label_new("No tracks available for this artist."));
    g_object_ref_sink(no_tracks_label_.get());
    gtk_widget_add_css_class(no_tracks_label_.get(),
                             "empty-state-description");
    gtk_label_set_wrap(GTK_LABEL(no_tracks_label_.get()), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(no_tracks_label_.get()), 40);
    gtk_label_set_xalign(GTK_LABEL(no_tracks_label_.get()), 0.5);
    gtk_widget_set_halign(no_tracks_label_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(no_tracks_label_.get(), 24);
    gtk_widget_set_margin_bottom(no_tracks_label_.get(), 24);
    gtk_widget_set_visible(no_tracks_label_.get(), FALSE);
    safe_box_append(GTK_BOX(music_box), no_tracks_label_.get());

    safe_box_append(GTK_BOX(widget_.get()), content_box_.get());
    host_.set_content(widget_.get());
}

void ArtistDetailPage::refresh() {
    if (!library_service_) {
        current_artist_ = Artist{};
        host_.show_error();
        return;
    }
    if (current_artist_id_.empty()) {
        current_artist_ = Artist{};
        host_.show_empty();
        return;
    }

    Artist artist = library_service_->get_artist(current_artist_id_);
    if (artist.id.empty()) {
        // Resolve through the same query as the artists grid when the direct
        // lookup misses, matching AlbumDetailPage's fallback behaviour.
        for (const auto& candidate : library_service_->get_all_artists()) {
            if (candidate.id == current_artist_id_) {
                artist = candidate;
                break;
            }
        }
    }
    if (artist.id.empty()) {
        current_artist_ = Artist{};
        host_.show_empty();
        return;
    }

    current_artist_ = artist;
    host_.show_content();
    populate_artist(artist);
}

void ArtistDetailPage::set_artist(const std::string& artist_id) {
    current_artist_id_ = artist_id;
    refresh();
}

void ArtistDetailPage::populate_artist(const Artist& artist) {
    gtk_label_set_text(GTK_LABEL(title_label_.get()),
                       artist.name.empty() ? "Unknown Artist"
                                           : artist.name.c_str());

    const std::vector<Track> tracks =
        library_service_ ? library_service_->get_tracks_by_artist(artist.name)
                         : std::vector<Track>{};
    const size_t album_count = library_service_
                                   ? library_service_
                                         ->get_albums_by_artist(artist.id)
                                         .size()
                                   : artist.album_ids.size();

    std::vector<std::string> genres;
    for (const auto& track : tracks) {
        if (track.genre.empty()) continue;
        if (std::find(genres.begin(), genres.end(), track.genre) !=
            genres.end()) {
            continue;
        }
        genres.push_back(track.genre);
        if (genres.size() == 2) break;
    }

    std::string meta;
    meta += std::to_string(album_count) +
            (album_count == 1 ? " album" : " albums");
    meta += "  •  ";
    meta += std::to_string(tracks.size()) +
            (tracks.size() == 1 ? " song" : " songs");
    if (!genres.empty()) {
        meta += "  •  ";
        for (size_t i = 0; i < genres.size(); ++i) {
            if (i > 0) meta += ", ";
            meta += genres[i];
        }
    }
    gtk_label_set_text(GTK_LABEL(meta_label_.get()), meta.c_str());

    artwork_view_->set_artwork_path(artist.artwork_path);

    safe_list_box_remove_all(GTK_LIST_BOX(track_list_box_.get()));
    track_rows_.clear();
    const bool has_tracks = !tracks.empty();
    gtk_widget_set_visible(track_list_box_.get(), has_tracks);
    gtk_widget_set_visible(no_tracks_label_.get(), !has_tracks);
    populate_tracks(tracks);
}

void ArtistDetailPage::populate_tracks(const std::vector<Track>& tracks) {
    TrackRowOptions options;
    options.show_track_number = true;
    options.show_album = true;

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1), options);
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
    if (auto* backend = main_window_ ? main_window_->get_backend() : nullptr) {
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

void ArtistDetailPage::on_play_artist() {
    if (!library_service_) return;
    const auto tracks = library_service_->get_tracks_by_artist(
        current_artist_.name);
    std::vector<std::string> track_ids;
    for (const auto& track : tracks) track_ids.push_back(track.id);
    if (track_ids.empty()) return;

    if (auto* backend = main_window_ ? main_window_->get_backend() : nullptr) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(track_ids, 0);
        }
    }
}

void ArtistDetailPage::on_shuffle_artist() {
    if (!library_service_) return;
    auto tracks = library_service_->get_tracks_by_artist(current_artist_.name);
    std::vector<std::string> track_ids;
    for (const auto& track : tracks) track_ids.push_back(track.id);
    if (track_ids.empty()) return;

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(track_ids.begin(), track_ids.end(), g);

    if (auto* backend = main_window_ ? main_window_->get_backend() : nullptr) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(track_ids, 0);
        }
    }
}

void ArtistDetailPage::on_play_artist_trampoline(GtkButton*, gpointer user_data) {
    if (auto* self = static_cast<ArtistDetailPage*>(user_data)) {
        self->on_play_artist();
    }
}

void ArtistDetailPage::on_shuffle_artist_trampoline(GtkButton*,
                                                    gpointer user_data) {
    if (auto* self = static_cast<ArtistDetailPage*>(user_data)) {
        self->on_shuffle_artist();
    }
}

} // namespace lollypop
