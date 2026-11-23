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

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gtk/gtkentry.h>
#include <iostream>
#include <string>
#include <vector>

namespace lollypop {

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
      search_entry_(nullptr),
      menu_button_(nullptr),
      back_button_(nullptr),
      forward_button_(nullptr),
      title_label_(nullptr) {
    setup_ui();
    create_placeholder_pages();
}

MiddleLibrary::~MiddleLibrary() {
    // Remove search timeout if pending
    if (search_timeout_id_) {
        g_source_remove(search_timeout_id_);
        search_timeout_id_ = 0;
    }
    g_weak_ref_clear(&self_weak_ref_);
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

    // Add top padding to content to account for floating toolbar
    // Toolbar height ~52px + gap ~12px = 64px
    gtk_widget_set_margin_top(content_box_, 64);
    // Bottom padding to clear bottom playback bar
    gtk_widget_set_margin_bottom(content_box_, 104);
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
    g_signal_connect(search_entry_, "changed", G_CALLBACK(on_search_changed), this);
    gtk_box_append(GTK_BOX(glass_toolbar_), search_entry_);

    // Menu button (density, view options, etc.)
    menu_button_ = gtk_menu_button_new();
    gtk_widget_add_css_class(menu_button_, "middle-toolbar-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("view-more-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button_), menu_icon);
    gtk_widget_set_visible(menu_button_, TRUE);

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(popover_box, 8);
    gtk_widget_set_margin_bottom(popover_box, 8);
    gtk_widget_set_margin_start(popover_box, 8);
    gtk_widget_set_margin_end(popover_box, 8);
    gtk_widget_set_visible(popover_box, TRUE);

    const char* menu_items[] = {"View Options", "Sort By", "Group By", "Settings"};
    for (const char* item : menu_items) {
        GtkWidget* btn = gtk_button_new_with_label(item);
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        gtk_widget_set_halign(btn, GTK_ALIGN_START);
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_margin_start(btn, 10);
        gtk_widget_set_margin_end(btn, 10);
        gtk_widget_set_margin_top(btn, 10);
        gtk_widget_set_margin_bottom(btn, 10);
        gtk_widget_set_visible(btn, TRUE);
        gtk_box_append(GTK_BOX(popover_box), btn);
    }
    gtk_popover_set_child(GTK_POPOVER(popover), popover_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button_), popover);

    gtk_box_append(GTK_BOX(glass_toolbar_), menu_button_);
}

void MiddleLibrary::create_placeholder_pages() {
    // Home page (real page with library data)
    auto hp = std::make_unique<HomePage>(main_window_, library_service_);
    GtkWidget* hw = hp->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), hw, "home");
    pages_["home"] = std::move(hp);

    // Albums (real grid page)
    auto albums = std::make_unique<AlbumsPage>(main_window_, library_service_);
    GtkWidget* aw = albums->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), aw, "albums");
    pages_["albums"] = std::move(albums);

    // Artists (real grid page)
    auto artists = std::make_unique<ArtistsPage>(main_window_, library_service_);
    GtkWidget* arw = artists->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), arw, "artists");
    pages_["artists"] = std::move(artists);

    // Playlists (real grid page)
    auto playlists = std::make_unique<PlaylistsPage>(main_window_, library_service_);
    GtkWidget* pw = playlists->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), pw, "playlists");
    pages_["playlists"] = std::move(playlists);

    // Genres (real list page)
    auto genres = std::make_unique<GenresPage>(main_window_, library_service_);
    GtkWidget* gw = genres->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), gw, "genres");
    pages_["genres"] = std::move(genres);

    // Songs (GtkColumnView page)
    auto songs = std::make_unique<SongsPage>(main_window_, library_service_);
    GtkWidget* sw = songs->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), sw, "songs");
    pages_["songs"] = std::move(songs);

    // Favourite Albums (real grid page)
    auto fav = std::make_unique<FavouriteAlbumsPage>(main_window_, library_service_);
    GtkWidget* fw = fav->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), fw, "favorites");
    pages_["favorites"] = std::move(fav);

    // Liked Music (real list page)
    auto liked = std::make_unique<LikedMusicPage>(main_window_, library_service_);
    GtkWidget* lw = liked->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), lw, "liked");
    pages_["liked"] = std::move(liked);

    // Album details (detail page)
    auto album_detail = std::make_unique<AlbumDetailPage>(main_window_, library_service_);
    GtkWidget* adw = album_detail->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), adw, "album_detail");
    pages_["album_detail"] = std::move(album_detail);

    // Artist details (detail page)
    auto artist_detail = std::make_unique<ArtistDetailPage>(main_window_, library_service_);
    GtkWidget* ardw = artist_detail->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), ardw, "artist_detail");
    pages_["artist_detail"] = std::move(artist_detail);

    // Set up HomePage navigation callbacks now that all pages exist
    if (auto it = pages_.find("home"); it != pages_.end()) {
        if (auto* home = dynamic_cast<HomePage*>(it->second.get())) {
            home->set_recently_added_callback([this]() { navigate_to_page("songs", true); });
            home->set_recently_played_callback([this]() { navigate_to_page("songs", true); });
            home->set_favourite_albums_callback([this]() { navigate_to_page("favorites", true); });
        }
    }

    // Show home page by default
    navigate_to_page("home", false);
}

void MiddleLibrary::register_page(std::unique_ptr<LibraryPage> page) {
    std::string page_id = page->get_page_id();
    GtkWidget* widget = page->get_widget();
    safe_stack_add_named(GTK_STACK(stack_), widget, page_id.c_str());
    pages_[page_id] = std::move(page);
}

void MiddleLibrary::navigate_to_page(const std::string& page_id, bool add_to_history) {
    if (pages_.find(page_id) == pages_.end()) return;

    if (add_to_history && active_page_id_ != page_id) {
        // Truncate forward history
        history_.resize(history_index_ + 1);
        history_.push_back(page_id);
        history_index_ = history_.size() - 1;
    } else if (!add_to_history) {
        // Initial navigation or programmatic - reset history
        history_.clear();
        history_.push_back(page_id);
        history_index_ = 0;
    }

    active_page_id_ = page_id;
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_id.c_str());
    update_toolbar_state();
}

void MiddleLibrary::set_active_page(const std::string& page_id) {
    navigate_to_page(page_id, true);
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
    if (auto it = pages_.find("album_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<AlbumDetailPage*>(it->second.get())) {
            detail->set_album(album_id);
            navigate_to_page("album_detail", true);
        }
    }
}

void MiddleLibrary::open_artist_detail(const std::string& artist_id) {
    if (auto it = pages_.find("artist_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<ArtistDetailPage*>(it->second.get())) {
            detail->set_artist(artist_id);
            navigate_to_page("artist_detail", true);
        }
    }
}

void MiddleLibrary::show_genre(const std::string& genre) {
    if (auto it = pages_.find("songs"); it != pages_.end()) {
        if (auto* songs = dynamic_cast<SongsPage*>(it->second.get())) {
            songs->set_genre_filter(genre);
        }
    }
    navigate_to_page("songs", true);
}

void MiddleLibrary::on_back_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (self->history_index_ > 0) {
        self->history_index_--;
        self->active_page_id_ = self->history_[self->history_index_];
        gtk_stack_set_visible_child_name(GTK_STACK(self->stack_), self->active_page_id_.c_str());
        self->update_toolbar_state();
    }
}

void MiddleLibrary::on_forward_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    if (self->history_index_ + 1 < self->history_.size()) {
        self->history_index_++;
        self->active_page_id_ = self->history_[self->history_index_];
        gtk_stack_set_visible_child_name(GTK_STACK(self->stack_), self->active_page_id_.c_str());
        self->update_toolbar_state();
    }
}

void MiddleLibrary::on_search_changed(GtkEditable* editable, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    const char* text = gtk_editable_get_text(editable);
    std::string query(text);

    // Cancel previous timeout
    if (self->search_timeout_id_) {
        g_source_remove(self->search_timeout_id_);
        self->search_timeout_id_ = 0;
    }

    // Initialize weak ref if not already
    if (!g_weak_ref_get(&self->self_weak_ref_)) {
        g_weak_ref_init(&self->self_weak_ref_, self);
    }

    // Debounce: wait 250ms before searching
    self->search_timeout_id_ = g_timeout_add(250, [](gpointer data) -> gboolean {
        auto* self = static_cast<MiddleLibrary*>(g_weak_ref_get(&((MiddleLibrary*)data)->self_weak_ref_));
        if (!self) return G_SOURCE_REMOVE;

        self->search_timeout_id_ = 0;
        uint64_t gen = ++self->search_generation_;
        const char* query_text = gtk_editable_get_text(GTK_EDITABLE(self->search_entry_));
        std::string query(query_text);
        self->perform_search(query, gen);
        return G_SOURCE_REMOVE;
    }, self);
}

void MiddleLibrary::perform_search(const std::string& query, uint64_t generation) {
    if (generation != search_generation_) return; // Stale request

    // Ensure search results page exists
    if (pages_.find("search") == pages_.end()) {
        auto search_page = std::make_unique<SearchResultsPage>(main_window_, library_service_);
        GtkWidget* sw = search_page->get_widget();
        safe_stack_add_named(GTK_STACK(stack_), sw, "search");
        pages_["search"] = std::move(search_page);
    }

    // Perform search
    if (auto it = pages_.find("search"); it != pages_.end()) {
        if (auto* search_page = dynamic_cast<SearchResultsPage*>(it->second.get())) {
            search_page->search(query, generation);
        }
    }

    if (!query.empty()) {
        navigate_to_page("search", true);
    } else {
        navigate_to_page("home", true);
    }
}

} // namespace lollypop