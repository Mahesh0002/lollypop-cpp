/**
 * Lollypop C++ - Search Results Page Implementation
 * Displays search results with lazy loading.
 */

#include "ui/pages/SearchResultsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <mutex>
#include <algorithm>

namespace lollypop {

// Per-search generation token - safely destroyed when the idle fires
// after the page is gone.
struct SearchResultsIdleData {
    GWeakRef page_ref;
    std::vector<Track> tracks;
    uint64_t generation = 0;
    guint source_id = 0;
};

SearchResultsPage::SearchResultsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service),
      loading_(false), loaded_count_(0), total_count_(0), PAGE_SIZE(50) {
    setup_ui();
}

SearchResultsPage::~SearchResultsPage() {
    // Cancel any pending idle sources so they don't fire against a dead page.
    if (idle_source_id_ != 0) {
        g_source_remove(idle_source_id_);
        idle_source_id_ = 0;
    }
}

void SearchResultsPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");
    gtk_widget_set_visible(widget_.get(), TRUE);

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main_box, TRUE);
    gtk_widget_set_vexpand(main_box, TRUE);
    gtk_widget_set_visible(main_box, TRUE);

    // Header with query display - appended exactly once
    header_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    g_object_ref_sink(header_box_.get());
    gtk_widget_add_css_class(header_box_.get(), "search-header");
    gtk_widget_set_margin_start(header_box_.get(), 24);
    gtk_widget_set_margin_end(header_box_.get(), 24);
    gtk_widget_set_margin_top(header_box_.get(), 16);
    gtk_widget_set_margin_bottom(header_box_.get(), 8);
    gtk_widget_set_visible(header_box_.get(), TRUE);

    GtkWidget* search_label = gtk_label_new("Search results");
    gtk_widget_add_css_class(search_label, "search-header-title");
    gtk_label_set_xalign(GTK_LABEL(search_label), 0.0);
    gtk_widget_set_visible(search_label, TRUE);
    gtk_box_append(GTK_BOX(header_box_.get()), search_label);

    safe_box_append(GTK_BOX(main_box), header_box_.get());

    // Results list
    scrolled_window_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(scrolled_window_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window_.get(), TRUE);
    gtk_widget_set_hexpand(scrolled_window_.get(), TRUE);
    gtk_widget_add_css_class(scrolled_window_.get(), "search-results-scrolled");

    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "search-results-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_.get()), list_box_.get());
    gtk_widget_set_vexpand(scrolled_window_.get(), TRUE);
    gtk_widget_set_hexpand(scrolled_window_.get(), TRUE);

    // Connect scroll for lazy loading
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window_.get()));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_scroll_changed), this);

    // Empty state - keep description label as a member so we don't walk children
    empty_state_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 16));
    g_object_ref_sink(empty_state_.get());
    gtk_widget_add_css_class(empty_state_.get(), "empty-state");
    gtk_widget_set_hexpand(empty_state_.get(), TRUE);
    gtk_widget_set_vexpand(empty_state_.get(), TRUE);
    gtk_widget_set_halign(empty_state_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(empty_state_.get(), GTK_ALIGN_CENTER);

    GtkWidget* icon = gtk_image_new_from_icon_name("system-search-symbolic");
    gtk_widget_add_css_class(icon, "empty-state-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon, TRUE);
    gtk_box_append(GTK_BOX(empty_state_.get()), icon);

    GtkWidget* empty_title = gtk_label_new("No results found");
    gtk_widget_add_css_class(empty_title, "empty-state-title");
    gtk_label_set_xalign(GTK_LABEL(empty_title), 0.5);
    gtk_widget_set_halign(empty_title, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(empty_title, TRUE);
    gtk_box_append(GTK_BOX(empty_state_.get()), empty_title);

    empty_desc_.reset(gtk_label_new("Try a different search term"));
    g_object_ref_sink(empty_desc_.get());
    gtk_widget_add_css_class(empty_desc_.get(), "empty-state-description");
    gtk_label_set_xalign(GTK_LABEL(empty_desc_.get()), 0.5);
    gtk_label_set_wrap(GTK_LABEL(empty_desc_.get()), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(empty_desc_.get()), 40);
    gtk_widget_set_halign(empty_desc_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(empty_desc_.get(), TRUE);
    gtk_box_append(GTK_BOX(empty_state_.get()), empty_desc_.get());

    safe_box_append(GTK_BOX(main_box), scrolled_window_.get());

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), main_box);
}

void SearchResultsPage::search(const std::string& query, uint64_t generation) {
    if (!library_service_) return;

    current_query_ = query;
    current_generation_ = generation;
    clear_results();

    if (query.empty()) {
        loading_ = false;
        show_empty_state("Enter a search term");
        return;
    }

    // Update header with query (update in place, don't append)
    char query_text[256];
    snprintf(query_text, sizeof(query_text), "Results for \"%s\"", query.c_str());
    if (!query_label_) {
        query_label_.reset(gtk_label_new(query_text));
        g_object_ref_sink(query_label_.get());
        gtk_widget_add_css_class(query_label_.get(), "search-header-query");
        gtk_label_set_xalign(GTK_LABEL(query_label_.get()), 0.0);
        gtk_box_append(GTK_BOX(header_box_.get()), query_label_.get());
    } else {
        gtk_label_set_text(GTK_LABEL(query_label_.get()), query_text);
    }

    // Snapshot tracks under lock, then release before any GTK work.
    std::vector<Track> tracks;
    {
        std::lock_guard lock(load_mutex_);
        loaded_count_ = 0;
        total_count_ = 0;
        tracks = library_service_->search_tracks(query);
        total_count_ = static_cast<uint64_t>(tracks.size());
    }

    if (tracks.empty()) {
        show_empty_state("No results found for \"" + query + "\"");
        return;
    }

    // Load first page
    size_t end = std::min(PAGE_SIZE, tracks.size());
    std::vector<Track> first_page(tracks.begin(), tracks.begin() + end);

    // Store remaining tracks for lazy loading under lock.
    {
        std::lock_guard lock(load_mutex_);
        remaining_tracks_.assign(tracks.begin() + end, tracks.end());
        loaded_count_ = 0;
        loading_ = false;
    }

    append_tracks(std::move(first_page));
}

void SearchResultsPage::append_tracks(std::vector<Track>&& tracks) {
    if (tracks.empty()) {
        if (loaded_count_ == 0) {
            show_empty_state("No results found");
        }
        return;
    }

    // Remove empty state if present
    if (gtk_widget_get_parent(empty_state_.get()) == scrolled_window_.get()) {
        gtk_widget_unparent(empty_state_.get());
    }

    // Ensure list_box is parented to scrolled_window
    if (gtk_widget_get_parent(list_box_.get()) != scrolled_window_.get()) {
        safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_.get()), list_box_.get());
    }

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(main_window_, tracks[i],
                                              static_cast<int>(loaded_count_ + i + 1));
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

void SearchResultsPage::show_empty_state(const std::string& message) {
    // Remove list box if present
    if (gtk_widget_get_parent(list_box_.get()) == scrolled_window_.get()) {
        gtk_widget_unparent(list_box_.get());
    }

    // Update the stored description label directly instead of walking children
    if (empty_desc_) {
        gtk_label_set_text(GTK_LABEL(empty_desc_.get()), message.c_str());
    }

    if (gtk_widget_get_parent(empty_state_.get()) == nullptr) {
        safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_.get()), empty_state_.get());
    }
}

void SearchResultsPage::clear_results() {
    // Remove list box children
    GtkWidget* child = gtk_widget_get_first_child(list_box_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(list_box_.get()), child);
        child = next;
    }
    track_rows_.clear();

    // Remove empty state if parented
    if (gtk_widget_get_parent(empty_state_.get()) == scrolled_window_.get()) {
        gtk_widget_unparent(empty_state_.get());
    }

    // Ensure list_box is not parented (will be reparented on next append)
    if (gtk_widget_get_parent(list_box_.get()) == scrolled_window_.get()) {
        gtk_widget_unparent(list_box_.get());
    }

    // Clear query label if present
    if (query_label_) {
        if (gtk_widget_get_parent(query_label_.get()) == header_box_.get()) {
            gtk_box_remove(GTK_BOX(header_box_.get()), query_label_.get());
        }
        query_label_.reset();
    }

    loaded_count_ = 0;
    total_count_ = 0;
}

void SearchResultsPage::on_track_played(const Track& track) {
    if (auto* backend = main_window_->get_backend()) {
        if (auto* player = dynamic_cast<IPlayerService*>(backend)) {
            player->play_track(track.id);
        }
    }
}

void SearchResultsPage::on_track_liked(const Track& track, bool /*liked*/) {
    if (library_service_) {
        library_service_->toggle_like(track.id);
    }
}

void SearchResultsPage::on_scroll_changed(GtkAdjustment* adjustment, gpointer user_data) {
    auto* self = static_cast<SearchResultsPage*>(user_data);
    double value = gtk_adjustment_get_value(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);

    // Load more when near bottom (within 200px)
    if (value + page_size >= upper - 200) {
        size_t next_start = self->loaded_count_;
        if (next_start < self->total_count_) {
            self->load_more();
        }
    }
}

void SearchResultsPage::load_more() {
    std::lock_guard lock(load_mutex_);
    if (loading_ || loaded_count_ >= total_count_) return;
    loading_ = true;

    size_t end = std::min(loaded_count_ + PAGE_SIZE, total_count_);
    std::vector<Track> batch;

    // Load from remaining_tracks_
    {
        size_t available = remaining_tracks_.size();
        size_t to_take = std::min(PAGE_SIZE, available);
        if (to_take > 0) {
            batch.assign(remaining_tracks_.begin(), remaining_tracks_.begin() + to_take);
            remaining_tracks_.erase(remaining_tracks_.begin(), remaining_tracks_.begin() + to_take);
        }
    }

    if (!batch.empty()) {
        // Use idle add to append on main thread with weak ref to page
        auto* load_data = new SearchResultsIdleData{};
        g_weak_ref_init(&load_data->page_ref, this);
        load_data->tracks = std::move(batch);
        load_data->generation = current_generation_;
        load_data->source_id = g_idle_add([](gpointer data) -> gboolean {
            auto* ld = static_cast<SearchResultsIdleData*>(data);
            
            // Check if page still exists and generation matches
            SearchResultsPage* page = static_cast<SearchResultsPage*>(g_weak_ref_get(&ld->page_ref));
            if (page && page->widget_ && ld->generation == page->current_generation_) {
                page->append_tracks(std::move(ld->tracks));
            }
            
            if (page) {
                page->idle_source_id_ = 0;
            }
            g_weak_ref_clear(&ld->page_ref);
            delete ld;
            return G_SOURCE_REMOVE;
        }, load_data);
        idle_source_id_ = load_data->source_id;
    } else {
        loading_ = false;
    }
}

void SearchResultsPage::refresh() {
    if (!current_query_.empty()) {
        search(current_query_, ++current_generation_);
    }
}

} // namespace lollypop