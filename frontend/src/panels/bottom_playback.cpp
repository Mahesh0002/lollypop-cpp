/**
 * Lollypop C++ - Bottom Playback Implementation
 */

#include "panels/bottom_playback.hpp"
#include "panels/waveform_widget.hpp"
#include "main_window.hpp"
#include "controller.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

BottomPlayback::BottomPlayback(MainWindow* main_window)
    : main_window_(main_window),
      main_box_(nullptr),
      left_box_(nullptr),
      artwork_image_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      center_box_(nullptr),
      prev_button_(nullptr),
      play_button_(nullptr),
      next_button_(nullptr),
      play_icon_(nullptr),
      is_playing_(false),
      progress_box_(nullptr),
      waveform_widget_(nullptr),
      time_label_(nullptr),
      right_box_(nullptr),
      volume_button_(nullptr),
      volume_scale_(nullptr),
      volume_popover_(nullptr),
      pip_button_(nullptr) {
    setup_ui();
}

BottomPlayback::~BottomPlayback() {
    delete waveform_widget_;
}

void BottomPlayback::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(main_box_, "bottom-playback");
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, FALSE);

    create_left_section();
    create_center_section();
    create_progress_section();
    create_right_section();
}

void BottomPlayback::create_left_section() {
    left_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(left_box_, "bottom-playback-left");
    gtk_widget_set_margin_start(left_box_, 16);
    gtk_widget_set_margin_top(left_box_, 8);
    gtk_widget_set_margin_bottom(left_box_, 8);
    gtk_widget_set_valign(left_box_, GTK_ALIGN_CENTER);

    // Artwork
    artwork_image_ = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_), GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_, "bottom-playback-artwork");
    gtk_widget_set_size_request(artwork_image_, 56, 56);

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

    gtk_box_append(GTK_BOX(left_box_), artwork_image_);
    gtk_box_append(GTK_BOX(left_box_), info_box);
    gtk_box_append(GTK_BOX(main_box_), left_box_);
}

void BottomPlayback::create_center_section() {
    center_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(center_box_, "bottom-playback-center");
    gtk_widget_set_hexpand(center_box_, TRUE);
    gtk_widget_set_halign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box_, GTK_ALIGN_CENTER);

    // Control buttons
    GtkWidget* controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_halign(controls_box, GTK_ALIGN_CENTER);

    // Previous
    prev_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_), FALSE);
    gtk_widget_add_css_class(prev_button_, "bottom-playback-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 24);
    gtk_button_set_child(GTK_BUTTON(prev_button_), prev_icon);
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);

    // Play/Pause
    play_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(play_button_), FALSE);
    gtk_widget_add_css_class(play_button_, "bottom-playback-play-button");
    play_icon_ = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_icon_), 36);
    gtk_button_set_child(GTK_BUTTON(play_button_), play_icon_);
    g_signal_connect(play_button_, "clicked", G_CALLBACK(on_play_clicked), this);

    // Next
    next_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(next_button_), FALSE);
    gtk_widget_add_css_class(next_button_, "bottom-playback-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    gtk_button_set_child(GTK_BUTTON(next_button_), next_icon);
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);

    gtk_box_append(GTK_BOX(controls_box), prev_button_);
    gtk_box_append(GTK_BOX(controls_box), play_button_);
    gtk_box_append(GTK_BOX(controls_box), next_button_);

    gtk_box_append(GTK_BOX(center_box_), controls_box);
    gtk_box_append(GTK_BOX(main_box_), center_box_);
}

void BottomPlayback::create_progress_section() {
    // Progress is part of center section, below controls
    progress_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(progress_box_, "bottom-playback-progress");
    gtk_widget_set_hexpand(progress_box_, TRUE);
    gtk_widget_set_halign(progress_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(progress_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(progress_box_, 48);
    gtk_widget_set_margin_end(progress_box_, 48);

    // Progress container: time_left | waveform | time_right
    GtkWidget* progress_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(progress_container, TRUE);
    gtk_widget_set_halign(progress_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(progress_container, GTK_ALIGN_CENTER);

    // Create waveform widget (constructor appends drawing_area_ to parent)
    waveform_widget_ = new WaveformWidget(progress_container);

    GtkWidget* time_left = gtk_label_new("0:00");
    gtk_widget_add_css_class(time_left, "bottom-playback-time");
    gtk_widget_set_size_request(time_left, 50, -1);

    GtkWidget* time_right = gtk_label_new("0:00");
    gtk_widget_add_css_class(time_right, "bottom-playback-time");
    gtk_widget_set_size_request(time_right, 50, -1);
    gtk_label_set_xalign(GTK_LABEL(time_right), 1.0);

    gtk_box_append(GTK_BOX(progress_container), time_left);
    gtk_box_append(GTK_BOX(progress_container), time_right);

    gtk_box_append(GTK_BOX(center_box_), progress_container);
}

void BottomPlayback::create_right_section() {
    right_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(right_box_, "bottom-playback-right");
    gtk_widget_set_margin_end(right_box_, 16);
    gtk_widget_set_margin_top(right_box_, 8);
    gtk_widget_set_margin_bottom(right_box_, 8);
    gtk_widget_set_valign(right_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(right_box_, GTK_ALIGN_END);

    // Volume button with popover - use GtkMenuButton for popover support
    volume_button_ = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(volume_button_), FALSE);
    gtk_widget_add_css_class(volume_button_, "bottom-playback-volume-button");
    GtkWidget* volume_icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(volume_icon), 22);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(volume_button_), volume_icon);

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

    // PiP button
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

    if (!artwork_path.empty()) {
        GFile* file = g_file_new_for_path(artwork_path.c_str());
        gtk_picture_set_file(GTK_PICTURE(artwork_image_), file);
        g_object_unref(file);
    }
}

void BottomPlayback::update_playback_position(double position, double duration) {
    int pos_min = static_cast<int>(position) / 60;
    int pos_sec = static_cast<int>(position) % 60;
    int dur_min = static_cast<int>(duration) / 60;
    int dur_sec = static_cast<int>(duration) % 60;

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d", pos_min, pos_sec, dur_min, dur_sec);
    gtk_label_set_text(GTK_LABEL(time_label_), time_str);

    double progress = duration > 0 ? position / duration : 0.0;
    if (waveform_widget_) {
        waveform_widget_->set_progress(progress);
    }
}

void BottomPlayback::set_playing(bool playing) {
    is_playing_ = playing;
    const char* icon_name = playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
    gtk_image_set_from_icon_name(GTK_IMAGE(play_icon_), icon_name);
    if (waveform_widget_) {
        waveform_widget_->set_playing(playing);
    }
}

void BottomPlayback::set_volume(double volume) {
    gtk_range_set_value(GTK_RANGE(volume_scale_), volume * 100);
}

void BottomPlayback::set_points(const std::vector<float>& points) {
    if (waveform_widget_) {
        waveform_widget_->set_points(points);
    }
}

void BottomPlayback::on_prev_clicked(GtkButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->previous_track();
        }
    }
}

void BottomPlayback::on_play_clicked(GtkButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->toggle_play_pause();
        }
    }
}

void BottomPlayback::on_next_clicked(GtkButton* button, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->next_track();
        }
    }
}

void BottomPlayback::on_volume_changed(GtkRange* range, gpointer user_data) {
    BottomPlayback* self = static_cast<BottomPlayback*>(user_data);
    double volume = gtk_range_get_value(range) / 100.0;
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->set_volume(volume);
        }
    }
}

void BottomPlayback::on_pip_clicked(GtkButton* button, gpointer user_data) {
    // TODO: Implement Picture-in-Picture mode
    std::cout << "PiP clicked" << std::endl;
}

} // namespace lollypop