/**
 * Lollypop C++ - Artist Detail Page Implementation
 *
 * Mirrors AlbumDetailPage: a fixed 500x500 artwork tile on the top-left,
 * vertically centered metadata and actions on the right, and the track list
 * below. MiddleLibrary owns the outer scroller, so this page stays a plain box.
 */

#include "ui/pages/ArtistDetailPage.hpp"
#include "main_window.hpp"
#include "panels/middle_library.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>

namespace lollypop {

namespace {

constexpr int kDetailArtworkSize = 500;
constexpr int kDetailHeaderHeight = 500;
constexpr int kRelatedArtworkSize = 112;

struct NavigationPayload {
    ArtistDetailPage* page;
    std::string value;
};

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

ArtistDetailPage::~ArtistDetailPage() {
    if (alive_) *alive_ = false;
}

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
    gtk_widget_set_margin_top(content_box_.get(), 50);
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
    gtk_box_set_homogeneous(GTK_BOX(actions), TRUE);
    gtk_widget_set_halign(actions, GTK_ALIGN_START);
    gtk_widget_set_valign(actions, GTK_ALIGN_END);
    gtk_widget_set_visible(actions, TRUE);
    safe_box_append(GTK_BOX(data_box), actions);

    GtkWidget* play_btn = gtk_button_new_with_label("Play Artist");
    gtk_widget_add_css_class(play_btn, "album-detail-action-button");
    gtk_widget_add_css_class(play_btn, "album-detail-play-button");
    if (ThemeManager* theme =
            main_window_ ? main_window_->get_theme_manager() : nullptr) {
        theme->apply_detail_action_style(play_btn);
    }
    g_signal_connect(play_btn, "clicked",
                     G_CALLBACK(on_play_artist_trampoline), this);
    gtk_widget_set_visible(play_btn, TRUE);
    safe_box_append(GTK_BOX(actions), play_btn);

    GtkWidget* shuffle_btn = gtk_button_new_with_label("Shuffle");
    gtk_widget_add_css_class(shuffle_btn, "album-detail-action-button");
    if (ThemeManager* theme =
            main_window_ ? main_window_->get_theme_manager() : nullptr) {
        theme->apply_detail_action_style(shuffle_btn);
    }
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

    // About section: biography plus library-derived facts.
    GtkWidget* about_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(about_box, "artist-detail-section");
    gtk_widget_set_hexpand(about_box, TRUE);
    gtk_widget_set_visible(about_box, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), about_box);

    GtkWidget* about_title = gtk_label_new("About");
    gtk_widget_add_css_class(about_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(about_title), 0.0);
    gtk_widget_set_halign(about_title, GTK_ALIGN_START);
    gtk_widget_set_visible(about_title, TRUE);
    safe_box_append(GTK_BOX(about_box), about_title);

    biography_label_.reset(gtk_label_new(""));
    g_object_ref_sink(biography_label_.get());
    gtk_widget_add_css_class(biography_label_.get(),
                             "artist-detail-biography");
    gtk_label_set_wrap(GTK_LABEL(biography_label_.get()), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(biography_label_.get()), 110);
    gtk_label_set_xalign(GTK_LABEL(biography_label_.get()), 0.0);
    gtk_widget_set_halign(biography_label_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_visible(biography_label_.get(), TRUE);
    safe_box_append(GTK_BOX(about_box), biography_label_.get());

    GtkWidget* facts_grid = gtk_grid_new();
    gtk_widget_add_css_class(facts_grid, "artist-detail-facts");
    gtk_grid_set_column_spacing(GTK_GRID(facts_grid), 32);
    gtk_grid_set_row_spacing(GTK_GRID(facts_grid), 12);
    gtk_widget_set_halign(facts_grid, GTK_ALIGN_START);
    gtk_widget_set_visible(facts_grid, TRUE);
    safe_box_append(GTK_BOX(about_box), facts_grid);

    auto make_fact = [this](GtkWidget* grid, int left, int top,
                            const char* caption,
                            std::unique_ptr<GtkWidget, WidgetDeleter>& value) {
        value.reset(gtk_label_new(""));
        g_object_ref_sink(value.get());
        gtk_widget_add_css_class(value.get(), "artist-detail-fact-value");
        gtk_label_set_xalign(GTK_LABEL(value.get()), 0.0);
        gtk_widget_set_halign(value.get(), GTK_ALIGN_START);
        gtk_widget_set_visible(value.get(), TRUE);
        gtk_grid_attach(GTK_GRID(grid), value.get(), left, top, 1, 1);

        GtkWidget* title = gtk_label_new(caption);
        gtk_widget_add_css_class(title, "artist-detail-fact-label");
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_widget_set_visible(title, TRUE);
        gtk_grid_attach(GTK_GRID(grid), title, left, top + 1, 1, 1);
    };

    make_fact(facts_grid, 0, 0, "Albums", albums_fact_label_);
    make_fact(facts_grid, 1, 0, "Songs", songs_fact_label_);
    make_fact(facts_grid, 2, 0, "Genres", genres_fact_label_);
    make_fact(facts_grid, 3, 0, "Years", years_fact_label_);

    // Related albums. Tiles stay compact so they wrap naturally at any width.
    GtkWidget* albums_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(albums_section, "artist-detail-section");
    gtk_widget_set_hexpand(albums_section, TRUE);
    gtk_widget_set_visible(albums_section, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), albums_section);

    GtkWidget* albums_title = gtk_label_new("Albums");
    gtk_widget_add_css_class(albums_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(albums_title), 0.0);
    gtk_widget_set_halign(albums_title, GTK_ALIGN_START);
    gtk_widget_set_visible(albums_title, TRUE);
    safe_box_append(GTK_BOX(albums_section), albums_title);

    albums_flow_box_.reset(gtk_flow_box_new());
    g_object_ref_sink(albums_flow_box_.get());
    gtk_widget_add_css_class(albums_flow_box_.get(), "artist-related-albums");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(albums_flow_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(albums_flow_box_.get()),
                                           2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(albums_flow_box_.get()),
                                           6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(albums_flow_box_.get()), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(albums_flow_box_.get()), 12);
    gtk_widget_set_hexpand(albums_flow_box_.get(), TRUE);
    gtk_widget_set_visible(albums_flow_box_.get(), TRUE);
    safe_box_append(GTK_BOX(albums_section), albums_flow_box_.get());

    no_albums_label_.reset(gtk_label_new("No albums available for this artist."));
    g_object_ref_sink(no_albums_label_.get());
    gtk_widget_add_css_class(no_albums_label_.get(),
                             "empty-state-description");
    gtk_widget_set_halign(no_albums_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(no_albums_label_.get(), FALSE);
    safe_box_append(GTK_BOX(albums_section), no_albums_label_.get());

    // Related genres link back into the filtered Songs page.
    GtkWidget* genres_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(genres_section, "artist-detail-section");
    gtk_widget_set_hexpand(genres_section, TRUE);
    gtk_widget_set_visible(genres_section, TRUE);
    safe_box_append(GTK_BOX(content_box_.get()), genres_section);

    GtkWidget* genres_title = gtk_label_new("Genres");
    gtk_widget_add_css_class(genres_title, "section-title");
    gtk_label_set_xalign(GTK_LABEL(genres_title), 0.0);
    gtk_widget_set_halign(genres_title, GTK_ALIGN_START);
    gtk_widget_set_visible(genres_title, TRUE);
    safe_box_append(GTK_BOX(genres_section), genres_title);

    genres_flow_box_.reset(gtk_flow_box_new());
    g_object_ref_sink(genres_flow_box_.get());
    gtk_widget_add_css_class(genres_flow_box_.get(), "artist-related-genres");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(genres_flow_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(genres_flow_box_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(genres_flow_box_.get()), 8);
    gtk_widget_set_hexpand(genres_flow_box_.get(), TRUE);
    gtk_widget_set_visible(genres_flow_box_.get(), TRUE);
    safe_box_append(GTK_BOX(genres_section), genres_flow_box_.get());

    no_genres_label_.reset(gtk_label_new("No genres available for this artist."));
    g_object_ref_sink(no_genres_label_.get());
    gtk_widget_add_css_class(no_genres_label_.get(),
                             "empty-state-description");
    gtk_widget_set_halign(no_genres_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(no_genres_label_.get(), FALSE);
    safe_box_append(GTK_BOX(genres_section), no_genres_label_.get());

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

    std::vector<Album> albums;
    if (library_service_) {
        albums = library_service_->get_albums_by_artist(artist.id);
        if (albums.empty()) {
            for (const auto& album_id : artist.album_ids) {
                Album album = library_service_->get_album(album_id);
                if (!album.id.empty()) albums.push_back(std::move(album));
            }
        }
        if (albums.empty() && !tracks.empty()) {
            const auto library_albums = library_service_->get_all_albums();
            for (const auto& track : tracks) {
                if (track.album.empty()) continue;
                for (const auto& album : library_albums) {
                    if (album.title != track.album) continue;
                    if (std::find_if(albums.begin(), albums.end(),
                                     [&album](const Album& existing) {
                                         return existing.id == album.id;
                                     }) != albums.end()) {
                        continue;
                    }
                    albums.push_back(album);
                    break;
                }
            }
        }
    }
    std::sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
        if (a.year != b.year) return a.year < b.year;
        return a.title < b.title;
    });

    std::vector<std::string> genres;
    for (const auto& track : tracks) {
        if (track.genre.empty()) continue;
        if (std::find(genres.begin(), genres.end(), track.genre) !=
            genres.end()) {
            continue;
        }
        genres.push_back(track.genre);
    }
    std::sort(genres.begin(), genres.end());

    std::string meta;
    meta += std::to_string(albums.size()) +
            (albums.size() == 1 ? " album" : " albums");
    meta += "  •  ";
    meta += std::to_string(tracks.size()) +
            (tracks.size() == 1 ? " song" : " songs");
    if (!genres.empty()) {
        meta += "  •  ";
        for (size_t i = 0; i < std::min<size_t>(genres.size(), 2); ++i) {
            if (i > 0) meta += ", ";
            meta += genres[i];
        }
        if (genres.size() > 2) {
            meta += " +" + std::to_string(genres.size() - 2);
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

    populate_about(tracks, albums, genres);
    populate_related_albums(albums);
    populate_genres(genres);
    request_biography();
}

void ArtistDetailPage::populate_about(const std::vector<Track>& tracks,
                                      const std::vector<Album>& albums,
                                      const std::vector<std::string>& genres) {
    gtk_label_set_text(GTK_LABEL(albums_fact_label_.get()),
                       std::to_string(albums.size()).c_str());
    gtk_label_set_text(GTK_LABEL(songs_fact_label_.get()),
                       std::to_string(tracks.size()).c_str());
    gtk_label_set_text(GTK_LABEL(genres_fact_label_.get()),
                       std::to_string(genres.size()).c_str());

    int first_year = 0;
    int last_year = 0;
    for (const auto& album : albums) {
        if (album.year <= 0) continue;
        if (first_year == 0 || album.year < first_year) first_year = album.year;
        if (album.year > last_year) last_year = album.year;
    }
    for (const auto& track : tracks) {
        if (track.year <= 0) continue;
        if (first_year == 0 || track.year < first_year) first_year = track.year;
        if (track.year > last_year) last_year = track.year;
    }

    std::string years = "Unknown";
    if (first_year > 0) {
        years = std::to_string(first_year);
        if (last_year > first_year) years += "–" + std::to_string(last_year);
    }
    gtk_label_set_text(GTK_LABEL(years_fact_label_.get()), years.c_str());

    // Show library-derived copy immediately; a provider result replaces it.
    gtk_label_set_text(GTK_LABEL(biography_label_.get()),
                       library_biography(tracks, albums, genres).c_str());
}

std::string ArtistDetailPage::library_biography(
    const std::vector<Track>& tracks,
    const std::vector<Album>& albums,
    const std::vector<std::string>& genres) const {
    const std::string name =
        current_artist_.name.empty() ? "This artist" : current_artist_.name;
    std::string text = name + " has " +
                       std::to_string(albums.size()) +
                       (albums.size() == 1 ? " album" : " albums") + " and " +
                       std::to_string(tracks.size()) +
                       (tracks.size() == 1 ? " song" : " songs") +
                       " in your library";
    if (!genres.empty()) {
        text += ", spanning ";
        for (size_t i = 0; i < std::min<size_t>(genres.size(), 3); ++i) {
            if (i > 0) text += i + 1 == std::min<size_t>(genres.size(), 3)
                                       ? ", and "
                                       : ", ";
            text += genres[i];
        }
    }
    text += ".";
    return text;
}

void ArtistDetailPage::request_biography() {
    if (!library_service_ || current_artist_.name.empty()) return;

    const uint64_t generation = ++info_generation_;
    const std::string artist_name = current_artist_.name;
    const auto alive = alive_;
    library_service_->request_artist_info(
        artist_name,
        [this, alive, generation, artist_name](const std::string&,
                                               const ArtistInfo& info) {
            if (!alive || !*alive) return;
            if (generation != info_generation_) return;
            if (artist_name != current_artist_.name) return;
            if (info.biography.empty()) return;
            gtk_label_set_text(GTK_LABEL(biography_label_.get()),
                               info.biography.c_str());
        });
}

void ArtistDetailPage::populate_related_albums(
    const std::vector<Album>& albums) {
    safe_flow_box_remove_all(GTK_FLOW_BOX(albums_flow_box_.get()));
    related_artwork_views_.clear();

    const bool has_albums = !albums.empty();
    gtk_widget_set_visible(albums_flow_box_.get(), has_albums);
    gtk_widget_set_visible(no_albums_label_.get(), !has_albums);

    for (const auto& album : albums) {
        GtkWidget* button = gtk_button_new();
        gtk_widget_add_css_class(button, "artist-related-album-card");
        gtk_widget_set_tooltip_text(button, album.title.c_str());
        accessibility::set_label(button,
                                 ("Open album " + album.title).c_str());

        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_halign(card, GTK_ALIGN_FILL);
        gtk_widget_set_visible(card, TRUE);

        auto artwork = std::make_unique<ArtworkView>();
        artwork->set_preferred_size(kRelatedArtworkSize,
                                    kRelatedArtworkSize);
        artwork->set_corner_radius(8.0);
        artwork->set_artwork_path(album.artwork_path);
        GtkWidget* artwork_widget = artwork->get_widget();
        gtk_widget_add_css_class(artwork_widget,
                                 "artist-related-album-artwork");
        gtk_widget_set_visible(artwork_widget, TRUE);
        gtk_widget_set_hexpand(artwork_widget, FALSE);
        gtk_widget_set_halign(artwork_widget, GTK_ALIGN_CENTER);
        safe_box_append(GTK_BOX(card), artwork_widget);
        related_artwork_views_.push_back(std::move(artwork));

        GtkWidget* title = gtk_label_new(album.title.c_str());
        gtk_widget_add_css_class(title, "artist-related-album-title");
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(title), 20);
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_widget_set_visible(title, TRUE);
        safe_box_append(GTK_BOX(card), title);

        const std::string year_text =
            album.year > 0 ? std::to_string(album.year) : "";
        GtkWidget* caption = gtk_label_new(year_text.c_str());
        gtk_widget_add_css_class(caption, "caption");
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_widget_set_visible(caption, TRUE);
        safe_box_append(GTK_BOX(card), caption);

        gtk_button_set_child(GTK_BUTTON(button), card);
        g_signal_connect_data(
            button, "clicked", G_CALLBACK(on_album_button_trampoline),
            new NavigationPayload{this, album.id},
            [](gpointer data, GClosure*) {
                delete static_cast<NavigationPayload*>(data);
            },
            G_CONNECT_DEFAULT);
        gtk_widget_set_visible(button, TRUE);
        safe_flow_box_append(GTK_FLOW_BOX(albums_flow_box_.get()), button);
    }
}

void ArtistDetailPage::populate_genres(
    const std::vector<std::string>& genres) {
    safe_flow_box_remove_all(GTK_FLOW_BOX(genres_flow_box_.get()));

    const bool has_genres = !genres.empty();
    gtk_widget_set_visible(genres_flow_box_.get(), has_genres);
    gtk_widget_set_visible(no_genres_label_.get(), !has_genres);

    for (const auto& genre : genres) {
        GtkWidget* button = gtk_button_new_with_label(genre.c_str());
        gtk_widget_add_css_class(button, "artist-genre-chip");
        gtk_widget_set_tooltip_text(button, ("Browse " + genre).c_str());
        accessibility::set_label(button, ("Browse " + genre).c_str());
        g_signal_connect_data(
            button, "clicked", G_CALLBACK(on_genre_button_trampoline),
            new NavigationPayload{this, genre},
            [](gpointer data, GClosure*) {
                delete static_cast<NavigationPayload*>(data);
            },
            G_CONNECT_DEFAULT);
        gtk_widget_set_visible(button, TRUE);
        safe_flow_box_append(GTK_FLOW_BOX(genres_flow_box_.get()), button);
    }
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

void ArtistDetailPage::on_album_open(const std::string& album_id) {
    if (main_window_ && main_window_->get_middle_library()) {
        main_window_->get_middle_library()->open_album_detail(album_id);
    }
}

void ArtistDetailPage::on_genre_open(const std::string& genre) {
    if (main_window_ && main_window_->get_middle_library()) {
        main_window_->get_middle_library()->show_genre(genre);
    }
}

void ArtistDetailPage::on_album_button_trampoline(GtkButton*,
                                                  gpointer user_data) {
    if (auto* payload = static_cast<NavigationPayload*>(user_data)) {
        if (payload->page) payload->page->on_album_open(payload->value);
    }
}

void ArtistDetailPage::on_genre_button_trampoline(GtkButton*,
                                                  gpointer user_data) {
    if (auto* payload = static_cast<NavigationPayload*>(user_data)) {
        if (payload->page) payload->page->on_genre_open(payload->value);
    }
}

} // namespace lollypop
