/**
 * Lollypop C++ - Genres Page
 *
 * List of genres derived from actual library metadata (plan Section 5.6).
 * Selecting a genre opens a filtered view.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class GenresPage : public LibraryPage {
public:
    GenresPage(MainWindow* main_window, ILibraryService* library_service);
    ~GenresPage();

    GenresPage(const GenresPage&) = delete;
    GenresPage& operator=(const GenresPage&) = delete;

    GtkWidget* get_widget() const { return widget_; }
    void refresh() override;
    std::string get_page_id() const override { return "genres"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    GtkWidget* widget_ = nullptr;  // ListBox (content), owned by list_box_
    std::unique_ptr<GtkWidget, WidgetDeleter> list_box_;

    MainWindow* main_window_;
    ILibraryService* library_service_;

    void setup_ui();
    void populate(const std::vector<std::string>& genres);
    static void on_genre_view_clicked(GtkButton* button, gpointer user_data);
    void on_genre_clicked(const std::string& genre);
};

} // namespace lollypop
