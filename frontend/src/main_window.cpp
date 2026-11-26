/**
 * Lollypop C++ - Main Window Implementation
 * Four-panel layout as per design specification.
 * Uses AdwToolbarView + AdwHeaderBar (libadwaita) for native toolbar.
 */

#include "main_window.hpp"
#include "panels/apple_sidebar.hpp"
#include "panels/middle_library.hpp"
#include "panels/right_now_playing.hpp"
#include "panels/bottom_playback.hpp"
#include "application.hpp"
#include "controller.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "services/MockLibraryService.hpp"
#include "services/MockLyricsService.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/components/VolumeDial.hpp"
#include "config/i18n.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <iostream>
#include <vector>

namespace lollypop {

namespace {

constexpr int kMiddlePanelWidth = 1700;
constexpr int kTotalPanelWidth = AppleSidebar::kPanelWidth +
                                 kMiddlePanelWidth +
                                 RightNowPlaying::kPanelWidth;

// Middle panel layout contract: a fixed 1700px column. Its scroller owns the
// height, and no album tile or track row contributes to the window request.
GtkSizeRequestMode content_slot_request_mode(GtkWidget*) {
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

void content_slot_measure(GtkWidget* widget, GtkOrientation orientation, int,
                          int* minimum, int* natural, int* minimum_baseline,
                          int* natural_baseline) {
    int requested_width = kMiddlePanelWidth;
    int requested_height = -1;
    gtk_widget_get_size_request(widget, &requested_width, &requested_height);
    const int width = requested_width > 0 ? requested_width : kMiddlePanelWidth;

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        if (minimum) *minimum = width;
        if (natural) *natural = width;
    } else {
        if (minimum) *minimum = 0;
        if (natural) *natural = 0;
    }
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

void content_slot_allocate(GtkWidget* widget, int width, int height,
                           int baseline) {
    if (GtkWidget* child = gtk_widget_get_first_child(widget)) {
        gtk_widget_allocate(child, width, height, baseline, nullptr);
    }
}

GtkLayoutManager* create_content_slot_layout() {
    return gtk_custom_layout_new(content_slot_request_mode,
                                 content_slot_measure,
                                 content_slot_allocate);
}

} // namespace

MainWindow::MainWindow(Application* app)
    : app_(app),
      window_(nullptr),
      toolbar_view_(nullptr),
      header_bar_(nullptr),
      main_box_(nullptr),
      content_stack_(nullptr),
      content_grid_(nullptr) {
    setup_window();   // Window shell only - header bar + empty content area.
    connect_signals();
    // setup_layout() and setup_css() are deferred to setup_after_backend()
    // because they create pages that query the backend (storage must be ready).
}

MainWindow::~MainWindow() {
    // Disconnect all backend callbacks before window/widgets are destroyed.
    // The backend may outlive MainWindow, so raw `this` captures would dangle.
    disconnect_backend();

    if (window_) {
        g_object_unref(window_);
    }
}

void MainWindow::disconnect_backend() {
    if (library_refresh_source_ != 0) {
        g_source_remove(library_refresh_source_);
        library_refresh_source_ = 0;
    }
    IPlayerService* backend = app_->get_backend();
    if (!backend) return;

    // Clear every callback set in connect_backend() using legacy setters
    // which will clear the multicast lists
    backend->set_track_changed_callback(nullptr);
    backend->set_position_changed_callback(nullptr);
    backend->set_visualization_changed_callback(nullptr);
    backend->set_playback_state_changed_callback(nullptr);
    backend->set_queue_changed_callback(nullptr);

    if (auto* library_service =
            dynamic_cast<ILibraryService*>(app_->get_backend())) {
        library_service->set_library_changed_callback(nullptr);
    }
}

void MainWindow::setup_window() {
    window_ = adw_application_window_new(GTK_APPLICATION(app_->get_gtk_application()));
    // This constructor returns a floating reference. Sink it here so the
    // destructor owns one matching reference instead of double-unreffing a
    // window already released by GTK during shutdown.
    g_object_ref_sink(window_);
    // No window title - keep the header bar clean (no "lollypop-cpp" text).
    gtk_window_set_title(GTK_WINDOW(window_), "");

    // Clamp the initial size to the monitor so the right panel is never
    // pushed off-screen on smaller displays.
    int initial_width = kTotalPanelWidth;
    int initial_height = 900;
    GdkDisplay* display = gtk_widget_get_display(window_);
    GListModel* monitors = display ? gdk_display_get_monitors(display) : nullptr;
    GdkMonitor* monitor = nullptr;
    if (monitors && g_list_model_get_n_items(monitors) > 0) {
        monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    }
    if (monitor) {
        GdkRectangle area{};
        gdk_monitor_get_geometry(monitor, &area);
        // Reserve a frame margin so no panel is pushed off-screen.
        initial_width = std::min(initial_width, std::max(640, area.width - 64));
        initial_height = std::min(initial_height,
                                  std::max(480, area.height - 64));
        g_object_unref(monitor);
    }
    initial_width = std::max(initial_width, 640);
    initial_height = std::max(initial_height, 480);
    gtk_window_set_default_size(GTK_WINDOW(window_), initial_width,
                                initial_height);
    gtk_window_set_decorated(GTK_WINDOW(window_), TRUE);
    gtk_widget_set_visible(window_, TRUE);

    // Create toolbar view
    toolbar_view_ = adw_toolbar_view_new();
    gtk_widget_set_hexpand(toolbar_view_, TRUE);
    gtk_widget_set_vexpand(toolbar_view_, TRUE);
    gtk_widget_set_visible(toolbar_view_, TRUE);

    // Create header bar
    header_bar_ = adw_header_bar_new();
    gtk_widget_add_css_class(header_bar_, "main-header-bar");
    gtk_widget_set_visible(header_bar_, TRUE);

    // Add header bar to toolbar view
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view_), header_bar_);

    // Main content container
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, TRUE);
    gtk_widget_set_visible(main_box_, TRUE);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view_), main_box_);

    // Content stack: show a lightweight loading state until the backend is
    // initialized and the real panels have been built. Without this the first
    // frame is an empty window that looks like a crash to the user.
    content_stack_ = gtk_stack_new();
    gtk_widget_set_hexpand(content_stack_, TRUE);
    gtk_widget_set_vexpand(content_stack_, TRUE);
    gtk_widget_set_visible(content_stack_, TRUE);

    GtkWidget* loading_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_halign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(loading_box, TRUE);

    GtkWidget* spinner = gtk_spinner_new();
    gtk_widget_set_size_request(spinner, 36, 36);
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_visible(spinner, TRUE);

    GtkWidget* loading_label = gtk_label_new("Loading library…");
    gtk_widget_add_css_class(loading_label, "loading-label");
    gtk_widget_set_visible(loading_label, TRUE);

    gtk_box_append(GTK_BOX(loading_box), spinner);
    gtk_box_append(GTK_BOX(loading_box), loading_label);
    gtk_stack_add_named(GTK_STACK(content_stack_), loading_box, "loading");

    // Content grid (starts empty; populated in setup_layout()).
    content_grid_ = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(content_grid_), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(content_grid_), FALSE);
    gtk_widget_set_hexpand(content_grid_, TRUE);
    gtk_widget_set_vexpand(content_grid_, TRUE);
    gtk_widget_set_visible(content_grid_, TRUE);
    gtk_stack_add_named(GTK_STACK(content_stack_), content_grid_, "content");

    gtk_stack_set_visible_child_name(GTK_STACK(content_stack_), "loading");
    gtk_box_append(GTK_BOX(main_box_), content_stack_);

    // Set toolbar view as window content (AdwApplicationWindow specific)
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), toolbar_view_);
}

void MainWindow::setup_toolbar() {
    // Sidebar only changes its labels when expanded. Window-level actions live
    // here so no sidebar icon appears or disappears during the transition.
    GtkWidget* menu = gtk_menu_button_new();
    gtk_widget_add_css_class(menu, "main-header-menu");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 18);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu), menu_icon);
    gtk_widget_set_tooltip_text(menu, "More actions");

    GtkWidget* popover = gtk_popover_new();
    GtkWidget* popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(popover_box, 6);
    gtk_widget_set_margin_end(popover_box, 6);
    gtk_widget_set_margin_top(popover_box, 6);
    gtk_widget_set_margin_bottom(popover_box, 6);

    GtkWidget* about = gtk_button_new_with_label("About");
    gtk_button_set_has_frame(GTK_BUTTON(about), FALSE);
    gtk_widget_set_halign(about, GTK_ALIGN_START);
    g_signal_connect(about, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer user_data) {
                         auto* self = static_cast<MainWindow*>(user_data);
                         GtkWidget* dialog = gtk_about_dialog_new();
                         gtk_about_dialog_set_program_name(
                             GTK_ABOUT_DIALOG(dialog), "Lollypop C++");
                         gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog),
                                                      "1.0");
                         gtk_about_dialog_set_comments(
                             GTK_ABOUT_DIALOG(dialog),
                             "A GTK4 music player built with C++20.");
                         if (self->window_) {
                             gtk_window_set_transient_for(
                                 GTK_WINDOW(dialog), GTK_WINDOW(self->window_));
                         }
                         gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
                         g_signal_connect(dialog, "response",
                                          G_CALLBACK(+[](GtkDialog* d, gint,
                                                         gpointer) {
                                              gtk_window_destroy(GTK_WINDOW(d));
                                          }),
                                          nullptr);
                         gtk_window_present(GTK_WINDOW(dialog));
                     }),
                     this);

    GtkWidget* fullscreen = gtk_button_new_with_label("Fullscreen player");
    gtk_button_set_has_frame(GTK_BUTTON(fullscreen), FALSE);
    gtk_widget_set_halign(fullscreen, GTK_ALIGN_START);
    g_signal_connect(fullscreen, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer user_data) {
                         auto* self = static_cast<MainWindow*>(user_data);
                         self->toggle_fullscreen_player();
                     }),
                     this);

    gtk_box_append(GTK_BOX(popover_box), about);
    gtk_box_append(GTK_BOX(popover_box), fullscreen);
    gtk_popover_set_child(GTK_POPOVER(popover), popover_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar_), menu);
}

void MainWindow::setup_layout() {
    // Get the backend (implements IPlayerService, ILibraryService,
    // ILyricsService).
    IPlayerService* backend = app_->get_backend();
    auto* library_service = dynamic_cast<ILibraryService*>(backend);
    auto* lyrics_service = dynamic_cast<ILyricsService*>(backend);

    // Create panels - MiddleLibrary must be created before AppleSidebar
    // because sidebar navigation calls set_active_page(), which requires
    // MiddleLibrary pages to exist.
    middle_library_ = std::make_unique<MiddleLibrary>(this, library_service);
    left_sidebar_ = std::make_unique<AppleSidebar>(this);
    right_now_playing_ = std::make_unique<RightNowPlaying>(this, lyrics_service, library_service);
    bottom_playback_ = std::make_unique<BottomPlayback>(this);
    fullscreen_player_ = std::make_unique<FullscreenPlayer>(
        this, lyrics_service, library_service);

    // Connect MiddleLibrary page changes to LeftSidebar for active state sync
    middle_library_->set_page_change_callback([this](const std::string& page_id) {
        if (left_sidebar_) {
            left_sidebar_->set_active_item_visual(page_id);
        }
    });

    // Setup toolbar after panels are created
    setup_toolbar();

    // Immersive player lives as a sibling of the normal four-panel layout.
    // It is revealed on demand and never participates in the grid allocation.
    gtk_stack_add_named(GTK_STACK(content_stack_),
                        fullscreen_player_->get_widget(), "fullscreen");

    // Get panel widgets
    GtkWidget* left_widget = left_sidebar_->get_widget();
    GtkWidget* middle_widget = middle_library_->get_widget();
    GtkWidget* right_widget = right_now_playing_->get_widget();
    GtkWidget* bottom_widget = bottom_playback_->get_widget();

    // Fixed middle column. The custom layout manager keeps library content
    // from expanding the window beyond the 1700px budget.
    GtkWidget* middle_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_layout_manager(middle_slot, create_content_slot_layout());
    gtk_widget_set_size_request(middle_slot, kMiddlePanelWidth, -1);
    gtk_widget_set_hexpand(middle_slot, FALSE);
    gtk_widget_set_vexpand(middle_slot, TRUE);
    gtk_widget_set_visible(middle_slot, TRUE);
    gtk_box_append(GTK_BOX(middle_slot), middle_widget);

    // Attach to grid: 3 columns (left, middle, right), 2 rows (main, bottom)
    // Row 0: Left | Middle | Right
    // Row 1: Bottom (spans all 3 columns)
    gtk_grid_attach(GTK_GRID(content_grid_), left_widget, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(content_grid_), middle_slot, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(content_grid_), right_widget, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(content_grid_), bottom_widget, 0, 1, 3, 1);

    // Fixed columns: 200px source list, 1700px library, 500px now playing.
    gtk_grid_set_column_homogeneous(GTK_GRID(content_grid_), FALSE);

    gtk_widget_set_hexpand(left_widget, FALSE);

    // The library fills exactly the middle slot.
    gtk_widget_set_hexpand(middle_widget, TRUE);
    gtk_widget_set_vexpand(middle_widget, TRUE);

    // RightNowPlaying owns its 500px size request.
    gtk_widget_set_hexpand(right_widget, FALSE);
    gtk_widget_set_vexpand(right_widget, TRUE);

    // Bottom panel: full width, compact fixed height matching its 56px artwork.
    gtk_widget_set_size_request(bottom_widget, -1, 76);
    gtk_widget_set_hexpand(bottom_widget, TRUE);
    gtk_widget_set_vexpand(bottom_widget, FALSE);

    // Set row expansion
    gtk_grid_set_row_homogeneous(GTK_GRID(content_grid_), FALSE);
}

void MainWindow::setup_css() {
    // Add style classes for panel identification
    gtk_widget_add_css_class(GTK_WIDGET(left_sidebar_->get_widget()),
                             "left-sidebar");
    gtk_widget_add_css_class(GTK_WIDGET(middle_library_->get_widget()),
                             "middle-library");
    gtk_widget_add_css_class(GTK_WIDGET(right_now_playing_->get_widget()),
                             "right-now-playing");
    gtk_widget_add_css_class(GTK_WIDGET(bottom_playback_->get_widget()),
                             "bottom-playback");
}

void MainWindow::connect_signals() {
    g_signal_connect(window_, "close-request", G_CALLBACK(on_window_close), this);

    GtkEventController* key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed",
                     G_CALLBACK(on_key_pressed), this);
    gtk_widget_add_controller(window_, key_controller);
}

void MainWindow::connect_backend() {
    IPlayerService* backend = app_->get_backend();
    if (!backend) return;

    // Track changed → update both playback panels + request lyrics
    backend->add_track_changed_callback([this](const Track& track) {
        bottom_playback_->update_track_info(track.title, track.artist,
                                            track.artwork_path);
        right_now_playing_->update_track_info(track.title, track.artist,
                                              track.artwork_path);
        fullscreen_player_->update_track_info(track.title, track.artist,
                                              track.album, track.artwork_path);
        // Keep a detail page's hero artwork as the palette source instead of
        // letting the newly playing track overwrite its tint.
        if (middle_library_) middle_library_->update_palette_for_active_page();
        // Auto-request lyrics for new track
        right_now_playing_->request_lyrics_for_track(track.id);
        fullscreen_player_->request_lyrics_for_track(track.id);
    });
    backend->add_track_changed_callback([backend](const Track& track) {
        TrackRow::update_playing_state(track.id, backend->is_playing());
    });

    // Position changed → update both playback panels
    backend->add_position_changed_callback([this](double position,
                                                   double duration) {
        bottom_playback_->update_playback_position(position, duration);
        right_now_playing_->update_playback_position(position, duration);
        fullscreen_player_->update_playback_position(position, duration);
    });

    // Playback state changed → update both panels
    backend->add_playback_state_changed_callback([this](bool playing) {
        bottom_playback_->set_playing(playing);
        right_now_playing_->set_playing(playing);
        fullscreen_player_->set_playing(playing);
    });
    backend->add_playback_state_changed_callback([backend](bool playing) {
        TrackRow::update_playing_state(backend->get_current_track().id, playing);
    });

    // Playback modes → keep the fullscreen options menu in sync.
    backend->add_shuffle_changed_callback([this](bool enabled) {
        fullscreen_player_->set_shuffle(enabled);
    });
    backend->add_repeat_changed_callback([this](bool enabled) {
        fullscreen_player_->set_repeat(enabled);
    });
    backend->add_repeat_one_changed_callback([this](bool enabled) {
        fullscreen_player_->set_repeat_one(enabled);
    });

    // Queue changed → update both the right panel and fullscreen queue view
    backend->add_queue_changed_callback([this](const std::vector<std::string>& queue_ids) {
        right_now_playing_->set_queue(queue_ids);
        fullscreen_player_->set_queue(queue_ids);
    });

    // Seed panels with current backend state. initialize() emits the initial
    // track-changed signal before these callbacks exist, so explicitly seed
    // track info and request lyrics for a restored/current track.
    const Track current_track = backend->get_current_track();
    if (!current_track.id.empty()) {
        bottom_playback_->update_track_info(current_track.title,
                                            current_track.artist,
                                            current_track.artwork_path);
        right_now_playing_->update_track_info(current_track.title,
                                              current_track.artist,
                                              current_track.artwork_path);
        fullscreen_player_->update_track_info(current_track.title,
                                              current_track.artist,
                                              current_track.album,
                                              current_track.artwork_path);
        if (middle_library_) middle_library_->update_palette_for_active_page();
        right_now_playing_->request_lyrics_for_track(current_track.id);
        fullscreen_player_->request_lyrics_for_track(current_track.id);
    }

    fullscreen_player_->update_playback_position(backend->get_position(),
                                                 backend->get_duration());
    fullscreen_player_->set_volume(backend->get_volume());
    bottom_playback_->set_playing(backend->is_playing());
    right_now_playing_->set_playing(backend->is_playing());
    fullscreen_player_->set_playing(backend->is_playing());
    TrackRow::update_playing_state(backend->get_current_track().id,
                                   backend->is_playing());
    right_now_playing_->set_queue(backend->get_queue());
    fullscreen_player_->set_queue(backend->get_queue());
    right_now_playing_->set_shuffle(backend->get_shuffle());
    right_now_playing_->set_repeat(backend->get_repeat());
    right_now_playing_->set_loop(backend->get_repeat_one());

    bottom_playback_->set_shuffle(backend->get_shuffle());
    bottom_playback_->set_loop(backend->get_repeat_one());

    // Library changed (scan finished, file watcher event, like/favourite
    // change) → refresh the page the user is currently looking at.
    if (auto* library_service = dynamic_cast<ILibraryService*>(backend)) {
        library_service->add_library_changed_callback([this]() {
            schedule_library_refresh();
        });
    }

    // Enable playback mode buttons (shuffle/repeat/loop) - they control the backend
    right_now_playing_->set_controls_sensitive(true);

    // Bottom transport buttons now work (play/pause, prev, next, seek).
    bottom_playback_->set_controls_sensitive(true);
    right_now_playing_->set_transport_controls_sensitive(true);

    std::cout << "MainWindow: Backend callbacks connected" << std::endl;
}

void MainWindow::schedule_library_refresh() {
    if (library_refresh_source_ != 0) return;

    // Coalesce bursts (scan progress, favourite toggles, file events) into a
    // single page refresh instead of rebuilding the current page per event.
    library_refresh_source_ = g_timeout_add(
        100,
        [](gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            self->library_refresh_source_ = 0;
            if (self->middle_library_) self->middle_library_->refresh_active_page();
            return G_SOURCE_REMOVE;
        },
        this);
}

void MainWindow::present() {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_));
    }
}

void MainWindow::show_fullscreen_player() {
    if (!fullscreen_player_ || !content_stack_ || fullscreen_player_visible_) {
        return;
    }
    fullscreen_player_visible_ = true;
    was_fullscreen_before_player_ =
        window_ && gtk_window_is_fullscreen(GTK_WINDOW(window_));
    if (window_ && !was_fullscreen_before_player_) {
        gtk_window_fullscreen(GTK_WINDOW(window_));
    }
    if (toolbar_view_) {
        adw_toolbar_view_set_reveal_top_bars(ADW_TOOLBAR_VIEW(toolbar_view_),
                                             FALSE);
    }
    fullscreen_player_->sync_from_backend();
    gtk_stack_set_visible_child_name(GTK_STACK(content_stack_), "fullscreen");
}

void MainWindow::hide_fullscreen_player() {
    if (!fullscreen_player_visible_) return;
    fullscreen_player_visible_ = false;
    gtk_stack_set_visible_child_name(GTK_STACK(content_stack_), "content");
    if (toolbar_view_) {
        adw_toolbar_view_set_reveal_top_bars(ADW_TOOLBAR_VIEW(toolbar_view_),
                                             TRUE);
    }
    if (window_ && !was_fullscreen_before_player_) {
        gtk_window_unfullscreen(GTK_WINDOW(window_));
    }
}

void MainWindow::toggle_fullscreen_player() {
    if (fullscreen_player_visible_) {
        hide_fullscreen_player();
    } else {
        show_fullscreen_player();
    }
}

gboolean MainWindow::on_key_pressed(GtkEventControllerKey*, guint keyval,
                                    guint, GdkModifierType, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    if (keyval == GDK_KEY_Escape && self->fullscreen_player_visible_) {
        self->hide_fullscreen_player();
        return TRUE;
    }
    return FALSE;
}

gboolean MainWindow::on_window_close(GtkWindow* window, gpointer user_data) {
    (void)window;
    (void)user_data;
    // Allow close, cleanup happens in destructor
    return FALSE;
}

IPlayerService* MainWindow::get_backend() const {
    return app_ ? app_->get_backend() : nullptr;
}

ThemeManager* MainWindow::get_theme_manager() const {
    return app_ ? app_->get_theme_manager() : nullptr;
}

void MainWindow::setup_after_backend() {
    setup_layout();
    setup_css();
    connect_backend();
    // Replace the loading state with the real UI once everything is ready.
    if (content_stack_) {
        gtk_stack_set_visible_child_name(GTK_STACK(content_stack_), "content");
    }
}

} // namespace lollypop
