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
#include "ui/components/PlaylistDialogs.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
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

constexpr int kTileSize = 150;
constexpr int kRowGap = 20;
constexpr int kColumnGap = 12;
constexpr int kMinColumns = 3;
constexpr int kMaxColumns = 8;
constexpr int kPreferredColumns = 6;

const char* sort_button_label(AlbumsPage::SortMode mode) {
    switch (mode) {
        case AlbumsPage::SortMode::Title: return "Sort by: Title";
        case AlbumsPage::SortMode::Artist: return "Sort by: Artist";
        case AlbumsPage::SortMode::Year: return "Sort by: Year";
        case AlbumsPage::SortMode::RecentlyAdded: return "Sort by: Recently Added";
    }
    return "Sort by: Title";
}

const char* sort_action_name(AlbumsPage::SortMode mode) {
    switch (mode) {
        case AlbumsPage::SortMode::Title: return "title";
        case AlbumsPage::SortMode::Artist: return "artist";
        case AlbumsPage::SortMode::Year: return "year";
        case AlbumsPage::SortMode::RecentlyAdded: return "recent";
    }
    return "title";
}

std::optional<AlbumsPage::SortMode> sort_mode_from_action(const char* name) {
    if (!name) return std::nullopt;
    if (std::strcmp(name, "title") == 0) return AlbumsPage::SortMode::Title;
    if (std::strcmp(name, "artist") == 0) return AlbumsPage::SortMode::Artist;
    if (std::strcmp(name, "year") == 0) return AlbumsPage::SortMode::Year;
    if (std::strcmp(name, "recent") == 0) return AlbumsPage::SortMode::RecentlyAdded;
    return std::nullopt;
}

} // namespace

AlbumsPage::AlbumsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

AlbumsPage::~AlbumsPage() {
    if (sort_action_group_) {
        g_object_unref(sort_action_group_);
        sort_action_group_ = nullptr;
    }
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

    sort_button_ = gtk_menu_button_new();
    gtk_widget_add_css_class(sort_button_, "page-sort-button");
    gtk_widget_set_halign(sort_button_, GTK_ALIGN_END);
    gtk_widget_set_valign(sort_button_, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(sort_button_, "Choose how albums are sorted");
    gtk_widget_set_visible(sort_button_, TRUE);
    create_sort_menu();

    gtk_box_append(GTK_BOX(header), title_block);
    gtk_box_append(GTK_BOX(header), sort_button_);
    gtk_box_append(GTK_BOX(root_.get()), header);

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "albums-grid");
    // Six homogeneous, expanding columns; tiles fill their cells and stay
    // square via AppleAlbumTile's fit callback.
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), kMinColumns);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), kMaxColumns);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), kRowGap);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), kColumnGap);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_halign(grid_.get(), GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(grid_.get(), FALSE);
    gtk_widget_set_visible(grid_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_.get()), grid_.get());

    empty_state_ = std::make_unique<EmptyState>();
    empty_state_->setup("folder-music-symbolic", "No albums yet",
                        "Add music to your library to see albums here.");
    gtk_widget_set_visible(empty_state_->get_widget(), FALSE);
    gtk_box_append(GTK_BOX(root_.get()), empty_state_->get_widget());

    error_state_ = std::make_unique<ErrorState>();
    error_state_->setup("Albums unavailable",
                        "The library could not be loaded. Try again.",
                        [this] { refresh(); });
    gtk_widget_set_visible(error_state_->get_widget(), FALSE);
    gtk_box_append(GTK_BOX(root_.get()), error_state_->get_widget());
}

void AlbumsPage::refresh() {
    // Detach children before repopulating. The tile objects are kept when the
    // album count is unchanged so their artwork textures can be reused.
    safe_flow_box_remove_all(GTK_FLOW_BOX(grid_.get()));

    if (!library_service_) {
        album_tiles_.clear();
        update_header(0);
        gtk_widget_set_visible(grid_.get(), FALSE);
        gtk_widget_set_visible(empty_state_->get_widget(), FALSE);
        gtk_widget_set_visible(error_state_->get_widget(), TRUE);
        return;
    }

    std::vector<Album> albums = library_service_->get_all_albums();
    sorted_albums(albums);

    favourite_album_ids_.clear();
    for (const auto& album : albums) {
        if (album.favorite) favourite_album_ids_.insert(album.id);
    }

    if (album_tiles_.size() != albums.size()) album_tiles_.clear();

    update_header(albums.size());
    const bool has_albums = !albums.empty();
    gtk_widget_set_visible(grid_.get(), has_albums);
    gtk_widget_set_visible(empty_state_->get_widget(), !has_albums);
    gtk_widget_set_visible(error_state_->get_widget(), FALSE);
    if (!has_albums) return;

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

void AlbumsPage::create_sort_menu() {
    sort_action_group_ = g_simple_action_group_new();

    struct SortEntry {
        const char* name;
        const char* label;
        SortMode mode;
    };
    const SortEntry entries[] = {
        {"title", "Title", SortMode::Title},
        {"artist", "Artist", SortMode::Artist},
        {"year", "Year", SortMode::Year},
        {"recent", "Recently Added", SortMode::RecentlyAdded},
    };

    GMenu* menu = g_menu_new();
    for (const auto& entry : entries) {
        GSimpleAction* action = g_simple_action_new_stateful(
            entry.name, nullptr, g_variant_new_boolean(FALSE));
        g_signal_connect(action, "activate", G_CALLBACK(on_sort_action), this);
        g_action_map_add_action(G_ACTION_MAP(sort_action_group_),
                                G_ACTION(action));
        g_object_unref(action);

        const std::string action_name = std::string("albums-sort.") + entry.name;
        g_menu_append(menu, entry.label, action_name.c_str());
    }
    gtk_widget_insert_action_group(sort_button_, "albums-sort",
                                   G_ACTION_GROUP(sort_action_group_));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(sort_button_),
                                   G_MENU_MODEL(menu));
    g_object_unref(menu);

    update_sort_action_states();
}

void AlbumsPage::update_sort_action_states() {
    if (!sort_action_group_) return;
    const SortMode modes[] = {
        SortMode::Title, SortMode::Artist, SortMode::Year,
        SortMode::RecentlyAdded,
    };
    for (SortMode mode : modes) {
        GSimpleAction* action = G_SIMPLE_ACTION(g_action_map_lookup_action(
            G_ACTION_MAP(sort_action_group_), sort_action_name(mode)));
        if (action) {
            g_simple_action_set_state(action,
                                      g_variant_new_boolean(mode == sort_mode_));
        }
    }
    gtk_menu_button_set_label(GTK_MENU_BUTTON(sort_button_),
                              sort_button_label(sort_mode_));
}

void AlbumsPage::set_sort_mode(SortMode mode) {
    if (mode == sort_mode_) return;
    sort_mode_ = mode;
    update_sort_action_states();
    refresh();
}

void AlbumsPage::on_sort_action(GSimpleAction* action, GVariant*,
                                gpointer user_data) {
    const auto mode = sort_mode_from_action(g_action_get_name(G_ACTION(action)));
    if (mode) static_cast<AlbumsPage*>(user_data)->set_sort_mode(*mode);
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
            auto owned = std::make_unique<AppleAlbumTile>(album, kTileSize, true);
            tile = owned.get();
            tile->set_play_callback(
                [this](const Album& a) { on_album_played(a); });
            tile->set_click_callback(
                [this](const Album& a) { on_album_clicked(a); });
            tile->set_play_toggle_callback([this](const Album&) {
                if (!main_window_) return;
                if (auto* player = dynamic_cast<IPlayerService*>(
                        main_window_->get_backend())) {
                    player->toggle_play_pause();
                }
            });
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
                    playlist_dialogs::show_add_to_playlist_dialog(
                        main_window_->get_window(), library_service_,
                        a.track_ids);
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

    // Load the visible rows plus one row of look-ahead so a small scroll does
    // not flash the fallback artwork. Column count follows the FlowBox's real
    // allocation instead of assuming a fixed layout.
    const int columns = visible_columns();
    int row_height = kTileSize + kRowGap;
    const int first_height = gtk_widget_get_height(album_tiles_.front()->get_widget());
    if (first_height > 0) row_height = first_height + kRowGap;

    double value = 0.0;
    double page_size = 0.0;
    if (artwork_scroll_adjustment_) {
        value = gtk_adjustment_get_value(artwork_scroll_adjustment_);
        page_size = gtk_adjustment_get_page_size(artwork_scroll_adjustment_);
    }
    if (page_size <= 0.0) page_size = kTileSize * 2.0;

    const int first_row = std::max(0, static_cast<int>(std::floor(value / row_height)) - 1);
    const int rows = std::max(2, static_cast<int>(std::ceil(page_size / row_height)) + 2);
    const size_t first = static_cast<size_t>(first_row) * columns;
    const size_t last = std::min(album_tiles_.size(),
                                 static_cast<size_t>(first_row + rows) * columns);

    for (size_t i = 0; i < album_tiles_.size(); ++i) {
        album_tiles_[i]->set_artwork_visible(i >= first && i < last);
    }
}

int AlbumsPage::visible_columns() const {
    const int width = gtk_widget_get_width(grid_.get());
    if (width <= 0 || album_tiles_.empty()) return kPreferredColumns;

    const int child_width = gtk_widget_get_width(album_tiles_.front()->get_widget());
    const int step = child_width > 0 ? child_width + kColumnGap
                                     : kTileSize + kColumnGap;
    const int columns = (width + kColumnGap) / std::max(1, step);
    return std::clamp(columns, kMinColumns, kMaxColumns);
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
