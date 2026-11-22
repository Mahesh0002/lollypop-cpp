/**
 * Lollypop C++ - PiP Window Implementation
 *
 * Compact floating player: [Artwork + Title/Artist] [Prev] [Play/Pause] [Waveform] [Next] [Volume] [Close]
 * Wayland: add Hyprland windowrule = float,pin for always-on-top
 */

#include "panels/pip_window.hpp"
#include "panels/waveform_view.hpp"
#include "application.hpp"
#include "ui/components/ArtworkView.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

PiPWindow::PiPWindow(Application* app)
    : app_(app),
      window_(nullptr),
      content_box_(nullptr),
      artwork_view_(nullptr),
      info_box_(nullptr),
      title_label_(nullptr),
      artist_label_(nullptr),
      controls_box_(nullptr),
      prev_button_(nullptr),
      play_pause_button_(nullptr),
      next_button_(nullptr),
      waveform_view_(nullptr),
      waveform_model_(nullptr),
      volume_button_(nullptr),
      volume_scale_(nullptr),
      volume_popover_(nullptr),
      close_button_(nullptr) {
    setup_ui();
}

PiPWindow::~PiPWindow() = default;

void PiPWindow::present() {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_.get()));
    }
}

void PiPWindow::set_backend(IPlayerService* backend) {
    backend_ = backend;
    if (!backend) return;

    // Use multicast callbacks so we don't overwrite MainWindow's callbacks
    backend->add_track_changed_callback([this](const Track& track) {
        update_track_info(track.title, track.artist, track.artwork_path);
        current_track_id_ = track.id;
    });

    backend->add_position_changed_callback([this](double position,
                                                   double duration) {
        update_playback_position(position, duration);
    });

    backend->add_playback_state_changed_callback([this](bool playing) {
        set_playing(playing);
    });

    backend->add_visualization_changed_callback([this](const std::vector<float>& points) {
        set_points(points);
    });

    // Enable controls based on capability
    update_control_sensitivity();
}

void PiPWindow::update_control_sensitivity() {
    if (!backend_) return;
    
    bool has_track = !current_track_id_.empty();
    
    if (prev_button_) gtk_widget_set_sensitive(prev_button_.get(), has_track && backend_->get_shuffle() /* always enable for now */);
    if (next_button_) gtk_widget_set_sensitive(next_button_.get(), has_track);
    if (play_pause_button_) gtk_widget_set_sensitive(play_pause_button_.get(), has_track);
    if (volume_button_) gtk_widget_set_sensitive(volume_button_.get(), true);
}

void PiPWindow::setup_ui() {
    window_.reset(gtk_window_new());
    g_object_ref_sink(window_.get());

    gtk_window_set_title(GTK_WINDOW(window_.get()), "Lollypop — PiP");
    gtk_window_set_decorated(GTK_WINDOW(window_.get()), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_.get()), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window_.get()), 480, 120);

    // Content box
    content_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    g_object_ref_sink(content_box_.get());
    gtk_widget_add_css_class(content_box_.get(), "pip-content");
    gtk_widget_set_margin_top(content_box_.get(), 8);
    gtk_widget_set_margin_bottom(content_box_.get(), 8);
    gtk_widget_set_margin_start(content_box_.get(), 12);
    gtk_widget_set_margin_end(content_box_.get(), 12);
    gtk_widget_set_hexpand(content_box_.get(), TRUE);
    gtk_widget_set_vexpand(content_box_.get(), TRUE);
    gtk_widget_set_visible(content_box_.get(), TRUE);

    gtk_window_set_child(GTK_WINDOW(window_.get()),
                         content_box_.get());

    // ArtworkView
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(88, 88);
    artwork_view_->set_corner_radius(8.0);
    gtk_box_append(GTK_BOX(content_box_.get()),
                   artwork_view_->get_widget());

    // Track info box
    info_box_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    g_object_ref_sink(info_box_.get());
    gtk_widget_set_hexpand(info_box_.get(), TRUE);
    gtk_widget_set_halign(info_box_.get(), GTK_ALIGN_START);
    gtk_widget_set_valign(info_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(info_box_.get(), TRUE);

    title_label_.reset(gtk_label_new("No track playing"));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "pip-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_label_.get(), TRUE);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), title_label_.get());

    artist_label_.reset(gtk_label_new("Select a track to begin"));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "pip-artist");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(artist_label_.get(), TRUE);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), artist_label_.get());

    gtk_box_append(GTK_BOX(content_box_.get()), info_box_.get());

    // Controls box: Prev | Play/Pause | Waveform | Next | Volume | Close
    controls_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    g_object_ref_sink(controls_box_.get());
    gtk_widget_add_css_class(controls_box_.get(), "pip-controls");
    gtk_widget_set_halign(controls_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(controls_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(controls_box_.get(), TRUE);

    // Previous
    prev_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_button_new());
    g_object_ref_sink(prev_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_.get()), FALSE);
    gtk_widget_add_css_class(prev_button_.get(),
                             "pip-control-button");
    GtkWidget* prev_icon =
        gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 20);
    gtk_button_set_child(GTK_BUTTON(prev_button_.get()), prev_icon);
    gtk_widget_set_sensitive(prev_button_.get(), FALSE);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(prev_button_.get(), 10);
    gtk_widget_set_margin_end(prev_button_.get(), 10);
    gtk_widget_set_margin_top(prev_button_.get(), 10);
    gtk_widget_set_margin_bottom(prev_button_.get(), 10);
    g_signal_connect(prev_button_.get(), "clicked",
                     G_CALLBACK(on_prev_clicked), this);
    gtk_widget_set_visible(prev_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()), prev_button_.get());

    // Play/Pause
    play_pause_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_button_new());
    g_object_ref_sink(play_pause_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(play_pause_button_.get()), FALSE);
    gtk_widget_add_css_class(play_pause_button_.get(),
                             "pip-control-button");
    play_pause_icon_ = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_pause_icon_), 20);
    gtk_button_set_child(GTK_BUTTON(play_pause_button_.get()), play_pause_icon_);
    gtk_widget_set_sensitive(play_pause_button_.get(), FALSE);
    gtk_widget_set_margin_start(play_pause_button_.get(), 10);
    gtk_widget_set_margin_end(play_pause_button_.get(), 10);
    gtk_widget_set_margin_top(play_pause_button_.get(), 10);
    gtk_widget_set_margin_bottom(play_pause_button_.get(), 10);
    g_signal_connect(play_pause_button_.get(), "clicked",
                     G_CALLBACK(on_play_pause_clicked), this);
    gtk_widget_set_visible(play_pause_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()), play_pause_button_.get());

    // Waveform
    waveform_view_ = std::make_unique<WaveformView>();
    waveform_view_->set_preferred_size(180, 28);
    gtk_widget_set_hexpand(waveform_view_->get_widget(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()),
                   waveform_view_->get_widget());

    // Next
    next_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_button_new());
    g_object_ref_sink(next_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(next_button_.get()), FALSE);
    gtk_widget_add_css_class(next_button_.get(),
                             "pip-control-button");
    GtkWidget* next_icon =
        gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 20);
    gtk_button_set_child(GTK_BUTTON(next_button_.get()), next_icon);
    gtk_widget_set_sensitive(next_button_.get(), FALSE);
    gtk_widget_set_margin_start(next_button_.get(), 10);
    gtk_widget_set_margin_end(next_button_.get(), 10);
    gtk_widget_set_margin_top(next_button_.get(), 10);
    gtk_widget_set_margin_bottom(next_button_.get(), 10);
    g_signal_connect(next_button_.get(), "clicked",
                     G_CALLBACK(on_next_clicked), this);
    gtk_widget_set_visible(next_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()), next_button_.get());

    // Volume button with popover
    volume_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_menu_button_new());
    g_object_ref_sink(volume_button_.get());
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(volume_button_.get()), FALSE);
    gtk_widget_add_css_class(volume_button_.get(), "pip-volume-button");
    GtkWidget* volume_icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(volume_icon), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(volume_button_.get()), volume_icon);
    gtk_widget_set_sensitive(volume_button_.get(), TRUE);
    gtk_widget_set_margin_start(volume_button_.get(), 10);
    gtk_widget_set_margin_end(volume_button_.get(), 10);
    gtk_widget_set_margin_top(volume_button_.get(), 10);
    gtk_widget_set_margin_bottom(volume_button_.get(), 10);
    gtk_widget_set_visible(volume_button_.get(), TRUE);

    volume_popover_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_popover_new());
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
    g_signal_connect(volume_scale_.get(), "value-changed",
                     G_CALLBACK(on_volume_changed), this);

    gtk_box_append(GTK_BOX(volume_box), volume_scale_.get());
    gtk_popover_set_child(GTK_POPOVER(volume_popover_.get()), volume_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(volume_button_.get()),
                                volume_popover_.get());

    gtk_box_append(GTK_BOX(controls_box_.get()), volume_button_.get());

    // Close button
    close_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(
        gtk_button_new());
    g_object_ref_sink(close_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(close_button_.get()), FALSE);
    gtk_widget_add_css_class(close_button_.get(), "pip-close-button");
    GtkWidget* close_icon = gtk_image_new_from_icon_name("window-close-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(close_icon), 20);
    gtk_button_set_child(GTK_BUTTON(close_button_.get()), close_icon);
    g_signal_connect(close_button_.get(), "clicked",
                     G_CALLBACK(on_close_clicked), this);
    gtk_widget_set_margin_start(close_button_.get(), 10);
    gtk_widget_set_margin_end(close_button_.get(), 10);
    gtk_widget_set_margin_top(close_button_.get(), 10);
    gtk_widget_set_margin_bottom(close_button_.get(), 10);
    gtk_widget_set_visible(close_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()), close_button_.get());

    gtk_box_append(GTK_BOX(content_box_.get()), controls_box_.get());

    // Waveform model
    waveform_model_ = std::make_unique<VisualizerModel>();
    waveform_view_->set_model(waveform_model_.get());
}

void PiPWindow::update_track_info(
    const std::string& title,
    const std::string& artist,
    const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(title_label_.get()), title.c_str());
    gtk_label_set_text(GTK_LABEL(artist_label_.get()), artist.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artwork_path);
    }
    
    update_control_sensitivity();
}

void PiPWindow::update_playback_position(double position,
                                          double duration) {
    double progress = duration > 0 ? position / duration : 0.0;
    if (waveform_view_) {
        waveform_view_->set_progress(progress);
    }
}

void PiPWindow::set_playing(bool playing) {
    is_playing_ = playing;
    if (waveform_view_) {
        waveform_view_->set_playing(playing);
    }
    // Update play/pause icon
    if (play_pause_icon_) {
        gtk_image_set_from_icon_name(GTK_IMAGE(play_pause_icon_),
            playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
    }
}

void PiPWindow::set_points(const std::vector<float>& points) {
    if (waveform_model_ && waveform_view_) {
        waveform_model_->samples = points;
        waveform_model_->sequenceNumber++;
        waveform_view_->set_model(waveform_model_.get());
    }
}

void PiPWindow::on_prev_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PiPWindow* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) {
        self->backend_->previous_track();
    }
}

void PiPWindow::on_play_pause_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PiPWindow* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) {
        self->backend_->toggle_play_pause();
    }
}

void PiPWindow::on_next_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PiPWindow* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) {
        self->backend_->next_track();
    }
}

void PiPWindow::on_volume_changed(GtkRange* range, gpointer user_data) {
    PiPWindow* self = static_cast<PiPWindow*>(user_data);
    double volume = gtk_range_get_value(range) / 100.0;
    if (self->backend_) {
        self->backend_->set_volume(volume);
    }
}

void PiPWindow::on_close_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    PiPWindow* self = static_cast<PiPWindow*>(user_data);
    if (self->app_) {
        self->app_->hide_pip();
    }
}

} // namespace lollypop