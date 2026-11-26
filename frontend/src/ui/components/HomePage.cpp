/**
 * Lollypop C++ - Home Page Implementation
 * Dashboard showing recently added, recently played, favourite albums.
 * No nested scroller - content flows in middle panel's single scroller.
 */

#include "ui/components/HomePage.hpp"
#include "main_window.hpp"
#include "ui/components/AppleAlbumTile.hpp"
#include "ui/components/TrackRow.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace lollypop {

namespace {

std::vector<Album> top_albums(const std::vector<Track>& tracks,
                              const std::vector<Album>& albums,
                              size_t limit) {
    std::vector<Album> picked;
    std::unordered_set<std::string> seen;

    for (const auto& track : tracks) {
        for (const auto& album : albums) {
            if (album.title != track.album) continue;
            if (!track.artist.empty() && album.artist != track.artist &&
                album.artist != track.album_artist) {
                continue;
            }

            const std::string key = album.id.empty()
                                        ? album.title + "|" + album.artist
                                        : album.id;
            if (seen.insert(key).second) picked.push_back(album);
            break;
        }
        if (picked.size() == limit) break;
    }
    return picked;
}

} // namespace

HomePage::HomePage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

HomePage::~HomePage() = default;

void HomePage::setup_ui() {
    // Main content box - no scroller, just a vertical box
    // The middle panel's scroller will handle scrolling
    main_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(main_box_.get());
    gtk_widget_add_css_class(main_box_.get(), "home-page");
    gtk_widget_set_margin_top(main_box_.get(), 50);
    gtk_widget_set_margin_bottom(main_box_.get(), 24);
    gtk_widget_set_margin_start(main_box_.get(), 24);
    gtk_widget_set_margin_end(main_box_.get(), 24);
    gtk_widget_set_hexpand(main_box_.get(), TRUE);
    gtk_widget_set_vexpand(main_box_.get(), FALSE);
    gtk_widget_set_visible(main_box_.get(), TRUE);

    // Recently Added - the four most recently added distinct albums.
    create_album_section("Recently Added",
                         [this]() {
                             if (!library_service_) return std::vector<Album>{};
                             return top_albums(
                                 library_service_->get_recently_added(32),
                                 library_service_->get_all_albums(), 4);
                         },
                         [this](const Album& a) { on_album_played(a); },
                         [this](const Album& a) { on_album_clicked(a); },
                         [this]() { if (recently_added_cb_) recently_added_cb_(); },
                         4);

    // Recently Played - the four most recently played distinct albums.
    create_album_section("Recently Played",
                         [this]() {
                             if (!library_service_) return std::vector<Album>{};
                             return top_albums(
                                 library_service_->get_recently_played(32),
                                 library_service_->get_all_albums(), 4);
                         },
                         [this](const Album& a) { on_album_played(a); },
                         [this](const Album& a) { on_album_clicked(a); },
                         [this]() { if (recently_played_cb_) recently_played_cb_(); },
                         4);

    // Favourite Albums
    create_album_section("Favourite Albums",
                         [this]() { return library_service_ ? library_service_->get_favourite_albums() : std::vector<Album>{}; },
                         [this](const Album& a) { on_album_played(a); },
                         [this](const Album& a) { on_album_clicked(a); },
                         [this]() { if (favourite_albums_cb_) favourite_albums_cb_(); });

    initialized_ = true;
}

GtkWidget* HomePage::create_section(const std::string& title,
                                    std::function<std::vector<Track>()> fetcher,
                                    std::function<void(const Track&)> play_cb,
                                    std::function<void(const Track&, bool)> like_cb,
                                    std::function<void()> view_all_cb) {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(section, TRUE);
    gtk_widget_set_visible(section, TRUE);

    // Section header
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_visible(header, TRUE);

    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(label, "section-title");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_visible(label, TRUE);
    gtk_box_append(GTK_BOX(header), label);

    gtk_box_append(GTK_BOX(section), header);

    // Track list
    GtkWidget* list_box = gtk_list_box_new();
    g_object_ref_sink(list_box);
    gtk_widget_add_css_class(list_box, "home-track-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box, TRUE);
    gtk_widget_set_visible(list_box, TRUE);

    auto data = std::make_unique<SectionData>();
    data->title = title;
    data->fetcher = std::move(fetcher);
    data->play_cb = std::move(play_cb);
    data->like_cb = std::move(like_cb);
    data->list_box = list_box;
    data->view_all_cb = std::move(view_all_cb);

    GtkWidget* result = section;
    sections_data_.push_back(std::move(data));

    // Safe append - check if widget already has a parent
    if (gtk_widget_get_parent(list_box) == nullptr) {
        gtk_box_append(GTK_BOX(section), list_box);
    }
    if (gtk_widget_get_parent(section) == nullptr) {
        gtk_box_append(GTK_BOX(main_box_.get()), section);
    }

    // Initial populate
    refresh_track_section(sections_data_.back().get());

    return result;
}

GtkWidget* HomePage::create_album_section(const std::string& title,
                                          std::function<std::vector<Album>()> fetcher,
                                          std::function<void(const Album&)> play_cb,
                                          std::function<void(const Album&)> click_cb,
                                          std::function<void()> view_all_cb,
                                          int max_per_row) {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(section, TRUE);
    gtk_widget_set_visible(section, TRUE);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_visible(header, TRUE);

    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(label, "section-title");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_visible(label, TRUE);
    gtk_box_append(GTK_BOX(header), label);

    gtk_box_append(GTK_BOX(section), header);

    GtkWidget* grid = gtk_flow_box_new();
    g_object_ref_sink(grid);
    gtk_widget_add_css_class(grid, "home-album-grid");
    // Homogeneous, expanding columns; each tile fills its cell and stays
    // square via AppleAlbumTile's fit callback.
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), TRUE);
    // Pin the column count so the tick-driven tile sizing cannot flip the
    // grid between 4 and 3 columns when sections are rebuilt.
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid), max_per_row);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), max_per_row);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 16);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), 16);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_halign(grid, GTK_ALIGN_FILL);
    gtk_widget_set_visible(grid, TRUE);

    auto data = std::make_unique<AlbumSectionData>();
    data->title = title;
    data->fetcher = std::move(fetcher);
    data->play_cb = std::move(play_cb);
    data->click_cb = std::move(click_cb);
    data->grid = grid;
    data->view_all_cb = std::move(view_all_cb);

    album_sections_data_.push_back(std::move(data));

    // Safe append - check if widget already has a parent
    if (gtk_widget_get_parent(grid) == nullptr) {
        gtk_box_append(GTK_BOX(section), grid);
    }
    if (gtk_widget_get_parent(section) == nullptr) {
        gtk_box_append(GTK_BOX(main_box_.get()), section);
    }

    // Initial populate
    refresh_album_section(album_sections_data_.back().get());

    return section;
}

void HomePage::refresh() {
    if (!initialized_) return;
    for (auto& section : sections_data_) {
        refresh_track_section(section.get());
    }
    for (auto& section : album_sections_data_) {
        refresh_album_section(section.get());
    }
}

void HomePage::refresh_track_section(SectionData* data) {
    if (!data || !data->list_box) return;

    // Clear existing - use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(data->list_box));
    data->track_rows.clear();

    auto tracks = data->fetcher();

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1));
        row->set_play_callback(data->play_cb);
        row->set_like_callback(data->like_cb);

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(data->list_box), w);
        data->track_rows.push_back(std::move(row));
    }
}

void HomePage::refresh_album_section(AlbumSectionData* data) {
    if (!data || !data->grid) return;

    // Clear existing - use safe helper to avoid invalid sibling pointers after unparenting
    safe_flow_box_remove_all(GTK_FLOW_BOX(data->grid));
    data->album_tiles.clear();

    auto albums = data->fetcher();

    for (size_t i = 0; i < albums.size(); ++i) {
        auto tile = std::make_unique<AppleAlbumTile>(albums[i], 300);
        tile->set_play_callback(data->play_cb);
        tile->set_click_callback(data->click_cb);
        tile->set_play_toggle_callback([this](const Album&) {
            if (!main_window_) return;
            if (auto* player = dynamic_cast<IPlayerService*>(
                    main_window_->get_backend())) {
                player->toggle_play_pause();
            }
        });

        GtkWidget* w = tile->get_widget();
        safe_flow_box_append(GTK_FLOW_BOX(data->grid), w);
        data->album_tiles.push_back(std::move(tile));
    }
}

void HomePage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void HomePage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

void HomePage::on_album_played(const Album& album) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(album.track_ids, 0);
        }
    }
}

void HomePage::on_album_clicked(const Album& album) {
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->open_album_detail(album.id);
        }
    }
}

void HomePage::set_recently_added_callback(std::function<void()> cb) {
    if (!album_sections_data_.empty()) {
        album_sections_data_[0]->view_all_cb = std::move(cb);
    }
}
void HomePage::set_recently_played_callback(std::function<void()> cb) {
    if (album_sections_data_.size() > 1) {
        album_sections_data_[1]->view_all_cb = std::move(cb);
    }
}

void HomePage::set_favourite_albums_callback(std::function<void()> cb) {
    if (album_sections_data_.size() > 2) {
        album_sections_data_[2]->view_all_cb = std::move(cb);
    }
}

} // namespace lollypop