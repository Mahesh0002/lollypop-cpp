/**
 * Lollypop C++ - Middle Library Implementation
 * Apple Music-inspired library browsing with multiple pages
 */

#include "panels/middle_library.hpp"
#include "main_window.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

// Placeholder page implementation
class PlaceholderPage : public LibraryPage {
public:
    PlaceholderPage(const std::string& page_id, const std::string& title)
        : page_id_(page_id), title_(title) {}

    GtkWidget* get_widget() override {
        if (!widget_) {
            widget_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
            gtk_widget_add_css_class(widget_, "library-page");
            gtk_widget_set_margin_top(widget_, 24);
            gtk_widget_set_margin_bottom(widget_, 24);
            gtk_widget_set_margin_start(widget_, 24);
            gtk_widget_set_margin_end(widget_, 24);

            // Title
            GtkWidget* title_label = gtk_label_new(title_.c_str());
            gtk_widget_add_css_class(title_label, "library-page-title");
            gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
            gtk_box_append(GTK_BOX(widget_), title_label);

            // Placeholder content
            GtkWidget* placeholder = gtk_label_new(("Content for " + title_ + " will appear here").c_str());
            gtk_widget_add_css_class(placeholder, "library-placeholder");
            gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0);
            gtk_widget_set_valign(placeholder, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(widget_), placeholder);
        }
        return widget_;
    }

    void refresh() override {
        // TODO: Refresh page content
    }

    std::string get_page_id() const override { return page_id_; }

private:
    std::string page_id_;
    std::string title_;
    GtkWidget* widget_ = nullptr;
};

MiddleLibrary::MiddleLibrary(MainWindow* main_window)
    : main_window_(main_window),
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

    // Header bar with search
    create_header_bar();
    gtk_box_append(GTK_BOX(main_box_), header_bar_);

    // Stack for pages
    stack_ = gtk_stack_new();
    gtk_widget_add_css_class(stack_, "library-stack");
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 200);

    // Scrolled window for content
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

    // Greeting / time-based title
    GtkWidget* greeting = gtk_label_new("Good evening");
    gtk_widget_add_css_class(greeting, "library-greeting");
    gtk_label_set_xalign(GTK_LABEL(greeting), 0.0);

    // Search entry - use regular entry with search icon
    search_entry_ = gtk_entry_new();
    gtk_widget_add_css_class(search_entry_, "library-search");
    gtk_widget_set_hexpand(search_entry_, TRUE);
    gtk_widget_set_size_request(search_entry_, 300, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry_), "Search your library");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search_entry_), GTK_ENTRY_ICON_PRIMARY, "system-search-symbolic");

    // Menu button (ellipsis)
    GtkWidget* menu_button = gtk_menu_button_new();
    gtk_widget_add_css_class(menu_button, "library-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("view-more-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), menu_icon);

    // Popover menu
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

    gtk_box_append(GTK_BOX(header_bar_), greeting);
    gtk_box_append(GTK_BOX(header_bar_), search_entry_);
    gtk_box_append(GTK_BOX(header_bar_), menu_button);
}

void MiddleLibrary::create_placeholder_pages() {
    // Register all library pages
    register_page(std::make_unique<PlaceholderPage>("home", "Listen Now"));
    register_page(std::make_unique<PlaceholderPage>("favorites", "Favourite Albums"));
    register_page(std::make_unique<PlaceholderPage>("albums", "Albums"));
    register_page(std::make_unique<PlaceholderPage>("artists", "Artists"));
    register_page(std::make_unique<PlaceholderPage>("playlists", "Playlists"));
    register_page(std::make_unique<PlaceholderPage>("genres", "Genres"));
    register_page(std::make_unique<PlaceholderPage>("songs", "Songs"));
    register_page(std::make_unique<PlaceholderPage>("liked", "Liked Music"));

    // Show home page by default
    set_active_page("home");
}

void MiddleLibrary::register_page(std::unique_ptr<LibraryPage> page) {
    std::string page_id = page->get_page_id();
    GtkWidget* widget = page->get_widget();
    gtk_stack_add_named(GTK_STACK(stack_), widget, page_id.c_str());
    pages_[page_id] = std::move(page);
}

void MiddleLibrary::set_active_page(const std::string& page_id) {
    if (pages_.find(page_id) != pages_.end()) {
        active_page_id_ = page_id;
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_id.c_str());

        // Refresh the page
        pages_[page_id]->refresh();
    }
}

} // namespace lollypop