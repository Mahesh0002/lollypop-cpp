/**
 * Lollypop C++ - Album Detail Page Implementation
 *
 * Referenced files (kept as comments so other models can follow the wiring):
 *   include/ui/pages/AlbumDetailPage.hpp
 *   frontend/src/panels/middle_library.cpp
 *   frontend/src/ui/components/TrackRow.cpp
 *   include/types.hpp
 *   include/services/ILibraryService.hpp
 *   storage/src/manager.cpp
 *   resources/style.css
 */

#include "ui/pages/AlbumDetailPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <algorithm>
#include <random>
#include <string>

namespace lollypop {

namespace {

constexpr int kDetailArtworkSize = 240;
constexpr int kDetailHeaderHeight = 240;

} // namespace

AlbumDetailPage::AlbumDetailPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

AlbumDetailPage::~AlbumDetailPage() = default;

void AlbumDetailPage::setup_ui() {
    // MiddleLibrary already owns the single outer scroller. A nested GtkScrolledWindow
    // here clamps the page to its viewport and traps the wheel, which made the
    // track list appear empty. Keep this page as a plain, expanding box.
    widget_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(widget_.get());
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "album-detail-page");
    gtk_widget_set_visible(widget_.get(), TRUE);

    content_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "album-detail-content");
    gtk_widget_set_vexpand(content_box_.get(), FALSE);
    gtk_widget_set_margin_top(content_box_.get(), 24);
    gtk_widget_set_margin_bottom(content_box_.get(), 24);
    gtk_widget_set_margin_start(content_box_.get(), 24);
    gtk_widget_set_margin_end(content_box_.get(), 24);
    gtk_widget_set_visible(content_box_.get(), TRUE);

    // ========================================================================
    // Component 1: artwork + data
    //   - 240x240 artwork on the left
    //   - album title (semi-bold), artist (normal), meta (year • tracks)
    //     centered vertically to the right of the artwork
    // ========================================================================
    GtkWidget* artwork_data_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_halign(artwork_data_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(artwork_data_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(artwork_data_box, TRUE);
    gtk_widget_set_vexpand(artwork_data_box, FALSE);
    // Cap the header to the artwork height; it must never consume the window.
    gtk_widget_set_size_request(artwork_data_box, -1, kDetailHeaderHeight);
    gtk_widget_set_visible(artwork_data_box, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), artwork_data_box);

    // Artwork wrapper - fixed square container, pinned to the top-left.
    // Hidden overflow is what clips the picture to the CSS corner radius.
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

    // Artwork uses the shared, HiDPI-aware loader. ArtworkView sets its own
    // square size request so no separate GtkPicture intrinsic size can inflate
    // the header row.
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(kDetailArtworkSize, kDetailArtworkSize);
    artwork_view_->set_corner_radius(12.0);
    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "detail-artwork-image");
    gtk_widget_set_visible(artwork, TRUE);
    safe_box_append(GTK_BOX(artwork_wrapper), artwork);

    // Data column matches the artwork height. The metadata group expands and
    // centers itself; the action row stays pinned to the bottom of this column.
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

    artist_label_.reset(gtk_label_new(""));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "body");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(artist_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), artist_label_.get());

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

    GtkWidget* play_btn = gtk_button_new_with_label("Play Album");
    gtk_widget_add_css_class(play_btn, "album-detail-action-button");
    gtk_widget_add_css_class(play_btn, "album-detail-play-button");
    g_signal_connect(play_btn, "clicked", G_CALLBACK(on_album_play_trampoline), this);
    gtk_widget_set_visible(play_btn, TRUE);
    safe_box_append(GTK_BOX(actions), play_btn);

    GtkWidget* shuffle_btn = gtk_button_new_with_label("Shuffle");
    gtk_widget_add_css_class(shuffle_btn, "album-detail-action-button");
    g_signal_connect(shuffle_btn, "clicked", G_CALLBACK(on_album_shuffle_trampoline), this);
    gtk_widget_set_visible(shuffle_btn, TRUE);
    safe_box_append(GTK_BOX(actions), shuffle_btn);

    // ========================================================================
    // Component 2: music (track list below the album header component)
    // ========================================================================
    GtkWidget* music_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_halign(music_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(music_box, GTK_ALIGN_START);
    gtk_widget_set_hexpand(music_box, TRUE);
    gtk_widget_set_vexpand(music_box, FALSE);
    gtk_widget_set_visible(music_box, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), music_box);

    // Music section title.
    GtkWidget* songs_header = gtk_label_new("Songs");
    gtk_widget_add_css_class(songs_header, "section-title");
    gtk_widget_add_css_class(songs_header, "album-detail-songs-title");
    gtk_label_set_xalign(GTK_LABEL(songs_header), 0.0);
    gtk_widget_set_halign(songs_header, GTK_ALIGN_START);
    gtk_widget_set_visible(songs_header, TRUE);
    safe_box_append(GTK_BOX(music_box), songs_header);

    // Track list
    track_list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(track_list_box_.get());
    gtk_widget_add_css_class(track_list_box_.get(), "album-detail-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(track_list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(track_list_box_.get(), TRUE);
    gtk_widget_set_visible(track_list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(music_box), track_list_box_.get());

    safe_box_append(GTK_BOX(widget_.get()), content_box_.get());
}

void AlbumDetailPage::refresh() {
    if (!library_service_ || current_album_id_.empty()) return;

    // The album grid is built from get_all_albums(), so resolve through the
    // same query as a fallback when the single-album lookup misses. This also
    // recovers the track list if album_tracks links are stale or missing.
    std::vector<Album> all_albums;
    auto find_in_all = [&](const std::string& id) -> const Album* {
        if (all_albums.empty()) all_albums = library_service_->get_all_albums();
        for (const auto& candidate : all_albums) {
            if (candidate.id == id) return &candidate;
        }
        return nullptr;
    };

    Album album = library_service_->get_album(current_album_id_);
    if (album.id.empty()) {
        if (const Album* found = find_in_all(current_album_id_)) album = *found;
    }
    if (album.id.empty()) {
        std::cerr << "[AlbumDetailPage] album not found: " << current_album_id_
                  << std::endl;
        return;
    }
    if (album.track_ids.empty()) {
        if (const Album* found = find_in_all(current_album_id_)) {
            album.track_ids = found->track_ids;
        }
    }

    current_album_ = album;
    populate_album(album);
}

void AlbumDetailPage::set_album(const std::string& album_id) {
    current_album_id_ = album_id;
    refresh();
}

void AlbumDetailPage::populate_album(const Album& album) {
    std::vector<Track> tracks;
    if (library_service_) {
        tracks = library_service_->get_tracks_by_ids(album.track_ids);
    }

    gtk_label_set_text(GTK_LABEL(title_label_.get()), album.title.c_str());
    gtk_label_set_text(GTK_LABEL(artist_label_.get()),
                       album.artist.empty() ? "Unknown Artist"
                                            : album.artist.c_str());

    // Album has no genre column; take the first track's genre as the album's.
    std::string genre;
    for (const auto& track : tracks) {
        if (!track.genre.empty()) {
            genre = track.genre;
            break;
        }
    }
    std::string meta;
    if (!genre.empty()) meta += genre + "  •  ";
    if (album.year > 0) meta += std::to_string(album.year) + "  •  ";
    meta += std::to_string(tracks.size()) +
            (tracks.size() == 1 ? " song" : " songs");
    gtk_label_set_text(GTK_LABEL(meta_label_.get()), meta.c_str());

    std::cout << "[AlbumDetailPage] " << album.title << ": " << tracks.size()
              << " tracks" << std::endl;

    artwork_view_->set_artwork_path(album.artwork_path);

    // Load tracks - use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(track_list_box_.get()));
    track_rows_.clear();
    populate_tracks(tracks);
}

void AlbumDetailPage::populate_tracks(const std::vector<Track>& tracks) {
    TrackRowOptions options;
    options.show_track_number = true;
    options.show_album = false;

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(
            main_window_, tracks[i], static_cast<int>(i + 1), options);
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(track_list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
}

void AlbumDetailPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void AlbumDetailPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

void AlbumDetailPage::on_album_play() {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(current_album_.track_ids, 0);
        }
    }
}

void AlbumDetailPage::on_album_shuffle() {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            std::vector<std::string> shuffled = current_album_.track_ids;
            std::mt19937_64 rng(std::random_device{}());
            std::shuffle(shuffled.begin(), shuffled.end(), rng);
            player->play_tracks(shuffled, 0);
        }
    }
}

void AlbumDetailPage::on_track_played_trampoline(GtkButton* button, gpointer user_data) {
    (void)button;
    (void)user_data;
    // This is called from TrackRow, but TrackRow passes itself as user_data
    // We need to track which track was clicked. For now, just log.
    std::cout << "[AlbumDetailPage] Track played (trampoline)" << std::endl;
}

void AlbumDetailPage::on_album_play_trampoline(GtkButton* button, gpointer user_data) {
    (void)button;
    auto* self = static_cast<AlbumDetailPage*>(user_data);
    self->on_album_play();
}

void AlbumDetailPage::on_album_shuffle_trampoline(GtkButton* button, gpointer user_data) {
    (void)button;
    auto* self = static_cast<AlbumDetailPage*>(user_data);
    self->on_album_shuffle();
}

} // namespace lollypop
