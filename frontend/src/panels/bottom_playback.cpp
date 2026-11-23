/**
 * Lollypop C++ - Bottom Playback Implementation
 *
 * Single horizontal row matching Quickshell PlayerControl.qml:
 *
 *   [Artwork + Title/Artist] [Prev] [Stretched Wavy Progress] [Next] [Modes] [Volume] [PiP]
 *
 * The wave is drawn with Cairo using WavyLine.qml's sine formula
 * (line width 4, amplitude 0.5x, six cycles, phase advancing while playing).
 * A transparent GtkScale is overlaid for accessible seeking.
 */

#include "panels/bottom_playback.hpp"
#include "main_window.hpp"
#include "application.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/VolumeDial.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <cmath>

namespace lollypop {

BottomPlayback::BottomPlayback(MainWindow* main_window)
    : main_window_(main_window),
      main_box_(nullptr),
      progress_box_(nullptr),
      progress_bar_(nullptr),
      wave_area_(nullptr),
      artwork_view_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      prev_button_(nullptr),
      next_button_(nullptr),
      middle_section_(nullptr),
      right_box_(nullptr),
      volume_dial_(nullptr),
      pip_button_(nullptr),
      shuffle_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr) {
    setup_ui();
    gtk_widget_add_tick_callback(main_box_, on_layout_tick, this, nullptr);

    // The backend restores player.volume before the UI exists. Seed the dial
    // from it so the first click never has to reconcile two different values.
    if (main_window_) {
        if (auto* backend = main_window_->get_backend()) {
            volume_dial_->set_volume(backend->get_volume());
        }
    }
}

BottomPlayback::~BottomPlayback() {
    if (wave_tick_source_ != 0) {
        g_source_remove(wave_tick_source_);
        wave_tick_source_ = 0;
    }
    if (seek_commit_source_ != 0) {
        g_source_remove(seek_commit_source_);
        seek_commit_source_ = 0;
    }
}

void BottomPlayback::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(main_box_, "bottom-playback");
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, FALSE);
    gtk_widget_set_valign(main_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(main_box_, TRUE);

    create_row();
}

void BottomPlayback::create_row() {
    // Four zones in one row, in order:
    //   1. artwork + metadata, fixed 190px (12px inset -> occupies 0..202)
    //   2. 13px gap, so the transport starts at x = 215px
    //   3. transport: prev + wavy progress + next, 1000..1200px
    //   4. playback modes + volume + PiP, right-aligned

    GtkWidget* left_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(left_section, "bottom-playback-left");
    gtk_widget_set_valign(left_section, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(left_section, FALSE);
    gtk_widget_set_halign(left_section, GTK_ALIGN_START);
    gtk_widget_set_size_request(left_section, 190, -1);
    gtk_widget_set_margin_start(left_section, 12);
    gtk_widget_set_margin_end(left_section, 13);
    gtk_widget_set_visible(left_section, TRUE);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(56, 56);
    artwork_view_->set_corner_radius(6.0);
    gtk_widget_add_css_class(artwork_view_->get_widget(), "bottom-playback-artwork");
    gtk_widget_set_size_request(artwork_view_->get_widget(), 56, 56);
    gtk_widget_set_hexpand(artwork_view_->get_widget(), FALSE);
    safe_box_append(GTK_BOX(left_section), artwork_view_->get_widget());

    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_visible(info_box, TRUE);

    track_title_label_ = gtk_label_new("No track playing");
    gtk_widget_add_css_class(track_title_label_, "bottom-playback-title");
    gtk_label_set_xalign(GTK_LABEL(track_title_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(track_title_label_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(track_title_label_, 40, -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(track_title_label_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, "Track title", -1);
    gtk_widget_set_visible(track_title_label_, TRUE);

    track_artist_label_ = gtk_label_new("Select a track");
    gtk_widget_add_css_class(track_artist_label_, "bottom-playback-artist");
    gtk_label_set_xalign(GTK_LABEL(track_artist_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(track_artist_label_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(track_artist_label_, 40, -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(track_artist_label_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, "Track artist", -1);
    gtk_widget_set_visible(track_artist_label_, TRUE);

    safe_box_append(GTK_BOX(info_box), track_title_label_);
    safe_box_append(GTK_BOX(info_box), track_artist_label_);
    safe_box_append(GTK_BOX(left_section), info_box);
    safe_box_append(GTK_BOX(main_box_), left_section);

    // Transport starts at x=215px and owns the flexible center region.
    middle_section_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(middle_section_, "bottom-playback-middle");
    gtk_widget_set_hexpand(middle_section_, FALSE);
    gtk_widget_set_halign(middle_section_, GTK_ALIGN_START);
    gtk_widget_set_valign(middle_section_, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(middle_section_, 1000, -1);
    gtk_widget_set_visible(middle_section_, TRUE);

    prev_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_), FALSE);
    gtk_widget_add_css_class(prev_button_, "bottom-playback-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 24);
    safe_button_set_child(GTK_BUTTON(prev_button_), prev_icon);
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);
    gtk_accessible_update_property(GTK_ACCESSIBLE(prev_button_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, "Previous track", -1);
    gtk_widget_set_margin_start(prev_button_, 4);
    gtk_widget_set_margin_end(prev_button_, 2);
    gtk_widget_set_size_request(prev_button_, 36, 36);
    gtk_widget_set_visible(prev_button_, TRUE);
    safe_box_append(GTK_BOX(middle_section_), prev_button_);

    create_progress(middle_section_);

    next_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(next_button_), FALSE);
    gtk_widget_add_css_class(next_button_, "bottom-playback-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    safe_button_set_child(GTK_BUTTON(next_button_), next_icon);
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);
    gtk_accessible_update_property(GTK_ACCESSIBLE(next_button_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, "Next track", -1);
    gtk_widget_set_margin_start(next_button_, 2);
    gtk_widget_set_margin_end(next_button_, 4);
    gtk_widget_set_size_request(next_button_, 36, 36);
    gtk_widget_set_visible(next_button_, TRUE);
    safe_box_append(GTK_BOX(middle_section_), next_button_);

    safe_box_append(GTK_BOX(main_box_), middle_section_);

    // Absorbs any window width beyond the 1200px center cap so the right
    // controls stay pinned to the end instead of drifting inward.
    GtkWidget* tail_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(tail_spacer, TRUE);
    gtk_widget_set_visible(tail_spacer, TRUE);
    safe_box_append(GTK_BOX(main_box_), tail_spacer);

    create_right_section();
}

gboolean BottomPlayback::on_layout_tick(GtkWidget* widget, GdkFrameClock*,
                                        gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self->middle_section_ || !self->right_box_) return G_SOURCE_CONTINUE;

    const int total_width = gtk_widget_get_width(widget);
    if (total_width <= 0) return G_SOURCE_CONTINUE;

    // 215px is the fixed left zone (12 inset + 190 metadata + 13 gap).
    // Reserve the measured right control cluster plus its 12px trailing inset,
    // then give the transport the remaining space, clamped to 1000..1200px.
    const int right_width = gtk_widget_get_width(self->right_box_);
    const int available = total_width - 215 - right_width - 12;
    const int target = std::clamp(available, 1000, 1200);

    if (target != self->last_middle_width_) {
        self->last_middle_width_ = target;
        gtk_widget_set_size_request(self->middle_section_, target, -1);
    }
    return G_SOURCE_CONTINUE;
}

void BottomPlayback::create_progress(GtkWidget* parent) {
    // The overlay owns all remaining horizontal space. The wave is the visible
    // child; the scale is an invisible but fully interactive layer on top.
    progress_box_ = gtk_overlay_new();
    gtk_widget_add_css_class(progress_box_, "bottom-playback-progress");
    gtk_widget_set_hexpand(progress_box_, TRUE);
    gtk_widget_set_halign(progress_box_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(progress_box_, -1, 28);
    gtk_widget_set_visible(progress_box_, TRUE);

    wave_area_ = gtk_drawing_area_new();
    gtk_widget_add_css_class(wave_area_, "progress-wavy-area");
    gtk_widget_set_hexpand(wave_area_, TRUE);
    gtk_widget_set_halign(wave_area_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(wave_area_, GTK_ALIGN_FILL);
    gtk_widget_set_can_target(wave_area_, FALSE);
    gtk_widget_set_size_request(wave_area_, -1, 28);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(wave_area_),
                                   on_wave_draw, this, nullptr);
    gtk_widget_set_visible(wave_area_, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(progress_box_), wave_area_);

    progress_bar_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                              0.0, 1.0, 0.001);
    gtk_widget_add_css_class(progress_bar_, "progress-wavy-scale");
    gtk_widget_set_hexpand(progress_bar_, TRUE);
    gtk_widget_set_halign(progress_bar_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress_bar_, GTK_ALIGN_FILL);
    gtk_widget_set_opacity(progress_bar_, 0.0);
    gtk_scale_set_draw_value(GTK_SCALE(progress_bar_), FALSE);
    gtk_range_set_value(GTK_RANGE(progress_bar_), 0.0);
    gtk_accessible_update_property(GTK_ACCESSIBLE(progress_bar_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Playback position", -1);
    gtk_widget_set_visible(progress_bar_, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(progress_box_), progress_bar_);

    g_signal_connect(progress_bar_, "value-changed",
                     G_CALLBACK(on_progress_value_changed), this);

    // Track drag state so the wave follows the pointer and the seek is
    // committed once on release instead of once per value-changed tick.
    auto* seek_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(seek_gesture), GDK_BUTTON_PRIMARY);
    // Do not steal the drag gesture GtkRange already uses for seeking.
    gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(seek_gesture), FALSE);
    g_signal_connect(seek_gesture, "pressed", G_CALLBACK(on_progress_pressed), this);
    g_signal_connect(seek_gesture, "released", G_CALLBACK(on_progress_released), this);
    gtk_widget_add_controller(progress_bar_, GTK_EVENT_CONTROLLER(seek_gesture));

    safe_box_append(GTK_BOX(parent), progress_box_);
}

void BottomPlayback::create_right_section() {
    right_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(right_box_, "bottom-playback-right");
    gtk_widget_set_margin_end(right_box_, 12);
    gtk_widget_set_valign(right_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(right_box_, GTK_ALIGN_END);
    gtk_widget_set_visible(right_box_, TRUE);

    gtk_accessible_update_property(GTK_ACCESSIBLE(right_box_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Additional controls", -1);

    GtkWidget* modes_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(modes_box, "bottom-playback-modes");
    gtk_widget_set_valign(modes_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(modes_box, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(modes_box),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Playback modes", -1);

    auto make_mode_button = [this](const char* icon_name,
                                   const char* acc_label,
                                   GCallback toggled_cb) -> GtkWidget* {
        GtkWidget* button = gtk_toggle_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
        gtk_widget_add_css_class(button, "bottom-playback-mode-button");
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
        safe_button_set_child(GTK_BUTTON(button), icon);
        g_signal_connect(button, "toggled", toggled_cb, this);
        gtk_widget_set_sensitive(button, FALSE);
        gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       acc_label, -1);
        gtk_widget_set_size_request(button, 30, 30);
        gtk_widget_set_visible(button, TRUE);
        return button;
    };

    shuffle_button_ = make_mode_button("media-playlist-shuffle-symbolic",
                                       "Shuffle",
                                       G_CALLBACK(on_shuffle_toggled));
    safe_box_append(GTK_BOX(modes_box), shuffle_button_);

    repeat_button_ = make_mode_button("media-playlist-repeat-symbolic",
                                      "Repeat playlist",
                                      G_CALLBACK(on_repeat_toggled));
    safe_box_append(GTK_BOX(modes_box), repeat_button_);

    loop_button_ = make_mode_button("media-playlist-repeat-song-symbolic",
                                    "Repeat track",
                                    G_CALLBACK(on_loop_toggled));
    safe_box_append(GTK_BOX(modes_box), loop_button_);
    safe_box_append(GTK_BOX(right_box_), modes_box);

    volume_dial_ = std::make_unique<VolumeDial>();
    volume_dial_->set_volume_changed_callback([this](double volume) {
        if (main_window_) {
            if (auto* backend = main_window_->get_backend()) {
                backend->set_volume(volume);
            }
        }
    });
    gtk_widget_add_css_class(volume_dial_->get_widget(), "bottom-playback-volume-dial");
    gtk_widget_set_size_request(volume_dial_->get_widget(), 40, 40);
    gtk_widget_set_margin_start(volume_dial_->get_widget(), 4);
    gtk_widget_set_margin_end(volume_dial_->get_widget(), 4);
    gtk_widget_set_visible(volume_dial_->get_widget(), TRUE);
    safe_box_append(GTK_BOX(right_box_), volume_dial_->get_widget());

    pip_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(pip_button_), FALSE);
    gtk_widget_add_css_class(pip_button_, "bottom-playback-pip-button");
    GtkWidget* pip_icon = gtk_image_new_from_icon_name("window-new-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(pip_icon), 22);
    safe_button_set_child(GTK_BUTTON(pip_button_), pip_icon);
    g_signal_connect(pip_button_, "clicked", G_CALLBACK(on_pip_clicked), this);
    gtk_accessible_update_property(GTK_ACCESSIBLE(pip_button_),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   "Picture-in-picture", -1);
    gtk_widget_set_margin_end(pip_button_, 4);
    gtk_widget_set_size_request(pip_button_, 30, 30);
    gtk_widget_set_visible(pip_button_, TRUE);
    safe_box_append(GTK_BOX(right_box_), pip_button_);

    safe_box_append(GTK_BOX(main_box_), right_box_);
}

void BottomPlayback::update_track_info(const std::string& title,
                                       const std::string& artist,
                                       const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(track_title_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(track_artist_label_), artist.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artwork_path);
    }
}

void BottomPlayback::update_playback_position(double position, double duration) {
    current_position_ = position;
    current_duration_ = duration;

    if (seeking_) {
        // User-owned value wins until the drag is released.
        gtk_widget_queue_draw(wave_area_);
        return;
    }

    double progress = duration > 0.0 ? position / duration : 0.0;
    current_progress_ = std::clamp(progress, 0.0, 1.0);

    if (progress_bar_) {
        updating_progress_ = true;
        gtk_range_set_value(GTK_RANGE(progress_bar_), current_progress_);
        updating_progress_ = false;
    }
    gtk_widget_queue_draw(wave_area_);
}

void BottomPlayback::set_playing(bool playing) {
    if (playing_ == playing) return;

    playing_ = playing;
    if (playing_) {
        start_wave_animation();
    } else {
        stop_wave_animation();
    }
}

void BottomPlayback::set_volume(double volume) {
    if (volume_dial_) {
        volume_dial_->set_volume(volume);
    }
}

void BottomPlayback::set_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    if (volume_dial_) {
        gtk_widget_set_sensitive(volume_dial_->get_widget(), sensitive);
    }
    gtk_widget_set_sensitive(pip_button_, sensitive);
    gtk_widget_set_sensitive(shuffle_button_, true);
    gtk_widget_set_sensitive(repeat_button_, true);
    gtk_widget_set_sensitive(loop_button_, true);
}

void BottomPlayback::set_shuffle(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_), enabled);
}

void BottomPlayback::set_repeat(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_), enabled);
}

void BottomPlayback::set_loop(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_), enabled);
}

void BottomPlayback::start_wave_animation() {
    if (wave_tick_source_ != 0) return;

    last_frame_time_ = g_get_monotonic_time();
    wave_tick_source_ = g_timeout_add(kWaveFrameMs, on_wave_tick, this);
}

void BottomPlayback::stop_wave_animation() {
    if (wave_tick_source_ != 0) {
        g_source_remove(wave_tick_source_);
        wave_tick_source_ = 0;
    }
    last_frame_time_ = 0;
    gtk_widget_queue_draw(wave_area_);
}

void BottomPlayback::commit_pending_seek() {
    if (pending_seek_fraction_ < 0.0) return;
    if (!main_window_ || current_duration_ <= 0.0) {
        pending_seek_fraction_ = -1.0;
        return;
    }

    IPlayerService* backend = main_window_->get_backend();
    if (backend) {
        backend->seek(std::clamp(pending_seek_fraction_, 0.0, 1.0) *
                      current_duration_);
    }
    pending_seek_fraction_ = -1.0;
}

void BottomPlayback::on_wave_draw(GtkDrawingArea*, cairo_t* cr,
                                  int width, int height, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self || width <= 0 || height <= 0) return;

    const double line_width = kWaveLineWidth;
    const double amplitude = line_width * kWaveAmplitudeMultiplier;
    const double center_y = height / 2.0;
    const double phase = self->wave_phase_;

    auto wave_y = [&](double x) {
        return center_y + amplitude *
            std::sin((2.0 * M_PI * kWaveFrequency * x / width) + phase);
    };

    // Same colors as the rest of the liquid-glass UI: --bg-active and
    // --accent-color (#a78bfa) from style.css.
    const GdkRGBA unplayed = {0.48, 0.44, 0.62, 0.80};
    const GdkRGBA played = {0.655, 0.545, 0.980, 0.95};

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    // Soft glass glow keeps the unplayed wave visible against --bg-secondary.
    cairo_set_line_width(cr, line_width + 6.0);
    cairo_set_source_rgba(cr, played.red, played.green, played.blue, 0.12);
    cairo_move_to(cr, 0.0, wave_y(0.0));
    for (int x = 1; x <= width; ++x) {
        cairo_line_to(cr, static_cast<double>(x), wave_y(x));
    }
    cairo_stroke(cr);

    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgba(cr, unplayed.red, unplayed.green,
                          unplayed.blue, unplayed.alpha);
    cairo_move_to(cr, 0.0, wave_y(0.0));
    for (int x = 1; x <= width; ++x) {
        cairo_line_to(cr, static_cast<double>(x), wave_y(x));
    }
    cairo_stroke(cr);

    const double progress = std::clamp(self->current_progress_, 0.0, 1.0);
    const double end_x = progress * width;
    if (end_x > 0.0) {
        cairo_set_source_rgba(cr, played.red, played.green,
                              played.blue, played.alpha);
        cairo_move_to(cr, 0.0, wave_y(0.0));
        for (int x = 1; x < static_cast<int>(end_x); ++x) {
            cairo_line_to(cr, static_cast<double>(x), wave_y(x));
        }
        cairo_line_to(cr, end_x, wave_y(end_x));
        cairo_stroke(cr);
    }

    // Playhead marker from StyledSlider.qml's thin handle.
    cairo_set_source_rgba(cr, played.red, played.green, played.blue, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, end_x, center_y - 9.0);
    cairo_line_to(cr, end_x, center_y + 9.0);
    cairo_stroke(cr);
    cairo_arc(cr, end_x, center_y, 4.0, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
}

gboolean BottomPlayback::on_wave_tick(gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self) return G_SOURCE_REMOVE;

    const gint64 now = g_get_monotonic_time();
    if (self->last_frame_time_ > 0) {
        const double elapsed_ms =
            static_cast<double>(now - self->last_frame_time_) / 1000.0;
        // WavyLine.qml uses Date.now() / 400 for its phase.
        self->wave_phase_ += elapsed_ms / 400.0;
    }
    self->last_frame_time_ = now;
    gtk_widget_queue_draw(self->wave_area_);
    return G_SOURCE_CONTINUE;
}

void BottomPlayback::on_progress_value_changed(GtkRange* range, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self || self->updating_progress_) return;
    if (self->current_duration_ <= 0.0) return;

    const double fraction = std::clamp(gtk_range_get_value(range), 0.0, 1.0);
    self->pending_seek_fraction_ = fraction;
    self->current_progress_ = fraction;
    gtk_widget_queue_draw(self->wave_area_);

    if (self->seeking_) {
        // Release commits drag seeks; avoid seeking repeatedly mid-drag.
        return;
    }

    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
    }
    self->seek_commit_source_ = g_timeout_add(
        kSeekCommitDelayMs, on_seek_commit_timeout, self);
}

gboolean BottomPlayback::on_progress_pressed(GtkGestureClick*, int, double, double,
                                             gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self) return FALSE;

    self->seeking_ = true;
    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
        self->seek_commit_source_ = 0;
    }
    return FALSE;
}

gboolean BottomPlayback::on_progress_released(GtkGestureClick*, int, double, double,
                                              gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self) return FALSE;

    self->seeking_ = false;
    if (self->seek_commit_source_ != 0) {
        g_source_remove(self->seek_commit_source_);
        self->seek_commit_source_ = 0;
    }
    self->commit_pending_seek();
    return FALSE;
}

gboolean BottomPlayback::on_seek_commit_timeout(gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (!self) return G_SOURCE_REMOVE;

    self->seek_commit_source_ = 0;
    self->commit_pending_seek();
    return G_SOURCE_REMOVE;
}

void BottomPlayback::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->previous_track();
        }
    }
}

void BottomPlayback::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->next_track();
        }
    }
}

void BottomPlayback::on_pip_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_ && self->main_window_->get_application()) {
        self->main_window_->get_application()->show_pip();
    }
}

void BottomPlayback::on_shuffle_toggled(GtkToggleButton* button, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->set_shuffle(gtk_toggle_button_get_active(button));
        }
    }
}

void BottomPlayback::on_repeat_toggled(GtkToggleButton* button, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->set_repeat(gtk_toggle_button_get_active(button));
        }
    }
}

void BottomPlayback::on_loop_toggled(GtkToggleButton* button, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->set_repeat_one(gtk_toggle_button_get_active(button));
        }
    }
}

} // namespace lollypop
