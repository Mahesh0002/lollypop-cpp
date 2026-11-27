/**
 * Lollypop C++ - Songs Page Implementation
 * Sortable multi-column song table using GtkListBox with TrackRow components.
 * Lazy loading with database-backed sorting via ILibraryService.
 */

#include "ui/pages/SongsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/components/TrackListMetrics.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <iostream>
#include <mutex>

namespace lollypop {

struct SongsPageLoadData {
    SongsPage* page;
    GObject* page_widget;  // keeps the widget alive until the idle callback runs
    std::vector<Track> tracks;
    uint64_t generation = 0;
};

static void free_load_data(gpointer data) {
    auto* load_data = static_cast<SongsPageLoadData*>(data);
    if (load_data->page_widget) g_object_unref(load_data->page_widget);
    delete load_data;
}

SongsPage::SongsPage(MainWindow* main_window, ILibraryService* library_service)
    : host_("folder-music-symbolic", "No songs found",
            "Try a different genre filter or add music to your library.",
            "Songs unavailable",
            "The library could not be loaded. Try again.",
            [this] { refresh(); }),
      main_window_(main_window), library_service_(library_service),
      sort_column_("title"), sort_ascending_(true), genre_filter_(""), generation_(0),
      loading_(false), loaded_count_(0), total_count_(0) {
    setup_ui();
}

SongsPage::~SongsPage() {
    if (widget_) {
        g_object_set_data(G_OBJECT(widget_.get()), "songs-page-owner", nullptr);
    }
}

void SongsPage::setup_ui() {
    // Root widget is a Box containing header + ListBox (both implement GtkScrollable)
    // MiddleLibrary provides the single shared scrolled window
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main_box, TRUE);
    gtk_widget_set_vexpand(main_box, TRUE);
    gtk_widget_set_visible(main_box, TRUE);
    g_object_set_data(G_OBJECT(main_box), "songs-page-owner", this);

    create_header(main_box);

    // Track list - ListBox directly (implements GtkScrollable)
    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "songs-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_vexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);

    // Scroll-to-load is wired by MiddleLibrary through the shared adjustment.

    safe_box_append(GTK_BOX(main_box), header_box_.get());
    safe_box_append(GTK_BOX(main_box), list_box_.get());

    widget_.reset(main_box);
    g_object_ref_sink(widget_.get());
    host_.set_content(widget_.get());
}

void SongsPage::create_header(GtkWidget* main_box) {
    // The genre filter scrolls with the list; only the sort-label row is
    // pinned by MiddleLibrary. Keeping them separate keeps the sticky bar as
    // tall as its sort buttons instead of stacking a 35px filter above them.
    filter_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    g_object_ref_sink(filter_box_.get());
    gtk_widget_add_css_class(filter_box_.get(), "songs-filter-row");
    gtk_widget_set_margin_start(filter_box_.get(),
                                track_list_metrics::kSideMargin);
    gtk_widget_set_margin_end(filter_box_.get(),
                              track_list_metrics::kSideMargin);
    gtk_widget_set_margin_top(filter_box_.get(), 8);
    gtk_widget_set_margin_bottom(filter_box_.get(), 4);
    gtk_widget_set_hexpand(filter_box_.get(), TRUE);
    gtk_widget_set_visible(filter_box_.get(), TRUE);
    safe_box_append(GTK_BOX(main_box), filter_box_.get());

    header_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(header_box_.get());
    gtk_widget_add_css_class(header_box_.get(), "songs-header");
    gtk_widget_set_margin_start(header_box_.get(),
                                track_list_metrics::kSideMargin);
    gtk_widget_set_margin_end(header_box_.get(),
                              track_list_metrics::kSideMargin);
    gtk_widget_set_margin_top(header_box_.get(), 0);
    gtk_widget_set_margin_bottom(header_box_.get(), 0);
    gtk_widget_set_hexpand(header_box_.get(), TRUE);

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
            gtk_widget_set_halign(genre_dropdown, GTK_ALIGN_START);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(genre_dropdown), 0);
            genre_dropdown_ = genre_dropdown;
            genre_handler_id_ = g_signal_connect(genre_dropdown, "notify::selected",
                                                 G_CALLBACK(on_genre_changed), this);
            gtk_box_append(GTK_BOX(filter_box_.get()), genre_dropdown);
        }
    }

    GtkWidget* columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,
                                     track_list_metrics::kColumnGap);
    gtk_widget_set_layout_manager(columns, create_track_columns_layout());
    gtk_widget_add_css_class(columns, "songs-columns");
    gtk_widget_set_hexpand(columns, TRUE);
    gtk_widget_set_vexpand(columns, FALSE);
    gtk_widget_set_valign(columns, GTK_ALIGN_START);
    gtk_widget_set_visible(columns, TRUE);

    const struct {
        const char* title;
        const char* field;
        TrackColumnRole role;
        bool use_star_icon;
    } columns_info[] = {
        {"",         "number",   TrackColumnRole::Fixed,     false},
        {"Title",    "title",    TrackColumnRole::Title,     false},
        {"Artist",   "artist",   TrackColumnRole::Artist,    false},
        {"Album",    "album",    TrackColumnRole::Album,     false},
        {"Year",     "year",     TrackColumnRole::Year,       false},
        {"Genre",    "genre",    TrackColumnRole::Genre,      false},
        {"Duration", "duration", TrackColumnRole::Duration,   false},
        {"",         "liked",    TrackColumnRole::Favourite,  true},
    };
    constexpr size_t column_count =
        sizeof(columns_info) / sizeof(columns_info[0]);

    for (size_t i = 0; i < column_count; ++i) {
        const auto& info = columns_info[i];
        GtkWidget* col_widget = nullptr;
        if (info.use_star_icon) {
            col_widget = gtk_image_new_from_icon_name("starred-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(col_widget),
                                     track_list_metrics::kLikeIconSize);
        } else {
            col_widget = gtk_label_new(info.title);
            gtk_label_set_xalign(GTK_LABEL(col_widget), 0.0);
        }
        gtk_widget_add_css_class(col_widget, "songs-column-header");
        if (i == 0) {
            gtk_widget_set_size_request(col_widget,
                                        track_list_metrics::kArtworkSize, -1);
        } else if (i == 4) {
            gtk_widget_set_size_request(col_widget,
                                        track_list_metrics::kYearWidth, -1);
        } else if (i == 5) {
            gtk_widget_set_size_request(col_widget,
                                        track_list_metrics::kGenreWidth, -1);
        } else if (i == 6) {
            gtk_widget_set_size_request(col_widget,
                                        track_list_metrics::kDurationWidth, -1);
        }

        if (i > 0) {
            GtkWidget* sort_btn = gtk_button_new();
            gtk_button_set_has_frame(GTK_BUTTON(sort_btn), FALSE);
            gtk_widget_set_hexpand(sort_btn, FALSE);
            gtk_widget_set_halign(sort_btn, GTK_ALIGN_FILL);
            gtk_widget_set_valign(sort_btn, GTK_ALIGN_CENTER);
            gtk_button_set_child(GTK_BUTTON(sort_btn), col_widget);
            if (info.use_star_icon) {
                gtk_widget_set_size_request(
                    sort_btn, track_list_metrics::kLikeColumnWidth, -1);
                gtk_widget_set_tooltip_text(sort_btn, "Favourite");
            }
            set_track_column_role(sort_btn, info.role);
            g_object_set_data_full(G_OBJECT(sort_btn), "sort-field",
                                   g_strdup(info.field), g_free);
            g_signal_connect(sort_btn, "clicked",
                             G_CALLBACK(on_sort_header_clicked), this);
            gtk_box_append(GTK_BOX(columns), sort_btn);
        } else {
            set_track_column_role(col_widget, info.role);
            gtk_box_append(GTK_BOX(columns), col_widget);
        }
    }

    safe_box_append(GTK_BOX(header_box_.get()), columns);
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
    if (!library_service_) {
        host_.show_error();
        return;
    }

    std::lock_guard lock(load_mutex_);
    generation_++;
    loading_ = false;
    loaded_count_ = 0;
    total_count_ = 0;

    // Clear existing - use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(list_box_.get()));
    track_rows_.clear();

    // Get sorted track IDs with genre filter
    auto track_ids = library_service_->get_sorted_track_ids(sort_column_, sort_ascending_, genre_filter_);
    {
        std::lock_guard ids_lock(ids_mutex_);
        sorted_ids_ = std::move(track_ids);
        total_count_ = sorted_ids_.size();
    }

    if (total_count_ == 0) {
        host_.show_empty();
        return;
    }

    host_.show_content();
    // Load first page
    load_page(0);
}

void SongsPage::load_page(size_t start) {
    std::lock_guard lock(load_mutex_);
    if (loading_ || start >= sorted_ids_.size()) return;
    loading_ = true;
    const uint64_t generation = generation_;

    size_t end = std::min(start + PAGE_SIZE, sorted_ids_.size());
    std::vector<std::string> batch;
    {
        std::lock_guard ids_lock(ids_mutex_);
        batch.assign(sorted_ids_.begin() + start, sorted_ids_.begin() + end);
    }

    auto tracks = library_service_->get_tracks_by_ids(batch);
    
    // Append on the main thread. The GObject ref keeps the widget alive; the
    // owner marker and generation guard drop results from a superseded load.
    auto* load_data = new SongsPageLoadData{
        this, G_OBJECT(g_object_ref(widget_.get())), std::move(tracks), generation};
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer data) -> gboolean {
        auto* ld = static_cast<SongsPageLoadData*>(data);
        if (g_object_get_data(ld->page_widget, "songs-page-owner") == ld->page &&
            ld->generation == ld->page->generation_) {
            ld->page->append_tracks(std::move(ld->tracks));
        }
        return G_SOURCE_REMOVE;
    }, load_data, free_load_data);
}

void SongsPage::append_tracks(std::vector<Track>&& tracks) {
    TrackRowOptions options;
    options.show_year = true;
    options.show_genre = true;

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(loaded_count_ + i + 1),
                                              options);
        row->set_play_callback(
            [this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
    loaded_count_ += tracks.size();
    loading_ = false;
}

void SongsPage::handle_scroll(GtkAdjustment* adjustment) {
    on_scroll_changed(adjustment, this);
}

// This will be connected to the parent scrolled window's adjustment by MiddleLibrary
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
    auto* self = static_cast<SongsPage*>(user_data);
    if (!self || self->updating_genre_dropdown_) return;

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
    // Keep the visible dropdown in sync when navigation sets the filter
    // programmatically (e.g. from the Genres page).
    if (genre_dropdown_ && genre_handler_id_) {
        GtkStringList* model = GTK_STRING_LIST(
            gtk_drop_down_get_model(GTK_DROP_DOWN(genre_dropdown_)));
        const char* target = genre.empty() ? "All Genres" : genre.c_str();
        guint count = g_list_model_get_n_items(G_LIST_MODEL(model));
        guint selected = 0;
        for (guint i = 0; i < count; ++i) {
            const char* item = gtk_string_list_get_string(model, i);
            if (item && genre == item) {
                selected = i;
                break;
            }
        }
        if (gtk_drop_down_get_selected(GTK_DROP_DOWN(genre_dropdown_)) != selected) {
            updating_genre_dropdown_ = true;
            gtk_drop_down_set_selected(GTK_DROP_DOWN(genre_dropdown_), selected);
            updating_genre_dropdown_ = false;
        }
        (void)target;
    }
    if (genre_filter_ == genre) return;
    genre_filter_ = genre;
    reload();
}

void SongsPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            // The songs list is the playback queue: seed the backend queue
            // with the full sorted list so the right-panel queue matches it.
            size_t index = 0;
            {
                std::lock_guard ids_lock(ids_mutex_);
                auto it = std::find(sorted_ids_.begin(), sorted_ids_.end(),
                                    track.id);
                if (it != sorted_ids_.end()) {
                    index = static_cast<size_t>(it - sorted_ids_.begin());
                }
            }
            player->play_tracks(sorted_ids_, index);
        }
    }
}

void SongsPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

void SongsPage::reattach_header() {
    if (!widget_ || !header_box_ || !filter_box_) return;
    if (gtk_widget_get_parent(header_box_.get()) == widget_.get()) return;
    if (gtk_widget_get_parent(filter_box_.get()) == widget_.get()) {
        gtk_box_insert_child_after(GTK_BOX(widget_.get()), header_box_.get(),
                                   filter_box_.get());
    } else {
        gtk_box_prepend(GTK_BOX(widget_.get()), header_box_.get());
    }
    gtk_widget_set_visible(header_box_.get(), TRUE);
}

void SongsPage::refresh() {
    reload();
}

} // namespace lollypop