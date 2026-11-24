/**
 * Lollypop C++ - Albums Page Implementation
 *
 * Referenced files: include/ui/pages/AlbumsPage.hpp,
 *                   frontend/include/ui/components/AppleAlbumTile.hpp,
 *                   resources/apple_ui.css, resources/style.css
 */

#include "ui/pages/AlbumsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

namespace lollypop {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

constexpr int kAlbumsTileSize = 150;

const char* sort_button_label(AlbumsPage::SortMode mode) {
    switch (mode) {
        case AlbumsPage::SortMode::Title: return "Sort: Title";
        case AlbumsPage::SortMode::Artist: return "Sort: Artist";
        case AlbumsPage::SortMode::Year: return "Sort: Year";
        case AlbumsPage::SortMode::RecentlyAdded: return "Sort: Recently Added";
    }
    return "Sort: Title";
}

} // namespace

AlbumsPage::AlbumsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

AlbumsPage::~AlbumsPage() {
    if (artwork_scroll_adjustment_) {
        if (artwork_scroll_value_changed_id_) {
            g_signal_handler_disconnect(artwork_scroll_adjustment_,
                                        artwork_scroll_value_changed_id_);
        }
        if (artwork_scroll_changed_id_) {
            g_signal_handler_disconnect(artwork_scroll_adjustment_,
                                        artwork_scroll_changed_id_);
        }
        g_object_unref(artwork_scroll_adjustment_);
    }
}

void AlbumsPage::setup_ui() {
    root_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(root_.get());
    gtk_widget_add_css_class(root_.get(), "albums-page");
    gtk_widget_set_hexpand(root_.get(), TRUE);
    gtk_widget_set_vexpand(root_.get(), FALSE);
    gtk_widget_set_visible(root_.get(), TRUE);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(header, "albums-page-header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_visible(header, TRUE);

    GtkWidget* title_block = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(title_block, TRUE);
    gtk_widget_set_halign(title_block, GTK_ALIGN_FILL);
    gtk_widget_set_valign(title_block, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(title_block, TRUE);

    GtkWidget* title = gtk_label_new("Albums");
    gtk_widget_add_css_class(title, "page-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_visible(title, TRUE);
    gtk_box_append(GTK_BOX(title_block), title);

    subtitle_label_ = gtk_label_new("");
    gtk_widget_add_css_class(subtitle_label_, "page-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle_label_), 0.0);
    gtk_widget_set_halign(subtitle_label_, GTK_ALIGN_START);
    gtk_widget_set_visible(subtitle_label_, TRUE);
    gtk_box_append(GTK_BOX(title_block), subtitle_label_);

    sort_button_ = gtk_button_new_with_label(sort_button_label(sort_mode_));
    gtk_widget_add_css_class(sort_button_, "page-sort-button");
    gtk_widget_set_halign(sort_button_, GTK_ALIGN_END);
    gtk_widget_set_valign(sort_button_, GTK_ALIGN_END);
    gtk_widget_set_visible(sort_button_, TRUE);
    g_signal_connect(sort_button_, "clicked", G_CALLBACK(on_sort_clicked), this);

    gtk_box_append(GTK_BOX(header), title_block);
    gtk_box_append(GTK_BOX(header), sort_button_);
    gtk_box_append(GTK_BOX(root_.get()), header);

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "albums-grid");
    // Six homogeneous, expanding columns; tiles fill their cells and stay
    // square via AppleAlbumTile's fit callback.
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 20);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 12);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_halign(grid_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(grid_.get(), FALSE);
    gtk_widget_set_visible(grid_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_.get()), grid_.get());
}

void AlbumsPage::refresh() {
    std::vector<Album> albums;
    if (library_service_) {
        albums = library_service_->get_all_albums();
        sorted_albums(albums);
    }

    favourite_album_ids_.clear();
    for (const auto& album : albums) {
        if (album.favorite) favourite_album_ids_.insert(album.id);
    }

    // Detach children before repopulating. The tile objects are kept when the
    // album count is unchanged so their artwork textures can be reused.
    GtkWidget* child = gtk_widget_get_first_child(grid_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(grid_.get()), child);
        child = next;
    }
    if (album_tiles_.size() != albums.size()) album_tiles_.clear();

    update_header(albums.size());
    populate(albums);
    connect_scroll_adjustment();
    update_visible_artwork();
}

void AlbumsPage::sorted_albums(std::vector<Album>& albums) const {
    switch (sort_mode_) {
        case SortMode::Title:
            std::stable_sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
                return lowercase(a.title) < lowercase(b.title);
            });
            break;
        case SortMode::Artist:
            std::stable_sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
                return lowercase(a.artist) < lowercase(b.artist);
            });
            break;
        case SortMode::Year:
            std::stable_sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
                return a.year > b.year;
            });
            break;
        case SortMode::RecentlyAdded:
            std::stable_sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
                return a.date_added > b.date_added;
            });
            break;
    }
}

void AlbumsPage::update_header(size_t album_count) {
    if (!subtitle_label_) return;
    char text[64];
    snprintf(text, sizeof(text), "%zu album%s", album_count,
             album_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(subtitle_label_), text);
}

void AlbumsPage::cycle_sort() {
    switch (sort_mode_) {
        case SortMode::Title: sort_mode_ = SortMode::Artist; break;
        case SortMode::Artist: sort_mode_ = SortMode::Year; break;
        case SortMode::Year: sort_mode_ = SortMode::RecentlyAdded; break;
        case SortMode::RecentlyAdded: sort_mode_ = SortMode::Title; break;
    }
    gtk_button_set_label(GTK_BUTTON(sort_button_),
                         sort_button_label(sort_mode_));
    refresh();
}

void AlbumsPage::on_sort_clicked(GtkButton*, gpointer user_data) {
    static_cast<AlbumsPage*>(user_data)->cycle_sort();
}

void AlbumsPage::populate(const std::vector<Album>& albums) {
    const bool reuse_tiles = album_tiles_.size() == albums.size();

    for (size_t i = 0; i < albums.size(); ++i) {
        const Album& album = albums[i];
        AppleAlbumTile* tile = nullptr;

        if (reuse_tiles) {
            tile = album_tiles_[i].get();
            tile->update_album(album, false);
        } else {
            auto owned = std::make_unique<AppleAlbumTile>(album, kAlbumsTileSize, true);
            tile = owned.get();
            tile->set_play_callback(
                [this](const Album& a) { on_album_played(a); });
            tile->set_click_callback(
                [this](const Album& a) { on_album_clicked(a); });
            tile->set_favorite_query_callback([this](const Album& a) {
                return favourite_album_ids_.count(a.id) != 0;
            });
            tile->set_favorite_toggle_callback([this, tile](const Album& a) {
                toggle_favorite(a, tile);
            });
            tile->set_album_action_callback([this, tile](const Album& a, const char* action) {
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
                    for (const auto& track_id : a.track_ids) {
                        backend->enqueue_track(track_id, false);
                    }
                } else if (std::strcmp(action, "add_to_playlist") == 0) {
                    // TODO: Show playlist selection dialog
                    // For now, add to queue as fallback
                    for (const auto& track_id : a.track_ids) {
                        backend->enqueue_track(track_id, false);
                    }
                } else if (std::strcmp(action, "toggle_favorite") == 0) {
                    toggle_favorite(a, tile);
                }
            });
            // Artwork for off-screen tiles is loaded later, when the tile is
            // near the viewport.
            tile->set_artwork_visible(false);
            album_tiles_.push_back(std::move(owned));
        }

        tile->set_favorite(favourite_album_ids_.count(album.id) != 0);
        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), tile->get_widget());
    }
}

void AlbumsPage::toggle_favorite(const Album& album, AppleAlbumTile* tile) {
    const bool was_favorite = favourite_album_ids_.count(album.id) != 0;
    if (library_service_) library_service_->toggle_favorite_album(album.id);

    if (was_favorite) {
        favourite_album_ids_.erase(album.id);
    } else {
        favourite_album_ids_.insert(album.id);
    }
    if (tile) tile->set_favorite(!was_favorite);
}

void AlbumsPage::connect_scroll_adjustment() {
    GtkWidget* parent = gtk_widget_get_parent(grid_.get());
    while (parent && !GTK_IS_SCROLLED_WINDOW(parent)) {
        parent = gtk_widget_get_parent(parent);
    }

    GtkAdjustment* adjustment = nullptr;
    if (parent) {
        adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
    }
    if (adjustment == artwork_scroll_adjustment_) return;

    if (artwork_scroll_adjustment_) {
        if (artwork_scroll_value_changed_id_) {
            g_signal_handler_disconnect(artwork_scroll_adjustment_,
                                        artwork_scroll_value_changed_id_);
        }
        if (artwork_scroll_changed_id_) {
            g_signal_handler_disconnect(artwork_scroll_adjustment_,
                                        artwork_scroll_changed_id_);
        }
        g_object_unref(artwork_scroll_adjustment_);
    }

    artwork_scroll_adjustment_ = adjustment ? g_object_ref(adjustment) : nullptr;
    artwork_scroll_value_changed_id_ = 0;
    artwork_scroll_changed_id_ = 0;
    if (adjustment) {
        artwork_scroll_value_changed_id_ = g_signal_connect(
            adjustment, "value-changed", G_CALLBACK(on_scroll_changed), this);
        artwork_scroll_changed_id_ = g_signal_connect(
            adjustment, "changed", G_CALLBACK(on_scroll_changed), this);
    }
}

void AlbumsPage::on_scroll_changed(GtkAdjustment*, gpointer user_data) {
    static_cast<AlbumsPage*>(user_data)->update_visible_artwork();
}

void AlbumsPage::update_visible_artwork() {
    if (album_tiles_.empty()) return;

    // Six-column grid; load the visible rows plus one row of look-ahead so a
    // small scroll does not flash the fallback artwork.
    constexpr int kColumns = 6;
    constexpr int kRowGap = 20;
    int row_height = kAlbumsTileSize + kRowGap;
    const int first_height = gtk_widget_get_height(album_tiles_.front()->get_widget());
    if (first_height > 0) row_height = first_height + kRowGap;

    double value = 0.0;
    double page_size = 0.0;
    if (artwork_scroll_adjustment_) {
        value = gtk_adjustment_get_value(artwork_scroll_adjustment_);
        page_size = gtk_adjustment_get_page_size(artwork_scroll_adjustment_);
    }
    if (page_size <= 0.0) page_size = kAlbumsTileSize * 2.0;

    const int first_row = std::max(0, static_cast<int>(std::floor(value / row_height)) - 1);
    const int rows = std::max(2, static_cast<int>(std::ceil(page_size / row_height)) + 2);
    const size_t first = static_cast<size_t>(first_row) * kColumns;
    const size_t last = std::min(album_tiles_.size(),
                                 static_cast<size_t>(first_row + rows) * kColumns);

    for (size_t i = 0; i < album_tiles_.size(); ++i) {
        album_tiles_[i]->set_artwork_visible(i >= first && i < last);
    }
}

void AlbumsPage::on_album_played(const Album& album) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(album.track_ids, 0);
        }
    }
}

void AlbumsPage::on_album_clicked(const Album& album) {
    std::cout << "[AlbumsPage] Open album detail: " << album.title << std::endl;
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->open_album_detail(album.id);
        }
    }
}

} // namespace lollypop
