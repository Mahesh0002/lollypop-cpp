/**
 * Lollypop C++ - Artists Page
 *
 * Responsive grid of ArtistItem components with deliberate empty and
 * unavailable states. Click opens artist detail page.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

#include "ui/components/LibraryPage.hpp"
#include "ui/components/ArtistItem.hpp"
#include "ui/components/EmptyState.hpp"
#include "ui/components/ErrorState.hpp"
#include "services/ILibraryService.hpp"

namespace lollypop {

class MainWindow;

class ArtistsPage : public LibraryPage {
public:
    ArtistsPage(MainWindow* main_window, ILibraryService* library_service);
    ~ArtistsPage();

    ArtistsPage(const ArtistsPage&) = delete;
    ArtistsPage& operator=(const ArtistsPage&) = delete;

    GtkWidget* get_widget() const override { return root_.get(); }
    void refresh() override;
    std::string get_page_id() const override { return "artists"; }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> root_;
    std::unique_ptr<GtkWidget, WidgetDeleter> grid_;
    std::unique_ptr<EmptyState> empty_state_;
    std::unique_ptr<ErrorState> error_state_;

    GtkWidget* subtitle_label_ = nullptr;

    MainWindow* main_window_;
    ILibraryService* library_service_;
    std::vector<std::unique_ptr<ArtistItem>> artist_items_;

    void setup_ui();
    void populate(const std::vector<Artist>& artists);
    void update_header(size_t artist_count);
    void on_artist_clicked(const Artist& artist);
};

} // namespace lollypop
