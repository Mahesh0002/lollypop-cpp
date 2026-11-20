/**
 * Lollypop C++ - Main Window Implementation
 * Four-panel layout as per design specification
 */

#include "main_window.hpp"
#include "panels/left_sidebar.hpp"
#include "panels/middle_library.hpp"
#include "panels/right_now_playing.hpp"
#include "panels/bottom_playback.hpp"
#include "application.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

MainWindow::MainWindow(Application* app)
    : app_(app),
      window_(nullptr),
      main_grid_(nullptr) {
    setup_window();
    setup_layout();
    setup_css();
    connect_signals();
}

MainWindow::~MainWindow() {
    if (window_) {
        g_object_unref(window_);
    }
}

void MainWindow::setup_window() {
    window_ = gtk_application_window_new(app_->get_gtk_application());
    gtk_window_set_title(GTK_WINDOW(window_), "Lollypop");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 800);
    gtk_window_set_decorated(GTK_WINDOW(window_), TRUE);

    // Set minimum size
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window_), content);

    main_grid_ = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(main_grid_), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(main_grid_), FALSE);
    gtk_widget_set_hexpand(main_grid_, TRUE);
    gtk_widget_set_vexpand(main_grid_, TRUE);
    gtk_box_append(GTK_BOX(content), main_grid_);
}

void MainWindow::setup_layout() {
    // Create panels
    left_sidebar_ = std::make_unique<LeftSidebar>(this);
    middle_library_ = std::make_unique<MiddleLibrary>(this);
    right_now_playing_ = std::make_unique<RightNowPlaying>(this);
    bottom_playback_ = std::make_unique<BottomPlayback>(this);

    // Get panel widgets
    GtkWidget* left_widget = left_sidebar_->get_widget();
    GtkWidget* middle_widget = middle_library_->get_widget();
    GtkWidget* right_widget = right_now_playing_->get_widget();
    GtkWidget* bottom_widget = bottom_playback_->get_widget();

    // Attach to grid: 3 columns (left, middle, right), 2 rows (main, bottom)
    // Row 0: Left | Middle | Right
    // Row 1: Bottom (spans all 3 columns)

    gtk_grid_attach(GTK_GRID(main_grid_), left_widget, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid_), middle_widget, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid_), right_widget, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid_), bottom_widget, 0, 1, 3, 1);

    // Set column expansion: middle gets most space, right ~25%, left fixed
    gtk_grid_set_column_homogeneous(GTK_GRID(main_grid_), FALSE);

    // Left sidebar: fixed width (compact ~64px, expanded ~200px)
    gtk_widget_set_size_request(left_widget, 64, -1);
    gtk_widget_set_hexpand(left_widget, FALSE);

    // Middle library: expands to fill remaining space
    gtk_widget_set_hexpand(middle_widget, TRUE);
    gtk_widget_set_vexpand(middle_widget, TRUE);

    // Right panel: ~25% width, minimum 300px
    gtk_widget_set_size_request(right_widget, 300, -1);
    gtk_widget_set_hexpand(right_widget, FALSE);
    gtk_widget_set_vexpand(right_widget, TRUE);

    // Bottom panel: full width, fixed height ~200-250px
    gtk_widget_set_size_request(bottom_widget, -1, 220);
    gtk_widget_set_hexpand(bottom_widget, TRUE);
    gtk_widget_set_vexpand(bottom_widget, FALSE);

    // Set row expansion
    gtk_grid_set_row_homogeneous(GTK_GRID(main_grid_), FALSE);
    // Row 0 (main content) expands
    // Row 1 (bottom) fixed
}

void MainWindow::setup_css() {
    // Add style classes for panel identification
    gtk_widget_add_css_class(GTK_WIDGET(left_sidebar_->get_widget()), "left-sidebar");
    gtk_widget_add_css_class(GTK_WIDGET(middle_library_->get_widget()), "middle-library");
    gtk_widget_add_css_class(GTK_WIDGET(right_now_playing_->get_widget()), "right-now-playing");
    gtk_widget_add_css_class(GTK_WIDGET(bottom_playback_->get_widget()), "bottom-playback");
}

void MainWindow::connect_signals() {
    g_signal_connect(window_, "close-request", G_CALLBACK(on_window_close), this);
    g_signal_connect(main_grid_, "size-allocate", G_CALLBACK(on_size_allocate), this);
}

void MainWindow::present() {
    gtk_window_present(GTK_WINDOW(window_));
}

void MainWindow::on_window_close(GtkWindow* window, gpointer user_data) {
    MainWindow* self = static_cast<MainWindow*>(user_data);
    // Allow close, cleanup happens in destructor
    return;
}

void MainWindow::on_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
    // Handle responsive layout adjustments if needed
    MainWindow* self = static_cast<MainWindow*>(user_data);
    // Could adjust right panel width based on window width here
    // For now, GTK handles it with size requests
}

BackendController* MainWindow::get_backend() const {
    return app_ ? app_->get_backend() : nullptr;
}

} // namespace lollypop