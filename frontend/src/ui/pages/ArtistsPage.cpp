/**
 * Lollypop C++ - Artists Page Implementation
 */

#include "ui/pages/ArtistsPage.hpp"
#include "main_window.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

ArtistsPage::ArtistsPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

ArtistsPage::~ArtistsPage() = default;

void ArtistsPage::setup_ui() {
    widget_.reset(gtk_scrolled_window_new());
    g_object_ref_sink(widget_.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget_.get()),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(widget_.get(), TRUE);
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_add_css_class(widget_.get(), "library-scrolled");
    gtk_widget_set_visible(widget_.get(), TRUE);

    grid_.reset(gtk_flow_box_new());
    g_object_ref_sink(grid_.get());
    gtk_widget_add_css_class(grid_.get(), "artists-grid");
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_.get()), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_.get()), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_.get()), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_.get()), 24);
    gtk_widget_set_hexpand(grid_.get(), TRUE);
    gtk_widget_set_visible(grid_.get(), TRUE);

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget_.get()), grid_.get());
}

void ArtistsPage::refresh() {
    // Clear existing - use gtk_flow_box_remove for proper cleanup
    GtkWidget* child = gtk_widget_get_first_child(grid_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(grid_.get()), child);
        child = next;
    }
    artist_items_.clear();

    if (library_service_) {
        auto artists = library_service_->get_all_artists();
        populate(artists);
    }
}

void ArtistsPage::populate(const std::vector<Artist>& artists) {
    for (const auto& artist : artists) {
        auto item = std::make_unique<ArtistItem>(main_window_, artist);
        item->set_click_callback(
            [this](const Artist& a) { on_artist_clicked(a); });

        GtkWidget* w = item->get_widget();
        safe_flow_box_append(GTK_FLOW_BOX(grid_.get()), w);
        artist_items_.push_back(std::move(item));
    }
}

void ArtistsPage::on_artist_clicked(const Artist& artist) {
    std::cout << "[ArtistsPage] Open artist detail: " << artist.name << std::endl;
    if (main_window_) {
        MiddleLibrary* middle = main_window_->get_middle_library();
        if (middle) {
            middle->open_artist_detail(artist.id);
        }
    }
}

} // namespace lollypop
