/**
 * Lollypop C++ - Artists Page Implementation
 *
 * Mirrors AlbumsPage: large title, count subtitle, sort menu, and a
 * responsive grid of square overlay-labeled tiles.
 */

#include "ui/pages/ArtistsPage.hpp"
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

const char* sort_button_label(ArtistsPage::SortMode mode) {
    switch (mode) {
        case ArtistsPage::SortMode::Name: return "Sort by: Name";
        case ArtistsPage::SortMode::RecentlyAdded: return "Sort by: Recently Added";
    }
    return "Sort by: Name";
}

const char* sort_action_name(ArtistsPage::SortMode mode) {
    switch (mode) {
        case ArtistsPage::SortMode::Name: return "name";
        case ArtistsPage::SortMode::RecentlyAdded: return "recent";
    }
    return "name";
}

std::optional<ArtistsPage::SortMode> sort_mode_from_action(const char* name) {
    if (!name) return std::nullopt;
    if (std::strcmp(name, "name") == 0) return ArtistsPage::SortMode::Name;
    if (std::strcmp(name, "recent") == 0) return ArtistsPage::SortMode::RecentlyAdded;
    return std::nullopt;
}

} // namespace

ArtistsPage::ArtistsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

ArtistsPage::~ArtistsPage() {
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

    GtkWidget* title_block = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(title_block, TRUE);
    gtk_widget_set_halign(title_block, GTK_ALIGN_FILL);
    gtk_widget_set_valign(title_block, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(title_block, TRUE);

    GtkWidget* title = gtk_label_new("Artists");
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
    gtk_widget_set_tooltip_text(sort_button_, "Choose how artists are sorted");
    gtk_widget_set_visible(sort_button_, TRUE);
    create_sort_menu();

    gtk_box_append(GTK_BOX(header), title_block);
    gtk_box_append(GTK_BOX(header), sort_button_);
    gtk_box_append(GTK_BOX(root_.get()), header);

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "artists-grid");
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

void ArtistsPage::create_sort_menu() {
    sort_action_group_ = g_simple_action_group_new();

    struct SortEntry {
        const char* name;
        const char* label;
        SortMode mode;
    };
    const SortEntry entries[] = {
        {"name", "Name", SortMode::Name},
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

        const std::string action_name = std::string("artists-sort.") + entry.name;
        g_menu_append(menu, entry.label, action_name.c_str());
    }
    gtk_widget_insert_action_group(sort_button_, "artists-sort",
                                   G_ACTION_GROUP(sort_action_group_));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(sort_button_),
                                   G_MENU_MODEL(menu));
    g_object_unref(menu);

    update_sort_action_states();
}

void ArtistsPage::update_sort_action_states() {
    if (!sort_action_group_) return;
    const SortMode modes[] = {SortMode::Name, SortMode::RecentlyAdded};
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

void ArtistsPage::set_sort_mode(SortMode mode) {
    if (mode == sort_mode_) return;
    sort_mode_ = mode;
    update_sort_action_states();
    refresh();
}

void ArtistsPage::on_sort_action(GSimpleAction* action, GVariant*,
                                 gpointer user_data) {
    const auto mode = sort_mode_from_action(g_action_get_name(G_ACTION(action)));
    if (mode) static_cast<ArtistsPage*>(user_data)->set_sort_mode(*mode);
}

void ArtistsPage::refresh() {
    safe_flow_box_remove_all(GTK_FLOW_BOX(grid_.get()));

    if (!library_service_) {
        artist_tiles_.clear();
        update_header(0);
        gtk_widget_set_visible(grid_.get(), FALSE);
        gtk_widget_set_visible(empty_state_->get_widget(), FALSE);
        gtk_widget_set_visible(error_state_->get_widget(), TRUE);
        return;
    }

    std::vector<Artist> artists = library_service_->get_all_artists();
    sorted_artists(artists);

    const bool has_artists = !artists.empty();
    update_header(artists.size());
    gtk_widget_set_visible(grid_.get(), has_artists);
    gtk_widget_set_visible(empty_state_->get_widget(), !has_artists);
    gtk_widget_set_visible(error_state_->get_widget(), FALSE);
    if (!has_artists) {
        artist_tiles_.clear();
        return;
    }

    populate(artists);
    connect_scroll_adjustment();
    update_visible_artwork();
}

void ArtistsPage::sorted_artists(std::vector<Artist>& artists) const {
    switch (sort_mode_) {
        case SortMode::Name:
            std::stable_sort(artists.begin(), artists.end(),
                             [](const Artist& a, const Artist& b) {
                                 return lowercase(a.name) < lowercase(b.name);
                             });
            break;
        case SortMode::RecentlyAdded:
            std::stable_sort(artists.begin(), artists.end(),
                             [](const Artist& a, const Artist& b) {
                                 return a.date_added > b.date_added;
                             });
            break;
    }
}

void ArtistsPage::populate(const std::vector<Artist>& artists) {
    const bool reuse_tiles = artist_tiles_.size() == artists.size();
    if (!reuse_tiles) artist_tiles_.clear();

    for (size_t i = 0; i < artists.size(); ++i) {
        const Artist& artist = artists[i];
        AppleArtistTile* tile = nullptr;

        if (reuse_tiles) {
            tile = artist_tiles_[i].get();
            tile->update_artist(artist, false);
        } else {
            auto owned = std::make_unique<AppleArtistTile>(artist, kTileSize);
            tile = owned.get();
            tile->set_click_callback(
                [this](const Artist& a) { on_artist_clicked(a); });
            tile->set_play_callback(
                [this](const Artist& a) { on_artist_played(a); });
            tile->set_play_toggle_callback([this](const Artist&) {
                if (!main_window_) return;
                if (auto* player = dynamic_cast<IPlayerService*>(
                        main_window_->get_backend())) {
                    player->toggle_play_pause();
                }
            });
            tile->set_artist_action_callback([this](const Artist& a, const char* action) {
                if (!main_window_) return;
                IPlayerService* backend = main_window_->get_backend();
                if (!backend) return;

                if (std::strcmp(action, "play") == 0) {
                    on_artist_played(a);
                } else if (std::strcmp(action, "shuffle") == 0) {
                    if (library_service_) {
                        auto tracks = library_service_->get_tracks_by_artist(a.id);
                        std::vector<std::string> track_ids;
                        for (const auto& track : tracks) track_ids.push_back(track.id);
                        std::random_device rd;
                        std::mt19937 g(rd());
                        std::shuffle(track_ids.begin(), track_ids.end(), g);
                        if (!track_ids.empty()) backend->play_tracks(track_ids, 0);
                    }
                } else if (std::strcmp(action, "add_to_queue") == 0) {
                    if (library_service_) {
                        auto tracks =
                            library_service_->get_tracks_by_artist(a.name);
                        for (const auto& track : tracks) {
                            backend->enqueue_track(track.id, false);
                        }
                    }
                } else if (std::strcmp(action, "add_to_playlist") == 0) {
                    if (library_service_) {
                        auto tracks =
                            library_service_->get_tracks_by_artist(a.name);
                        std::vector<std::string> track_ids;
                        for (const auto& track : tracks) {
                            track_ids.push_back(track.id);
                        }
                        playlist_dialogs::show_add_to_playlist_dialog(
                            main_window_->get_window(), library_service_,
                            track_ids);
                    }
                }
            });
            tile->set_artwork_visible(false);
            artist_tiles_.push_back(std::move(owned));
        }

        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), tile->get_widget());
    }
}

void ArtistsPage::update_header(size_t artist_count) {
    if (!subtitle_label_) return;
    char text[64];
    snprintf(text, sizeof(text), "%zu artist%s", artist_count,
             artist_count == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(subtitle_label_), text);
}

void ArtistsPage::connect_scroll_adjustment() {
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

void ArtistsPage::on_scroll_changed(GtkAdjustment*, gpointer user_data) {
    static_cast<ArtistsPage*>(user_data)->update_visible_artwork();
}

void ArtistsPage::update_visible_artwork() {
    if (artist_tiles_.empty()) return;

    const int columns = visible_columns();
    int row_height = kTileSize + kRowGap;
    const int first_height = gtk_widget_get_height(artist_tiles_.front()->get_widget());
    if (first_height > 0) row_height = first_height + kRowGap;

    double value = 0.0;
    double page_size = 0.0;
    if (artwork_scroll_adjustment_) {
        value = gtk_adjustment_get_value(artwork_scroll_adjustment_);
        page_size = gtk_adjustment_get_page_size(artwork_scroll_adjustment_);
    }
    if (page_size <= 0.0) page_size = kTileSize * 2.0;

    const int first_row =
        std::max(0, static_cast<int>(std::floor(value / row_height)) - 1);
    const int rows =
        std::max(2, static_cast<int>(std::ceil(page_size / row_height)) + 2);
    const size_t column_count = static_cast<size_t>(columns);
    const size_t first = static_cast<size_t>(first_row) * column_count;
    const size_t last =
        std::min(artist_tiles_.size(),
                 static_cast<size_t>(first_row + rows) * column_count);

    for (size_t i = 0; i < artist_tiles_.size(); ++i) {
        artist_tiles_[i]->set_artwork_visible(i >= first && i < last);
    }
}

int ArtistsPage::visible_columns() const {
    const int width = gtk_widget_get_width(grid_.get());
    if (width <= 0 || artist_tiles_.empty()) return kPreferredColumns;

    const int child_width =
        gtk_widget_get_width(artist_tiles_.front()->get_widget());
    const int step = child_width > 0 ? child_width + kColumnGap
                                     : kTileSize + kColumnGap;
    const int columns = (width + kColumnGap) / std::max(1, step);
    return std::clamp(columns, kMinColumns, kMaxColumns);
}

void ArtistsPage::on_artist_played(const Artist& artist) {
    if (!library_service_) return;
    auto tracks = library_service_->get_tracks_by_artist(artist.id);
    std::vector<std::string> track_ids;
    for (const auto& track : tracks) track_ids.push_back(track.id);
    if (track_ids.empty()) return;

    if (auto* backend = main_window_ ? main_window_->get_backend() : nullptr) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_tracks(track_ids, 0);
        }
    }
}

void ArtistsPage::on_artist_clicked(const Artist& artist) {
    std::cout << "[ArtistsPage] Open artist detail: " << artist.name << std::endl;
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) middle->open_artist_detail(artist.id);
    }
}

} // namespace lollypop
