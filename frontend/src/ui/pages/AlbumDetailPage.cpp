/**
 * Lollypop C++ - Album Detail Page Implementation
 */

#include "ui/pages/AlbumDetailPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"
#include "ui/components/TrackRow.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <algorithm>
#include <random>
#include <string>

namespace lollypop {

AlbumDetailPage::AlbumDetailPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

AlbumDetailPage::~AlbumDetailPage() = default;

void AlbumDetailPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");
    gtk_widget_set_visible(widget_.get(), TRUE);

    content_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "album-detail-content");
    gtk_widget_set_margin_top(content_box_.get(), 24);
    gtk_widget_set_margin_bottom(content_box_.get(), 24);
    gtk_widget_set_margin_start(content_box_.get(), 24);
    gtk_widget_set_margin_end(content_box_.get(), 24);
    gtk_widget_set_visible(content_box_.get(), TRUE);

    // Artwork
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "now-playing-artwork-image");
    gtk_widget_add_css_class(artwork_image_.get(), "detail-artwork");
    gtk_widget_set_size_request(artwork_image_.get(), 300, 300);
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(artwork_image_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), artwork_image_.get());

    // Title
    title_label_.reset(gtk_label_new(""));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "title-2");
    gtk_widget_add_css_class(title_label_.get(), "detail-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.5);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), title_label_.get());

    // Artist
    artist_label_.reset(gtk_label_new(""));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "body-strong");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.5);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), artist_label_.get());

    // Meta (year, track count)
    meta_label_.reset(gtk_label_new(""));
    g_object_ref_sink(meta_label_.get());
    gtk_widget_add_css_class(meta_label_.get(), "caption");
    gtk_widget_add_css_class(meta_label_.get(), "detail-meta");
    gtk_label_set_xalign(GTK_LABEL(meta_label_.get()), 0.5);
    gtk_widget_set_visible(meta_label_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), meta_label_.get());

    // Action buttons
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(actions, TRUE);

    GtkWidget* play_btn = gtk_button_new_with_label("Play Album");
    gtk_widget_add_css_class(play_btn, "body-strong");
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(play_btn, 10);
    gtk_widget_set_margin_end(play_btn, 10);
    gtk_widget_set_margin_top(play_btn, 10);
    gtk_widget_set_margin_bottom(play_btn, 10);
    g_signal_connect(play_btn, "clicked", G_CALLBACK(on_album_play_trampoline), this);
    gtk_widget_set_visible(play_btn, TRUE);
    safe_box_append(GTK_BOX(actions), play_btn);

    GtkWidget* shuffle_btn = gtk_button_new_with_label("Shuffle");
    gtk_widget_add_css_class(shuffle_btn, "body-strong");
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(shuffle_btn, 10);
    gtk_widget_set_margin_end(shuffle_btn, 10);
    gtk_widget_set_margin_top(shuffle_btn, 10);
    gtk_widget_set_margin_bottom(shuffle_btn, 10);
    g_signal_connect(shuffle_btn, "clicked", G_CALLBACK(on_album_shuffle_trampoline), this);
    gtk_widget_set_visible(shuffle_btn, TRUE);
    safe_box_append(GTK_BOX(actions), shuffle_btn);

    safe_box_append(GTK_BOX(content_box_.get()), actions);

    // Track list
    track_list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(track_list_box_.get());
    gtk_widget_add_css_class(track_list_box_.get(), "album-detail-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(track_list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(track_list_box_.get(), TRUE);
    gtk_widget_set_visible(track_list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), track_list_box_.get());

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), content_box_.get());
}

void AlbumDetailPage::refresh() {
    if (!current_album_id_.empty() && library_service_) {
        auto album = library_service_->get_album(current_album_id_);
        if (!album.id.empty()) {
            current_album_ = album;
            populate_album(album);
        }
    }
}

void AlbumDetailPage::set_album(const std::string& album_id) {
    current_album_id_ = album_id;
    refresh();
}

void AlbumDetailPage::populate_album(const Album& album) {
    gtk_label_set_text(GTK_LABEL(title_label_.get()), album.title.c_str());
    gtk_label_set_text(GTK_LABEL(artist_label_.get()), album.artist.c_str());

    char meta[64];
    snprintf(meta, sizeof(meta), "%d  •  %zu tracks", album.year,
             album.track_ids.size());
    gtk_label_set_text(GTK_LABEL(meta_label_.get()), meta);

    if (artwork_image_) {
        if (!album.artwork_path.empty()) {
            GFile* file = g_file_new_for_path(album.artwork_path.c_str());
            safe_picture_set_file(GTK_PICTURE(artwork_image_.get()), file);
            g_object_unref(file);
        } else {
            safe_picture_set_file(GTK_PICTURE(artwork_image_.get()), nullptr);
        }
    }

    // Load tracks
    GtkWidget* child = gtk_widget_get_first_child(track_list_box_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(track_list_box_.get()), child);
        child = next;
    }
    track_rows_.clear();

    if (library_service_) {
        auto tracks = library_service_->get_tracks_by_ids(album.track_ids);
        populate_tracks(tracks);
    }
}

void AlbumDetailPage::populate_tracks(const std::vector<Track>& tracks) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::unique_ptr<TrackRow, TrackRowDeleter>(
            new TrackRow(main_window_, tracks[i], static_cast<int>(i + 1)));
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
    auto* self = static_cast<AlbumDetailPage*>(user_data);
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
