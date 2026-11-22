/**
 * Lollypop C++ - Middle Library Implementation
 * Apple Music-inspired library browsing with multiple pages.
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

#include <gtk/gtk.h>
#include <gtk/gtkentry.h>
#include <iostream>

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
      main_box_(nullptr),
      header_bar_(nullptr),
      search_entry_(nullptr),
      stack_(nullptr),
      scrolled_window_(nullptr) {
    setup_ui();
    create_placeholder_pages();
}

MiddleLibrary::~MiddleLibrary() = default;

void MiddleLibrary::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(main_box_, "middle-library");
    gtk_widget_set_vexpand(main_box_, TRUE);
    gtk_widget_set_hexpand(main_box_, TRUE);

    create_header_bar();
    gtk_box_append(GTK_BOX(main_box_), header_bar_);

    stack_ = gtk_stack_new();
    gtk_widget_add_css_class(stack_, "library-stack");
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 200);

    // Initialize navigator
    navigator_ = std::make_unique<StackNavigator>(GTK_STACK(stack_));
    navigator_->set_transition(StackNavigator::Transition::SlideLeft);

    scrolled_window_ = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled_window_, "library-scrolled");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_), stack_);
    gtk_widget_set_vexpand(scrolled_window_, TRUE);
    gtk_widget_set_hexpand(scrolled_window_, TRUE);

    gtk_box_append(GTK_BOX(main_box_), scrolled_window_);
}

void MiddleLibrary::create_header_bar() {
    header_bar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(header_bar_, "library-header");
    gtk_widget_set_margin_top(header_bar_, 16);
    gtk_widget_set_margin_bottom(header_bar_, 8);
    gtk_widget_set_margin_start(header_bar_, 24);
    gtk_widget_set_margin_end(header_bar_, 24);

    search_entry_ = gtk_entry_new();
    gtk_widget_add_css_class(search_entry_, "library-search");
    gtk_widget_set_hexpand(search_entry_, TRUE);
    gtk_widget_set_size_request(search_entry_, 300, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry_), "Search your library");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search_entry_), GTK_ENTRY_ICON_PRIMARY, "system-search-symbolic");

    // Connect search with debounce
    g_signal_connect(search_entry_, "changed", G_CALLBACK(on_search_changed), this);

    GtkWidget* menu_button = gtk_menu_button_new();
    gtk_widget_add_css_class(menu_button, "library-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("view-more-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), menu_icon);

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(popover_box, 8);
    gtk_widget_set_margin_bottom(popover_box, 8);
    gtk_widget_set_margin_start(popover_box, 8);
    gtk_widget_set_margin_end(popover_box, 8);

    const char* menu_items[] = {"View Options", "Sort By", "Group By", "Settings"};
    for (const char* item : menu_items) {
        GtkWidget* btn = gtk_button_new_with_label(item);
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        gtk_widget_set_halign(btn, GTK_ALIGN_START);
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_box_append(GTK_BOX(popover_box), btn);
    }
    gtk_popover_set_child(GTK_POPOVER(popover), popover_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), popover);

    // No greeting - clean header with search and menu only
    gtk_box_append(GTK_BOX(header_bar_), search_entry_);
    gtk_box_append(GTK_BOX(header_bar_), menu_button);
}

void MiddleLibrary::create_placeholder_pages() {
    // Home page (real page with library data)
    auto hp = std::make_unique<HomePage>(main_window_, library_service_);
    GtkWidget* hw = hp->get_widget();
    navigator_->push("home", hw);
    pages_["home"] = std::move(hp);

    // Albums (real grid page)
    auto albums = std::make_unique<AlbumsPage>(main_window_, library_service_);
    GtkWidget* aw = albums->get_widget();
    navigator_->push("albums", aw);
    pages_["albums"] = std::move(albums);

    // Artists (real grid page)
    auto artists = std::make_unique<ArtistsPage>(main_window_, library_service_);
    GtkWidget* arw = artists->get_widget();
    navigator_->push("artists", arw);
    pages_["artists"] = std::move(artists);

    // Playlists (real grid page)
    auto playlists = std::make_unique<PlaylistsPage>(main_window_, library_service_);
    GtkWidget* pw = playlists->get_widget();
    navigator_->push("playlists", pw);
    pages_["playlists"] = std::move(playlists);

    // Genres (real list page)
    auto genres = std::make_unique<GenresPage>(main_window_, library_service_);
    GtkWidget* gw = genres->get_widget();
    navigator_->push("genres", gw);
    pages_["genres"] = std::move(genres);

    // Songs (GtkColumnView page)
    auto songs = std::make_unique<SongsPage>(main_window_, library_service_);
    GtkWidget* sw = songs->get_widget();
    navigator_->push("songs", sw);
    pages_["songs"] = std::move(songs);

    // Favourite Albums (real grid page)
    auto fav = std::make_unique<FavouriteAlbumsPage>(main_window_, library_service_);
    GtkWidget* fw = fav->get_widget();
    navigator_->push("favorites", fw);
    pages_["favorites"] = std::move(fav);

    // Liked Music (real list page)
    auto liked = std::make_unique<LikedMusicPage>(main_window_, library_service_);
    GtkWidget* lw = liked->get_widget();
    navigator_->push("liked", lw);
    pages_["liked"] = std::move(liked);

    // Album details (detail page)
    auto album_detail = std::make_unique<AlbumDetailPage>(main_window_, library_service_);
    GtkWidget* adw = album_detail->get_widget();
    navigator_->push("album_detail", adw);
    pages_["album_detail"] = std::move(album_detail);

    // Artist details (detail page)
    auto artist_detail = std::make_unique<ArtistDetailPage>(main_window_, library_service_);
    GtkWidget* ardw = artist_detail->get_widget();
    navigator_->push("artist_detail", ardw);
    pages_["artist_detail"] = std::move(artist_detail);

    // Show home page by default
    set_active_page("home");
}

void MiddleLibrary::register_page(std::unique_ptr<LibraryPage> page) {
    std::string page_id = page->get_page_id();
    GtkWidget* widget = page->get_widget();
    navigator_->push(page_id, widget);
    pages_[page_id] = std::move(page);
}

void MiddleLibrary::set_active_page(const std::string& page_id) {
    if (pages_.find(page_id) != pages_.end()) {
        active_page_id_ = page_id;
        navigator_->navigate_to(page_id);
        pages_[page_id]->refresh();
    }
}

void MiddleLibrary::refresh_active_page() {
    auto it = pages_.find(active_page_id_);
    if (it != pages_.end()) {
        it->second->refresh();
    }
}

// Navigation to detail pages
void MiddleLibrary::open_album_detail(const std::string& album_id) {
    if (auto it = pages_.find("album_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<AlbumDetailPage*>(it->second.get())) {
            detail->set_album(album_id);
            set_active_page("album_detail");
        }
    }
}

void MiddleLibrary::open_artist_detail(const std::string& artist_id) {
    if (auto it = pages_.find("artist_detail"); it != pages_.end()) {
        if (auto* detail = dynamic_cast<ArtistDetailPage*>(it->second.get())) {
            detail->set_artist(artist_id);
            set_active_page("artist_detail");
        }
    }
}

void MiddleLibrary::show_genre(const std::string& genre) {
    // For now, switch to songs page and filter by genre
    // In a full implementation, we'd create a genre-filtered view
    if (auto it = pages_.find("songs"); it != pages_.end()) {
        if (auto* songs = dynamic_cast<SongsPage*>(it->second.get())) {
            // TODO: Add genre filter to SongsPage
            // songs->set_genre_filter(genre);
        }
    }
    set_active_page("songs");
}

void MiddleLibrary::on_search_changed(GtkEditable* editable, gpointer user_data) {
    auto* self = static_cast<MiddleLibrary*>(user_data);
    const char* text = gtk_editable_get_text(editable);
    std::string query(text);

    // Cancel previous timeout
    if (self->search_timeout_id_) {
        g_source_remove(self->search_timeout_id_);
    }

    // Debounce: wait 250ms before searching
    self->search_timeout_id_ = g_timeout_add(250, [](gpointer data) -> gboolean {
        auto* self = static_cast<MiddleLibrary*>(data);
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
        navigator_->push("search", sw);
        pages_["search"] = std::move(search_page);
    }

    // Perform search
    if (auto it = pages_.find("search"); it != pages_.end()) {
        if (auto* search_page = dynamic_cast<SearchResultsPage*>(it->second.get())) {
            search_page->search(query, generation);
        }
    }

    if (!query.empty()) {
        set_active_page("search");
    } else {
        set_active_page("home");
    }
}

} // namespace lollypop
