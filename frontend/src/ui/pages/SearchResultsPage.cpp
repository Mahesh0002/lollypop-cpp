/**
 * Lollypop C++ - Search Results Page Implementation
 * Worker-thread search with generation checks and main-thread application.
 */

#include "ui/pages/SearchResultsPage.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <thread>

namespace lollypop {

namespace {

// SQLite storage is shared with the UI thread; serialising searches keeps
// concurrent queries from overlapping while still keeping the UI responsive.
std::mutex g_search_mutex;

struct SearchResultPayload {
    std::shared_ptr<bool> alive;
    SearchResultsPage* page;
    uint64_t generation;
    std::vector<Track> tracks;
};

void free_search_result(gpointer data) {
    delete static_cast<SearchResultPayload*>(data);
}

} // namespace

SearchResultsPage::SearchResultsPage(MainWindow* main_window,
                                     ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

SearchResultsPage::~SearchResultsPage() {
    *alive_ = false;
}

void SearchResultsPage::setup_ui() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    gtk_widget_add_css_class(root, "library-page");
    gtk_widget_add_css_class(root, "search-page");
    g_object_ref_sink(root);
    widget_.reset(root);

    header_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    g_object_ref_sink(header_box_.get());
    gtk_widget_add_css_class(header_box_.get(), "search-header");
    gtk_widget_set_margin_start(header_box_.get(), 24);
    gtk_widget_set_margin_end(header_box_.get(), 24);
    gtk_widget_set_margin_top(header_box_.get(), 8);
    gtk_widget_set_margin_bottom(header_box_.get(), 8);
    gtk_widget_set_visible(header_box_.get(), TRUE);

    GtkWidget* search_label = gtk_label_new("Search results");
    gtk_widget_add_css_class(search_label, "search-header-title");
    gtk_label_set_xalign(GTK_LABEL(search_label), 0.0);
    gtk_widget_set_visible(search_label, TRUE);
    gtk_box_append(GTK_BOX(header_box_.get()), search_label);

    query_label_.reset(gtk_label_new(""));
    g_object_ref_sink(query_label_.get());
    gtk_widget_add_css_class(query_label_.get(), "search-header-query");
    gtk_label_set_xalign(GTK_LABEL(query_label_.get()), 0.0);
    gtk_widget_set_visible(query_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(header_box_.get()), query_label_.get());

    safe_box_append(GTK_BOX(root), header_box_.get());

    content_area_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    g_object_ref_sink(content_area_.get());
    gtk_widget_set_hexpand(content_area_.get(), TRUE);
    gtk_widget_set_vexpand(content_area_.get(), TRUE);
    gtk_widget_set_visible(content_area_.get(), TRUE);
    safe_box_append(GTK_BOX(root), content_area_.get());

    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "search-results-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);

    empty_state_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 16));
    g_object_ref_sink(empty_state_.get());
    gtk_widget_add_css_class(empty_state_.get(), "empty-state");
    gtk_widget_set_hexpand(empty_state_.get(), TRUE);
    gtk_widget_set_vexpand(empty_state_.get(), TRUE);
    gtk_widget_set_halign(empty_state_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(empty_state_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(empty_state_.get(), 48);
    gtk_widget_set_margin_bottom(empty_state_.get(), 48);

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

    set_content(empty_state_.get());
}

void SearchResultsPage::set_content(GtkWidget* child) {
    if (!content_area_ || !child) return;

    GtkWidget* current = gtk_widget_get_first_child(content_area_.get());
    while (current) {
        GtkWidget* next = gtk_widget_get_next_sibling(current);
        if (current != child) {
            gtk_box_remove(GTK_BOX(content_area_.get()), current);
        }
        current = next;
    }

    if (gtk_widget_get_parent(child) != content_area_.get()) {
        gtk_box_append(GTK_BOX(content_area_.get()), child);
    }
}

void SearchResultsPage::search(const std::string& query, uint64_t generation) {
    current_query_ = query;
    current_generation_ = generation;
    clear_rows();
    remaining_tracks_.clear();
    loaded_count_ = 0;
    total_count_ = 0;

    if (query_label_) {
        const std::string label = query.empty()
            ? std::string()
            : "Results for \"" + query + "\"";
        gtk_label_set_text(GTK_LABEL(query_label_.get()), label.c_str());
    }

    if (!library_service_) return;

    if (query.empty()) {
        show_empty_state("Enter a search term");
        return;
    }

    // Show a stable placeholder while the worker runs. Results are applied
    // only if this generation is still current when the idle fires.
    set_content(empty_state_.get());
    if (empty_desc_) {
        gtk_label_set_text(GTK_LABEL(empty_desc_.get()),
                           "Searching your library...");
    }

    ILibraryService* service = library_service_;
    std::shared_ptr<bool> alive = alive_;
    SearchResultsPage* self = this;

    std::thread([service, alive, self, generation, query]() {
        std::vector<Track> tracks;
        {
            std::lock_guard<std::mutex> lock(g_search_mutex);
            tracks = service->search_tracks(query);
        }

        auto* payload = new SearchResultPayload{
            alive, self, generation, std::move(tracks)};
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            [](gpointer data) -> gboolean {
                auto* p = static_cast<SearchResultPayload*>(data);
                if (p->alive && *p->alive && p->page) {
                    p->page->apply_search_results(p->generation,
                                                  std::move(p->tracks));
                }
                return G_SOURCE_REMOVE;
            },
            payload, free_search_result);
    }).detach();
}

void SearchResultsPage::apply_search_results(uint64_t generation,
                                             std::vector<Track>&& tracks) {
    if (generation != current_generation_) return;  // Stale result

    total_count_ = tracks.size();
    if (tracks.empty()) {
        show_empty_state("No results found for \"" + current_query_ + "\"");
        return;
    }

    size_t first = std::min(PAGE_SIZE, tracks.size());
    std::vector<Track> page(tracks.begin(), tracks.begin() + first);
    remaining_tracks_.assign(tracks.begin() + first, tracks.end());
    append_tracks(std::move(page));
}

void SearchResultsPage::append_tracks(std::vector<Track>&& tracks) {
    if (tracks.empty()) return;

    set_content(list_box_.get());

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto row = std::make_unique<TrackRow>(
            main_window_, tracks[i], static_cast<int>(loaded_count_ + i + 1));
        row->set_play_callback([this](const Track& t) { on_track_played(t); });
        row->set_like_callback(
            [this](const Track& t, bool liked) { on_track_liked(t, liked); });

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(list_box_.get()), w);
        track_rows_.push_back(std::move(row));
    }
    loaded_count_ += tracks.size();
}

void SearchResultsPage::clear_rows() {
    if (!list_box_) return;
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(list_box_.get()));
    track_rows_.clear();
}

void SearchResultsPage::show_empty_state(const std::string& message) {
    clear_rows();
    if (empty_desc_) {
        gtk_label_set_text(GTK_LABEL(empty_desc_.get()), message.c_str());
    }
    set_content(empty_state_.get());
}

void SearchResultsPage::load_more() {
    if (loaded_count_ >= total_count_ || remaining_tracks_.empty()) return;

    size_t take = std::min(PAGE_SIZE, remaining_tracks_.size());
    std::vector<Track> batch(remaining_tracks_.begin(),
                             remaining_tracks_.begin() + take);
    remaining_tracks_.erase(remaining_tracks_.begin(),
                            remaining_tracks_.begin() + take);
    append_tracks(std::move(batch));
}

void SearchResultsPage::handle_scroll(GtkAdjustment* adjustment) {
    if (!adjustment) return;
    double value = gtk_adjustment_get_value(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);
    if (value + page_size >= upper - 240.0) {
        load_more();
    }
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

void SearchResultsPage::refresh() {
    if (!current_query_.empty()) {
        search(current_query_, ++current_generation_);
    }
}

} // namespace lollypop
