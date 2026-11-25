/**
 * Lollypop C++ - Artists Page
 *
 * Responsive grid of ArtistItem components. Click opens artist detail page.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/ArtistItem.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class ArtistsPage : public LibraryPage {
public:
    ArtistsPage(MainWindow* main_window, ILibraryService* library_service);
    ~ArtistsPage();

    ArtistsPage(const ArtistsPage&) = delete;
    ArtistsPage& operator=(const ArtistsPage&) = delete;

    GtkWidget* get_widget() const { return widget_; }
    void refresh() override;
    std::string get_page_id() const override { return "artists"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            // Managed widgets are ref-sunk at construction, so a container
            // holds a second reference while the widget is parented.
            if (gtk_widget_get_parent(w) != nullptr) {
                gtk_widget_unparent(w);
            }
            g_object_unref(w);
        }
    };
    GtkWidget* widget_ = nullptr;  // FlowBox (content), owned by grid_
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<ArtistItem>> artist_items_;

    void setup_ui();
    void populate(const std::vector<Artist>& artists);
    void on_artist_clicked(const Artist& artist);
};

} // namespace lollypop
