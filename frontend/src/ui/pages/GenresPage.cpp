/**
 * Lollypop C++ - Genres Page Implementation
 */

#include "ui/pages/GenresPage.hpp"
#include "main_window.hpp"
#include "panels/middle_library.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

GenresPage::GenresPage(MainWindow* main_window, ILibraryService* library_service)
    : main_window_(main_window), library_service_(library_service) {
    setup_ui();
}

GenresPage::~GenresPage() = default;

void GenresPage::setup_ui() {
    // Root widget is the ListBox directly (implements GtkScrollable)
    list_box_.reset(gtk_list_box_new());
    g_object_ref_sink(list_box_.get());
    gtk_widget_add_css_class(list_box_.get(), "genres-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_.get()),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_.get(), TRUE);
    gtk_widget_set_vexpand(list_box_.get(), TRUE);
    gtk_widget_set_visible(list_box_.get(), TRUE);

    widget_ = list_box_.get();
}

void GenresPage::refresh() {
    GtkWidget* child = gtk_widget_get_first_child(list_box_.get());
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(list_box_.get()), child);
        child = next;
    }

    if (library_service_) {
        auto genres = library_service_->get_all_genres();
        populate(genres);
    }
}

void GenresPage::populate(const std::vector<std::string>& genres) {
    for (const auto& genre : genres) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(row, 16);
        gtk_widget_set_margin_end(row, 16);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        gtk_widget_set_hexpand(row, TRUE);

        GtkWidget* label = gtk_label_new(genre.c_str());
        gtk_widget_add_css_class(label, "body-strong");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_append(GTK_BOX(row), label);

        GtkWidget* button = gtk_button_new_with_label("View");
        gtk_widget_set_halign(button, GTK_ALIGN_END);
        gtk_widget_set_hexpand(button, FALSE);
        gtk_widget_set_margin_start(button, 10);
        gtk_widget_set_margin_end(button, 10);
        gtk_widget_set_margin_top(button, 10);
        gtk_widget_set_margin_bottom(button, 10);
        g_object_set_data_full(G_OBJECT(button), "genre", g_strdup(genre.c_str()), g_free);
        g_signal_connect(button, "clicked",
                         G_CALLBACK(on_genre_view_clicked), this);

        gtk_box_append(GTK_BOX(row), button);
        safe_list_box_append(GTK_LIST_BOX(list_box_.get()), row);
    }
}

void GenresPage::on_genre_view_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    auto* self = static_cast<GenresPage*>(user_data);
    if (!self) return;
    
    const char* genre = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "genre"));
    if (!genre) return;
    
    std::cout << "[GenresPage] View genre: " << genre << std::endl;
    if (self->main_window_) {
        MiddleLibrary* middle = self->main_window_->get_middle_library();
        if (middle) {
            middle->show_genre(genre);
        }
    }
}

void GenresPage::on_genre_clicked(const std::string& genre) {
    std::cout << "[GenresPage] View genre: " << genre << std::endl;
}

} // namespace lollypop