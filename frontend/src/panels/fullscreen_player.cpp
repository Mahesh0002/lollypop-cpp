/**
 * Lollypop C++ - Fullscreen Now Playing Implementation
 */

#include "panels/fullscreen_player.hpp"

#include "main_window.hpp"
#include "panels/fullscreen_close_button.hpp"
#include "panels/fullscreen_lyrics_panel.hpp"
#include "panels/fullscreen_player_panel.hpp"
#include "panels/fullscreen_queue_panel.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "services/ILyricsService.hpp"
#include "types.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace lollypop {
namespace {

constexpr int kLyricsPanelMaxWidth = 940;
constexpr int kPlayerLeftInset = 100;
constexpr int kLyricsGap = 100;
constexpr int kLyricsRightInset = 200;
constexpr int kDividerWidth = 3;
constexpr double kPanelAnimMs = 340.0;

int player_panel_width(int root_width) {
    // Both panels are the same width: left inset + gap + right inset must fit
    // around two equal panels. The width is identical in both reveal states.
    const int reserved =
        kPlayerLeftInset + kLyricsGap + kLyricsRightInset;
    return std::clamp(
        (root_width - reserved) / 2,
        360,
        kLyricsPanelMaxWidth);
}

int panel_slide_offset(int root_width, int panel_width, double reveal) {
    if (reveal <= 0.0) return 0;
    const int centered_left = (root_width - panel_width) / 2;
    const int target_offset = std::max(0, centered_left - kPlayerLeftInset);
    return static_cast<int>(std::lround(target_offset * reveal));
}

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

} // namespace

FullscreenPlayer::FullscreenPlayer(MainWindow* main_window,
                                   ILyricsService* lyrics_service,
                                   ILibraryService* library_service)
    : main_window_(main_window),
      lyrics_service_(lyrics_service),
      library_service_(library_service),
      root_(nullptr),
      queue_toggle_(nullptr),
      lyrics_toggle_(nullptr),
      panel_divider_(nullptr),
      volume_button_(nullptr),
      volume_revealer_(nullptr),
      volume_scale_(nullptr),
      volume_adjustment_(nullptr) {
    setup_ui();
    gtk_widget_add_tick_callback(root_, on_allocation_tick, this, nullptr);
}

FullscreenPlayer::~FullscreenPlayer() = default;

void FullscreenPlayer::setup_ui() {
    root_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root_, "fullscreen-player");
    gtk_widget_set_hexpand(root_, TRUE);
    gtk_widget_set_vexpand(root_, TRUE);
    gtk_widget_set_visible(root_, TRUE);

    create_top_bar();

    player_panel_ = std::make_unique<FullscreenPlayerPanel>();
    safe_box_append(GTK_BOX(root_), player_panel_->get_widget());

    player_panel_->set_play_callback([this]() {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->toggle_play_pause();
        }
    });
    player_panel_->set_prev_callback([this]() {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->previous_track();
        }
    });
    player_panel_->set_next_callback([this]() {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->next_track();
        }
    });
    player_panel_->set_shuffle_callback([this](bool enabled) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->set_shuffle(enabled);
        }
    });
    player_panel_->set_repeat_callback([this](bool enabled) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->set_repeat(enabled);
        }
    });
    player_panel_->set_repeat_one_callback([this](bool enabled) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->set_repeat_one(enabled);
        }
    });
    player_panel_->set_seek_callback([this](double seconds) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->seek(seconds);
        }
    });

    create_divider();
    create_queue();
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

    close_button_ = std::make_unique<FullscreenCloseButton>();
    close_button_->set_clicked_callback([this]() {
        if (main_window_) main_window_->hide_fullscreen_player();
    });
    safe_box_append(GTK_BOX(top_bar), close_button_->get_widget());

    safe_box_append(GTK_BOX(root_), top_bar);
}

void FullscreenPlayer::create_divider() {
    panel_divider_ = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(panel_divider_, "fullscreen-panel-divider");
    gtk_widget_set_size_request(panel_divider_, kDividerWidth, -1);
    gtk_widget_set_hexpand(panel_divider_, FALSE);
    gtk_widget_set_vexpand(panel_divider_, TRUE);
    gtk_widget_set_halign(panel_divider_, GTK_ALIGN_END);
    gtk_widget_set_valign(panel_divider_, GTK_ALIGN_FILL);
    gtk_widget_set_can_target(panel_divider_, FALSE);
    gtk_widget_set_opacity(panel_divider_, 0.0);
    gtk_widget_set_visible(panel_divider_, TRUE);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(player_panel_->get_widget()), panel_divider_);
}

void FullscreenPlayer::create_queue() {
    queue_panel_ = std::make_unique<FullscreenQueuePanel>(
        main_window_, library_service_);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(player_panel_->get_widget()), queue_panel_->get_widget());

    queue_toggle_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(queue_toggle_), FALSE);
    gtk_widget_add_css_class(queue_toggle_, "fullscreen-control-button");
    gtk_widget_set_hexpand(queue_toggle_, TRUE);
    gtk_widget_set_size_request(queue_toggle_, 44, 44);
    gtk_widget_set_tooltip_text(queue_toggle_, "Queue");
    accessibility::set_label(queue_toggle_, "Show queue");
    GtkWidget* queue_icon =
        gtk_image_new_from_icon_name("view-list-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(queue_icon), 20);
    safe_button_set_child(GTK_BUTTON(queue_toggle_), queue_icon);
    g_signal_connect(queue_toggle_, "toggled",
                     G_CALLBACK(on_queue_toggled), this);
    // Row order: queue, shuffle, repeat all, repeat one, play, lyrics.
    gtk_box_prepend(GTK_BOX(player_panel_->get_controls_row()), queue_toggle_);
}

void FullscreenPlayer::create_lyrics() {
    lyrics_panel_ = std::make_unique<FullscreenLyricsPanel>(lyrics_service_);
    lyrics_panel_->set_seek_callback([this](int position_ms) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->seek(position_ms / 1000.0);
        }
    });

    if (main_window_) {
        if (ThemeManager* theme = main_window_->get_theme_manager()) {
            double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
            theme->get_accent_color(r, g, b, a);
            lyrics_panel_->set_accent(GdkRGBA{
                static_cast<float>(r), static_cast<float>(g),
                static_cast<float>(b), static_cast<float>(a)});
        }
    }

    gtk_overlay_add_overlay(
        GTK_OVERLAY(player_panel_->get_widget()),
        lyrics_panel_->get_widget());

    lyrics_toggle_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(lyrics_toggle_), FALSE);
    gtk_widget_add_css_class(lyrics_toggle_, "fullscreen-control-button");
    gtk_widget_set_hexpand(lyrics_toggle_, TRUE);
    gtk_widget_set_size_request(lyrics_toggle_, 44, 44);
    gtk_widget_set_tooltip_text(lyrics_toggle_, "Lyrics");
    accessibility::set_label(lyrics_toggle_, "Show lyrics");
    GtkWidget* lyrics_icon =
        gtk_image_new_from_icon_name("text-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(lyrics_icon), 20);
    safe_button_set_child(GTK_BUTTON(lyrics_toggle_), lyrics_icon);
    g_signal_connect(lyrics_toggle_, "toggled",
                     G_CALLBACK(on_lyrics_toggled), this);
    safe_box_append(GTK_BOX(player_panel_->get_controls_row()), lyrics_toggle_);
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
    safe_box_append(GTK_BOX(player_panel_->get_player_box()), row);
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
    set_queue(backend->get_queue());
    set_playing(backend->is_playing());
}

void FullscreenPlayer::update_track_info(const std::string& title,
                                         const std::string& artist,
                                         const std::string& album,
                                         const std::string& artwork_path) {
    if (player_panel_) {
        player_panel_->set_track_info(title, artist, album, artwork_path);
    }
}

void FullscreenPlayer::update_playback_position(double position,
                                                double duration) {
    current_position_ = position;
    current_duration_ = duration;

    bool can_seek = false;
    if (main_window_) {
        if (IPlayerService* backend = main_window_->get_backend()) {
            can_seek = backend->can_seek();
        }
    }
    if (player_panel_) {
        player_panel_->set_playback_position(position, duration, can_seek);
    }
    if (lyrics_panel_) {
        lyrics_panel_->set_position(static_cast<int>(position * 1000),
                                    static_cast<int>(duration * 1000));
    }
}

void FullscreenPlayer::set_playing(bool playing) {
    playing_ = playing;
    if (player_panel_) player_panel_->set_playing(playing);
}

void FullscreenPlayer::set_volume(double volume) {
    if (!volume_adjustment_) return;
    updating_controls_ = true;
    gtk_adjustment_set_value(volume_adjustment_,
                             std::clamp(volume, 0.0, 1.0));
    updating_controls_ = false;
}

void FullscreenPlayer::set_shuffle(bool enabled) {
    if (player_panel_) player_panel_->set_shuffle(enabled);
}

void FullscreenPlayer::set_repeat(bool enabled) {
    if (player_panel_) player_panel_->set_repeat(enabled);
}

void FullscreenPlayer::set_repeat_one(bool enabled) {
    if (player_panel_) player_panel_->set_repeat_one(enabled);
}

void FullscreenPlayer::request_lyrics_for_track(const std::string& track_id) {
    if (lyrics_panel_) lyrics_panel_->request_lyrics_for_track(track_id);
}

void FullscreenPlayer::set_queue(
    const std::vector<std::string>& queue_track_ids) {
    if (queue_panel_) queue_panel_->set_queue(queue_track_ids);
}

void FullscreenPlayer::apply_layout() {
    if (!root_ || !player_panel_) return;
    const int width = gtk_widget_get_width(root_);
    const int height = gtk_widget_get_height(root_);
    if (width <= 0 || height <= 0) return;

    const int panel_width = player_panel_width(width);
    player_panel_->set_width(panel_width);

    const int offset =
        panel_slide_offset(width, panel_width, panel_reveal_);
    player_panel_->set_slide_offset(offset * 2);

    // Both side panels share the same slot: equal width to the player, held
    // 200px from the right edge so the 100px gap is preserved.
    if (lyrics_panel_) {
        lyrics_panel_->set_width(panel_width);
        lyrics_panel_->set_right_margin(kLyricsRightInset);
    }
    if (queue_panel_) {
        queue_panel_->set_width(panel_width);
        queue_panel_->set_right_margin(kLyricsRightInset);
    }

    // Keep the divider centred inside the actual gap between the two panels.
    if (panel_divider_) {
        const int player_left = (width - panel_width) / 2 - offset;
        const int player_right = player_left + panel_width;
        const int lyrics_left = width - kLyricsRightInset - panel_width;
        const double gap_center =
            0.5 * (static_cast<double>(player_right) + lyrics_left);
        const int margin = std::max(
            0,
            static_cast<int>(std::lround(
                static_cast<double>(width) - gap_center -
                static_cast<double>(kDividerWidth) / 2.0)));
        if (margin != last_divider_margin_) {
            last_divider_margin_ = margin;
            gtk_widget_set_margin_end(panel_divider_, margin);
        }
    }

    // Artwork reaches its 900px target on tall windows and scales down on
    // shorter ones. Metadata and transport need 240px of vertical room.
    const int reserved = 240;
    int side = std::min(panel_width - 8, height - reserved);
    side = std::clamp(side, 200, 900);
    player_panel_->set_artwork_side(side);
}

void FullscreenPlayer::set_panel_visible(bool visible) {
    // Switching lyrics ↔ queue keeps the player in place: only the revealers
    // cross-fade in the shared slot.
    if (visible == panel_visible_ && panel_anim_source_ == 0) return;

    panel_visible_ = visible;
    panel_reveal_from_ = panel_reveal_;
    panel_reveal_target_ = visible ? 1.0 : 0.0;
    panel_anim_start_ = g_get_monotonic_time();

    if (root_) {
        if (panel_anim_source_ != 0) {
            gtk_widget_remove_tick_callback(root_, panel_anim_source_);
        }
        panel_anim_source_ = gtk_widget_add_tick_callback(
            root_, on_panel_animation_tick, this, nullptr);
    }
}

gboolean FullscreenPlayer::on_panel_animation_tick(GtkWidget*, GdkFrameClock*,
                                                   gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    const gint64 elapsed_us =
        g_get_monotonic_time() - self->panel_anim_start_;
    const double t = std::clamp(
        static_cast<double>(elapsed_us) / (kPanelAnimMs * 1000.0), 0.0, 1.0);
    // Quintic ease-in-out: slow departure and arrival, faster middle.
    const double eased = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    self->panel_reveal_ = self->panel_reveal_from_ +
        (self->panel_reveal_target_ - self->panel_reveal_from_) * eased;

    if (self->panel_divider_) {
        gtk_widget_set_opacity(self->panel_divider_, self->panel_reveal_);
    }
    self->apply_layout();
    if (self->root_) gtk_widget_queue_resize(self->root_);

    if (t >= 1.0) {
        self->panel_reveal_ = self->panel_reveal_target_;
        self->panel_anim_source_ = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
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
    self->apply_layout();
    return G_SOURCE_CONTINUE;
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

void FullscreenPlayer::on_lyrics_toggled(GtkToggleButton* button,
                                         gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_toggles_) return;
    const bool visible = gtk_toggle_button_get_active(button) != FALSE;

    if (visible && self->queue_toggle_) {
        self->updating_toggles_ = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->queue_toggle_),
                                     FALSE);
        self->updating_toggles_ = false;
        gtk_widget_set_tooltip_text(self->queue_toggle_, "Queue");
        accessibility::set_label(self->queue_toggle_, "Show queue");
    }
    self->lyrics_visible_ = visible;
    if (visible) self->queue_visible_ = false;
    if (self->lyrics_panel_) self->lyrics_panel_->set_visible(visible);
    if (self->queue_panel_) {
        self->queue_panel_->set_visible(self->queue_visible_);
    }
    self->set_panel_visible(visible || self->queue_visible_);
    if (self->root_) gtk_widget_queue_resize(self->root_);

    gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                visible ? "Hide lyrics" : "Lyrics");
    accessibility::set_label(GTK_WIDGET(button),
                             visible ? "Hide lyrics" : "Show lyrics");
}

void FullscreenPlayer::on_queue_toggled(GtkToggleButton* button,
                                        gpointer user_data) {
    auto* self = static_cast<FullscreenPlayer*>(user_data);
    if (self->updating_toggles_) return;
    const bool visible = gtk_toggle_button_get_active(button) != FALSE;

    if (visible && self->lyrics_toggle_) {
        self->updating_toggles_ = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->lyrics_toggle_),
                                     FALSE);
        self->updating_toggles_ = false;
        gtk_widget_set_tooltip_text(self->lyrics_toggle_, "Lyrics");
        accessibility::set_label(self->lyrics_toggle_, "Show lyrics");
    }
    self->queue_visible_ = visible;
    if (visible) self->lyrics_visible_ = false;
    if (self->queue_panel_) self->queue_panel_->set_visible(visible);
    if (self->lyrics_panel_) {
        self->lyrics_panel_->set_visible(self->lyrics_visible_);
    }
    self->set_panel_visible(visible || self->lyrics_visible_);
    if (self->root_) gtk_widget_queue_resize(self->root_);

    gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                visible ? "Hide queue" : "Queue");
    accessibility::set_label(GTK_WIDGET(button),
                             visible ? "Hide queue" : "Show queue");
}

} // namespace lollypop
