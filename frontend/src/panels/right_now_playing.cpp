/**
 * Lollypop C++ - Right Now Playing Implementation
 */

#include "panels/right_now_playing.hpp"
#include "panels/waveform_view.hpp"
#include "main_window.hpp"
#include "controller.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILyricsService.hpp"
#include "services/ILibraryService.hpp"
#include "models/VisualizerModel.hpp"
#include "types.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>
#include <memory>

namespace lollypop {

RightNowPlaying::RightNowPlaying(MainWindow* main_window,
                                 ILyricsService* lyrics_service,
                                 ILibraryService* library_service)
    : main_window_(main_window),
      lyrics_service_(lyrics_service),
      library_service_(library_service),
      main_box_(nullptr),
      scrolled_window_(nullptr),
      content_box_(nullptr),
      artwork_box_(nullptr),
      artwork_view_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      waveform_box_(nullptr),
      waveform_view_(nullptr),
      waveform_model_(nullptr),
      time_label_(nullptr),
      prev_button_(nullptr),
      next_button_(nullptr),
      modes_box_(nullptr),
      shuffle_button_(nullptr),
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
}

RightNowPlaying::~RightNowPlaying() = default;

void RightNowPlaying::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(main_box_, "right-now-playing");
    gtk_widget_set_vexpand(main_box_, TRUE);
    gtk_widget_set_hexpand(main_box_, TRUE);

    // Scrolled window for content
    scrolled_window_ = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled_window_, "now-playing-scrolled");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window_, TRUE);

    content_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_add_css_class(content_box_, "now-playing-content");
    gtk_widget_set_margin_top(content_box_, 16);
    gtk_widget_set_margin_bottom(content_box_, 16);
    gtk_widget_set_margin_start(content_box_, 16);
    gtk_widget_set_margin_end(content_box_, 16);

    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_), content_box_);
    safe_box_append(GTK_BOX(main_box_), scrolled_window_);

    // Create sections
    create_artwork_section();
    create_track_info();
    create_waveform_section();
    create_playback_modes();
    create_tabs();
}

void RightNowPlaying::create_artwork_section() {
    artwork_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(artwork_box_, "now-playing-artwork");
    gtk_widget_set_halign(artwork_box_, GTK_ALIGN_CENTER);

    // ArtworkView component
    artwork_view_ = new ArtworkView();
    artwork_view_->set_preferred_size(280, 280);
    artwork_view_->set_corner_radius(12.0);
    safe_box_append(GTK_BOX(artwork_box_), artwork_view_->get_widget());

    safe_box_append(GTK_BOX(content_box_), artwork_box_);
}

void RightNowPlaying::create_track_info() {
    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(info_box, "now-playing-track-info");
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);

    track_title_label_ = gtk_label_new("No track playing");
    gtk_widget_add_css_class(track_title_label_, "now-playing-title");
    gtk_label_set_wrap(GTK_LABEL(track_title_label_), TRUE);
    gtk_label_set_justify(GTK_LABEL(track_title_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(track_title_label_), 0.5);

    track_artist_label_ = gtk_label_new("Select a track to begin");
    gtk_widget_add_css_class(track_artist_label_, "now-playing-artist");
    gtk_label_set_xalign(GTK_LABEL(track_artist_label_), 0.5);

    safe_box_append(GTK_BOX(info_box), track_title_label_);
    safe_box_append(GTK_BOX(info_box), track_artist_label_);
    safe_box_append(GTK_BOX(content_box_), info_box);
}

void RightNowPlaying::create_waveform_section() {
    waveform_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(waveform_box_, "now-playing-waveform");
    gtk_widget_set_margin_top(waveform_box_, 8);

    // Time label
    time_label_ = gtk_label_new("0:00 / 0:00");
    gtk_widget_add_css_class(time_label_, "now-playing-time");
    gtk_widget_set_halign(time_label_, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(waveform_box_), time_label_);

    // Waveform with prev/next buttons
    GtkWidget* waveform_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(waveform_controls, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(waveform_controls, GTK_ALIGN_CENTER);

    // Previous button
    prev_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(prev_button_), FALSE);
    gtk_widget_add_css_class(prev_button_, "now-playing-control-button");
    GtkWidget* prev_icon = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(prev_icon), 24);
    safe_button_set_child(GTK_BUTTON(prev_button_), prev_icon);
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);
    gtk_widget_set_sensitive(prev_button_, FALSE);

    // Waveform widget
    waveform_view_ = new WaveformView();
    waveform_view_->set_preferred_size(300, 40);
    safe_box_append(GTK_BOX(waveform_controls), waveform_view_->get_widget());

    waveform_model_ = std::make_unique<VisualizerModel>();
    waveform_view_->set_model(waveform_model_.get());

    // Next button
    next_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(next_button_), FALSE);
    gtk_widget_add_css_class(next_button_, "now-playing-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    safe_button_set_child(GTK_BUTTON(next_button_), next_icon);
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);
    gtk_widget_set_sensitive(next_button_, FALSE);

    safe_box_append(GTK_BOX(waveform_box_), waveform_controls);
    safe_box_append(GTK_BOX(content_box_), waveform_box_);
}

void RightNowPlaying::create_playback_modes() {
    modes_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(modes_box_, "now-playing-modes");
    gtk_widget_set_halign(modes_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(modes_box_, 16);

    // Shuffle
    shuffle_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(shuffle_button_), FALSE);
    gtk_widget_add_css_class(shuffle_button_, "now-playing-mode-button");
    GtkWidget* shuffle_icon = gtk_image_new_from_icon_name("media-shuffle-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(shuffle_icon), 20);
    safe_button_set_child(GTK_BUTTON(shuffle_button_), shuffle_icon);
    g_signal_connect(shuffle_button_, "toggled", G_CALLBACK(on_shuffle_toggled), this);
    gtk_widget_set_sensitive(shuffle_button_, FALSE);

    // Repeat
    repeat_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(repeat_button_), FALSE);
    gtk_widget_add_css_class(repeat_button_, "now-playing-mode-button");
    GtkWidget* repeat_icon = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(repeat_icon), 20);
    safe_button_set_child(GTK_BUTTON(repeat_button_), repeat_icon);
    g_signal_connect(repeat_button_, "toggled", G_CALLBACK(on_repeat_toggled), this);
    gtk_widget_set_sensitive(repeat_button_, FALSE);

    // Loop (repeat one)
    loop_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(loop_button_), FALSE);
    gtk_widget_add_css_class(loop_button_, "now-playing-mode-button");
    GtkWidget* loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat-song-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(loop_icon), 20);
    safe_button_set_child(GTK_BUTTON(loop_button_), loop_icon);
    g_signal_connect(loop_button_, "toggled", G_CALLBACK(on_loop_toggled), this);
    gtk_widget_set_sensitive(loop_button_, FALSE);

    safe_box_append(GTK_BOX(modes_box_), shuffle_button_);
    safe_box_append(GTK_BOX(modes_box_), repeat_button_);
    safe_box_append(GTK_BOX(modes_box_), loop_button_);
    safe_box_append(GTK_BOX(content_box_), modes_box_);
}

void RightNowPlaying::create_lyrics_empty_state() {
    lyrics_empty_state_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(lyrics_empty_state_, "empty-state");
    gtk_widget_set_hexpand(lyrics_empty_state_, TRUE);
    gtk_widget_set_vexpand(lyrics_empty_state_, TRUE);
    gtk_widget_set_halign(lyrics_empty_state_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(lyrics_empty_state_, GTK_ALIGN_CENTER);

    auto* icon = gtk_image_new_from_icon_name("document-empty-symbolic");
    gtk_widget_add_css_class(icon, "empty-state-icon");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(lyrics_empty_state_), icon);

    auto* title = gtk_label_new("No lyrics available");
    gtk_widget_add_css_class(title, "empty-state-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(lyrics_empty_state_), title);

    auto* desc = gtk_label_new("Lyrics are not available for this track.");
    gtk_widget_add_css_class(desc, "empty-state-description");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.5);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(desc), 40);
    gtk_widget_set_halign(desc, GTK_ALIGN_CENTER);
    safe_box_append(GTK_BOX(lyrics_empty_state_), desc);
}

void RightNowPlaying::create_tabs() {
    tabs_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(tabs_box_, "now-playing-tabs");
    gtk_widget_set_margin_top(tabs_box_, 16);

    // Tab buttons
    GtkWidget* tab_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab_buttons, "now-playing-tab-buttons");
    gtk_widget_set_halign(tab_buttons, GTK_ALIGN_CENTER);

    lyrics_tab_ = gtk_button_new_with_label("Lyrics");
    gtk_button_set_has_frame(GTK_BUTTON(lyrics_tab_), FALSE);
    gtk_widget_add_css_class(lyrics_tab_, "now-playing-tab-button");
    gtk_widget_set_hexpand(lyrics_tab_, TRUE);

    queue_tab_ = gtk_button_new_with_label("Queue");
    gtk_button_set_has_frame(GTK_BUTTON(queue_tab_), FALSE);
    gtk_widget_add_css_class(queue_tab_, "now-playing-tab-button");
    gtk_widget_set_hexpand(queue_tab_, TRUE);

    safe_box_append(GTK_BOX(tab_buttons), lyrics_tab_);
    safe_box_append(GTK_BOX(tab_buttons), queue_tab_);

    // Stack for tab content
    tab_content_stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(tab_content_stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(tab_content_stack_), 200);

    // Lyrics view
    lyrics_view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(lyrics_view_), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(lyrics_view_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(lyrics_view_), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(lyrics_view_), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(lyrics_view_), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(lyrics_view_), 16);
    gtk_widget_add_css_class(lyrics_view_, "now-playing-lyrics");
    GtkWidget* lyrics_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lyrics_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lyrics_scroll), lyrics_view_);
    safe_stack_add_named(GTK_STACK(tab_content_stack_), lyrics_scroll, "lyrics");

    // Lyrics empty state (overlaid when no lyrics)
    create_lyrics_empty_state();
    safe_stack_add_named(GTK_STACK(tab_content_stack_), lyrics_empty_state_, "lyrics_empty");

    // Queue view
    queue_view_ = gtk_list_box_new();
    gtk_widget_add_css_class(queue_view_, "now-playing-queue");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(queue_view_), GTK_SELECTION_NONE);
    GtkWidget* queue_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(queue_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(queue_scroll), queue_view_);
    safe_stack_add_named(GTK_STACK(tab_content_stack_), queue_scroll, "queue");

    // Connect tab buttons
    g_signal_connect(lyrics_tab_, "clicked", G_CALLBACK(on_lyrics_tab_clicked), this);
    g_signal_connect(queue_tab_, "clicked", G_CALLBACK(on_queue_tab_clicked), this);

    // Default to lyrics
    gtk_stack_set_visible_child_name(GTK_STACK(tab_content_stack_), "lyrics");
    gtk_widget_add_css_class(lyrics_tab_, "active");

    safe_box_append(GTK_BOX(tabs_box_), tab_buttons);
    safe_box_append(GTK_BOX(tabs_box_), tab_content_stack_);
    safe_box_append(GTK_BOX(content_box_), tabs_box_);
}

void RightNowPlaying::update_track_info(const std::string& title,
                                         const std::string& artist,
                                         const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(track_title_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(track_artist_label_), artist.c_str());

    if (artwork_view_) {
        artwork_view_->set_artwork_path(artwork_path);
    }
}

void RightNowPlaying::update_playback_position(double position,
                                                double duration) {
    int pos_min = static_cast<int>(position) / 60;
    int pos_sec = static_cast<int>(position) % 60;
    int dur_min = static_cast<int>(duration) / 60;
    int dur_sec = static_cast<int>(duration) % 60;

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d",
             pos_min, pos_sec, dur_min, dur_sec);
    gtk_label_set_text(GTK_LABEL(time_label_), time_str);

    double progress = duration > 0 ? position / duration : 0.0;
    if (waveform_view_) {
        waveform_view_->set_progress(progress);
    }
}

void RightNowPlaying::set_playing(bool playing) {
    // Animate waveform when playing
    if (waveform_view_) {
        waveform_view_->set_playing(playing);
    }
}

void RightNowPlaying::set_queue(const std::vector<std::string>& queue_track_ids) {
    populate_queue(queue_track_ids);
}

void RightNowPlaying::set_lyrics(const std::string& lyrics) {
    if (lyrics.empty()) {
        // Show empty state
        gtk_stack_set_visible_child_name(GTK_STACK(tab_content_stack_),
                                         "lyrics_empty");
        return;
    }

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lyrics_view_));
    gtk_text_buffer_set_text(buffer, lyrics.c_str(), -1);
    gtk_stack_set_visible_child_name(GTK_STACK(tab_content_stack_), "lyrics");
}

void RightNowPlaying::set_points(const std::vector<float>& points) {
    if (waveform_model_ && waveform_view_) {
        waveform_model_->samples = points;
        waveform_model_->sequenceNumber++;
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
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    gtk_widget_set_sensitive(shuffle_button_, sensitive);
    gtk_widget_set_sensitive(repeat_button_, sensitive);
    gtk_widget_set_sensitive(loop_button_, sensitive);
}

void RightNowPlaying::set_transport_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
    // Mode buttons stay sensitive (controlled by set_controls_sensitive)
}

void RightNowPlaying::set_panel_height(int height) {
    panel_height_ = height;
    adapt_to_height(height);
}

void RightNowPlaying::adapt_to_height(int height) {
    // Short-height mode thresholds (plan Section 6.3/6.15)
    if (height < 500) {
        // Short-height: collapse artwork to thumbnail, prioritize Lyrics/Queue
        artwork_view_->set_preferred_size(100, 100);
        artwork_view_->set_corner_radius(8.0);
        gtk_widget_set_margin_top(content_box_, 8);
        gtk_widget_set_margin_bottom(content_box_, 8);
    } else if (height < 700) {
        // Compact: medium artwork
        artwork_view_->set_preferred_size(180, 180);
        artwork_view_->set_corner_radius(10.0);
        gtk_widget_set_margin_top(content_box_, 12);
        gtk_widget_set_margin_bottom(content_box_, 12);
    } else {
        // Normal: full artwork
        artwork_view_->set_preferred_size(280, 280);
        artwork_view_->set_corner_radius(12.0);
        gtk_widget_set_margin_top(content_box_, 16);
        gtk_widget_set_margin_bottom(content_box_, 16);
    }

    gtk_widget_queue_resize(content_box_);
}

void RightNowPlaying::request_lyrics_for_track(const std::string& track_id) {
    if (!lyrics_service_) return;

    lyrics_service_->request_lyrics(
        track_id,
        [this](std::optional<Lyrics> result) {
            if (result.has_value() && !result->text.empty()) {
                set_lyrics(result->text);
            } else {
                set_lyrics("");
            }
        });
}

void RightNowPlaying::populate_queue(
    const std::vector<std::string>& queue_track_ids) {
    // Clear existing queue items
    GtkWidget* child = gtk_widget_get_first_child(queue_view_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(queue_view_), child);
        child = next;
    }

    if (queue_track_ids.empty()) {
        // Show empty state for queue
        GtkWidget* empty = gtk_label_new("Queue is empty");
        gtk_widget_add_css_class(empty, "library-placeholder");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.5);
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        safe_list_box_append(GTK_LIST_BOX(queue_view_), empty);
        return;
    }

    for (size_t i = 0; i < queue_track_ids.size(); ++i) {
        const auto& track_id = queue_track_ids[i];

        // Resolve track details from library service
        Track track;
        if (library_service_) {
            track = library_service_->get_track(track_id);
        }
        track.id = track_id;

        auto row = std::make_unique<TrackRow>(main_window_, track,
                                              static_cast<int>(i + 1));
        row->set_play_callback(
            [this, track_id](const Track&) {
                if (auto* backend = main_window_->get_backend()) {
                    // Find index of this track in queue
                    auto q = backend->get_queue();
                    for (size_t j = 0; j < q.size(); ++j) {
                        if (q[j] == track_id) {
                            backend->play_queue_index(static_cast<size_t>(j));
                            break;
                        }
                    }
                }
            });
        row->set_like_callback(
            [this](const Track& t, bool liked) {
                if (library_service_) {
                    library_service_->toggle_like(t.id);
                }
            });

        GtkWidget* w = row->get_widget();
        safe_list_box_append(GTK_LIST_BOX(queue_view_), w);
    }
}

void RightNowPlaying::on_prev_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend) backend->previous_track();
    }
}

void RightNowPlaying::on_next_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend) backend->next_track();
    }
}

void RightNowPlaying::on_shuffle_toggled(GtkToggleButton* button,
                                          gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_shuffle(gtk_toggle_button_get_active(button));
    }
}

void RightNowPlaying::on_repeat_toggled(GtkToggleButton* button,
                                         gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_repeat(gtk_toggle_button_get_active(button));
    }
}

void RightNowPlaying::on_loop_toggled(GtkToggleButton* button,
                                       gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        IPlayerService* backend = self->main_window_->get_backend();
        if (backend)
            backend->set_repeat_one(gtk_toggle_button_get_active(button));
    }
}

void RightNowPlaying::on_lyrics_tab_clicked(GtkButton* button,
                                             gpointer user_data) {
    (void)button;
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    gtk_stack_set_visible_child_name(GTK_STACK(self->tab_content_stack_),
                                     "lyrics");
    gtk_widget_add_css_class(self->lyrics_tab_, "active");
    gtk_widget_remove_css_class(self->queue_tab_, "active");
}

void RightNowPlaying::on_queue_tab_clicked(GtkButton* button,
                                            gpointer user_data) {
    (void)button;
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    gtk_stack_set_visible_child_name(GTK_STACK(self->tab_content_stack_),
                                     "queue");
    gtk_widget_add_css_class(self->queue_tab_, "active");
    gtk_widget_remove_css_class(self->lyrics_tab_, "active");
}

} // namespace lollypop
