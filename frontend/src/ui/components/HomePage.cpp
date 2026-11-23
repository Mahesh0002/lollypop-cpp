/**
 * Lollypop C++ - Home Page Implementation
 * Dashboard showing recently added, recently played, favourite albums.
 * No nested scroller - content flows in middle panel's single scroller.
 */

#include "ui/components/HomePage.hpp"
#include "main_window.hpp"
#include "ui/components/AlbumCard.hpp"
#include "ui/components/TrackRow.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"
#include "ui/animation/SpringAnimation.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

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
    gtk_widget_set_margin_top(main_box_.get(), 0);
    gtk_widget_set_margin_bottom(main_box_.get(), 24);
    gtk_widget_set_margin_start(main_box_.get(), 24);
    gtk_widget_set_margin_end(main_box_.get(), 24);
    gtk_widget_set_hexpand(main_box_.get(), TRUE);
    gtk_widget_set_vexpand(main_box_.get(), FALSE);
    gtk_widget_set_visible(main_box_.get(), TRUE);

    create_welcome_section();

    // Recently Added
    create_section("Recently Added",
                   [this]() { return library_service_ ? library_service_->get_recently_added(8) : std::vector<Track>{}; },
                   [this](const Track& t) { on_track_played(t); },
                   [this](const Track& t, bool liked) { on_track_liked(t, liked); },
                   [this]() { if (recently_added_cb_) recently_added_cb_(); });

    // Recently Played
    create_section("Recently Played",
                   [this]() { return library_service_ ? library_service_->get_recently_played(8) : std::vector<Track>{}; },
                   [this](const Track& t) { on_track_played(t); },
                   [this](const Track& t, bool liked) { on_track_liked(t, liked); },
                   [this]() { if (recently_played_cb_) recently_played_cb_(); });

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

    GtkWidget* view_all = gtk_button_new_with_label("View all");
    gtk_button_set_has_frame(GTK_BUTTON(view_all), FALSE);
    gtk_widget_add_css_class(view_all, "section-view-all");
    gtk_widget_set_margin_start(view_all, 10);
    gtk_widget_set_margin_end(view_all, 10);
    gtk_widget_set_margin_top(view_all, 10);
    gtk_widget_set_margin_bottom(view_all, 10);
    if (view_all_cb) {
        g_signal_connect(view_all, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto cb = static_cast<std::function<void()>*>(data);
            if (cb && *cb) (*cb)();
        }), new std::function<void()>(view_all_cb));
    }
    gtk_widget_set_visible(view_all, TRUE);
    gtk_box_append(GTK_BOX(header), view_all);

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
    data->view_all_button = view_all;
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
                                          std::function<void()> view_all_cb) {
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

    GtkWidget* view_all = gtk_button_new_with_label("View all");
    gtk_button_set_has_frame(GTK_BUTTON(view_all), FALSE);
    gtk_widget_add_css_class(view_all, "section-view-all");
    gtk_widget_set_margin_start(view_all, 10);
    gtk_widget_set_margin_end(view_all, 10);
    gtk_widget_set_margin_top(view_all, 10);
    gtk_widget_set_margin_bottom(view_all, 10);
    if (view_all_cb) {
        g_signal_connect(view_all, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto cb = static_cast<std::function<void()>*>(data);
            if (cb && *cb) (*cb)();
        }), new std::function<void()>(view_all_cb));
    }
    gtk_widget_set_visible(view_all, TRUE);
    gtk_box_append(GTK_BOX(header), view_all);

    gtk_box_append(GTK_BOX(section), header);

    GtkWidget* grid = gtk_flow_box_new();
    g_object_ref_sink(grid);
    gtk_widget_add_css_class(grid, "home-album-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 16);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), 16);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_visible(grid, TRUE);

    auto data = std::make_unique<AlbumSectionData>();
    data->title = title;
    data->fetcher = std::move(fetcher);
    data->play_cb = std::move(play_cb);
    data->click_cb = std::move(click_cb);
    data->grid = grid;
    data->view_all_button = view_all;
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

gboolean HomePage::on_track_anim_timeout(gpointer data) {
    auto* d = static_cast<TrackAnimData*>(data);
    SpringAnimation::animate_to(d->widget, "opacity", 1.0);
    SpringAnimation::animate_to(d->widget, "margin-top", 0);
    return G_SOURCE_REMOVE;
}

void HomePage::refresh_track_section(SectionData* data) {
    if (!data || !data->list_box) return;

    // Cancel any pending animations - destroy notify frees payloads
    for (guint id : data->anim_timeout_ids) {
        if (g_main_context_find_source_by_id(nullptr, id)) {
            g_source_remove(id);
        }
    }
    data->anim_timeout_ids.clear();

    // Clear existing
    GtkWidget* child = gtk_widget_get_first_child(data->list_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(data->list_box), child);
        child = next;
    }
    data->track_rows.clear();

    // Fetch and populate with staggered spring animations
    auto tracks = data->fetcher();

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(i + 1));
        row->set_play_callback(data->play_cb);
        row->set_like_callback(data->like_cb);

        GtkWidget* w = row->get_widget();
        // Start invisible for stagger animation
        gtk_widget_set_opacity(w, 0.0);
        gtk_widget_set_margin_top(w, 20);

        safe_list_box_append(GTK_LIST_BOX(data->list_box), w);
        data->track_rows.push_back(std::move(row));

        // Staggered spring animation with destroy notify for leak-free cancellation
        double delay = i * 0.05; // 50ms stagger
        auto* anim_data = new TrackAnimData{w};
        guint timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
            static_cast<guint>(delay * 1000),
            on_track_anim_timeout,
            anim_data,
            [](gpointer data) { delete static_cast<TrackAnimData*>(data); });
        data->anim_timeout_ids.push_back(timeout_id);
    }
}

gboolean HomePage::on_album_anim_timeout(gpointer data) {
    auto* d = static_cast<HomePage::AlbumAnimData*>(data);
    SpringAnimation::animate_to(d->widget, "opacity", 1.0);
    return G_SOURCE_REMOVE;
}

void HomePage::refresh_album_section(AlbumSectionData* data) {
    if (!data || !data->grid) return;

    // Cancel any pending animations - destroy notify frees payloads
    for (guint id : data->anim_timeout_ids) {
        if (g_main_context_find_source_by_id(nullptr, id)) {
            g_source_remove(id);
        }
    }
    data->anim_timeout_ids.clear();

    // Clear existing
    GtkWidget* child = gtk_widget_get_first_child(data->grid);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(data->grid), child);
        child = next;
    }
    data->album_cards.clear();

    // Fetch and populate with staggered spring animations
    auto albums = data->fetcher();

    for (size_t i = 0; i < albums.size(); ++i) {
        auto card = std::make_unique<AlbumCard>(main_window_, albums[i]);
        card->set_play_callback(data->play_cb);
        card->set_click_callback(data->click_cb);

        GtkWidget* w = card->get_widget();
        // Start invisible for stagger animation
        gtk_widget_set_opacity(w, 0.0);

        safe_flow_box_append(GTK_FLOW_BOX(data->grid), w);
        data->album_cards.push_back(std::move(card));

        // Staggered spring animation with destroy notify for leak-free cancellation
        double delay = i * 0.05; // 50ms stagger
        auto* anim_data = new AlbumAnimData{w};
        guint timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
            static_cast<guint>(delay * 1000),
            on_album_anim_timeout,
            anim_data,
            [](gpointer data) { delete static_cast<HomePage::AlbumAnimData*>(data); });
        data->anim_timeout_ids.push_back(timeout_id);
    }
}

void HomePage::create_welcome_section() {
    GtkWidget* welcome = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(welcome, "home-welcome");
    gtk_widget_set_margin_bottom(welcome, 16);
    gtk_widget_set_visible(welcome, TRUE);

    // No greeting - just a subtle prompt
    GtkWidget* subtitle = gtk_label_new("What would you like to listen to?");
    gtk_widget_add_css_class(subtitle, "home-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
    gtk_widget_set_visible(subtitle, TRUE);

    if (gtk_widget_get_parent(subtitle) == nullptr) {
        gtk_box_append(GTK_BOX(welcome), subtitle);
    }
    if (gtk_widget_get_parent(welcome) == nullptr) {
        gtk_box_append(GTK_BOX(main_box_.get()), welcome);
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
    if (!sections_data_.empty()) {
        sections_data_[0]->view_all_cb = std::move(cb);
    }
}

void HomePage::set_recently_played_callback(std::function<void()> cb) {
    if (sections_data_.size() > 1) {
        sections_data_[1]->view_all_cb = std::move(cb);
    }
}

void HomePage::set_favourite_albums_callback(std::function<void()> cb) {
    if (!album_sections_data_.empty()) {
        album_sections_data_[0]->view_all_cb = std::move(cb);
    }
}

} // namespace lollypop