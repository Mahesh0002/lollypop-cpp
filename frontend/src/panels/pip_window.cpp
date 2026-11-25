/**
 * Lollypop C++ - PiP Window Implementation
 *
 * Apple Music-style mini-player with Liquid Glass material,
 * transport controls, progress bar, synced lyrics, and mode toggles.
 */

#include "panels/pip_window.hpp"
#include "panels/progress_wave.hpp"
#include "panels/spectrum_router.hpp"
#include "application.hpp"
#include "ui/components/ArtworkView.hpp"
#include "util/accessibility.hpp"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <algorithm>
#include <cmath>

namespace lollypop {

PiPWindow::PiPWindow(Application* app)
    : window_(nullptr),
      root_box_(nullptr),
      artwork_view_(nullptr),
      info_box_(nullptr),
      title_label_(nullptr),
      artist_label_(nullptr),
      album_label_(nullptr),
      transport_box_(nullptr),
      prev_button_(nullptr),
      play_pause_button_(nullptr),
      next_button_(nullptr),
      progress_wave_(nullptr),
      time_elapsed_label_(nullptr),
      time_remaining_label_(nullptr),
      compact_lyrics_label_(nullptr),
      footer_box_(nullptr),
      shuffle_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr),
      volume_button_(nullptr),
      volume_scale_(nullptr),
      volume_popover_(nullptr),
      menu_button_(nullptr),
      close_button_(nullptr),
      app_(app) {
    setup_ui();
}

PiPWindow::~PiPWindow() {
    // Invalidate queued backend callbacks before the widget tree is torn down.
    if (alive_) *alive_ = false;
    SpectrumRouter::instance().unregister_wave(progress_wave_.get());
    backend_ = nullptr;
}

void PiPWindow::present() {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_.get()));
    }
}

void PiPWindow::set_backend(IPlayerService* backend) {
    backend_ = backend;
    lyrics_service_ = dynamic_cast<ILyricsService*>(backend);
    if (!backend) return;

    auto alive = alive_;
    backend->add_track_changed_callback([this, alive](const Track& track) {
        if (!*alive) return;
        update_track_info(track.title, track.artist, track.artwork_path);
        current_track_id_ = track.id;
        request_lyrics_for_track(track.id);
    });

    backend->add_position_changed_callback(
        [this, alive](double position, double duration) {
            if (!*alive) return;
            update_playback_position(position, duration);
        });

    backend->add_playback_state_changed_callback([this, alive](bool playing) {
        if (!*alive) return;
        set_playing(playing);
    });

    backend->add_shuffle_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        shuffle_active_ = enabled;
        if (shuffle_button_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_.get()), enabled);
        }
    });

    backend->add_repeat_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        repeat_active_ = enabled;
        if (repeat_button_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_.get()), enabled);
        }
    });

    backend->add_repeat_one_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        loop_active_ = enabled;
        if (loop_button_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_.get()), enabled);
        }
    });

    // Initial sync
    if (shuffle_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_.get()), backend->get_shuffle());
    if (repeat_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_.get()), backend->get_repeat());
    if (loop_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_.get()), backend->get_repeat_one());

    progress_wave_->set_can_seek(backend->can_seek());
    update_control_sensitivity();
}

void PiPWindow::update_control_sensitivity() {
    if (!backend_) return;

    const bool has_track = !current_track_id_.empty();
    if (prev_button_) gtk_widget_set_sensitive(prev_button_.get(), has_track);
    if (play_pause_button_) gtk_widget_set_sensitive(play_pause_button_.get(), has_track);
    if (next_button_) gtk_widget_set_sensitive(next_button_.get(), has_track);
    if (volume_button_) gtk_widget_set_sensitive(volume_button_.get(), TRUE);
}

std::string PiPWindow::format_time(int seconds) {
    int mins = seconds / 60;
    int secs = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    return std::string(buf);
}

void PiPWindow::seek_relative(double seconds) {
    if (!backend_) return;
    double pos = backend_->get_position();
    double dur = backend_->get_duration();
    double new_pos = std::clamp(pos + seconds, 0.0, dur);
    backend_->seek(new_pos);
}

void PiPWindow::adjust_volume(double delta) {
    if (!backend_) return;
    double vol = backend_->get_volume();
    double new_vol = std::clamp(vol + delta, 0.0, 1.0);
    backend_->set_volume(new_vol);
}

void PiPWindow::request_lyrics_for_track(const std::string& track_id) {
    set_lyrics("");
    if (!lyrics_service_) return;
    lyrics_service_->request_synced_lyrics(track_id, [this, track_id](const std::string& id, const std::string& lrc_text) {
        if (id == track_id) {
            set_lyrics(lrc_text);
        }
    });
}

void PiPWindow::set_lyrics(const std::string& lyrics) {
    has_lyrics_ = !lyrics.empty();
    current_lyric_index_ = -1;
    if (compact_lyrics_label_) {
        if (has_lyrics_ && lyrics.find('[') != std::string::npos && lyrics.find(']') != std::string::npos) {
            current_lyrics_ = LrcParser::parse(lyrics);
            if (current_lyrics_.valid()) {
                compact_lyrics_label_->set_active(true);
            } else {
                compact_lyrics_label_->set_active(false);
                compact_lyrics_label_->set_text(lyrics);
            }
        } else {
            compact_lyrics_label_->set_active(false);
            compact_lyrics_label_->set_text(lyrics.empty() ? "No lyrics available" : lyrics);
        }
    }
}

void PiPWindow::update_track_info(const std::string& title,
                                  const std::string& artist,
                                  const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(title_label_.get()), title.c_str());
    gtk_label_set_text(GTK_LABEL(artist_label_.get()), artist.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artwork_path);
    }
    update_control_sensitivity();
}

void PiPWindow::update_playback_position(double position, double duration) {
    if (!progress_wave_) return;

    progress_wave_->set_can_seek(backend_ && backend_->can_seek());
    progress_wave_->set_duration(duration);
    progress_wave_->set_progress(duration > 0.0 ? position / duration : 0.0);

    if (time_elapsed_label_) {
        gtk_label_set_text(GTK_LABEL(time_elapsed_label_.get()), format_time(static_cast<int>(position)).c_str());
    }
    if (time_remaining_label_) {
        gtk_label_set_text(GTK_LABEL(time_remaining_label_.get()),
                           (std::string("-") + format_time(static_cast<int>(duration - position))).c_str());
    }

    // Update compact lyrics
    if (compact_lyrics_label_ && has_lyrics_ && current_lyrics_.valid()) {
        const int pos_ms = static_cast<int>(position * 1000);
        int index = -1;
        for (size_t i = 0; i < current_lyrics_.lines.size(); ++i) {
            if (current_lyrics_.lines[i].start_ms <= pos_ms) {
                index = static_cast<int>(i);
            } else {
                break;
            }
        }
        if (index >= 0) {
            const auto& line = current_lyrics_.lines[static_cast<size_t>(index)];
            if (index != current_lyric_index_) {
                compact_lyrics_label_->set_text(line.text);
                current_lyric_index_ = index;
            }
            const int next_ms = index + 1 < static_cast<int>(current_lyrics_.lines.size())
                                    ? current_lyrics_.lines[static_cast<size_t>(index + 1)].start_ms
                                    : static_cast<int>(duration * 1000);
            const double span = static_cast<double>(std::max(1, next_ms - line.start_ms));
            const double fraction = std::clamp(
                static_cast<double>(pos_ms - line.start_ms) / span, 0.0, 1.0);
            compact_lyrics_label_->set_progress(fraction);
        }
    }
}

void PiPWindow::set_playing(bool playing) {
    if (progress_wave_) {
        progress_wave_->set_playing(playing);
    }
    if (play_pause_button_) {
        GtkWidget* icon = gtk_image_new_from_icon_name(
            playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
        gtk_button_set_child(GTK_BUTTON(play_pause_button_.get()), icon);
        const char* label = playing ? "Pause" : "Play";
        gtk_widget_set_tooltip_text(play_pause_button_.get(), label);
        accessibility::set_label(play_pause_button_.get(), label);
    }
}

void PiPWindow::setup_ui() {
    window_.reset(gtk_window_new());
    g_object_ref_sink(window_.get());

    gtk_window_set_title(GTK_WINDOW(window_.get()), "Lollypop — Mini Player");
    gtk_window_set_decorated(GTK_WINDOW(window_.get()), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_.get()), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window_.get()), 400, 200);

    // Enable drag-to-move on undecorated window
    auto* drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), this);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), this);
    gtk_widget_add_controller(window_.get(), GTK_EVENT_CONTROLLER(drag));

    // Keyboard shortcuts
    auto* shortcuts = GTK_SHORTCUT_CONTROLLER(gtk_shortcut_controller_new());
    GActionEntry entries[] = {
        {"play-pause", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->on_play_pause_clicked(nullptr, self);
        }, nullptr, nullptr, nullptr},
        {"seek-backward", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->seek_relative(-5.0);
        }, nullptr, nullptr, nullptr},
        {"seek-forward", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->seek_relative(5.0);
        }, nullptr, nullptr, nullptr},
        {"volume-up", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->adjust_volume(0.05);
        }, nullptr, nullptr, nullptr},
        {"volume-down", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->adjust_volume(-0.05);
        }, nullptr, nullptr, nullptr},
        {"close", [](GSimpleAction*, GVariant*, gpointer self) {
            static_cast<PiPWindow*>(self)->on_close_clicked(nullptr, self);
        }, nullptr, nullptr, nullptr},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app_), entries, 6, this);

    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_space, GdkModifierType(0)),
                         gtk_named_action_new("app.play-pause")));
    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Left, GdkModifierType(0)),
                         gtk_named_action_new("app.seek-backward")));
    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Right, GdkModifierType(0)),
                         gtk_named_action_new("app.seek-forward")));
    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Up, GdkModifierType(0)),
                         gtk_named_action_new("app.volume-up")));
    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Down, GdkModifierType(0)),
                         gtk_named_action_new("app.volume-down")));
    gtk_shortcut_controller_add_shortcut(shortcuts,
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, GdkModifierType(0)),
                         gtk_named_action_new("app.close")));
    gtk_widget_add_controller(window_.get(), GTK_EVENT_CONTROLLER(shortcuts));

    // Root box
    root_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    g_object_ref_sink(root_box_.get());
    gtk_widget_add_css_class(root_box_.get(), "pip-window");
    gtk_widget_set_margin_top(root_box_.get(), 12);
    gtk_widget_set_margin_bottom(root_box_.get(), 12);
    gtk_widget_set_margin_start(root_box_.get(), 12);
    gtk_widget_set_margin_end(root_box_.get(), 12);
    gtk_window_set_child(GTK_WINDOW(window_.get()), root_box_.get());

    // ============================================================
    // HEADER ROW: Artwork + Info + Menu/Close
    // ============================================================
    auto* header = gtk_center_box_new();
    gtk_widget_add_css_class(header, "pip-header");
    gtk_widget_set_visible(header, TRUE);
    gtk_box_append(GTK_BOX(root_box_.get()), header);

    // Start: Artwork + Info
    auto* start_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_visible(start_box, TRUE);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(header), start_box);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(64, 64);
    artwork_view_->set_corner_radius(8.0);
    gtk_widget_add_css_class(artwork_view_->get_widget(), "pip-artwork");
    gtk_box_append(GTK_BOX(start_box), artwork_view_->get_widget());

    info_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
    g_object_ref_sink(info_box_.get());
    gtk_widget_add_css_class(info_box_.get(), "pip-info-box");
    gtk_widget_set_hexpand(info_box_.get(), TRUE);
    gtk_widget_set_halign(info_box_.get(), GTK_ALIGN_START);
    gtk_widget_set_valign(info_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(info_box_.get(), TRUE);

    title_label_.reset(gtk_label_new("No track playing"));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "pip-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_label_.get(), TRUE);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), title_label_.get());

    artist_label_.reset(gtk_label_new("Select a track to begin"));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "pip-subtitle");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(artist_label_.get(), TRUE);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), artist_label_.get());

    album_label_.reset(gtk_label_new(""));
    g_object_ref_sink(album_label_.get());
    gtk_widget_add_css_class(album_label_.get(), "pip-subtitle");
    gtk_label_set_xalign(GTK_LABEL(album_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(album_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(album_label_.get(), TRUE);
    gtk_widget_set_visible(album_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), album_label_.get());

    gtk_box_append(GTK_BOX(start_box), info_box_.get());

    // End: Menu + Close
    auto* end_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_visible(end_box, TRUE);
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(header), end_box);

    menu_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_menu_button_new());
    g_object_ref_sink(menu_button_.get());
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(menu_button_.get()), FALSE);
    gtk_widget_add_css_class(menu_button_.get(), "pip-menu-button");
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(menu_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button_.get()), menu_icon);
    gtk_widget_set_size_request(menu_button_.get(), 32, 32);
    gtk_widget_set_tooltip_text(menu_button_.get(), "More options");
    accessibility::set_label(menu_button_.get(), "More mini-player options");
    gtk_widget_set_visible(menu_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(end_box), menu_button_.get());

    // Menu model
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Open Main Window", "app.show-main-window");
    g_menu_append(menu, "Add to Queue", "pip.add-to-queue");
    g_menu_append(menu, "Add to Playlist…", "pip.add-to-playlist");
    g_menu_append(menu, "Sleep Timer", "pip.sleep-timer");
    g_menu_append(menu, "Settings", "app.preferences");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button_.get()), G_MENU_MODEL(menu));

    close_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
    g_object_ref_sink(close_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(close_button_.get()), FALSE);
    gtk_widget_add_css_class(close_button_.get(), "pip-close-button");
    GtkWidget* close_icon = gtk_image_new_from_icon_name("window-close-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(close_icon), 20);
    gtk_button_set_child(GTK_BUTTON(close_button_.get()), close_icon);
    gtk_widget_set_size_request(close_button_.get(), 32, 32);
    gtk_widget_set_tooltip_text(close_button_.get(), "Close mini player");
    accessibility::set_label(close_button_.get(), "Close mini player");
    gtk_widget_set_visible(close_button_.get(), TRUE);
    g_signal_connect(close_button_.get(), "clicked", G_CALLBACK(on_close_clicked), this);
    gtk_box_append(GTK_BOX(end_box), close_button_.get());

    // ============================================================
    // TRANSPORT ROW: Prev | Play/Pause | Next
    // ============================================================
    transport_box_.reset(gtk_center_box_new());
    g_object_ref_sink(transport_box_.get());
    gtk_widget_add_css_class(transport_box_.get(), "pip-transport");
    gtk_widget_set_visible(transport_box_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_box_.get()), transport_box_.get());

    auto* transport_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_visible(transport_center, TRUE);
    gtk_widget_set_halign(transport_center, GTK_ALIGN_CENTER);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(transport_box_.get()), transport_center);

    // Prev
    prev_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
    g_object_ref_sink(prev_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_.get()), FALSE);
    gtk_widget_add_css_class(prev_button_.get(), "pip-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 24);
    gtk_button_set_child(GTK_BUTTON(prev_button_.get()), prev_icon);
    gtk_widget_set_size_request(prev_button_.get(), 40, 40);
    gtk_widget_set_tooltip_text(prev_button_.get(), "Previous track");
    accessibility::set_label(prev_button_.get(), "Previous track");
    gtk_widget_set_visible(prev_button_.get(), TRUE);
    g_signal_connect(prev_button_.get(), "clicked", G_CALLBACK(on_prev_clicked), this);
    gtk_box_append(GTK_BOX(transport_center), prev_button_.get());

    // Play/Pause - prominent
    play_pause_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_toggle_button_new());
    g_object_ref_sink(play_pause_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(play_pause_button_.get()), FALSE);
    gtk_widget_add_css_class(play_pause_button_.get(), "pip-play-pause");
    GtkWidget* pp_icon = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(pp_icon), 24);
    gtk_button_set_child(GTK_BUTTON(play_pause_button_.get()), pp_icon);
    gtk_widget_set_size_request(play_pause_button_.get(), 48, 48);
    gtk_widget_set_tooltip_text(play_pause_button_.get(), "Play");
    accessibility::set_label(play_pause_button_.get(), "Play");
    gtk_widget_set_visible(play_pause_button_.get(), TRUE);
    g_signal_connect(play_pause_button_.get(), "clicked", G_CALLBACK(on_play_pause_clicked), this);
    gtk_box_append(GTK_BOX(transport_center), play_pause_button_.get());

    // Next
    next_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
    g_object_ref_sink(next_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(next_button_.get()), FALSE);
    gtk_widget_add_css_class(next_button_.get(), "pip-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    gtk_button_set_child(GTK_BUTTON(next_button_.get()), next_icon);
    gtk_widget_set_size_request(next_button_.get(), 40, 40);
    gtk_widget_set_tooltip_text(next_button_.get(), "Next track");
    accessibility::set_label(next_button_.get(), "Next track");
    gtk_widget_set_visible(next_button_.get(), TRUE);
    g_signal_connect(next_button_.get(), "clicked", G_CALLBACK(on_next_clicked), this);
    gtk_box_append(GTK_BOX(transport_center), next_button_.get());

    // ============================================================
    // PROGRESS ROW: Time | ProgressWave | Time
    // ============================================================
    auto* progress_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(progress_box, "pip-progress-row");
    gtk_widget_set_visible(progress_box, TRUE);
    gtk_box_append(GTK_BOX(root_box_.get()), progress_box);

    time_elapsed_label_.reset(gtk_label_new("0:00"));
    g_object_ref_sink(time_elapsed_label_.get());
    gtk_widget_add_css_class(time_elapsed_label_.get(), "pip-time");
    gtk_widget_set_halign(time_elapsed_label_.get(), GTK_ALIGN_START);
    gtk_widget_set_visible(time_elapsed_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(progress_box), time_elapsed_label_.get());

    progress_wave_ = std::make_unique<ProgressWave>();
    SpectrumRouter::instance().register_wave(progress_wave_.get());
    GtkWidget* wave = progress_wave_->get_widget();
    gtk_widget_add_css_class(wave, "pip-progress");
    gtk_widget_set_hexpand(wave, TRUE);
    gtk_widget_set_halign(wave, GTK_ALIGN_FILL);
    gtk_widget_set_valign(wave, GTK_ALIGN_CENTER);
    progress_wave_->set_seek_callback([this](double seconds) {
        if (backend_) backend_->seek(seconds);
    });

    // Hover controller for progress bar
    auto* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_progress_enter), this);
    g_signal_connect(motion, "leave", G_CALLBACK(on_progress_leave), this);
    gtk_widget_add_controller(wave, GTK_EVENT_CONTROLLER(motion));

    gtk_box_append(GTK_BOX(progress_box), wave);

    time_remaining_label_.reset(gtk_label_new("0:00"));
    g_object_ref_sink(time_remaining_label_.get());
    gtk_widget_add_css_class(time_remaining_label_.get(), "pip-time");
    gtk_widget_set_halign(time_remaining_label_.get(), GTK_ALIGN_END);
    gtk_widget_set_visible(time_remaining_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(progress_box), time_remaining_label_.get());

    // ============================================================
    // LYRICS ROW: Compact SyncedLyricsLabel
    // ============================================================
    auto* lyrics_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(lyrics_box, "pip-lyrics-row");
    gtk_widget_set_visible(lyrics_box, TRUE);
    gtk_box_append(GTK_BOX(root_box_.get()), lyrics_box);

    compact_lyrics_label_ = std::make_unique<SyncedLyricsLabel>("", 0.0);
    compact_lyrics_label_->set_line_wrap(false);
    compact_lyrics_label_->set_width_chars(50);
    gtk_widget_add_css_class(compact_lyrics_label_->get_widget(), "pip-lyrics");
    gtk_widget_set_hexpand(compact_lyrics_label_->get_widget(), TRUE);
    gtk_box_append(GTK_BOX(lyrics_box), compact_lyrics_label_->get_widget());

    // ============================================================
    // FOOTER ROW: Shuffle/Repeat/Loop | Volume
    // ============================================================
    footer_box_.reset(gtk_center_box_new());
    g_object_ref_sink(footer_box_.get());
    gtk_widget_add_css_class(footer_box_.get(), "pip-footer");
    gtk_widget_set_visible(footer_box_.get(), TRUE);
    gtk_box_append(GTK_BOX(root_box_.get()), footer_box_.get());

    // Start: Mode toggles in linked box
    auto* modes_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(modes_box, "linked");
    gtk_widget_set_visible(modes_box, TRUE);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(footer_box_.get()), modes_box);

    auto make_mode_toggle = [this](const char* icon, const char* tooltip) {
        GtkWidget* btn = gtk_toggle_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        gtk_widget_add_css_class(btn, "pip-mode-button");
        GtkWidget* icon_w = gtk_image_new_from_icon_name(icon);
        gtk_image_set_pixel_size(GTK_IMAGE(icon_w), 16);
        gtk_button_set_child(GTK_BUTTON(btn), icon_w);
        gtk_widget_set_tooltip_text(btn, tooltip);
        gtk_widget_set_size_request(btn, 32, 32);
        g_object_ref_sink(btn);  // WidgetDeleter expects an owned reference.
        return btn;
    };

    shuffle_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(make_mode_toggle("media-playlist-shuffle-symbolic", "Shuffle"));
    repeat_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(make_mode_toggle("media-playlist-repeat-symbolic", "Repeat"));
    loop_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(make_mode_toggle("media-playlist-repeat-song-symbolic", "Repeat One"));

    gtk_box_append(GTK_BOX(modes_box), shuffle_button_.get());
    gtk_box_append(GTK_BOX(modes_box), repeat_button_.get());
    gtk_box_append(GTK_BOX(modes_box), loop_button_.get());

    g_signal_connect(shuffle_button_.get(), "toggled", G_CALLBACK(on_shuffle_toggled), this);
    g_signal_connect(repeat_button_.get(), "toggled", G_CALLBACK(on_repeat_toggled), this);
    g_signal_connect(loop_button_.get(), "toggled", G_CALLBACK(on_loop_toggled), this);

    // End: Volume
    volume_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_menu_button_new());
    g_object_ref_sink(volume_button_.get());
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(volume_button_.get()), FALSE);
    gtk_widget_add_css_class(volume_button_.get(), "pip-volume-button");
    GtkWidget* volume_icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(volume_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(volume_button_.get()), volume_icon);
    gtk_widget_set_size_request(volume_button_.get(), 32, 32);
    gtk_widget_set_visible(volume_button_.get(), TRUE);
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(footer_box_.get()), volume_button_.get());

    volume_popover_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_popover_new());
    g_object_ref_sink(volume_popover_.get());
    gtk_popover_set_has_arrow(GTK_POPOVER(volume_popover_.get()), FALSE);
    GtkWidget* volume_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(volume_box, 12);
    gtk_widget_set_margin_bottom(volume_box, 12);
    gtk_widget_set_margin_start(volume_box, 12);
    gtk_widget_set_margin_end(volume_box, 12);
    gtk_widget_set_visible(volume_box, TRUE);

    volume_scale_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 100, 1));
    g_object_ref_sink(volume_scale_.get());
    gtk_range_set_value(GTK_RANGE(volume_scale_.get()), 70);
    gtk_range_set_inverted(GTK_RANGE(volume_scale_.get()), TRUE);
    gtk_widget_set_size_request(volume_scale_.get(), -1, 100);
    gtk_widget_add_css_class(volume_scale_.get(), "pip-volume-scale");
    gtk_widget_set_visible(volume_scale_.get(), TRUE);
    g_signal_connect(volume_scale_.get(), "value-changed", G_CALLBACK(on_volume_changed), this);

    gtk_box_append(GTK_BOX(volume_box), volume_scale_.get());
    gtk_popover_set_child(GTK_POPOVER(volume_popover_.get()), volume_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(volume_button_.get()), volume_popover_.get());
}

// ============================================================
// Callbacks
// ============================================================

void PiPWindow::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->previous_track();
}

void PiPWindow::on_play_pause_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->toggle_play_pause();
}

void PiPWindow::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->next_track();
}

void PiPWindow::on_shuffle_toggled(GtkToggleButton* btn, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->set_shuffle(gtk_toggle_button_get_active(btn));
}

void PiPWindow::on_repeat_toggled(GtkToggleButton* btn, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->set_repeat(gtk_toggle_button_get_active(btn));
}

void PiPWindow::on_loop_toggled(GtkToggleButton* btn, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->set_repeat_one(gtk_toggle_button_get_active(btn));
}

void PiPWindow::on_volume_changed(GtkRange* range, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    const double volume = gtk_range_get_value(range) / 100.0;
    if (self->backend_) self->backend_->set_volume(volume);
}

void PiPWindow::on_close_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->app_) self->app_->hide_pip();
}

void PiPWindow::on_menu_clicked(GtkMenuButton*, gpointer) {
    // Menu is handled by GtkMenuButton automatically
}

void PiPWindow::on_drag_begin(GtkGestureDrag* gesture, double x, double y, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    self->drag_start_x_ = x;
    self->drag_start_y_ = y;

    // GTK 4 delegates top-level moves to GDK; tracking screen coordinates here
    // would be wrong on scaled or multi-monitor setups.
    auto* device = gtk_gesture_get_device(GTK_GESTURE(gesture));
    auto* surface = gtk_native_get_surface(GTK_NATIVE(self->window_.get()));
    if (!device || !GDK_IS_TOPLEVEL(surface)) return;
    const int button = static_cast<int>(
        gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));
    gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device, button, x, y,
                            GDK_CURRENT_TIME);
}

void PiPWindow::on_drag_update(GtkGestureDrag*, double, double, gpointer) {
    // GDK owns the interactive move once gdk_toplevel_begin_move() starts.
}

void PiPWindow::on_progress_enter(GtkEventControllerMotion*, double, double, gpointer) {
    // Could show tooltip with seek time
}

void PiPWindow::on_progress_leave(GtkEventControllerMotion*, gpointer) {
    // Hide tooltip
}

} // namespace lollypop