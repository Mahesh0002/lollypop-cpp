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

    content_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "artist-detail-content");
    gtk_widget_set_margin_top(content_box_.get(), 24);
    gtk_widget_set_margin_bottom(content_box_.get(), 24);
    gtk_widget_set_margin_start(content_box_.get(), 24);
    gtk_widget_set_margin_end(content_box_.get(), 24);

    // Artwork
    artwork_image_.reset(gtk_picture_new());
    g_object_ref_sink(artwork_image_.get());
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_.get()),
                                GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_.get(), "now-playing-artwork-image");
    gtk_widget_set_size_request(artwork_image_.get(), 300, 300);
    gtk_widget_set_halign(artwork_image_.get(), GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(content_box_.get()), artwork_image_.get());

    // Name
    name_label_.reset(gtk_label_new(""));
    g_object_ref_sink(name_label_.get());
    gtk_widget_add_css_class(name_label_.get(), "title-2");
    gtk_label_set_xalign(GTK_LABEL(name_label_.get()), 0.5);
    safe_box_append(GTK_BOX(content_box_.get()), name_label_.get());

    // Track list
    track_list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(track_list_box_.get());
    gtk_widget_add_css_class(track_list_box_.get(), "artist-detail-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(track_list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(track_list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), track_list_box_.get());

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), content_box_.get());
}

void ArtistDetailPage::refresh() {
    if (!current_artist_id_.empty() && library_service_) {
        auto artist = library_service_->get_artist(current_artist_id_);
        if (!artist.id.empty()) {
            current_artist_ = artist;
            populate_artist(artist);
        }
    }
}

void ArtistDetailPage::set_artist(const std::string& artist_id) {
    current_artist_id_ = artist_id;
    refresh();
}

void ArtistDetailPage::populate_artist(const Artist& artist) {
    gtk_label_set_text(GTK_LABEL(name_label_.get()), artist.name.c_str());

    if (artwork_image_) {
        if (!artist.artwork_path.empty()) {
            GFile* file = g_file_new_for_path(artist.artwork_path.c_str());
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
        auto tracks = library_service_->get_tracks_by_artist(artist.name);
        populate_tracks(tracks);
    }
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
