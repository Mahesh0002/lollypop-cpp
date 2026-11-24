/**
 * Lollypop C++ - Bottom Playback Implementation
 *
 *   [Artwork + Title/Artist] [Prev] [Wavy Progress] [Next] [Shuffle/Loop] [PiP]
 *
 * The wide progress strip uses the shared ProgressWave without spectrum
 * input, so it stays a clean decorative wave rather than a visualizer.
 */

#include "panels/bottom_playback.hpp"
#include "panels/pip_window.hpp"
#include "panels/progress_wave.hpp"
#include "main_window.hpp"
#include "application.hpp"
#include "services/IPlayerService.hpp"
#include "ui/components/ArtworkView.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>

namespace lollypop {

BottomPlayback::BottomPlayback(MainWindow* main_window)
    : main_window_(main_window),
      main_box_(nullptr),
      progress_widget_(nullptr),
      artwork_view_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      prev_button_(nullptr),
      next_button_(nullptr),
      middle_section_(nullptr),
      right_box_(nullptr),
      pip_button_(nullptr),
      shuffle_button_(nullptr),
      loop_button_(nullptr) {
    setup_ui();

    if (main_window_) {
        if (auto* backend = main_window_->get_backend()) {
            progress_wave_->set_can_seek(backend->can_seek());
        }
    }
}

BottomPlayback::~BottomPlayback() = default;

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
    // Left metadata: fixed 200px wide, artwork keeps its original 56x56 size.
    GtkWidget* left_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(left_section, "bottom-playback-left");
    gtk_widget_add_css_class(left_section, "bottom-playback-music");
    gtk_widget_set_size_request(left_section, 200, -1);
    gtk_widget_set_valign(left_section, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(left_section, FALSE);
    gtk_widget_set_halign(left_section, GTK_ALIGN_START);
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

    // Transport: Prev | shared wavy progress | Next.
    middle_section_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(middle_section_, "bottom-playback-middle");
    gtk_widget_set_hexpand(middle_section_, TRUE);
    gtk_widget_set_halign(middle_section_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(middle_section_, GTK_ALIGN_CENTER);
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

    create_right_section();
}

void BottomPlayback::create_progress(GtkWidget* parent) {
    progress_wave_ = std::make_unique<ProgressWave>();
    progress_wave_->set_wave_frequency(18.0);
    progress_widget_ = progress_wave_->get_widget();

    gtk_widget_add_css_class(progress_widget_, "bottom-playback-progress");
    gtk_widget_set_hexpand(progress_widget_, TRUE);
    gtk_widget_set_halign(progress_widget_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress_widget_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(progress_widget_, TRUE);

    progress_wave_->set_seek_callback([this](double seconds) {
        if (!main_window_) return;
        if (auto* backend = main_window_->get_backend()) {
            backend->seek(seconds);
        }
    });

    safe_box_append(GTK_BOX(parent), progress_widget_);
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

    loop_button_ = make_mode_button("media-playlist-repeat-song-symbolic",
                                    "Repeat track",
                                    G_CALLBACK(on_loop_toggled));
    safe_box_append(GTK_BOX(modes_box), loop_button_);
    safe_box_append(GTK_BOX(right_box_), modes_box);

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
    bool can_seek = false;
    if (main_window_) {
        if (auto* backend = main_window_->get_backend()) {
            can_seek = backend->can_seek();
        }
    }

    progress_wave_->set_can_seek(can_seek);
    progress_wave_->set_duration(duration);
    progress_wave_->set_progress(duration > 0.0 ? position / duration : 0.0);
}

void BottomPlayback::set_playing(bool playing) {
    progress_wave_->set_playing(playing);
}

void BottomPlayback::set_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    gtk_widget_set_sensitive(pip_button_, sensitive);
    gtk_widget_set_sensitive(shuffle_button_, sensitive);
    gtk_widget_set_sensitive(loop_button_, sensitive);
}

void BottomPlayback::set_shuffle(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_), enabled);
}

void BottomPlayback::set_loop(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_), enabled);
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
        auto* app = self->main_window_->get_application();
        static bool pip_visible = false;
        if (pip_visible) {
            app->hide_pip();
        } else {
            app->show_pip();
        }
        pip_visible = !pip_visible;
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

void BottomPlayback::on_loop_toggled(GtkToggleButton* button, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->main_window_) {
        if (auto* backend = self->main_window_->get_backend()) {
            backend->set_repeat_one(gtk_toggle_button_get_active(button));
        }
    }
}

} // namespace lollypop
