/**
 * Lollypop C++ - Middle Library Implementation
 * Apple Music-inspired library browsing with multiple pages.
 * Single scroller with floating glass toolbar overlay.
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
#include "util/safe_gtk.hpp"
#include "ui/components/TrackListMetrics.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gtk/gtkentry.h>
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

struct SearchTimeoutPayload {
    MiddleLibrary* self;
    std::shared_ptr<bool> alive;
};

void free_search_timeout(gpointer data) {
    delete static_cast<SearchTimeoutPayload*>(data);
}
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
      glass_toolbar_(nullptr),
      sticky_header_(nullptr),
      search_entry_(nullptr),
      menu_button_(nullptr),
      back_button_(nullptr),
      forward_button_(nullptr),
      title_label_(nullptr) {
    setup_ui();
    create_placeholder_pages();
}

MiddleLibrary::~MiddleLibrary() {
    // Invalidate any pending callbacks before tearing down widgets.
    if (alive_) *alive_ = false;
    if (search_timeout_id_) {
        g_source_remove(search_timeout_id_);
        search_timeout_id_ = 0;
    }
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

    // Create the floating glass toolbar (added as overlay)
    create_glass_toolbar();

    // Add scroller as main child of overlay
    gtk_overlay_set_child(GTK_OVERLAY(overlay_), scrolled_window_);

    // Add glass toolbar as overlay (floating on top)
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay_), glass_toolbar_);

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

    // Content box must expand to fill scroller so pages can scroll
    gtk_widget_set_vexpand(content_box_, TRUE);
    gtk_widget_set_valign(content_box_, GTK_ALIGN_FILL);

    // Add top padding to content to account for floating toolbar
    // Toolbar height ~52px + gap ~12px = 64px
    gtk_widget_set_margin_top(content_box_, 64);
    // Bottom padding to clear bottom playback bar
    gtk_widget_set_margin_bottom(content_box_, 104);

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

void MiddleLibrary::create_glass_toolbar() {
    // Glass toolbar - floating overlay at top
    glass_toolbar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(glass_toolbar_, "middle-glass-toolbar");
    gtk_widget_set_halign(glass_toolbar_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(glass_toolbar_, GTK_ALIGN_START);
    gtk_widget_set_margin_top(glass_toolbar_, 12);
    gtk_widget_set_margin_start(glass_toolbar_, 18);
    gtk_widget_set_margin_end(glass_toolbar_, 18);
    gtk_widget_set_visible(glass_toolbar_, TRUE);

    // Back button
    back_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(back_button_), FALSE);
    gtk_widget_add_css_class(back_button_, "middle-toolbar-button");
    GtkWidget* back_icon = gtk_image_new_from_icon_name("go-previous-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(back_icon), 20);
    gtk_button_set_child(GTK_BUTTON(back_button_), back_icon);
    gtk_widget_set_sensitive(back_button_, FALSE);
    gtk_widget_set_tooltip_text(back_button_, "Back");
    g_signal_connect(back_button_, "clicked", G_CALLBACK(on_back_clicked), this);
    gtk_widget_set_visible(back_button_, TRUE);
    gtk_box_append(GTK_BOX(glass_toolbar_), back_button_);

    // Forward button
    forward_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(forward_button_), FALSE);
    gtk_widget_add_css_class(forward_button_, "middle-toolbar-button");
    GtkWidget* forward_icon = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(forward_icon), 20);
    gtk_button_set_child(GTK_BUTTON(forward_button_), forward_icon);
    gtk_widget_set_sensitive(forward_button_, FALSE);
    gtk_widget_set_tooltip_text(forward_button_, "Forward");
    g_signal_connect(forward_button_, "clicked", G_CALLBACK(on_forward_clicked), this);
    gtk_widget_set_visible(forward_button_, TRUE);
    gtk_box_append(GTK_BOX(glass_toolbar_), forward_button_);

    // Title/Breadcrumb
    title_label_ = gtk_label_new("Library");
    gtk_widget_add_css_class(title_label_, "middle-toolbar-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_), 0.0);
    gtk_widget_set_hexpand(title_label_, TRUE);
    gtk_widget_set_visible(title_label_, TRUE);
    gtk_box_append(GTK_BOX(glass_toolbar_), title_label_);

    // Search entry
    search_entry_ = gtk_entry_new();
    gtk_widget_add_css_class(search_entry_, "middle-toolbar-search");
    gtk_widget_set_hexpand(search_entry_, TRUE);
    gtk_widget_set_size_request(search_entry_, 300, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry_), "Search your library");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search_entry_), GTK_ENTRY_ICON_PRIMARY, "system-search-symbolic");
    gtk_widget_set_visible(search_entry_, TRUE);
    search_changed_handler_id_ =
        g_signal_connect(search_entry_, "changed", G_CALLBACK(on_search_changed), this);
    gtk_box_append(GTK_BOX(glass_toolbar_), search_entry_);

    // The previous "View Options / Sort By / Group By / Settings" popover had
    // no handlers attached; dead controls were removed rather than left to
    // mislead the user.
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

    navigate_to_page("home", false);
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
    sync_search_entry(page_id);
    update_toolbar_state();

    if (page_change_cb_) page_change_cb_(page_id);

    // Top-level navigation starts at the top; history restores its own
    // offset through apply_history_entry().
    schedule_scroll(0.0);
}

void MiddleLibrary::sync_search_entry(const std::string& page_id) {
    if (!search_entry_ || !search_changed_handler_id_) return;

    std::string desired;
    if (page_id == "search" && history_index_ < history_.size()) {
        desired = history_[history_index_].param;
    }
    if (desired == gtk_editable_get_text(GTK_EDITABLE(search_entry_))) return;

    g_signal_handler_block(search_entry_, search_changed_handler_id_);
    gtk_editable_set_text(GTK_EDITABLE(search_entry_), desired.c_str());
    g_signal_handler_unblock(search_entry_, search_changed_handler_id_);
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
    sync_search_entry(entry.page_id);
    update_toolbar_state();

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
            if (child) gtk_box_remove(GTK_BOX(sticky_header_), child);
            gtk_box_append(GTK_BOX(sticky_header_), header);
        }
        gtk_widget_set_visible(sticky_header_, TRUE);
        gtk_widget_set_margin_top(
            content_box_, track_list_metrics::kStickyHeaderOffset +
                              track_list_metrics::kStickyHeaderHeight);
        return;
    }

    if (child) {
        gtk_box_remove(GTK_BOX(sticky_header_), child);
        if (songs) songs->reattach_header();
    }
    gtk_widget_set_visible(sticky_header_, FALSE);
    gtk_widget_set_margin_top(content_box_,
                              track_list_metrics::kStickyHeaderOffset);
}

void MiddleLibrary::update_toolbar_state() {
    // Update back/forward button sensitivity
    gtk_widget_set_sensitive(back_button_, history_index_ > 0);
    gtk_widget_set_sensitive(forward_button_, history_index_ + 1 < history_.size());

    // Update title/breadcrumb based on active page
    std::string title;
    if (active_page_id_ == "home") {
        title = "Library";
    } else if (active_page_id_ == "albums") {
        title = "Albums";
    } else if (active_page_id_ == "artists") {
        title = "Artists";
    } else if (active_page_id_ == "playlists") {
        title = "Playlists";
    } else if (active_page_id_ == "genres") {
        title = "Genres";
    } else if (active_page_id_ == "songs") {
        title = "Songs";
    } else if (active_page_id_ == "favorites") {
        title = "Favourite Albums";
    } else if (active_page_id_ == "liked") {
        title = "Liked Music";
    } else if (active_page_id_ == "album_detail") {
        if (auto it = pages_.find("album_detail"); it != pages_.end()) {
            if (auto* detail = dynamic_cast<AlbumDetailPage*>(it->second.get())) {
                title = detail->get_current_album_title();
                if (title.empty()) title = "Album";
            }
        }
    } else if (active_page_id_ == "artist_detail") {
        if (auto it = pages_.find("artist_detail"); it != pages_.end()) {
            if (auto* detail = dynamic_cast<ArtistDetailPage*>(it->second.get())) {
                title = detail->get_current_artist_name();
                if (title.empty()) title = "Artist";
            }
        }
    } else if (active_page_id_ == "search") {
        title = "Search Results";
    } else {
        title = "Library";
    }
    gtk_label_set_text(GTK_LABEL(title_label_), title.c_str());
}

void MiddleLibrary::refresh_active_page() {
    if (auto it = pages_.find(active_page_id_); it != pages_.end()) {
        it->second->refresh();
    }
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

void MiddleLibrary::on_back_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (self->history_index_ == 0 || self->history_.empty()) return;

    // Remember where the user was on the page being left.
    self->history_[self->history_index_].scroll_offset = self->current_scroll();
    self->history_index_--;
    self->apply_history_entry(self->history_[self->history_index_], true);
}

void MiddleLibrary::on_forward_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (self->history_index_ + 1 >= self->history_.size()) return;

    self->history_[self->history_index_].scroll_offset = self->current_scroll();
    self->history_index_++;
    self->apply_history_entry(self->history_[self->history_index_], true);
}

gboolean MiddleLibrary::on_search_timeout(gpointer data) {
    auto* payload = static_cast<SearchTimeoutPayload*>(data);
    if (!payload->alive || !*payload->alive || !payload->self) {
        return G_SOURCE_REMOVE;
    }
    MiddleLibrary* self = payload->self;
    self->search_timeout_id_ = 0;
    uint64_t generation = ++self->search_generation_;
    std::string query;
    if (self->search_entry_) {
        query = gtk_editable_get_text(GTK_EDITABLE(self->search_entry_));
    }
    self->perform_search(query, generation);
    return G_SOURCE_REMOVE;
}

void MiddleLibrary::on_search_changed(GtkEditable* /*editable*/, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (!self || !*self->alive_) return;

    if (self->search_timeout_id_) {
        g_source_remove(self->search_timeout_id_);
        self->search_timeout_id_ = 0;
    }

    auto* payload = new SearchTimeoutPayload{self, self->alive_};
    self->search_timeout_id_ = g_timeout_add_full(
        G_PRIORITY_DEFAULT, 250, on_search_timeout, payload, free_search_timeout);
}

void MiddleLibrary::perform_search(const std::string& query, uint64_t generation) {
    if (generation != search_generation_) return;  // Stale request

    if (query.empty()) {
        // Clearing the field returns to the page the search started from.
        if (active_page_id_ == "search") {
            if (history_index_ > 0 && !history_.empty()) {
                history_index_--;
                apply_history_entry(history_[history_index_], true);
            } else {
                navigate_to_page("home", false);
            }
        }
        return;
    }

    ensure_search_page();
    auto it = pages_.find("search");
    if (it == pages_.end()) return;
    if (auto* search_page = dynamic_cast<SearchResultsPage*>(it->second.get())) {
        search_page->search(query, generation);
    }

    navigate_to_page("search", true, query);
}

} // namespace lollypop