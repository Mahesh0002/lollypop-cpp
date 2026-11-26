/**
 * Lollypop C++ - Fullscreen Now Playing Implementation
 */

#include "panels/fullscreen_player.hpp"

#include "main_window.hpp"
#include "panels/progress_wave.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "services/ILyricsService.hpp"
#include "types.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/SyncedLyricsView.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "lyrics/LyricTypes.hpp"
#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

FullscreenPlayer::FullscreenPlayer(MainWindow* main_window,
                                   ILyricsService* lyrics_service,
                                   ILibraryService* library_service)
    : main_window_(main_window),
      lyrics_service_(lyrics_service),
      library_service_(library_service),
      root_(nullptr),
      center_box_(nullptr),
      title_label_(nullptr),
      subtitle_label_(nullptr),
      play_button_(nullptr),
      play_icon_(nullptr),
      volume_button_(nullptr),
      volume_revealer_(nullptr),
      volume_scale_(nullptr),
      shuffle_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr),
      lyrics_toggle_(nullptr),
      lyrics_revealer_(nullptr),
      volume_adjustment_(nullptr) {
    setup_ui();
    gtk_widget_add_tick_callback(root_, on_allocation_tick, this, nullptr);
}

FullscreenPlayer::~FullscreenPlayer() {
    *alive_ = false;
}

void FullscreenPlayer::setup_ui() {
    root_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root_, "fullscreen-player");
    gtk_widget_set_hexpand(root_, TRUE);
    gtk_widget_set_vexpand(root_, TRUE);
    gtk_widget_set_visible(root_, TRUE);

    create_top_bar();

    center_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(center_box_, "fullscreen-player-center");
    gtk_widget_set_size_request(center_box_, 790, -1);
    gtk_widget_set_hexpand(center_box_, TRUE);
    gtk_widget_set_vexpand(center_box_, TRUE);
    gtk_widget_set_halign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(center_box_, TRUE);
    safe_box_append(GTK_BOX(root_), center_box_);

    create_artwork();
    create_track_info();
    create_progress();
    create_controls();
    create_volume();
    create_lyrics();
}

void FullscreenPlayer::create_top_bar() {
    GtkWidget* top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(top_bar, "fullscreen-top-bar");
    gtk_widget_set_hexpand(top_bar, TRUE);
    gtk_widget_set_halign(top_bar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(top_bar, GTK_ALIGN_START);
    gtk_widget_set_margin_start(top_bar, 20);
    gtk_widget_set_margin_end(top_bar, 20);
    gtk_widget_set_margin_top(top_bar, 16);
    gtk_widget_set_visible(top_bar, TRUE);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_widget_set_visible(spacer, TRUE);
    safe_box_append(GTK_BOX(top_bar), spacer);

    GtkWidget* close_button = create_icon_button(
        "window-close-symbolic", 18, "Leave fullscreen", "Leave fullscreen player");
    gtk_widget_add_css_class(close_button, "fullscreen-close-button");
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_close_clicked), this);
    safe_box_append(GTK_BOX(top_bar), close_button);

    safe_box_append(GTK_BOX(root_), top_bar);
}

void FullscreenPlayer::create_artwork() {
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(750, 750);
    artwork_view_->set_corner_radius(26.0);

    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_add_css_class(artwork, "fullscreen-artwork");
    gtk_widget_set_hexpand(artwork, FALSE);
    gtk_widget_set_vexpand(artwork, FALSE);
    gtk_widget_set_halign(artwork, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(artwork, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(artwork, TRUE);
    safe_box_append(GTK_BOX(center_box_), artwork);
}

void FullscreenPlayer::create_track_info() {
    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(info_box, "fullscreen-track-info");
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(info_box, TRUE);

    GtkWidget* title = gtk_label_new("No track playing");
    title_label_ = title;
    gtk_widget_add_css_class(title, "fullscreen-track-title");
    gtk_label_set_wrap(GTK_LABEL(title), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(title), 48);
    gtk_label_set_justify(GTK_LABEL(title), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_visible(title, TRUE);

    GtkWidget* subtitle = gtk_label_new("Select a track to begin");
    subtitle_label_ = subtitle;
    gtk_widget_add_css_class(subtitle, "fullscreen-track-subtitle");
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(subtitle), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(subtitle), 64);
    gtk_label_set_justify(GTK_LABEL(subtitle), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.5);
    gtk_widget_set_hexpand(subtitle, TRUE);
    gtk_widget_set_visible(subtitle, TRUE);

    safe_box_append(GTK_BOX(info_box), title);
    safe_box_append(GTK_BOX(info_box), subtitle);
    safe_box_append(GTK_BOX(center_box_), info_box);
}

void FullscreenPlayer::create_progress() {
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
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->seek(seconds);
        }
    });
    safe_box_append(GTK_BOX(row), progress);

    GtkWidget* next_button = create_icon_button(
        "media-skip-forward-symbolic", 22, "Next track", "Next track");
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_clicked), this);
    safe_box_append(GTK_BOX(row), next_button);

    safe_box_append(GTK_BOX(center_box_), row);
}

void FullscreenPlayer::create_controls() {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
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

    lyrics_toggle_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(lyrics_toggle_), FALSE);
    gtk_widget_add_css_class(lyrics_toggle_, "fullscreen-control-button");
    gtk_widget_set_size_request(lyrics_toggle_, 44, 44);
    gtk_widget_set_tooltip_text(lyrics_toggle_, "Lyrics");
    accessibility::set_label(lyrics_toggle_, "Show lyrics");
    GtkWidget* lyrics_icon = gtk_image_new_from_icon_name("text-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(lyrics_icon), 20);
    safe_button_set_child(GTK_BUTTON(lyrics_toggle_), lyrics_icon);
    g_signal_connect(lyrics_toggle_, "toggled",
                     G_CALLBACK(on_lyrics_toggled), this);
    safe_box_append(GTK_BOX(row), lyrics_toggle_);

    for (GtkWidget* button = gtk_widget_get_first_child(row); button;
         button = gtk_widget_get_next_sibling(button)) {
        gtk_widget_set_hexpand(button, TRUE);
    }

    safe_box_append(GTK_BOX(center_box_), row);
}

void FullscreenPlayer::create_volume() {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "fullscreen-volume-row");
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_halign(row, GTK_ALIGN_FILL);
    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(row, TRUE);

    volume_button_ = create_icon_button(
        "audio-volume-high-symbolic", 20, "Volume", "Volume");
    gtk_widget_set_halign(volume_button_, GTK_ALIGN_START);
    g_signal_connect(volume_button_, "clicked",
                     G_CALLBACK(on_volume_clicked), this);
    safe_box_append(GTK_BOX(row), volume_button_);

    volume_revealer_ = gtk_revealer_new();
    gtk_widget_add_css_class(volume_revealer_, "fullscreen-volume-revealer");
    gtk_revealer_set_transition_type(GTK_REVEALER(volume_revealer_),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(volume_revealer_), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(volume_revealer_), FALSE);
    gtk_widget_set_halign(volume_revealer_, GTK_ALIGN_START);
    gtk_widget_set_valign(volume_revealer_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(volume_revealer_, TRUE);

    volume_adjustment_ = gtk_adjustment_new(1.0, 0.0, 1.0, 0.01, 0.1, 0.0);
    volume_scale_ = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL,
                                  GTK_ADJUSTMENT(volume_adjustment_));
    gtk_widget_add_css_class(volume_scale_, "fullscreen-volume");
    gtk_scale_set_draw_value(GTK_SCALE(volume_scale_), FALSE);
    gtk_widget_set_size_request(volume_scale_, 190, 44);
    gtk_widget_set_valign(volume_scale_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(volume_scale_, TRUE);
    accessibility::set_label(volume_scale_, "Volume level");
    g_signal_connect(volume_adjustment_, "value-changed",
                     G_CALLBACK(on_volume_value_changed), this);

    gtk_revealer_set_child(GTK_REVEALER(volume_revealer_), volume_scale_);
    safe_box_append(GTK_BOX(row), volume_revealer_);
    safe_box_append(GTK_BOX(center_box_), row);
}

void FullscreenPlayer::create_lyrics() {
    lyrics_view_ = std::make_unique<SyncedLyricsView>(
        [this](int position_ms) {
            if (!main_window_) return;
            if (IPlayerService* backend = main_window_->get_backend()) {
                backend->seek(position_ms / 1000.0);
            }
        },
        true);

    if (main_window_) {
        if (ThemeManager* theme = main_window_->get_theme_manager()) {
            double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
            theme->get_accent_color(r, g, b, a);
            lyrics_view_->set_accent(GdkRGBA{
                static_cast<float>(r), static_cast<float>(g),
                static_cast<float>(b), static_cast<float>(a)});
        }
    }

    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(frame, "fullscreen-lyrics-frame");
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_size_request(frame, -1, 200);
    gtk_widget_set_visible(frame, TRUE);
    gtk_frame_set_child(GTK_FRAME(frame), lyrics_view_->get_widget());

    lyrics_revealer_ = gtk_revealer_new();
    gtk_widget_add_css_class(lyrics_revealer_, "fullscreen-lyrics");
    gtk_revealer_set_transition_type(GTK_REVEALER(lyrics_revealer_),
                                      GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(lyrics_revealer_), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(lyrics_revealer_), FALSE);
    gtk_widget_set_hexpand(lyrics_revealer_, TRUE);
    gtk_widget_set_visible(lyrics_revealer_, TRUE);
    gtk_revealer_set_child(GTK_REVEALER(lyrics_revealer_), frame);

    safe_box_append(GTK_BOX(center_box_), lyrics_revealer_);
}

void FullscreenPlayer::sync_from_backend() {
    if (!main_window_) return;
    IPlayerService* backend = main_window_->get_backend();
    if (!backend) return;

    const Track track = backend->get_current_track();
    update_track_info(track.title, track.artist, track.album,
                      track.artwork_path);
    request_lyrics_for_track(track.id);
    update_playback_position(backend->get_position(), backend->get_duration());
    set_volume(backend->get_volume());
    set_shuffle(backend->get_shuffle());
    set_repeat(backend->get_repeat());
    set_repeat_one(backend->get_repeat_one());
    set_playing(backend->is_playing());
}

void FullscreenPlayer::update_track_info(const std::string& title,
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

void FullscreenPlayer::update_playback_position(double position,
                                                 double duration) {
    current_position_ = position;
    current_duration_ = duration;

    if (progress_wave_) {
        bool can_seek = false;
        if (main_window_) {
            if (IPlayerService* backend = main_window_->get_backend()) {
                can_seek = backend->can_seek();
            }
        }
        progress_wave_->set_can_seek(can_seek);
        progress_wave_->set_duration(duration);
        progress_wave_->set_progress(duration > 0.0
            ? std::clamp(position / duration, 0.0, 1.0)
            : 0.0);
    }

    if (lyrics_view_) {
        lyrics_view_->set_position(static_cast<int>(position * 1000),
                                   static_cast<int>(duration * 1000));
    }
}

void FullscreenPlayer::set_playing(bool playing) {
    playing_ = playing;
    apply_playback_icon();
    if (progress_wave_) progress_wave_->set_playing(playing);
}

void FullscreenPlayer::apply_playback_icon() {
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
}

void FullscreenPlayer::set_volume(double volume) {
    if (!volume_adjustment_) return;
    updating_controls_ = true;
    gtk_adjustment_set_value(volume_adjustment_,
                             std::clamp(volume, 0.0, 1.0));
    updating_controls_ = false;
}

void FullscreenPlayer::set_shuffle(bool enabled) {
    if (!shuffle_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayer::set_repeat(bool enabled) {
    if (!repeat_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayer::set_repeat_one(bool enabled) {
    if (!loop_button_) return;
    updating_controls_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_), enabled);
    updating_controls_ = false;
}

void FullscreenPlayer::request_lyrics_for_track(const std::string& track_id) {
    current_track_id_ = track_id;
    if (lyrics_view_) {
        lyrics_view_->set_message(track_id.empty()
            ? "No lyrics available."
            : "Loading lyrics…");
    }
    if (!lyrics_service_ || track_id.empty()) return;

    std::shared_ptr<bool> alive = alive_;
    lyrics_service_->request_synced_lyrics(
        track_id,
        [this, alive, track_id](const std::string& id,
                                const std::string& lrc_text) {
            if (!*alive || id != current_track_id_) return;
            if (!lyrics_view_) return;

            if (lrc_text.empty()) {
                lyrics_view_->set_message("No lyrics available.");
                return;
            }
            if (lrc_text.find('[') != std::string::npos &&
                lrc_text.find(']') != std::string::npos) {
                SyncedLyrics parsed = LrcParser::parse(lrc_text);
                if (parsed.valid()) {
                    lyrics_view_->set_lyrics(parsed);
                    return;
                }
            }
            lyrics_view_->set_message(lrc_text);
        });
}

gboolean FullscreenPlayer::on_allocation_tick(GtkWidget* widget, GdkFrameClock*,
                                              gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    const int width = gtk_widget_get_width(widget);
    const int height = gtk_widget_get_height(widget);
    if (width <= 0 || height <= 0 ||
        (width == self->last_width_ && height == self->last_height_)) {
        return G_SOURCE_CONTINUE;
    }
    self->last_width_ = width;
    self->last_height_ = height;

    const int center_width = std::clamp(width - 48, 360, 790);
    if (self->center_box_) {
        gtk_widget_set_size_request(self->center_box_, center_width, -1);
    }

    // Reserve vertical room for metadata, transport, modes, volume and (when
    // revealed) lyrics. Artwork reaches its 750px target on tall windows and
    // scales down gracefully on shorter ones.
    const int reserved = self->lyrics_visible_ ? 500 : 300;
    int side = std::min(center_width - 8, height - reserved);
    side = std::clamp(side, 200, 750);
    if (self->artwork_view_ && std::abs(side - self->last_artwork_side_) >= 8) {
        self->artwork_view_->set_preferred_size(side, side);
        self->artwork_view_->set_corner_radius(
            static_cast<double>(side) / 28.0);
        self->last_artwork_side_ = side;
        gtk_widget_queue_resize(self->center_box_);
    }
    return G_SOURCE_CONTINUE;
}

void FullscreenPlayer::on_close_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->main_window_) self->main_window_->hide_fullscreen_player();
}

void FullscreenPlayer::on_play_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (!self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->toggle_play_pause();
    }
}

void FullscreenPlayer::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (!self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->previous_track();
    }
}

void FullscreenPlayer::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (!self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->next_track();
    }
}

void FullscreenPlayer::on_volume_clicked(GtkButton* button,
                                         gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (!self->volume_revealer_) return;
    const bool visible =
        !gtk_revealer_get_reveal_child(GTK_REVEALER(self->volume_revealer_));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->volume_revealer_), visible);
    gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                visible ? "Hide volume slider" : "Volume");
    accessibility::set_label(GTK_WIDGET(button),
                             visible ? "Hide volume slider" : "Volume");
}

void FullscreenPlayer::on_volume_value_changed(GtkAdjustment* adjustment,
                                               gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_controls_ || !self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->set_volume(gtk_adjustment_get_value(adjustment));
    }
}

void FullscreenPlayer::on_shuffle_toggled(GtkToggleButton* button,
                                          gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_controls_ || !self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->set_shuffle(gtk_toggle_button_get_active(button));
    }
}

void FullscreenPlayer::on_repeat_toggled(GtkToggleButton* button,
                                         gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_controls_ || !self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->set_repeat(gtk_toggle_button_get_active(button));
    }
}

void FullscreenPlayer::on_loop_toggled(GtkToggleButton* button,
                                       gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_controls_ || !self->main_window_) return;
    if (IPlayerService* backend = self->main_window_->get_backend()) {
        backend->set_repeat_one(gtk_toggle_button_get_active(button));
    }
}

void FullscreenPlayer::on_lyrics_toggled(GtkToggleButton* button,
                                         gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    const bool visible = gtk_toggle_button_get_active(button);
    self->lyrics_visible_ = visible;
    self->last_height_ = 0;
    if (self->lyrics_revealer_) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->lyrics_revealer_),
                                      visible);
        gtk_widget_queue_resize(self->root_);
    }
    gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                visible ? "Hide lyrics" : "Lyrics");
    accessibility::set_label(GTK_WIDGET(button),
                             visible ? "Hide lyrics" : "Show lyrics");
}

} // namespace lollypop
