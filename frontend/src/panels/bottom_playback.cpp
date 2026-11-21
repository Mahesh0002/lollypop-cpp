/**
 * Lollypop C++ - Bottom Playback Implementation
 * 
 * Design per user specification:
 * [Artwork + Metadata] [Rewind] [Progress Bar (wide)] [Fast Forward] [Shuffle/Repeat/Loop] [Volume] [PiP]
 * No play/pause button - shell/MPRIS controls playback
 */

#include "panels/bottom_playback.hpp"
#include "panels/waveform_view.hpp"
#include "main_window.hpp"
#include "application.hpp"
#include "services/IPlayerService.hpp"
#include "models/VisualizerModel.hpp"
#include "ui/components/ArtworkView.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <memory>

namespace lollypop {

BottomPlayback::BottomPlayback(MainWindow* main_window)
    : main_window_(main_window),
      main_box_(nullptr),
      left_box_(nullptr),
      artwork_view_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      center_box_(nullptr),
      prev_button_(nullptr),
      next_button_(nullptr),
      waveform_view_(nullptr),
      waveform_model_(nullptr),
      time_label_(nullptr),
      time_right_label_(nullptr),
      right_box_(nullptr),
      shuffle_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr),
      volume_button_(nullptr),
      volume_scale_(nullptr),
      volume_popover_(nullptr),
      pip_button_(nullptr) {
    setup_ui();
}

BottomPlayback::~BottomPlayback() = default;

void BottomPlayback::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(main_box_, "bottom-playback");
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, FALSE);

    create_left_section();
    create_center_section();
    create_right_section();
}

void BottomPlayback::create_left_section() {
    left_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(left_box_, "bottom-playback-left");
    gtk_widget_set_margin_start(left_box_, 16);
    gtk_widget_set_margin_top(left_box_, 8);
    gtk_widget_set_margin_bottom(left_box_, 8);
    gtk_widget_set_valign(left_box_, GTK_ALIGN_CENTER);

    // ArtworkView component
    artwork_view_ = new ArtworkView();
    artwork_view_->set_preferred_size(56, 56);
    artwork_view_->set_corner_radius(6.0);
    gtk_box_append(GTK_BOX(left_box_), artwork_view_->get_widget());

    // Track info
    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(info_box, GTK_ALIGN_CENTER);

    track_title_label_ = gtk_label_new("No track playing");
    gtk_widget_add_css_class(track_title_label_, "bottom-playback-title");
    gtk_label_set_xalign(GTK_LABEL(track_title_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(track_title_label_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(track_title_label_, 200, -1);

    track_artist_label_ = gtk_label_new("Select a track");
    gtk_widget_add_css_class(track_artist_label_, "bottom-playback-artist");
    gtk_label_set_xalign(GTK_LABEL(track_artist_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(track_artist_label_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(track_artist_label_, 200, -1);

    gtk_box_append(GTK_BOX(info_box), track_title_label_);
    gtk_box_append(GTK_BOX(info_box), track_artist_label_);

    gtk_box_append(GTK_BOX(left_box_), info_box);
    gtk_box_append(GTK_BOX(main_box_), left_box_);
}

void BottomPlayback::create_center_section() {
    center_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(center_box_, "bottom-playback-center");
    gtk_widget_set_hexpand(center_box_, TRUE);
    gtk_widget_set_halign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box_, GTK_ALIGN_CENTER);

    // Rewind button
    prev_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_), FALSE);
    gtk_widget_add_css_class(prev_button_, "bottom-playback-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 24);
    gtk_button_set_child(GTK_BUTTON(prev_button_), prev_icon);
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);
    gtk_widget_set_sensitive(prev_button_, FALSE);  // read-only; shell owns playback
    gtk_box_append(GTK_BOX(center_box_), prev_button_);

    // Progress container: time_left | waveform (expands) | time_right
    GtkWidget* progress_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(progress_container, "bottom-playback-progress-container");
    gtk_widget_set_hexpand(progress_container, TRUE);
    gtk_widget_set_halign(progress_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(progress_container, GTK_ALIGN_CENTER);

    // Time left label
    time_label_ = gtk_label_new("0:00");
    gtk_widget_add_css_class(time_label_, "bottom-playback-time");
    gtk_widget_set_size_request(time_label_, 44, -1);
    gtk_box_append(GTK_BOX(progress_container), time_label_);

    // Waveform widget — wide and prominent, expands to fill available width
    waveform_view_ = new WaveformView();
    waveform_view_->set_preferred_size(600, 48);
    gtk_widget_set_hexpand(waveform_view_->get_widget(), TRUE);
    gtk_box_append(GTK_BOX(progress_container), waveform_view_->get_widget());

    // Time right label
    time_right_label_ = gtk_label_new("0:00");
    gtk_widget_add_css_class(time_right_label_, "bottom-playback-time");
    gtk_widget_set_size_request(time_right_label_, 44, -1);
    gtk_label_set_xalign(GTK_LABEL(time_right_label_), 1.0);
    gtk_box_append(GTK_BOX(progress_container), time_right_label_);

    // Create a VisualizerModel to feed the waveform
    waveform_model_ = std::make_unique<VisualizerModel>();
    waveform_view_->set_model(waveform_model_.get());

    gtk_box_append(GTK_BOX(center_box_), progress_container);
    gtk_box_append(GTK_BOX(main_box_), center_box_);

    // Fast forward button
    next_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(next_button_), FALSE);
    gtk_widget_add_css_class(next_button_, "bottom-playback-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    gtk_button_set_child(GTK_BUTTON(next_button_), next_icon);
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);
    gtk_widget_set_sensitive(next_button_, FALSE);  // read-only; shell owns playback
    gtk_box_append(GTK_BOX(center_box_), next_button_);
}

void BottomPlayback::create_right_section() {
    right_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(right_box_, "bottom-playback-right");
    gtk_widget_set_margin_end(right_box_, 16);
    gtk_widget_set_margin_top(right_box_, 8);
    gtk_widget_set_margin_bottom(right_box_, 8);
    gtk_widget_set_valign(right_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(right_box_, GTK_ALIGN_END);

    // Playback mode buttons (shuffle, repeat, loop)
    GtkWidget* modes_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(modes_box, "bottom-playback-modes");
    gtk_widget_set_valign(modes_box, GTK_ALIGN_CENTER);

    // Shuffle
    shuffle_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(shuffle_button_), FALSE);
    gtk_widget_add_css_class(shuffle_button_, "bottom-playback-mode-button");
    GtkWidget* shuffle_icon = gtk_image_new_from_icon_name("media-shuffle-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(shuffle_icon), 18);
    gtk_button_set_child(GTK_BUTTON(shuffle_button_), shuffle_icon);
    g_signal_connect(shuffle_button_, "toggled", G_CALLBACK(on_shuffle_toggled), this);
    gtk_widget_set_sensitive(shuffle_button_, FALSE);  // will be enabled by MainWindow
    gtk_box_append(GTK_BOX(modes_box), shuffle_button_);

    // Repeat
    repeat_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(repeat_button_), FALSE);
    gtk_widget_add_css_class(repeat_button_, "bottom-playback-mode-button");
    GtkWidget* repeat_icon = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(repeat_icon), 18);
    gtk_button_set_child(GTK_BUTTON(repeat_button_), repeat_icon);
    g_signal_connect(repeat_button_, "toggled", G_CALLBACK(on_repeat_toggled), this);
    gtk_widget_set_sensitive(repeat_button_, FALSE);
    gtk_box_append(GTK_BOX(modes_box), repeat_button_);

    // Loop (repeat one)
    loop_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(loop_button_), FALSE);
    gtk_widget_add_css_class(loop_button_, "bottom-playback-mode-button");
    GtkWidget* loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat-song-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(loop_icon), 18);
    gtk_button_set_child(GTK_BUTTON(loop_button_), loop_icon);
    g_signal_connect(loop_button_, "toggled", G_CALLBACK(on_loop_toggled), this);
    gtk_widget_set_sensitive(loop_button_, FALSE);
    gtk_box_append(GTK_BOX(modes_box), loop_button_);

    gtk_box_append(GTK_BOX(right_box_), modes_box);

    // Volume button with popover
    volume_button_ = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(volume_button_), FALSE);
    gtk_widget_add_css_class(volume_button_, "bottom-playback-volume-button");
    GtkWidget* volume_icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(volume_icon), 22);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(volume_button_), volume_icon);
    gtk_widget_set_sensitive(volume_button_, TRUE);  // Volume is a user control

    volume_popover_ = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(volume_popover_), FALSE);
    GtkWidget* volume_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(volume_box, 12);
    gtk_widget_set_margin_bottom(volume_box, 12);
    gtk_widget_set_margin_start(volume_box, 12);
    gtk_widget_set_margin_end(volume_box, 12);

    volume_scale_ = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 100, 1);
    gtk_range_set_value(GTK_RANGE(volume_scale_), 70);
    gtk_range_set_inverted(GTK_RANGE(volume_scale_), TRUE);
    gtk_widget_set_size_request(volume_scale_, -1, 120);
    gtk_widget_add_css_class(volume_scale_, "bottom-playback-volume-scale");
    g_signal_connect(volume_scale_, "value-changed", G_CALLBACK(on_volume_changed), this);

    gtk_box_append(GTK_BOX(volume_box), volume_scale_);
    gtk_popover_set_child(GTK_POPOVER(volume_popover_), volume_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(volume_button_), volume_popover_);

    // PiP button - opens the floating Picture-in-Picture window
    pip_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(pip_button_), FALSE);
    gtk_widget_add_css_class(pip_button_, "bottom-playback-pip-button");
    GtkWidget* pip_icon = gtk_image_new_from_icon_name("window-new-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(pip_icon), 22);
    gtk_button_set_child(GTK_BUTTON(pip_button_), pip_icon);
    g_signal_connect(pip_button_, "clicked", G_CALLBACK(on_pip_clicked), this);

    gtk_box_append(GTK_BOX(right_box_), volume_button_);
    gtk_box_append(GTK_BOX(right_box_), pip_button_);
    gtk_box_append(GTK_BOX(main_box_), right_box_);
}

void BottomPlayback::update_track_info(const std::string& title, const std::string& artist, const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(track_title_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(track_artist_label_), artist.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artwork_path);
    }
}

void BottomPlayback::update_playback_position(double position, double duration) {
    int pos_min = static_cast<int>(position) / 60;
    int pos_sec = static_cast<int>(position) % 60;
    int dur_min = static_cast<int>(duration) / 60;
    int dur_sec = static_cast<int>(duration) % 60;

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d", pos_min, pos_sec);
    gtk_label_set_text(GTK_LABEL(time_label_), time_str);

    snprintf(time_str, sizeof(time_str), "%d:%02d", dur_min, dur_sec);
    gtk_label_set_text(GTK_LABEL(time_right_label_), time_str);

    double progress = duration > 0 ? position / duration : 0.0;
    if (waveform_view_) {
        waveform_view_->set_progress(progress);
    }
}

void BottomPlayback::set_playing(bool playing) {
    // No play/pause button to update
    // Animate waveform when playing
    if (waveform_view_) {
        waveform_view_->set_playing(playing);
    }
}

void BottomPlayback::set_volume(double volume) {
    gtk_range_set_value(GTK_RANGE(volume_scale_), volume * 100);
}

void BottomPlayback::set_points(const std::vector<float>& points) {
    if (waveform_model_ && waveform_view_) {
        waveform_model_->samples = points;
        waveform_model_->sequenceNumber++;
        waveform_view_->set_model(waveform_model_.get());
    }
}

void BottomPlayback::set_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    gtk_widget_set_sensitive(volume_button_, sensitive);
    gtk_widget_set_sensitive(pip_button_, sensitive);
    // Mode buttons are controlled by MainWindow based on playback state
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

void BottomPlayback::on_prev_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend) {
            backend->previous_track();
        }
    }
}

void BottomPlayback::on_next_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend) {
            backend->next_track();
        }
    }
}

void BottomPlayback::on_volume_changed(GtkRange* range, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    double volume = gtk_range_get_value(range) / 100.0;
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend) {
            backend->set_volume(volume);
        }
    }
}

void BottomPlayback::on_pip_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        self->main_window_->get_application()->show_pip();
    }
}

void BottomPlayback::on_shuffle_toggled(GtkToggleButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_shuffle(gtk_toggle_button_get_active(button));
    }
}

void BottomPlayback::on_repeat_toggled(GtkToggleButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_repeat(gtk_toggle_button_get_active(button));
    }
}

void BottomPlayback::on_loop_toggled(GtkToggleButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_repeat_one(gtk_toggle_button_get_active(button));
    }
}

} // namespace lollypop