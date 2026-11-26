/**
 * Lollypop C++ - Middle Library Implementation
 * Apple Music-inspired library browsing with multiple pages.
 * Single scroller owned by this panel; page widgets must not add another one.
 *
 * Referenced files (kept as comments so other models can follow the wiring):
 *   frontend/include/panels/middle_library.hpp
 *   frontend/src/ui/pages/AlbumsPage.cpp
 *   frontend/src/ui/pages/AlbumDetailPage.cpp
 *   include/services/ILibraryService.hpp
 */

#include "panels/middle_library.hpp"
#include "main_window.hpp"
#include "ui/pages/AlbumsPage.hpp"
#include "ui/pages/ArtistsPage.hpp"
#include "ui/pages/PlaylistsPage.hpp"
#include "ui/pages/GenresPage.hpp"
#include "ui/pages/SongsPage.hpp"
#include "ui/pages/FavouriteAlbumsPage.hpp"
#include "ui/pages/LikedMusicPage.hpp"
#include "ui/pages/AlbumDetailPage.hpp"
#include "ui/pages/ArtistDetailPage.hpp"
#include "ui/pages/SearchResultsPage.hpp"
#include "ui/components/HomePage.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "services/IPlayerService.hpp"
#include "util/safe_gtk.hpp"
#include "ui/components/TrackListMetrics.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <iostream>
#include <string>
#include <vector>

namespace lollypop {

namespace {
struct ScrollPayload {
    MiddleLibrary* self;
    std::shared_ptr<bool> alive;
    double offset;
};

} // namespace

// Placeholder page implementation (for pages not yet implemented)
class PlaceholderPage : public LibraryPage {
public:
    PlaceholderPage(const std::string& page_id, const std::string& title)
        : page_id_(page_id), title_(title) {}

    GtkWidget* get_widget() const override {
        if (!widget_) {
            widget_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
            gtk_widget_add_css_class(widget_, "library-page");
            gtk_widget_set_margin_top(widget_, 24);
            gtk_widget_set_margin_bottom(widget_, 24);
            gtk_widget_set_margin_start(widget_, 24);
            gtk_widget_set_margin_end(widget_, 24);

            GtkWidget* title_label = gtk_label_new(title_.c_str());
            gtk_widget_add_css_class(title_label, "library-page-title");
            gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
            gtk_box_append(GTK_BOX(widget_), title_label);

            GtkWidget* placeholder = gtk_label_new(
                ("Content for " + title_ + " will appear here").c_str());
            gtk_widget_add_css_class(placeholder, "library-placeholder");
            gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0);
            gtk_widget_set_valign(placeholder, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(widget_), placeholder);
        }
        return widget_;
    }

    void refresh() override {}

    std::string get_page_id() const override { return page_id_; }

private:
    std::string page_id_;
    std::string title_;
    mutable GtkWidget* widget_ = nullptr;
};

MiddleLibrary::MiddleLibrary(MainWindow* main_window,
                             ILibraryService* library_service)
    : main_window_(main_window),
      library_service_(library_service),
      overlay_(nullptr),
      scrolled_window_(nullptr),
      content_box_(nullptr),
      stack_(nullptr),
      sticky_header_(nullptr) {
    setup_ui();
    create_placeholder_pages();
}

MiddleLibrary::~MiddleLibrary() {
    // Invalidate any pending callbacks before tearing down widgets.
    if (alive_) *alive_ = false;
}

void MiddleLibrary::setup_ui() {
    // Root overlay container
    overlay_ = gtk_overlay_new();
    gtk_widget_add_css_class(overlay_, "middle-library-overlay");
    gtk_widget_set_vexpand(overlay_, TRUE);
    gtk_widget_set_hexpand(overlay_, TRUE);
    gtk_widget_set_visible(overlay_, TRUE);

    // Create the content area (scroller + content box + stack)
    create_content_area();

    // Add scroller as main child of overlay
    gtk_overlay_set_child(GTK_OVERLAY(overlay_), scrolled_window_);

    // Pinned column header, shared by the Songs page. It lives above the
    // scroller so the sort labels stay visible while rows scroll underneath.
    sticky_header_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sticky_header_, "middle-sticky-header");
    gtk_widget_set_hexpand(sticky_header_, TRUE);
    gtk_widget_set_valign(sticky_header_, GTK_ALIGN_START);
    gtk_widget_set_margin_top(sticky_header_,
                              track_list_metrics::kStickyHeaderOffset);
    gtk_widget_set_visible(sticky_header_, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay_), sticky_header_);
}

void MiddleLibrary::create_content_area() {
    // Scrolled window - single vertical scroller for the entire panel
    scrolled_window_ = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled_window_, "middle-viewport");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // Apple-like overlay scrollbars: keep the full viewport for page content
    // instead of reserving space for a classic scrollbar.
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(scrolled_window_), TRUE);
    gtk_widget_set_vexpand(scrolled_window_, TRUE);
    gtk_widget_set_hexpand(scrolled_window_, TRUE);
    gtk_widget_set_visible(scrolled_window_, TRUE);

    // Content box inside scroller - fills width, natural height
    content_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content_box_, "middle-content");
    gtk_widget_set_hexpand(content_box_, TRUE);
    gtk_widget_set_vexpand(content_box_, FALSE); // Natural height from content
    gtk_widget_set_valign(content_box_, GTK_ALIGN_START);
    gtk_widget_set_visible(content_box_, TRUE);

    // Stack for page switching
    stack_ = gtk_stack_new();
    gtk_widget_add_css_class(stack_, "library-stack");
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 200);
    gtk_widget_set_hexpand(stack_, TRUE);
    gtk_widget_set_visible(stack_, TRUE);

    safe_box_append(GTK_BOX(content_box_), stack_);

    // Set content box as child of scrolled window
    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_), content_box_);

    // Keep the scrolled-window child at its natural height. Forcing it to fill
    // the viewport stretches page roots (and the album tiles inside them)
    // whenever a different page was previously visible.

    // No floating toolbar: content starts at the top of the middle panel.
    gtk_widget_set_margin_top(content_box_, 0);
    // HIG 8pt grid: keep only the 24px breathing space below the list.
    gtk_widget_set_margin_bottom(content_box_, 24);

    // Connect scroll handling for pages that need it (SongsPage)
    connect_scroll_handling();
}

void MiddleLibrary::connect_scroll_handling() {
    GtkAdjustment* adjustment =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window_));
    if (!adjustment) return;

    g_signal_connect(adjustment, "value-changed",
                     G_CALLBACK(on_scroll_changed), this);
}

void MiddleLibrary::on_scroll_changed(GtkAdjustment* adjustment,
                                      gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (!self) return;

    if (self->active_page_id_ == "songs") {
        auto it = self->pages_.find("songs");
        if (it == self->pages_.end()) return;
        if (auto* songs = dynamic_cast<SongsPage*>(it->second.get())) {
            songs->handle_scroll(adjustment);
        }
    } else if (self->active_page_id_ == "search") {
        auto it = self->pages_.find("search");
        if (it == self->pages_.end()) return;
        if (auto* search = dynamic_cast<SearchResultsPage*>(it->second.get())) {
            search->handle_scroll(adjustment);
        }
    }
}

void MiddleLibrary::create_placeholder_pages() {
    // Only the Home page is built eagerly. Every other page is constructed on
    // first navigation, which keeps the first frame cheap even for large
    // libraries. All pages share the middle panel's single scroller.
    auto hp = std::make_unique<HomePage>(main_window_, library_service_);
    GtkWidget* hw = hp->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), hw, "home");
    pages_["home"] = std::move(hp);

    if (auto it = pages_.find("home"); it != pages_.end()) {
        if (auto* home = dynamic_cast<HomePage*>(it->second.get())) {
            home->set_recently_added_callback([this]() { navigate_to_page("songs", true); });
            home->set_recently_played_callback([this]() { navigate_to_page("songs", true); });
            home->set_favourite_albums_callback([this]() { navigate_to_page("favorites", true); });
        }
    }

    // Optional startup page override is useful for diagnostics and deep links.
    const char* start_page = g_getenv("LOLLYPOP_START_PAGE");
    navigate_to_page(start_page && *start_page ? start_page : "home", false);
}

void MiddleLibrary::ensure_page(const std::string& page_id) {
    if (page_id == "search") {
        ensure_search_page();
        return;
    }
    if (pages_.find(page_id) != pages_.end()) return;

    std::unique_ptr<LibraryPage> page;
    if (page_id == "albums") {
        page = std::make_unique<AlbumsPage>(main_window_, library_service_);
    } else if (page_id == "artists") {
        page = std::make_unique<ArtistsPage>(main_window_, library_service_);
    } else if (page_id == "playlists") {
        page = std::make_unique<PlaylistsPage>(main_window_, library_service_);
    } else if (page_id == "genres") {
        page = std::make_unique<GenresPage>(main_window_, library_service_);
    } else if (page_id == "songs") {
        page = std::make_unique<SongsPage>(main_window_, library_service_);
    } else if (page_id == "favorites") {
        page = std::make_unique<FavouriteAlbumsPage>(main_window_, library_service_);
    } else if (page_id == "liked") {
        page = std::make_unique<LikedMusicPage>(main_window_, library_service_);
    } else if (page_id == "album_detail") {
        page = std::make_unique<AlbumDetailPage>(main_window_, library_service_);
    } else if (page_id == "artist_detail") {
        page = std::make_unique<ArtistDetailPage>(main_window_, library_service_);
    }

    if (!page) return;

    GtkWidget* widget = page->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), widget, page_id.c_str());
    page->refresh();
    pages_[page_id] = std::move(page);
}

void MiddleLibrary::ensure_search_page() {
    if (pages_.find("search") != pages_.end()) return;

    auto search_page = std::make_unique<SearchResultsPage>(main_window_, library_service_);
    GtkWidget* sw = search_page->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), sw, "search");
    pages_["search"] = std::move(search_page);
}

void MiddleLibrary::register_page(std::unique_ptr<LibraryPage> page) {
    std::string page_id = page->get_page_id();
    GtkWidget* widget = page->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), widget, page_id.c_str());
    pages_[page_id] = std::move(page);
}

void MiddleLibrary::navigate_to_page(const std::string& page_id,
                                     bool add_to_history,
                                     const std::string& param) {
    ensure_page(page_id);
    if (pages_.find(page_id) == pages_.end()) return;

    // Capture where the user actually was before leaving the current page.
    if (add_to_history && !active_page_id_.empty() && active_page_id_ != page_id &&
        !history_.empty() && history_index_ < history_.size()) {
        history_[history_index_].scroll_offset = current_scroll();
    }

    if (add_to_history && active_page_id_ != page_id) {
        // Truncate forward history and record the full entry (page, params,
        // scroll offset). The current scroll is captured as zero here; it is
        // saved before Back/Forward moves away from a page.
        history_.resize(history_index_ + 1);
        history_.push_back(HistoryEntry{page_id, param, 0.0});
        history_index_ = history_.size() - 1;
    } else if (!add_to_history) {
        history_.clear();
        history_.push_back(HistoryEntry{page_id, param, 0.0});
        history_index_ = 0;
    } else {
        // Same page, different params (e.g. search query changed, another
        // album opened). Update the current entry instead of stacking.
        if (!history_.empty()) history_[history_index_].param = param;
    }

    active_page_id_ = page_id;
    update_sticky_header();
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_id.c_str());
    update_palette_for_active_page();

    if (page_change_cb_) page_change_cb_(page_id);

    // Top-level navigation starts at the top; history restores its own
    // offset through apply_history_entry().
    schedule_scroll(0.0);
}

double MiddleLibrary::current_scroll() const {
    if (!scrolled_window_) return 0.0;
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(scrolled_window_));
    return adjustment ? gtk_adjustment_get_value(adjustment) : 0.0;
}

gboolean MiddleLibrary::on_scroll_restore_idle(gpointer data) {
    auto* payload = static_cast<ScrollPayload*>(data);
    if (payload->alive && *payload->alive && payload->self &&
        payload->self->scrolled_window_) {
        GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(payload->self->scrolled_window_));
        if (adjustment) {
            gtk_adjustment_set_value(adjustment, payload->offset);
        }
    }
    delete payload;
    return G_SOURCE_REMOVE;
}

void MiddleLibrary::schedule_scroll(double offset) {
    auto* payload = new ScrollPayload{this, alive_, offset};
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, on_scroll_restore_idle, payload,
                    nullptr);
}

void MiddleLibrary::apply_history_entry(const HistoryEntry& entry,
                                         bool restore_scroll) {
    ensure_page(entry.page_id);
    auto it = pages_.find(entry.page_id);
    if (it == pages_.end()) return;

    if (entry.page_id == "album_detail") {
        if (auto* detail = dynamic_cast<AlbumDetailPage*>(it->second.get())) {
            detail->set_album(entry.param);
        }
    } else if (entry.page_id == "artist_detail") {
        if (auto* detail = dynamic_cast<ArtistDetailPage*>(it->second.get())) {
            detail->set_artist(entry.param);
        }
    } else if (entry.page_id == "songs") {
        if (auto* songs = dynamic_cast<SongsPage*>(it->second.get())) {
            songs->set_genre_filter(entry.param);
        }
    } else if (entry.page_id == "search") {
        if (auto* search = dynamic_cast<SearchResultsPage*>(it->second.get())) {
            if (!entry.param.empty()) {
                search->search(entry.param, ++search_generation_);
            }
        }
    }

    active_page_id_ = entry.page_id;
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), entry.page_id.c_str());
    update_palette_for_active_page();

    if (page_change_cb_) page_change_cb_(entry.page_id);

    schedule_scroll(restore_scroll ? entry.scroll_offset : 0.0);
}

void MiddleLibrary::set_active_page(const std::string& page_id) {
    navigate_to_page(page_id, true);
}

void MiddleLibrary::update_sticky_header() {
    if (!sticky_header_ || !content_box_) return;

    SongsPage* songs = nullptr;
    if (auto it = pages_.find("songs"); it != pages_.end()) {
        songs = dynamic_cast<SongsPage*>(it->second.get());
    }

    GtkWidget* child = gtk_widget_get_first_child(sticky_header_);
    const bool want_header =
        active_page_id_ == "songs" && songs && songs->get_header_widget();

    if (want_header) {
        GtkWidget* header = songs->get_header_widget();
        if (gtk_widget_get_parent(header) != sticky_header_) {
            // The header can still belong to the songs page when switching
            // back to it. GTK requires an unparented child before append.
            if (GtkWidget* previous = gtk_widget_get_parent(header)) {
                if (GTK_IS_WIDGET(previous) && GTK_IS_BOX(previous)) {
                    gtk_box_remove(GTK_BOX(previous), header);
                } else if (GTK_IS_WIDGET(header) && gtk_widget_get_parent(header)) {
                    gtk_widget_unparent(header);
                }
            }
            if (child && GTK_IS_WIDGET(child)) {
                gtk_box_remove(GTK_BOX(sticky_header_), child);
            }
            gtk_box_append(GTK_BOX(sticky_header_), header);
        }
        gtk_widget_set_visible(sticky_header_, TRUE);
        gtk_widget_set_margin_top(
            content_box_, track_list_metrics::kStickyHeaderOffset +
                              track_list_metrics::kStickyHeaderHeight);
        return;
    }

    if (child && GTK_IS_WIDGET(child)) {
        gtk_box_remove(GTK_BOX(sticky_header_), child);
        if (songs) songs->reattach_header();
    }
    gtk_widget_set_visible(sticky_header_, FALSE);
    gtk_widget_set_margin_top(content_box_,
                              track_list_metrics::kStickyHeaderOffset);
}

void MiddleLibrary::refresh_active_page() {
    if (auto it = pages_.find(active_page_id_); it != pages_.end()) {
        it->second->refresh();
    }
    update_palette_for_active_page();
}

void MiddleLibrary::update_palette_for_active_page() {
    auto* theme = main_window_ ? main_window_->get_theme_manager() : nullptr;
    if (!theme) return;

    std::string artwork_path;
    if (auto it = pages_.find(active_page_id_); it != pages_.end()) {
        artwork_path = it->second->palette_artwork_path();
    }
    if (artwork_path.empty()) {
        if (auto* backend = main_window_ ? main_window_->get_backend() : nullptr) {
            artwork_path = backend->get_current_track().artwork_path;
        }
    }
    theme->set_artwork_tint_from_path(artwork_path);
}

void MiddleLibrary::open_album_detail(const std::string& album_id) {
    ensure_page("album_detail");
    if (auto it = pages_.find("album_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<AlbumDetailPage*>(it->second.get())) {
            detail->set_album(album_id);
        }
    }
    navigate_to_page("album_detail", true, album_id);
}

void MiddleLibrary::open_artist_detail(const std::string& artist_id) {
    ensure_page("artist_detail");
    if (auto it = pages_.find("artist_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<ArtistDetailPage*>(it->second.get())) {
            detail->set_artist(artist_id);
        }
    }
    navigate_to_page("artist_detail", true, artist_id);
}

void MiddleLibrary::show_genre(const std::string& genre) {
    ensure_page("songs");
    if (auto it = pages_.find("songs"); it != pages_.end()) {
        if (auto* songs = dynamic_cast<SongsPage*>(it->second.get())) {
            songs->set_genre_filter(genre);
        }
    }
    navigate_to_page("songs", true, genre);
}

} // namespace lollypop