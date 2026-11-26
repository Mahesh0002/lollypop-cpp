/**
 * Lollypop C++ - Favourite Music Page Implementation
 */

#include "ui/pages/LikedMusicPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/PlaylistDialogs.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace lollypop {

namespace {

constexpr int kFavouriteArtworkSize = 500;
constexpr double kFavouriteArtworkRadius = 12.0;

std::string format_updated_label(int64_t latest_date_added) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (latest_date_added <= 0 || now < latest_date_added) {
        return "Updated just now";
    }

    const int64_t minutes = (now - latest_date_added) / 60;
    if (minutes < 1) return "Updated just now";
    if (minutes < 60) {
        return "Updated " + std::to_string(minutes) + "min ago";
    }

    const int64_t hours = minutes / 60;
    if (hours < 24) {
        return "Updated " + std::to_string(hours) + "h ago";
    }

    return "Updated " + std::to_string(hours / 24) + "d ago";
}

} // namespace

LikedMusicPage::LikedMusicPage(MainWindow* main_window, ILibraryService* library_service)
    : host_("emblem-favorite-symbolic", "No favourite music yet",
            "Songs you like will appear here.",
            "Favourite music unavailable",
            "The library could not be loaded. Try again.",
            [this] { refresh(); }),
      main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

LikedMusicPage::~LikedMusicPage() {
    if (menu_action_group_) {
        g_object_unref(menu_action_group_);
        menu_action_group_ = nullptr;
    }
}

void LikedMusicPage::setup_ui() {
    root_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 24));
    g_object_ref_sink(root_.get());
    gtk_widget_add_css_class(root_.get(), "favourite-music-page");
    gtk_widget_set_hexpand(root_.get(), TRUE);
    gtk_widget_set_vexpand(root_.get(), TRUE);
    gtk_widget_set_margin_top(root_.get(), 50);
    gtk_widget_set_margin_bottom(root_.get(), 24);
    gtk_widget_set_margin_start(root_.get(), 24);
    gtk_widget_set_margin_end(root_.get(), 24);
    gtk_widget_set_visible(root_.get(), TRUE);

    // Header: 500x500 artwork on the left; title, freshness, and actions on
    // the right. The metadata block centers vertically while the actions stay
    // pinned to the bottom of the same height, mirroring AlbumDetailPage.
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(header, "favourite-music-header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_widget_set_vexpand(header, FALSE);
    gtk_widget_set_valign(header, GTK_ALIGN_START);
    gtk_widget_set_size_request(header, -1, kFavouriteArtworkSize);
    gtk_widget_set_visible(header, TRUE);
    safe_box_append(GTK_BOX(root_.get()), header);

    GtkWidget* artwork_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(artwork_wrapper, "favourite-music-artwork");
    gtk_widget_set_size_request(artwork_wrapper, kFavouriteArtworkSize,
                                kFavouriteArtworkSize);
    gtk_widget_set_overflow(artwork_wrapper, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_hexpand(artwork_wrapper, FALSE);
    gtk_widget_set_vexpand(artwork_wrapper, FALSE);
    gtk_widget_set_halign(artwork_wrapper, GTK_ALIGN_START);
    gtk_widget_set_valign(artwork_wrapper, GTK_ALIGN_START);
    gtk_widget_set_visible(artwork_wrapper, TRUE);
    safe_box_append(GTK_BOX(header), artwork_wrapper);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(kFavouriteArtworkSize,
                                      kFavouriteArtworkSize);
    artwork_view_->set_corner_radius(kFavouriteArtworkRadius);
    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "favourite-music-artwork-image");
    gtk_widget_set_visible(artwork, TRUE);
    safe_box_append(GTK_BOX(artwork_wrapper), artwork);

    GtkWidget* data_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(data_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(data_box, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(data_box, TRUE);
    gtk_widget_set_vexpand(data_box, FALSE);
    gtk_widget_set_size_request(data_box, -1, kFavouriteArtworkSize);
    gtk_widget_set_visible(data_box, TRUE);
    safe_box_append(GTK_BOX(header), data_box);

    GtkWidget* metadata_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(metadata_box, GTK_ALIGN_START);
    gtk_widget_set_valign(metadata_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(metadata_box, TRUE);
    gtk_widget_set_vexpand(metadata_box, TRUE);
    gtk_widget_set_visible(metadata_box, TRUE);
    safe_box_append(GTK_BOX(data_box), metadata_box);

    title_label_.reset(gtk_label_new("Favourite Music"));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "favourite-music-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(title_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), title_label_.get());

    updated_label_.reset(gtk_label_new("Updated just now"));
    g_object_ref_sink(updated_label_.get());
    gtk_widget_add_css_class(updated_label_.get(), "favourite-music-updated");
    gtk_label_set_xalign(GTK_LABEL(updated_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(updated_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(updated_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(updated_label_.get(), TRUE);
    safe_box_append(GTK_BOX(metadata_box), updated_label_.get());

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(actions, "favourite-music-actions");
    gtk_widget_set_halign(actions, GTK_ALIGN_FILL);
    gtk_widget_set_valign(actions, GTK_ALIGN_END);
    gtk_widget_set_hexpand(actions, TRUE);
    gtk_widget_set_visible(actions, TRUE);
    safe_box_append(GTK_BOX(data_box), actions);

    // Nested homogeneous row keeps Play and Shuffle equal-width without
    // dragging the trailing menu spacer into the equal-width calculation.
    GtkWidget* action_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_set_homogeneous(GTK_BOX(action_buttons), TRUE);
    gtk_widget_set_halign(action_buttons, GTK_ALIGN_START);
    gtk_widget_set_visible(action_buttons, TRUE);
    safe_box_append(GTK_BOX(actions), action_buttons);

    GtkWidget* play_button = gtk_button_new_with_label("Play");
    gtk_widget_add_css_class(play_button, "favourite-music-action-button");
    gtk_widget_add_css_class(play_button, "favourite-music-play-button");
    if (ThemeManager* theme =
            main_window_ ? main_window_->get_theme_manager() : nullptr) {
        theme->apply_detail_action_style(play_button);
    }
    g_signal_connect(play_button, "clicked",
                     G_CALLBACK(on_play_clicked), this);
    gtk_widget_set_visible(play_button, TRUE);
    safe_box_append(GTK_BOX(action_buttons), play_button);

    GtkWidget* shuffle_button = gtk_button_new_with_label("Shuffle");
    gtk_widget_add_css_class(shuffle_button, "favourite-music-action-button");
    if (ThemeManager* theme =
            main_window_ ? main_window_->get_theme_manager() : nullptr) {
        theme->apply_detail_action_style(shuffle_button);
    }
    g_signal_connect(shuffle_button, "clicked",
                     G_CALLBACK(on_shuffle_clicked), this);
    gtk_widget_set_visible(shuffle_button, TRUE);
    safe_box_append(GTK_BOX(action_buttons), shuffle_button);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_widget_set_visible(spacer, TRUE);
    safe_box_append(GTK_BOX(actions), spacer);

    GtkWidget* menu_button = gtk_menu_button_new();
    gtk_widget_add_css_class(menu_button, "favourite-music-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic");
    gtk_button_set_child(GTK_BUTTON(menu_button), menu_icon);
    gtk_widget_set_tooltip_text(menu_button, "More options");
    gtk_widget_set_halign(menu_button, GTK_ALIGN_END);
    gtk_widget_set_valign(menu_button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(menu_button, TRUE);
    safe_box_append(GTK_BOX(actions), menu_button);
    create_menu(menu_button);

    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "favourite-music-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_vexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);
    safe_box_append(GTK_BOX(root_.get()), list_box_.get());

    host_.set_content(root_.get());
}

void LikedMusicPage::create_menu(GtkWidget* button) {
    menu_action_group_ = g_simple_action_group_new();

    struct MenuEntry {
        const char* name;
        const char* label;
    };
    const MenuEntry entries[] = {
        {"play", "Play"},
        {"shuffle", "Shuffle"},
        {"add-to-queue", "Add to Queue"},
        {"add-to-playlist", "Add to Playlist..."},
    };

    GMenu* menu = g_menu_new();
    for (const auto& entry : entries) {
        GSimpleAction* action = g_simple_action_new(entry.name, nullptr);
        g_signal_connect(action, "activate", G_CALLBACK(on_menu_action), this);
        g_action_map_add_action(G_ACTION_MAP(menu_action_group_),
                                G_ACTION(action));
        g_object_unref(action);

        const std::string action_name =
            std::string("favourite-music.") + entry.name;
        g_menu_append(menu, entry.label, action_name.c_str());
    }

    gtk_widget_insert_action_group(button, "favourite-music",
                                   G_ACTION_GROUP(menu_action_group_));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
}

void LikedMusicPage::refresh() {
    palette_artwork_path_.clear();
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(list_box_.get()));
    track_rows_.clear();
    current_track_ids_.clear();

    if (!library_service_) {
        host_.show_error();
        return;
    }

    const auto tracks = library_service_->get_liked_tracks();
    update_header(tracks);
    if (tracks.empty()) {
        host_.show_empty();
        return;
    }

    host_.show_content();
    populate(tracks);
}

void LikedMusicPage::update_header(const std::vector<Track>& tracks) {
    std::string artwork_path;
    int64_t latest_date_added = 0;
    for (const auto& track : tracks) {
        if (artwork_path.empty() && !track.artwork_path.empty()) {
            artwork_path = track.artwork_path;
        }
        if (track.date_added > latest_date_added) {
            latest_date_added = track.date_added;
        }
    }

    palette_artwork_path_ = artwork_path;
    if (artwork_view_) artwork_view_->set_artwork_path(artwork_path);
    if (updated_label_) {
        gtk_label_set_text(GTK_LABEL(updated_label_.get()),
                           format_updated_label(latest_date_added).c_str());
    }
}

void LikedMusicPage::populate(const std::vector<Track>& tracks) {
    current_track_ids_.reserve(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1));
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });
        row->set_liked(true);

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(list_box_.get()), w);
        track_rows_.push_back(std::move(row));
        current_track_ids_.push_back(tracks[i].id);
    }
}

void LikedMusicPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void LikedMusicPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
    // The backend emits library-changed for like toggles; MainWindow coalesces
    // that into a deferred refresh of this page, which removes the row.
}

void LikedMusicPage::on_play_all() {
    if (!main_window_ || current_track_ids_.empty()) return;
    if (auto* player = dynamic_cast<IPlayerService*>(main_window_->get_backend())) {
        player->play_tracks(current_track_ids_, 0);
    }
}

void LikedMusicPage::on_shuffle_all() {
    if (!main_window_ || current_track_ids_.empty()) return;
    if (auto* player = dynamic_cast<IPlayerService*>(main_window_->get_backend())) {
        std::vector<std::string> shuffled = current_track_ids_;
        std::mt19937_64 rng(std::random_device{}());
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        player->play_tracks(shuffled, 0);
    }
}

void LikedMusicPage::on_add_to_queue() {
    if (!main_window_) return;
    if (auto* player = dynamic_cast<IPlayerService*>(main_window_->get_backend())) {
        for (const auto& id : current_track_ids_) {
            player->enqueue_track(id, false);
        }
    }
}

void LikedMusicPage::on_add_to_playlist() {
    if (!main_window_ || !library_service_) return;
    playlist_dialogs::show_add_to_playlist_dialog(
        main_window_->get_window(), library_service_, current_track_ids_);
}

void LikedMusicPage::on_play_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    static_cast<LikedMusicPage*>(user_data)->on_play_all();
}

void LikedMusicPage::on_shuffle_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    static_cast<LikedMusicPage*>(user_data)->on_shuffle_all();
}

void LikedMusicPage::on_menu_action(GSimpleAction* action, GVariant* parameter,
                                    gpointer user_data) {
    (void)parameter;
    auto* self = static_cast<LikedMusicPage*>(user_data);
    if (!self) return;

    const gchar* name = g_action_get_name(G_ACTION(action));
    if (g_strcmp0(name, "play") == 0) {
        self->on_play_all();
    } else if (g_strcmp0(name, "shuffle") == 0) {
        self->on_shuffle_all();
    } else if (g_strcmp0(name, "add-to-queue") == 0) {
        self->on_add_to_queue();
    } else if (g_strcmp0(name, "add-to-playlist") == 0) {
        self->on_add_to_playlist();
    }
}

} // namespace lollypop
