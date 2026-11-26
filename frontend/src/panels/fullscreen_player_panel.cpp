/**
 * Lollypop C++ - Fullscreen Player Panel Implementation
 */

#include "panels/fullscreen_player_panel.hpp"

#include "panels/progress_wave.hpp"
#include "ui/components/ArtworkView.hpp"
#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>

namespace lollypop {
namespace {

GtkWidget* create_icon_button(const char* icon_name,
                              int pixel_size,
                              const char* tooltip,
                              const char* accessible_label) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "fullscreen-control-button");
    gtk_widget_set_size_request(button, 44, 44);
    gtk_widget_set_tooltip_text(button, tooltip);
    accessibility::set_label(button, accessible_label);

    GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), pixel_size);
    safe_button_set_child(GTK_BUTTON(button), icon);
    return button;
}

GtkWidget* create_icon_toggle_button(const char* icon_name,
                                     int pixel_size,
                                     const char* tooltip,
                                     const char* accessible_label) {
    GtkWidget* button = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "fullscreen-control-button");
    gtk_widget_add_css_class(button, "fullscreen-mode-button");
    gtk_widget_set_size_request(button, 44, 44);
    gtk_widget_set_tooltip_text(button, tooltip);
    accessibility::set_label(button, accessible_label);

    GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), pixel_size);
    safe_button_set_child(GTK_BUTTON(button), icon);
    return button;
}

} // namespace

FullscreenPlayerPanel::FullscreenPlayerPanel() {
    setup_ui();
}

FullscreenPlayerPanel::~FullscreenPlayerPanel() = default;

void FullscreenPlayerPanel::setup_ui() {
    root_ = gtk_overlay_new();
    gtk_widget_add_css_class(root_, "fullscreen-content");
    gtk_widget_set_hexpand(root_, TRUE);
    gtk_widget_set_vexpand(root_, TRUE);
    gtk_widget_set_visible(root_, TRUE);

    center_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(center_box_, "fullscreen-player-center");
    gtk_widget_set_size_request(center_box_, 940, -1);
    gtk_widget_set_hexpand(center_box_, FALSE);
    gtk_widget_set_vexpand(center_box_, TRUE);
    gtk_widget_set_halign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(center_box_, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(root_), center_box_);

    create_artwork();
    create_track_info();
    create_progress();
    create_controls();
}

void FullscreenPlayerPanel::create_artwork() {
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(900, 900);
    artwork_view_->set_corner_radius(50.0);

    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "fullscreen-artwork");
    gtk_widget_set_hexpand(artwork, FALSE);
    gtk_widget_set_vexpand(artwork, FALSE);
    gtk_widget_set_halign(artwork, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(artwork, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(artwork, TRUE);
    safe_box_append(GTK_BOX(center_box_), artwork);
}

void FullscreenPlayerPanel::create_track_info() {
    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(info_box, "fullscreen-track-info");
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(info_box, TRUE);

    title_label_ = gtk_label_new("No track playing");
    gtk_widget_add_css_class(title_label_, "fullscreen-track-title");
    gtk_label_set_wrap(GTK_LABEL(title_label_), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(title_label_), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(title_label_), 48);
    gtk_label_set_justify(GTK_LABEL(title_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(title_label_), 0.5);
    gtk_widget_set_hexpand(title_label_, TRUE);
    gtk_widget_set_visible(title_label_, TRUE);

    subtitle_label_ = gtk_label_new("Select a track to begin");
    gtk_widget_add_css_class(subtitle_label_, "fullscreen-track-subtitle");
    gtk_label_set_wrap(GTK_LABEL(subtitle_label_), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(subtitle_label_), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(subtitle_label_), 64);
    gtk_label_set_justify(GTK_LABEL(subtitle_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(subtitle_label_), 0.5);
    gtk_widget_set_hexpand(subtitle_label_, TRUE);
    gtk_widget_set_visible(subtitle_label_, TRUE);

    safe_box_append(GTK_BOX(info_box), title_label_);
    safe_box_append(GTK_BOX(info_box), subtitle_label_);
    safe_box_append(GTK_BOX(center_box_), info_box);
}

void FullscreenPlayerPanel::create_progress() {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "fullscreen-progress-row");
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_halign(row, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row, TRUE);

    GtkWidget* prev_button = create_icon_button(
        "media-skip-backward-symbolic", 22, "Previous track", "Previous track");
    g_signal_connect(prev_button, "clicked", G_CALLBACK(on_prev_clicked), this);
    safe_box_append(GTK_BOX(row), prev_button);

    progress_wave_ = std::make_unique<ProgressWave>();
    progress_wave_->set_wave_frequency(12.0);
    GtkWidget* progress = progress_wave_->get_widget();
    gtk_widget_add_css_class(progress, "fullscreen-progress");
    gtk_widget_set_hexpand(progress, TRUE);
    gtk_widget_set_halign(progress, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(progress, TRUE);
    progress_wave_->set_seek_callback([this](double seconds) {
        if (seek_callback_) seek_callback_(seconds);
    });
    safe_box_append(GTK_BOX(row), progress);

    GtkWidget* next_button = create_icon_button(
        "media-skip-forward-symbolic", 22, "Next track", "Next track");
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_clicked), this);
    safe_box_append(GTK_BOX(row), next_button);

    safe_box_append(GTK_BOX(center_box_), row);
}

void FullscreenPlayerPanel::create_controls() {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    controls_row_ = row;
    gtk_widget_add_css_class(row, "fullscreen-controls");
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_halign(row, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row, TRUE);

    shuffle_button_ = create_icon_toggle_button(
        "media-playlist-shuffle-symbolic", 18, "Shuffle", "Shuffle");
    g_signal_connect(shuffle_button_, "toggled",
                     G_CALLBACK(on_shuffle_toggled), this);
    safe_box_append(GTK_BOX(row), shuffle_button_);

    repeat_button_ = create_icon_toggle_button(
        "media-playlist-repeat-symbolic", 18, "Repeat all", "Repeat all");
    g_signal_connect(repeat_button_, "toggled",
                     G_CALLBACK(on_repeat_toggled), this);
    safe_box_append(GTK_BOX(row), repeat_button_);

    loop_button_ = create_icon_toggle_button(
        "media-playlist-repeat-song-symbolic", 18, "Repeat one", "Repeat one");
    g_signal_connect(loop_button_, "toggled",
                     G_CALLBACK(on_loop_toggled), this);
    safe_box_append(GTK_BOX(row), loop_button_);

    play_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(play_button_), FALSE);
    gtk_widget_add_css_class(play_button_, "fullscreen-play-button");
    gtk_widget_set_size_request(play_button_, 44, 44);
    gtk_widget_set_tooltip_text(play_button_, "Play");
    accessibility::set_label(play_button_, "Start playback");
    play_icon_ = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_icon_), 24);
    safe_button_set_child(GTK_BUTTON(play_button_), play_icon_);
    g_signal_connect(play_button_, "clicked", G_CALLBACK(on_play_clicked), this);
    safe_box_append(GTK_BOX(row), play_button_);

    for (GtkWidget* button = gtk_widget_get_first_child(row); button;
         button = gtk_widget_get_next_sibling(button)) {
        gtk_widget_set_hexpand(button, TRUE);
    }

    safe_box_append(GTK_BOX(center_box_), row);
}

void FullscreenPlayerPanel::set_track_info(const std::string& title,
                                           const std::string& artist,
                                           const std::string& album,
                                           const std::string& artwork_path) {
    if (title_label_) {
        gtk_label_set_text(GTK_LABEL(title_label_),
                           title.empty() ? "No track playing" : title.c_str());
    }
    if (subtitle_label_) {
        std::string subtitle;
        if (!artist.empty()) subtitle = artist;
        if (!album.empty()) {
            subtitle += subtitle.empty() ? album : " — " + album;
        }
        if (subtitle.empty()) subtitle = "Select a track to begin";
        gtk_label_set_text(GTK_LABEL(subtitle_label_), subtitle.c_str());
    }
    if (artwork_view_) artwork_view_->set_artwork_path(artwork_path);
}

void FullscreenPlayerPanel::set_playback_position(double position,
                                                  double duration,
                                                  bool can_seek) {
    if (!progress_wave_) return;
    progress_wave_->set_can_seek(can_seek);
    progress_wave_->set_duration(duration);
    progress_wave_->set_progress(duration > 0.0
        ? std::clamp(position / duration, 0.0, 1.0)
        : 0.0);
}

void FullscreenPlayerPanel::set_playing(bool playing) {
    playing_ = playing;
    if (!play_icon_) return;
    gtk_image_set_from_icon_name(
        GTK_IMAGE(play_icon_),
        playing_ ? "media-playback-pause-symbolic"
                 : "media-playback-start-symbolic");
    if (play_button_) {
        gtk_widget_set_tooltip_text(play_button_, playing_ ? "Pause" : "Play");
        accessibility::set_label(play_button_,
                                 playing_ ? "Pause playback"
                                          : "Start playback");
    }
    if (progress_wave_) progress_wave_->set_playing(playing);
}

void FullscreenPlayerPanel::set_shuffle(bool enabled) {
    if (!shuffle_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayerPanel::set_repeat(bool enabled) {
    if (!repeat_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayerPanel::set_repeat_one(bool enabled) {
    if (!loop_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayerPanel::set_width(int width) {
    if (!center_box_ || width <= 0) return;
    gtk_widget_set_size_request(center_box_, width, -1);
}

void FullscreenPlayerPanel::set_slide_offset(int margin_end) {
    if (!center_box_) return;
    gtk_widget_set_margin_end(center_box_, margin_end);
}

void FullscreenPlayerPanel::set_artwork_side(int side) {
    if (!artwork_view_ || side <= 0 || std::abs(side - last_artwork_side_) < 8) {
        return;
    }
    artwork_view_->set_preferred_size(side, side);
    artwork_view_->set_corner_radius(static_cast<double>(side) / 18.0);
    last_artwork_side_ = side;
    if (center_box_) gtk_widget_queue_resize(center_box_);
}

void FullscreenPlayerPanel::on_play_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->play_callback_) self->play_callback_();
}

void FullscreenPlayerPanel::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->prev_callback_) self->prev_callback_();
}

void FullscreenPlayerPanel::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->next_callback_) self->next_callback_();
}

void FullscreenPlayerPanel::on_shuffle_toggled(GtkToggleButton* button,
                                               gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->updating_controls_ || !self->shuffle_callback_) return;
    self->shuffle_callback_(gtk_toggle_button_get_active(button) != FALSE);
}

void FullscreenPlayerPanel::on_repeat_toggled(GtkToggleButton* button,
                                              gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->updating_controls_ || !self->repeat_callback_) return;
    self->repeat_callback_(gtk_toggle_button_get_active(button) != FALSE);
}

void FullscreenPlayerPanel::on_loop_toggled(GtkToggleButton* button,
                                            gpointer user_data) {
    auto* self = static_cast<FullscreenPlayerPanel*>(user_data);
    if (self->updating_controls_ || !self->repeat_one_callback_) return;
    self->repeat_one_callback_(gtk_toggle_button_get_active(button) != FALSE);
}

} // namespace lollypop
