/**
 * Lollypop C++ - PiP Window Implementation
 *
 *   [Artwork + Title/Artist] [Prev] [Wavy Progress] [Next] [Volume] [Close]
 */

#include "panels/pip_window.hpp"
#include "panels/progress_wave.hpp"
#include "application.hpp"
#include "ui/components/ArtworkView.hpp"

#include <gtk/gtk.h>

namespace lollypop {

PiPWindow::PiPWindow(Application* app)
    : window_(nullptr),
      content_box_(nullptr),
      artwork_view_(nullptr),
      info_box_(nullptr),
      title_label_(nullptr),
      artist_label_(nullptr),
      controls_box_(nullptr),
      prev_button_(nullptr),
      progress_wave_(nullptr),
      next_button_(nullptr),
      volume_button_(nullptr),
      volume_scale_(nullptr),
      volume_popover_(nullptr),
      close_button_(nullptr),
      app_(app) {
    setup_ui();
}

PiPWindow::~PiPWindow() {
    // Invalidate queued backend callbacks before the widget tree is torn down.
    if (alive_) *alive_ = false;
    backend_ = nullptr;
}

void PiPWindow::present() {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_.get()));
    }
}

void PiPWindow::set_backend(IPlayerService* backend) {
    backend_ = backend;
    if (!backend) return;

    auto alive = alive_;
    backend->add_track_changed_callback([this, alive](const Track& track) {
        if (!*alive) return;
        update_track_info(track.title, track.artist, track.artwork_path);
        current_track_id_ = track.id;
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

    progress_wave_->set_can_seek(backend->can_seek());
    update_control_sensitivity();
}

void PiPWindow::update_control_sensitivity() {
    if (!backend_) return;

    const bool has_track = !current_track_id_.empty();
    if (prev_button_) gtk_widget_set_sensitive(prev_button_.get(), has_track);
    if (next_button_) gtk_widget_set_sensitive(next_button_.get(), has_track);
    if (volume_button_) gtk_widget_set_sensitive(volume_button_.get(), TRUE);
}

void PiPWindow::setup_ui() {
    window_.reset(gtk_window_new());
    g_object_ref_sink(window_.get());

    gtk_window_set_title(GTK_WINDOW(window_.get()), "Lollypop — PiP");
    gtk_window_set_decorated(GTK_WINDOW(window_.get()), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_.get()), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window_.get()), 480, 120);

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
    gtk_window_set_child(GTK_WINDOW(window_.get()), content_box_.get());

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(88, 88);
    artwork_view_->set_corner_radius(8.0);
    gtk_box_append(GTK_BOX(content_box_.get()), artwork_view_->get_widget());

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
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_label_.get(), TRUE);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), title_label_.get());

    artist_label_.reset(gtk_label_new("Select a track to begin"));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "pip-artist");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(artist_label_.get(), TRUE);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(info_box_.get()), artist_label_.get());
    gtk_box_append(GTK_BOX(content_box_.get()), info_box_.get());

    controls_box_.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    g_object_ref_sink(controls_box_.get());
    gtk_widget_add_css_class(controls_box_.get(), "pip-controls");
    gtk_widget_set_halign(controls_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(controls_box_.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(controls_box_.get(), TRUE);

    prev_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
    g_object_ref_sink(prev_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_.get()), FALSE);
    gtk_widget_add_css_class(prev_button_.get(), "pip-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 20);
    gtk_button_set_child(GTK_BUTTON(prev_button_.get()), prev_icon);
    gtk_widget_set_sensitive(prev_button_.get(), FALSE);
    gtk_widget_set_margin_start(prev_button_.get(), 10);
    gtk_widget_set_margin_end(prev_button_.get(), 10);
    gtk_widget_set_margin_top(prev_button_.get(), 10);
    gtk_widget_set_margin_bottom(prev_button_.get(), 10);
    g_signal_connect(prev_button_.get(), "clicked",
                     G_CALLBACK(on_prev_clicked), this);
    gtk_widget_set_visible(prev_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(controls_box_.get()), prev_button_.get());

    progress_wave_ = std::make_unique<ProgressWave>();
    GtkWidget* wave = progress_wave_->get_widget();
    gtk_widget_add_css_class(wave, "pip-progress");
    gtk_widget_set_size_request(wave, 180, 28);
    gtk_widget_set_hexpand(wave, TRUE);
    gtk_widget_set_halign(wave, GTK_ALIGN_FILL);
    gtk_widget_set_valign(wave, GTK_ALIGN_CENTER);
    progress_wave_->set_seek_callback([this](double seconds) {
        if (backend_) backend_->seek(seconds);
    });
    gtk_box_append(GTK_BOX(controls_box_.get()), wave);

    next_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
    g_object_ref_sink(next_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(next_button_.get()), FALSE);
    gtk_widget_add_css_class(next_button_.get(), "pip-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
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

    volume_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_menu_button_new());
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
    g_signal_connect(volume_scale_.get(), "value-changed",
                     G_CALLBACK(on_volume_changed), this);

    gtk_box_append(GTK_BOX(volume_box), volume_scale_.get());
    gtk_popover_set_child(GTK_POPOVER(volume_popover_.get()), volume_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(volume_button_.get()),
                                volume_popover_.get());
    gtk_box_append(GTK_BOX(controls_box_.get()), volume_button_.get());

    close_button_ = std::unique_ptr<GtkWidget, WidgetDeleter>(gtk_button_new());
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
}

void PiPWindow::set_playing(bool playing) {
    if (progress_wave_) {
        progress_wave_->set_playing(playing);
    }
}

void PiPWindow::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->previous_track();
}

void PiPWindow::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<PiPWindow*>(user_data);
    if (self->backend_) self->backend_->next_track();
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

} // namespace lollypop
