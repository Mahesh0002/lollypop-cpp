/**
 * Lollypop C++ - Right Now Playing Implementation
 */

#include "panels/right_now_playing.hpp"
#include "panels/waveform_view.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILyricsService.hpp"
#include "services/ILibraryService.hpp"
#include "models/VisualizerModel.hpp"
#include "types.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lollypop {

namespace {

GtkWidget* create_icon_button(const char* icon_name,
                              int pixel_size,
                              bool toggle,
                              GtkWidget** icon_out = nullptr) {
    GtkWidget* button = toggle ? gtk_toggle_button_new() : gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_set_visible(button, TRUE);

    GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), pixel_size);
    gtk_widget_set_visible(icon, TRUE);
    safe_button_set_child(GTK_BUTTON(button), icon);

    if (icon_out) *icon_out = icon;
    return button;
}

GtkWidget* create_page_scroller(GtkWidget* child) {
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_halign(scroller, GTK_ALIGN_FILL);
    gtk_widget_set_valign(scroller, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(scroller, "now-playing-tab-scroll");
    gtk_widget_set_visible(scroller, TRUE);
    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), child);
    return scroller;
}

std::string format_time(double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return std::string(buffer);
}

} // namespace

RightNowPlaying::RightNowPlaying(MainWindow* main_window,
                                 ILyricsService* lyrics_service,
                                 ILibraryService* library_service)
    : main_window_(main_window),
      lyrics_service_(lyrics_service),
      library_service_(library_service),
      main_box_(nullptr),
      content_box_(nullptr),
      artwork_box_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      progress_box_(nullptr),
      time_label_(nullptr),
      duration_label_(nullptr),
      transport_box_(nullptr),
      shuffle_button_(nullptr),
      prev_button_(nullptr),
      play_pause_button_(nullptr),
      play_pause_icon_(nullptr),
      next_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr),
      tabs_box_(nullptr),
      lyrics_tab_(nullptr),
      queue_tab_(nullptr),
      tab_content_stack_(nullptr),
      lyrics_view_(nullptr),
      queue_view_(nullptr),
      lyrics_empty_state_(nullptr) {
    setup_ui();
    gtk_widget_add_tick_callback(main_box_, on_allocation_tick, this, nullptr);
}

RightNowPlaying::~RightNowPlaying() = default;

void RightNowPlaying::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(main_box_, "right-now-playing");
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, TRUE);
    gtk_widget_set_visible(main_box_, TRUE);

    // One continuous column. Only the Lyrics/Queue pane scrolls, so the panel
    // can never leave an unused empty region below a short content block.
    content_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(content_box_, "now-playing-content");
    gtk_widget_set_hexpand(content_box_, TRUE);
    gtk_widget_set_vexpand(content_box_, TRUE);
    gtk_widget_set_halign(content_box_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(content_box_, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start(content_box_, 12);
    gtk_widget_set_margin_end(content_box_, 12);
    gtk_widget_set_margin_top(content_box_, 10);
    gtk_widget_set_margin_bottom(content_box_, 10);
    gtk_widget_set_visible(content_box_, TRUE);
    safe_box_append(GTK_BOX(main_box_), content_box_);

    create_artwork_section();
    create_track_info();
    create_progress_section();
    create_transport_row();
    create_tabs();
}

void RightNowPlaying::create_artwork_section() {
    artwork_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(artwork_box_, "now-playing-artwork");
    gtk_widget_set_hexpand(artwork_box_, TRUE);
    gtk_widget_set_halign(artwork_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(artwork_box_, TRUE);

    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(280, 280);
    artwork_view_->set_corner_radius(12.0);

    GtkWidget* artwork = artwork_view_->get_widget();
    gtk_widget_set_hexpand(artwork, TRUE);
    gtk_widget_set_halign(artwork, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(artwork, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(artwork_box_), artwork);

    safe_box_append(GTK_BOX(content_box_), artwork_box_);
}

void RightNowPlaying::create_track_info() {
    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(info_box, "now-playing-track-info");
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_halign(info_box, GTK_ALIGN_FILL);
    gtk_widget_set_visible(info_box, TRUE);

    track_title_label_ = gtk_label_new("No track playing");
    gtk_widget_add_css_class(track_title_label_, "now-playing-title");
    gtk_label_set_wrap(GTK_LABEL(track_title_label_), TRUE);
    gtk_label_set_justify(GTK_LABEL(track_title_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(track_title_label_), 0.5);
    gtk_widget_set_hexpand(track_title_label_, TRUE);
    gtk_widget_set_visible(track_title_label_, TRUE);

    track_artist_label_ = gtk_label_new("Select a track to begin");
    gtk_widget_add_css_class(track_artist_label_, "now-playing-artist");
    gtk_label_set_wrap(GTK_LABEL(track_artist_label_), TRUE);
    gtk_label_set_justify(GTK_LABEL(track_artist_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(track_artist_label_), 0.5);
    gtk_widget_set_hexpand(track_artist_label_, TRUE);
    gtk_widget_set_visible(track_artist_label_, TRUE);

    safe_box_append(GTK_BOX(info_box), track_title_label_);
    safe_box_append(GTK_BOX(info_box), track_artist_label_);
    safe_box_append(GTK_BOX(content_box_), info_box);
}

void RightNowPlaying::create_progress_section() {
    progress_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(progress_box_, "now-playing-waveform");
    gtk_widget_set_hexpand(progress_box_, TRUE);
    gtk_widget_set_halign(progress_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(progress_box_, TRUE);

    GtkWidget* times = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(times, TRUE);
    gtk_widget_set_halign(times, GTK_ALIGN_FILL);
    gtk_widget_set_visible(times, TRUE);

    time_label_ = gtk_label_new("0:00");
    gtk_widget_add_css_class(time_label_, "now-playing-elapsed");
    gtk_label_set_xalign(GTK_LABEL(time_label_), 0.0);
    gtk_widget_set_visible(time_label_, TRUE);
    safe_box_append(GTK_BOX(times), time_label_);

    duration_label_ = gtk_label_new("0:00");
    gtk_widget_add_css_class(duration_label_, "now-playing-duration");
    gtk_label_set_xalign(GTK_LABEL(duration_label_), 1.0);
    gtk_widget_set_hexpand(duration_label_, TRUE);
    gtk_widget_set_visible(duration_label_, TRUE);
    safe_box_append(GTK_BOX(times), duration_label_);

    safe_box_append(GTK_BOX(progress_box_), times);

    waveform_model_ = std::make_unique<VisualizerModel>();

    waveform_view_ = std::make_unique<WaveformView>();
    waveform_view_->set_model(waveform_model_.get());
    waveform_view_->set_smoothing(2);
    waveform_view_->set_preferred_size(-1, 56);

    GtkWidget* waveform = waveform_view_->get_widget();
    gtk_widget_set_hexpand(waveform, TRUE);
    gtk_widget_set_halign(waveform, GTK_ALIGN_FILL);
    gtk_widget_set_valign(waveform, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(progress_box_), waveform);

    safe_box_append(GTK_BOX(content_box_), progress_box_);
}

void RightNowPlaying::create_transport_row() {
    transport_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(transport_box_), TRUE);
    gtk_widget_add_css_class(transport_box_, "now-playing-transport");
    gtk_widget_set_hexpand(transport_box_, TRUE);
    gtk_widget_set_halign(transport_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(transport_box_, TRUE);

    shuffle_button_ = create_icon_button("media-playlist-shuffle-symbolic",
                                         20, true);
    gtk_widget_add_css_class(shuffle_button_, "now-playing-mode-button");
    g_signal_connect(shuffle_button_, "toggled",
                     G_CALLBACK(on_shuffle_toggled), this);

    prev_button_ = create_icon_button("media-skip-backward-symbolic", 22, false);
    gtk_widget_add_css_class(prev_button_, "now-playing-control-button");
    g_signal_connect(prev_button_, "clicked",
                     G_CALLBACK(on_prev_clicked), this);

    play_pause_button_ = create_icon_button("media-playback-start-symbolic",
                                            24, false, &play_pause_icon_);
    gtk_widget_add_css_class(play_pause_button_, "now-playing-play-button");
    g_signal_connect(play_pause_button_, "clicked",
                     G_CALLBACK(on_play_pause_clicked), this);

    next_button_ = create_icon_button("media-skip-forward-symbolic", 22, false);
    gtk_widget_add_css_class(next_button_, "now-playing-control-button");
    g_signal_connect(next_button_, "clicked",
                     G_CALLBACK(on_next_clicked), this);

    repeat_button_ = create_icon_button("media-playlist-repeat-symbolic",
                                        20, true);
    gtk_widget_add_css_class(repeat_button_, "now-playing-mode-button");
    g_signal_connect(repeat_button_, "toggled",
                     G_CALLBACK(on_repeat_toggled), this);

    loop_button_ = create_icon_button("media-playlist-repeat-song-symbolic",
                                      20, true);
    gtk_widget_add_css_class(loop_button_, "now-playing-mode-button");
    g_signal_connect(loop_button_, "toggled",
                     G_CALLBACK(on_loop_toggled), this);

    safe_box_append(GTK_BOX(transport_box_), shuffle_button_);
    safe_box_append(GTK_BOX(transport_box_), prev_button_);
    safe_box_append(GTK_BOX(transport_box_), play_pause_button_);
    safe_box_append(GTK_BOX(transport_box_), next_button_);
    safe_box_append(GTK_BOX(transport_box_), repeat_button_);
    safe_box_append(GTK_BOX(transport_box_), loop_button_);

    safe_box_append(GTK_BOX(content_box_), transport_box_);
}

void RightNowPlaying::create_lyrics_empty_state() {
    lyrics_empty_state_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(lyrics_empty_state_, "empty-state");
    gtk_widget_set_hexpand(lyrics_empty_state_, TRUE);
    gtk_widget_set_vexpand(lyrics_empty_state_, TRUE);
    gtk_widget_set_halign(lyrics_empty_state_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lyrics_empty_state_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(lyrics_empty_state_, TRUE);

    GtkWidget* icon = gtk_image_new_from_icon_name("text-x-generic-symbolic");
    gtk_widget_add_css_class(icon, "empty-state-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 40);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(icon, TRUE);
    safe_box_append(GTK_BOX(lyrics_empty_state_), icon);

    GtkWidget* title = gtk_label_new("No lyrics available");
    gtk_widget_add_css_class(title, "empty-state-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(title, TRUE);
    safe_box_append(GTK_BOX(lyrics_empty_state_), title);

    GtkWidget* description =
        gtk_label_new("Lyrics are not available for this track.");
    gtk_widget_add_css_class(description, "empty-state-description");
    gtk_label_set_xalign(GTK_LABEL(description), 0.5);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(description), 32);
    gtk_widget_set_halign(description, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(description, TRUE);
    safe_box_append(GTK_BOX(lyrics_empty_state_), description);
}

void RightNowPlaying::create_tabs() {
    tabs_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(tabs_box_, "now-playing-tabs");
    gtk_widget_set_hexpand(tabs_box_, TRUE);
    gtk_widget_set_vexpand(tabs_box_, TRUE);
    gtk_widget_set_halign(tabs_box_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(tabs_box_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(tabs_box_, TRUE);

    GtkWidget* tab_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(tab_buttons), TRUE);
    gtk_widget_add_css_class(tab_buttons, "now-playing-tab-buttons");
    gtk_widget_set_hexpand(tab_buttons, TRUE);
    gtk_widget_set_halign(tab_buttons, GTK_ALIGN_FILL);
    gtk_widget_set_visible(tab_buttons, TRUE);

    lyrics_tab_ = gtk_button_new_with_label("Lyrics");
    gtk_button_set_has_frame(GTK_BUTTON(lyrics_tab_), FALSE);
    gtk_widget_add_css_class(lyrics_tab_, "now-playing-tab-button");
    gtk_widget_set_hexpand(lyrics_tab_, TRUE);
    gtk_widget_set_halign(lyrics_tab_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(lyrics_tab_, TRUE);
    g_signal_connect(lyrics_tab_, "clicked",
                     G_CALLBACK(on_lyrics_tab_clicked), this);

    queue_tab_ = gtk_button_new_with_label("Queue");
    gtk_button_set_has_frame(GTK_BUTTON(queue_tab_), FALSE);
    gtk_widget_add_css_class(queue_tab_, "now-playing-tab-button");
    gtk_widget_set_hexpand(queue_tab_, TRUE);
    gtk_widget_set_halign(queue_tab_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(queue_tab_, TRUE);
    g_signal_connect(queue_tab_, "clicked",
                     G_CALLBACK(on_queue_tab_clicked), this);

    safe_box_append(GTK_BOX(tab_buttons), lyrics_tab_);
    safe_box_append(GTK_BOX(tab_buttons), queue_tab_);
    safe_box_append(GTK_BOX(tabs_box_), tab_buttons);

    tab_content_stack_ = gtk_stack_new();
    gtk_widget_add_css_class(tab_content_stack_, "now-playing-tab-content");
    gtk_widget_set_hexpand(tab_content_stack_, TRUE);
    gtk_widget_set_vexpand(tab_content_stack_, TRUE);
    gtk_widget_set_halign(tab_content_stack_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(tab_content_stack_, GTK_ALIGN_FILL);
    gtk_stack_set_transition_type(GTK_STACK(tab_content_stack_),
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(tab_content_stack_), 160);
    gtk_widget_set_visible(tab_content_stack_, TRUE);

    lyrics_view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(lyrics_view_), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(lyrics_view_),
                                GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(lyrics_view_), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(lyrics_view_), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(lyrics_view_), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(lyrics_view_), 8);
    gtk_widget_add_css_class(lyrics_view_, "now-playing-lyrics");
    gtk_widget_set_hexpand(lyrics_view_, TRUE);
    gtk_widget_set_vexpand(lyrics_view_, TRUE);
    gtk_widget_set_visible(lyrics_view_, TRUE);
    safe_stack_add_named(GTK_STACK(tab_content_stack_),
                         create_page_scroller(lyrics_view_), "lyrics");

    create_lyrics_empty_state();
    safe_stack_add_named(GTK_STACK(tab_content_stack_),
                         lyrics_empty_state_, "lyrics_empty");

    queue_view_ = gtk_list_box_new();
    gtk_widget_add_css_class(queue_view_, "now-playing-queue");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(queue_view_),
                                    GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(queue_view_, TRUE);
    gtk_widget_set_vexpand(queue_view_, TRUE);
    gtk_widget_set_visible(queue_view_, TRUE);
    safe_stack_add_named(GTK_STACK(tab_content_stack_),
                         create_page_scroller(queue_view_), "queue");

    safe_box_append(GTK_BOX(tabs_box_), tab_content_stack_);
    safe_box_append(GTK_BOX(content_box_), tabs_box_);
    show_active_tab();
}

void RightNowPlaying::show_active_tab() {
    if (!tab_content_stack_) return;

    const char* page = "queue";
    if (lyrics_tab_active_) {
        page = has_lyrics_ ? "lyrics" : "lyrics_empty";
    }

    gtk_stack_set_visible_child_name(GTK_STACK(tab_content_stack_), page);

    if (lyrics_tab_active_) {
        gtk_widget_add_css_class(lyrics_tab_, "active");
        gtk_widget_remove_css_class(queue_tab_, "active");
    } else {
        gtk_widget_add_css_class(queue_tab_, "active");
        gtk_widget_remove_css_class(lyrics_tab_, "active");
    }
}

void RightNowPlaying::update_play_pause_icon() {
    if (!play_pause_icon_) return;
    gtk_image_set_from_icon_name(
        GTK_IMAGE(play_pause_icon_),
        playing_ ? "media-playback-pause-symbolic"
                 : "media-playback-start-symbolic");
}

void RightNowPlaying::adapt_to_allocation(int width, int height) {
    if (width <= 0 || height <= 0) return;

    panel_width_ = width;
    panel_height_ = height;

    const int available_width = std::max(120, width - 24);
    const int usable_height = std::max(180, height - 48);

    int side = available_width;
    if (height < 480) {
        side = std::min(available_width, 120);
    } else if (height < 620) {
        side = std::min(available_width, 180);
    } else {
        side = std::min(available_width,
                        std::max(180, std::min(usable_height / 2, 420)));
    }
    side = std::max(72, side);

    if (artwork_view_ && std::abs(side - last_artwork_side_) >= 8) {
        artwork_view_->set_preferred_size(side, side);
        artwork_view_->set_corner_radius(static_cast<double>(side) / 24.0);
        last_artwork_side_ = side;
    }

    const int waveform_height = height < 480 ? 40 : (height < 620 ? 48 : 56);
    if (waveform_view_ && waveform_height != last_waveform_height_) {
        // -1 width lets the drawing area use the entire panel width.
        waveform_view_->set_preferred_size(-1, waveform_height);
        last_waveform_height_ = waveform_height;
    }
}

void RightNowPlaying::update_track_info(const std::string& title,
                                         const std::string& artist,
                                         const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(track_title_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(track_artist_label_), artist.c_str());

    current_artwork_path_ = artwork_path;
    if (artwork_view_) {
        artwork_view_->set_artwork_path(current_artwork_path_);
    }
}

void RightNowPlaying::update_playback_position(double position,
                                               double duration) {
    const double safe_duration = std::max(0.0, duration);
    const double safe_position = std::max(0.0, position);

    gtk_label_set_text(GTK_LABEL(time_label_),
                       format_time(safe_position).c_str());
    gtk_label_set_text(GTK_LABEL(duration_label_),
                       format_time(safe_duration).c_str());

    const double progress = safe_duration > 0.0
                                ? std::min(1.0, safe_position / safe_duration)
                                : 0.0;
    if (waveform_view_) {
        waveform_view_->set_progress(progress);
    }
}

void RightNowPlaying::set_playing(bool playing) {
    playing_ = playing;
    if (waveform_view_) {
        waveform_view_->set_playing(playing);
    }
    update_play_pause_icon();
}

void RightNowPlaying::set_queue(
    const std::vector<std::string>& queue_track_ids) {
    populate_queue(queue_track_ids);
}

void RightNowPlaying::set_lyrics(const std::string& lyrics) {
    has_lyrics_ = !lyrics.empty();

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lyrics_view_));
    gtk_text_buffer_set_text(buffer, lyrics.c_str(), -1);
    show_active_tab();
}

void RightNowPlaying::set_points(const std::vector<float>& points) {
    if (!waveform_model_) return;

    waveform_model_->samples = points;
    waveform_model_->sequenceNumber++;
    if (waveform_view_) {
        waveform_view_->set_model(waveform_model_.get());
    }
}

void RightNowPlaying::set_shuffle(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_), enabled);
}

void RightNowPlaying::set_repeat(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_), enabled);
}

void RightNowPlaying::set_loop(bool enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_), enabled);
}

void RightNowPlaying::set_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(shuffle_button_, sensitive);
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(play_pause_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    gtk_widget_set_sensitive(repeat_button_, sensitive);
    gtk_widget_set_sensitive(loop_button_, sensitive);
}

void RightNowPlaying::set_transport_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(play_pause_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
}

void RightNowPlaying::set_panel_height(int height) {
    adapt_to_allocation(panel_width_ > 0 ? panel_width_ : 340, height);
}

void RightNowPlaying::request_lyrics_for_track(const std::string& track_id) {
    current_track_id_ = track_id;
    set_lyrics("");

    if (!lyrics_service_) return;

    lyrics_service_->request_lyrics(
        track_id,
        [this](std::optional<Lyrics> result) {
            set_lyrics(result && !result->text.empty() ? result->text
                                                       : std::string());
        });
}

void RightNowPlaying::populate_queue(
    const std::vector<std::string>& queue_track_ids) {
    GtkWidget* child = gtk_widget_get_first_child(queue_view_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(queue_view_), child);
        child = next;
    }
    queue_rows_.clear();

    if (queue_track_ids.empty()) {
        GtkWidget* empty = gtk_label_new("Queue is empty");
        gtk_widget_add_css_class(empty, "library-placeholder");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.5);
        gtk_widget_set_hexpand(empty, TRUE);
        gtk_widget_set_vexpand(empty, TRUE);
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(empty, TRUE);
        safe_list_box_append(GTK_LIST_BOX(queue_view_), empty);
        return;
    }

    std::map<std::string, Track> tracks_by_id;
    if (library_service_) {
        for (const Track& track :
             library_service_->get_tracks_by_ids(queue_track_ids)) {
            if (!track.id.empty()) {
                tracks_by_id.emplace(track.id, track);
            }
        }
    }

    queue_rows_.reserve(queue_track_ids.size());
    for (size_t i = 0; i < queue_track_ids.size(); ++i) {
        const std::string& requested_id = queue_track_ids[i];
        Track track;
        auto it = tracks_by_id.find(requested_id);
        if (it != tracks_by_id.end()) {
            track = it->second;
        } else {
            track.id = requested_id;
        }

        const std::string track_id =
            track.id.empty() ? requested_id : track.id;

        auto row = std::make_unique<TrackRow>(
            main_window_, track, static_cast<int>(i + 1));

        row->set_play_callback([this, track_id](const Track&) {
            if (!main_window_) return;
            IPlayerService* backend = main_window_->get_backend();
            if (!backend) return;

            const auto queue = backend->get_queue();
            for (size_t j = 0; j < queue.size(); ++j) {
                if (queue[j] == track_id) {
                    backend->play_queue_index(j);
                    return;
                }
            }
        });

        row->set_like_callback([this](const Track& track, bool liked) {
            (void)liked;
            if (library_service_) {
                library_service_->toggle_like(track.id);
            }
        });

        GtkWidget* row_widget = row->get_widget();
        gtk_widget_set_hexpand(row_widget, TRUE);
        safe_list_box_append(GTK_LIST_BOX(queue_view_), row_widget);
        queue_rows_.push_back(std::move(row));
    }
}

gboolean RightNowPlaying::on_allocation_tick(GtkWidget* widget, GdkFrameClock*,
                                             gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    const int width = gtk_widget_get_width(widget);
    const int height = gtk_widget_get_height(widget);
    if (width > 0 && height > 0 &&
        (width != self->last_width_ || height != self->last_height_)) {
        self->last_width_ = width;
        self->last_height_ = height;
        self->adapt_to_allocation(width, height);
    }
    return G_SOURCE_CONTINUE;
}

void RightNowPlaying::on_prev_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->previous_track();
        }
    }
}

void RightNowPlaying::on_play_pause_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->toggle_play_pause();
        }
    }
}

void RightNowPlaying::on_next_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->next_track();
        }
    }
}

void RightNowPlaying::on_shuffle_toggled(GtkToggleButton* button,
                                         gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->set_shuffle(gtk_toggle_button_get_active(button));
        }
    }
}

void RightNowPlaying::on_repeat_toggled(GtkToggleButton* button,
                                        gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->set_repeat(gtk_toggle_button_get_active(button));
        }
    }
}

void RightNowPlaying::on_loop_toggled(GtkToggleButton* button,
                                      gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        if (IPlayerService* backend = self->main_window_->get_backend()) {
            backend->set_repeat_one(gtk_toggle_button_get_active(button));
        }
    }
}

void RightNowPlaying::on_lyrics_tab_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    self->lyrics_tab_active_ = true;
    self->show_active_tab();
}

void RightNowPlaying::on_queue_tab_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    self->lyrics_tab_active_ = false;
    self->show_active_tab();
}

} // namespace lollypop
