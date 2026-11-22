/**
 * Lollypop C++ - Songs Page Implementation
 * Sortable multi-column song table using GtkListBox with TrackRow components.
 * Lazy loading with database-backed sorting via ILibraryService.
 */

#include "ui/pages/SongsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <mutex>

namespace lollypop {

struct SongsPageLoadData {
    SongsPage* page;
    std::vector<Track> tracks;
};

SongsPage::SongsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service),
      sort_column_("title"), sort_ascending_(true), genre_filter_(""), generation_(0),
      loading_(false), loaded_count_(0), total_count_(0) {
    setup_ui();
}

SongsPage::~SongsPage() = default;

void SongsPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main_box, TRUE);
    gtk_widget_set_vexpand(main_box, TRUE);

    // Column headers with sort buttons
    create_header(main_box);

    // Track list with scrolled window for scroll detection
    scrolled_list_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(scrolled_list_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_list_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_list_.get(), TRUE);
    gtk_widget_set_hexpand(scrolled_list_.get(), TRUE);
    gtk_widget_add_css_class(scrolled_list_.get(), "songs-list-scrolled");

    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "songs-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_list_.get()), list_box_.get());
    gtk_widget_set_vexpand(scrolled_list_.get(), TRUE);
    gtk_widget_set_hexpand(scrolled_list_.get(), TRUE);

    // Connect scroll to load more
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_list_.get()));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_scroll_changed), this);

    gtk_box_append(GTK_BOX(main_box), header_box_.get());
    gtk_box_append(GTK_BOX(main_box), scrolled_list_.get());

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), main_box);
}

void SongsPage::create_header(GtkWidget* main_box) {
    header_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    g_object_ref_sink(header_box_.get());
    gtk_widget_add_css_class(header_box_.get(), "songs-header");
    gtk_widget_set_margin_start(header_box_.get(), 12);
    gtk_widget_set_margin_end(header_box_.get(), 12);
    gtk_widget_set_margin_top(header_box_.get(), 8);
    gtk_widget_set_margin_bottom(header_box_.get(), 8);

    // Genre filter dropdown
    if (library_service_) {
        auto genres = library_service_->get_all_genres();
        if (!genres.empty()) {
            GtkStringList* string_list = gtk_string_list_new(nullptr);
            gtk_string_list_append(string_list, "All Genres");
            for (const auto& genre : genres) {
                gtk_string_list_append(string_list, genre.c_str());
            }

            GtkWidget* genre_dropdown = gtk_drop_down_new(G_LIST_MODEL(string_list), nullptr);
            gtk_widget_add_css_class(genre_dropdown, "songs-genre-filter");
            gtk_widget_set_tooltip_text(genre_dropdown, "Filter by genre");
            g_signal_connect(genre_dropdown, "notify::selected", G_CALLBACK(on_genre_changed), this);
            gtk_box_append(GTK_BOX(header_box_.get()), genre_dropdown);
        }
    }

    const struct {
        const char* title;
        const char* field;
    } columns[] = {
        {"#", "number"},
        {"Title", "title"},
        {"Artist", "artist"},
        {"Album", "album"},
        {"Duration", "duration"},
    };

    int col_widths[] = {24, -1, -1, -1, 40};

    for (int i = 0; i < 5; ++i) {
        GtkWidget* col_label = gtk_label_new(columns[i].title);
        gtk_widget_add_css_class(col_label, "songs-column-header");
        gtk_label_set_xalign(GTK_LABEL(col_label),
                             i == 4 ? 1.0 : 0.0);
        gtk_widget_set_size_request(col_label, col_widths[i] >= 0 ? col_widths[i] : -1, -1);
        if (i > 0) {
            GtkWidget* sort_btn = gtk_button_new();
            gtk_button_set_has_frame(GTK_BUTTON(sort_btn), FALSE);
            gtk_widget_set_hexpand(sort_btn, TRUE);
            gtk_widget_set_halign(sort_btn, GTK_ALIGN_FILL);
            gtk_button_set_child(GTK_BUTTON(sort_btn), col_label);
            g_object_set_data_full(G_OBJECT(sort_btn), "sort-field",
                                   g_strdup(columns[i].field), g_free);
            g_signal_connect(sort_btn, "clicked",
                             G_CALLBACK(on_sort_header_clicked), this);
            gtk_box_append(GTK_BOX(header_box_.get()), sort_btn);
        } else {
            gtk_box_append(GTK_BOX(header_box_.get()), col_label);
        }
    }

    gtk_box_append(GTK_BOX(main_box), header_box_.get());
}

void SongsPage::on_sort_header_clicked(GtkButton* button, gpointer user_data) {
    auto* self = static_cast<SongsPage*>(user_data);
    const char* field = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "sort-field"));
    if (!self || !field) return;

    if (self->sort_column_ == field) {
        self->sort_ascending_ = !self->sort_ascending_;
    } else {
        self->sort_column_ = field;
        self->sort_ascending_ = true;
    }
    self->reload();
}

void SongsPage::reload() {
    if (!library_service_) return;

    std::lock_guard lock(load_mutex_);
    generation_++;
    loaded_count_ = 0;
    total_count_ = 0;

    // Clear existing
    GtkWidget* child = gtk_widget_get_first_child(list_box_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }
    track_rows_.clear();

    // Get sorted track IDs with genre filter
    auto track_ids = library_service_->get_sorted_track_ids(sort_column_, sort_ascending_, genre_filter_);
    {
        std::lock_guard lock(ids_mutex_);
        sorted_ids_ = std::move(track_ids);
    }
    total_count_ = sorted_ids_.size();

    // Load first page
    load_page(0);
}

void SongsPage::load_page(size_t start) {
    std::lock_guard lock(load_mutex_);
    if (loading_ || start >= sorted_ids_.size()) return;
    loading_ = true;

    size_t end = std::min(start + PAGE_SIZE, sorted_ids_.size());
    std::vector<std::string> batch;
    {
        std::lock_guard lock(ids_mutex_);
        batch.assign(sorted_ids_.begin() + start, sorted_ids_.begin() + end);
    }

    auto tracks = library_service_->get_tracks_by_ids(batch);
    
    // Append to list box on main thread
    auto* load_data = new SongsPageLoadData{this, std::move(tracks)};
    g_idle_add([](gpointer data) -> gboolean {
        auto* ld = static_cast<SongsPageLoadData*>(data);
        ld->page->append_tracks(std::move(ld->tracks));
        delete ld;
        return G_SOURCE_REMOVE;
    }, load_data);
}

void SongsPage::append_tracks(std::vector<Track>&& tracks) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(loaded_count_ + i + 1));
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });

        GtkWidget* w = row->get_widget();
        gtk_list_box_append(GTK_LIST_BOX(list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
    loaded_count_ += tracks.size();
    loading_ = false;
}

void SongsPage::on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data) {
    auto* self = static_cast<SongsPage*>(user_data);
    double value = gtk_adjustment_get_value(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);

    // Load more when near bottom (within 200px)
    if (value + page_size >= upper - 200) {
        size_t next_start = self->loaded_count_;
        if (next_start < self->total_count_) {
            self->load_page(next_start);
        }
    }
}

void SongsPage::on_genre_changed(GtkDropDown* dropdown, gpointer user_data) {
    (void)dropdown;
    auto* self = static_cast<SongsPage*>(user_data);
    guint selected = gtk_drop_down_get_selected(dropdown);
    if (selected == GTK_INVALID_LIST_POSITION) return;
    
    const char* genre = gtk_string_list_get_string(GTK_STRING_LIST(gtk_drop_down_get_model(dropdown)), selected);
    if (genre) {
        if (std::string(genre) == "All Genres") {
            self->set_genre_filter("");
        } else {
            self->set_genre_filter(genre);
        }
    }
}

void SongsPage::set_genre_filter(const std::string& genre) {
    genre_filter_ = genre;
    reload();
}

void SongsPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void SongsPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

void SongsPage::refresh() {
    reload();
}

} // namespace lollypop