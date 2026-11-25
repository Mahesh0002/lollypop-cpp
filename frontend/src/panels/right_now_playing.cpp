/**
 * Lollypop C++ - Right Now Playing Implementation
 */

#include "panels/right_now_playing.hpp"
#include "panels/progress_wave.hpp"
#include "panels/spectrum_router.hpp"
#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILyricsService.hpp"
#include "services/ILibraryService.hpp"
#include "types.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/TrackRow.hpp"
#include "ui/components/SyncedLyricsView.hpp"
#include "ui/theme/ThemeManager.hpp"
#include "lyrics/LyricTypes.hpp"
#include "util/accessibility.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>
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
    gtk_widget_set_hexpand(button, FALSE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
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

void set_button_size(GtkWidget* button, int width, int height) {
    gtk_widget_set_size_request(button, width, height);
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

struct DeferredQueueAction {
    RightNowPlaying* panel;
    std::function<void()> task;
    guint source_id;
};

constexpr int kCompactPanelWidth = 320;
// The LTR border-left / RTL border-right adds 1px outside the measured width.
constexpr int kPanelBorder = 1;

GtkSizeRequestMode fixed_width_request_mode(GtkWidget*) {
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

void fixed_width_measure(GtkWidget* widget, GtkOrientation orientation,
                         int /*for_size*/, int* minimum, int* natural,
                         int* minimum_baseline, int* natural_baseline) {
    GtkWidget* child = gtk_widget_get_first_child(widget);
    if (!child) {
        if (minimum) *minimum = 0;
        if (natural) *natural = 0;
        if (minimum_baseline) *minimum_baseline = -1;
        if (natural_baseline) *natural_baseline = -1;
        return;
    }

    int requested_width = RightNowPlaying::kPanelWidth - kPanelBorder;
    int requested_height = -1;
    gtk_widget_get_size_request(widget, &requested_width, &requested_height);
    // Fixed 500px panel; the size request excludes the CSS separator.
    const int target_width = std::max(0, requested_width);

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        // A fixed request caps the grid column. Queue rows keep their full
        // text as natural width, which otherwise widens the whole panel.
        if (minimum) *minimum = target_width;
        if (natural) *natural = target_width;
        if (minimum_baseline) *minimum_baseline = -1;
        if (natural_baseline) *natural_baseline = -1;
        return;
    }

    // The queue and lyrics scrollers report their full content height as
    // natural. Keep that out of the window request; the allocation supplies
    // the real height and each scroller handles its own overflow.
    if (minimum) *minimum = 0;
    if (natural) *natural = 0;
    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

void fixed_width_allocate(GtkWidget* widget, int width, int height,
                          int baseline) {
    GtkWidget* child = gtk_widget_get_first_child(widget);
    if (child) {
        gtk_widget_allocate(child, width, height, baseline, nullptr);
    }
}

GtkLayoutManager* create_fixed_width_layout() {
    return gtk_custom_layout_new(fixed_width_request_mode, fixed_width_measure,
                                 fixed_width_allocate);
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
      prev_button_(nullptr),
      next_button_(nullptr),
      transport_box_(nullptr),
      shuffle_button_(nullptr),
      repeat_button_(nullptr),
      loop_button_(nullptr),
      tabs_box_(nullptr),
      lyrics_tab_(nullptr),
      queue_tab_(nullptr),
      tab_content_stack_(nullptr),
      queue_view_(nullptr),
      lyrics_empty_state_(nullptr),
      queue_undo_bar_(nullptr),
      queue_undo_label_(nullptr),
      queue_undo_button_(nullptr) {
    setup_ui();
    gtk_widget_add_tick_callback(main_box_, on_allocation_tick, this, nullptr);
}

RightNowPlaying::~RightNowPlaying() {
    for (guint source_id : deferred_queue_actions_) {
        g_source_remove(source_id);
    }
    deferred_queue_actions_.clear();
    SpectrumRouter::instance().unregister_wave(progress_wave_.get());
}

void RightNowPlaying::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(main_box_, "right-now-playing");
    gtk_widget_set_layout_manager(main_box_, create_fixed_width_layout());
    gtk_widget_set_size_request(main_box_, kPanelWidth - kPanelBorder, -1);
    gtk_widget_set_hexpand(main_box_, TRUE);
    gtk_widget_set_vexpand(main_box_, TRUE);
    gtk_widget_set_visible(main_box_, TRUE);

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
    // Leave slack for borders and text reflow. The allocation tick resizes
    // this with the panel, but the artwork never becomes the width driver.
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
    gtk_label_set_wrap_mode(GTK_LABEL(track_title_label_),
                            PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(track_title_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(track_title_label_), 32);
    gtk_label_set_justify(GTK_LABEL(track_title_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(track_title_label_), 0.5);
    gtk_widget_set_hexpand(track_title_label_, TRUE);
    gtk_widget_set_size_request(track_title_label_, 0, -1);
    gtk_widget_set_visible(track_title_label_, TRUE);

    track_artist_label_ = gtk_label_new("Select a track to begin");
    gtk_widget_add_css_class(track_artist_label_, "now-playing-artist");
    gtk_label_set_wrap(GTK_LABEL(track_artist_label_), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(track_artist_label_),
                            PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(track_artist_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(track_artist_label_), 32);
    gtk_label_set_justify(GTK_LABEL(track_artist_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(track_artist_label_), 0.5);
    gtk_widget_set_hexpand(track_artist_label_, TRUE);
    gtk_widget_set_size_request(track_artist_label_, 0, -1);
    gtk_widget_set_visible(track_artist_label_, TRUE);

    safe_box_append(GTK_BOX(info_box), track_title_label_);
    safe_box_append(GTK_BOX(info_box), track_artist_label_);
    safe_box_append(GTK_BOX(content_box_), info_box);
}

void RightNowPlaying::create_progress_section() {
    // [rewind] [shared wavy progress] [fast-forward]
    progress_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(progress_box_, "now-playing-progress-row");
    gtk_widget_set_hexpand(progress_box_, TRUE);
    gtk_widget_set_halign(progress_box_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(progress_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(progress_box_, TRUE);

    prev_button_ = create_icon_button("media-skip-backward-symbolic", 20, false);
    gtk_widget_add_css_class(prev_button_, "now-playing-control-button");
    set_button_size(prev_button_, 36, 36);
    gtk_widget_set_tooltip_text(prev_button_, "Previous track");
    accessibility::set_label(prev_button_, "Rewind / previous track");
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);
    safe_box_append(GTK_BOX(progress_box_), prev_button_);

    progress_wave_ = std::make_unique<ProgressWave>();
    SpectrumRouter::instance().register_wave(progress_wave_.get());
    GtkWidget* wave = progress_wave_->get_widget();
    gtk_widget_add_css_class(wave, "now-playing-progress");
    gtk_widget_set_hexpand(wave, TRUE);
    gtk_widget_set_halign(wave, GTK_ALIGN_FILL);
    gtk_widget_set_valign(wave, GTK_ALIGN_CENTER);
    progress_wave_->set_seek_callback([this](double seconds) {
        if (!main_window_) return;
        if (IPlayerService* backend = main_window_->get_backend()) {
            backend->seek(seconds);
        }
    });
    safe_box_append(GTK_BOX(progress_box_), wave);

    next_button_ = create_icon_button("media-skip-forward-symbolic", 20, false);
    gtk_widget_add_css_class(next_button_, "now-playing-control-button");
    set_button_size(next_button_, 36, 36);
    gtk_widget_set_tooltip_text(next_button_, "Next track");
    accessibility::set_label(next_button_, "Fast-forward / next track");
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);
    safe_box_append(GTK_BOX(progress_box_), next_button_);

    safe_box_append(GTK_BOX(content_box_), progress_box_);
}

void RightNowPlaying::create_transport_row() {
    // Three mode buttons; playback is driven by track activation / media keys.
    transport_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(transport_box_, "now-playing-main-controls");
    gtk_widget_set_hexpand(transport_box_, TRUE);
    gtk_widget_set_halign(transport_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(transport_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(transport_box_, TRUE);

    shuffle_button_ = create_icon_button("media-playlist-shuffle-symbolic",
                                         20, true);
    gtk_widget_add_css_class(shuffle_button_, "now-playing-mode-button");
    set_button_size(shuffle_button_, 40, 40);
    gtk_widget_set_tooltip_text(shuffle_button_, "Shuffle");
    accessibility::set_label(shuffle_button_, "Shuffle");
    g_signal_connect(shuffle_button_, "toggled",
                     G_CALLBACK(on_shuffle_toggled), this);

    repeat_button_ = create_icon_button("media-playlist-repeat-symbolic",
                                        20, true);
    gtk_widget_add_css_class(repeat_button_, "now-playing-mode-button");
    set_button_size(repeat_button_, 40, 40);
    gtk_widget_set_tooltip_text(repeat_button_, "Repeat playlist");
    accessibility::set_label(repeat_button_, "Repeat playlist");
    g_signal_connect(repeat_button_, "toggled",
                     G_CALLBACK(on_repeat_toggled), this);

    loop_button_ = create_icon_button("media-playlist-repeat-song-symbolic",
                                      20, true);
    gtk_widget_add_css_class(loop_button_, "now-playing-mode-button");
    set_button_size(loop_button_, 40, 40);
    gtk_widget_set_tooltip_text(loop_button_, "Repeat track");
    accessibility::set_label(loop_button_, "Repeat track");
    g_signal_connect(loop_button_, "toggled",
                     G_CALLBACK(on_loop_toggled), this);

    safe_box_append(GTK_BOX(transport_box_), shuffle_button_);
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

void RightNowPlaying::create_queue_undo_bar() {
    queue_undo_bar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(queue_undo_bar_, "queue-undo-bar");
    gtk_widget_set_hexpand(queue_undo_bar_, TRUE);
    gtk_widget_set_halign(queue_undo_bar_, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(queue_undo_bar_, 0, -1);
    gtk_widget_set_visible(queue_undo_bar_, FALSE);

    queue_undo_label_ = gtk_label_new("");
    gtk_widget_add_css_class(queue_undo_label_, "queue-undo-label");
    gtk_label_set_xalign(GTK_LABEL(queue_undo_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(queue_undo_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(queue_undo_label_), 24);
    gtk_widget_set_hexpand(queue_undo_label_, TRUE);
    gtk_widget_set_halign(queue_undo_label_, GTK_ALIGN_START);
    gtk_widget_set_size_request(queue_undo_label_, 0, -1);
    gtk_widget_set_visible(queue_undo_label_, TRUE);
    safe_box_append(GTK_BOX(queue_undo_bar_), queue_undo_label_);

    queue_undo_button_ = gtk_button_new_with_label("Undo");
    gtk_widget_add_css_class(queue_undo_button_, "queue-undo-button");
    gtk_widget_set_tooltip_text(queue_undo_button_, "Put the track back");
    accessibility::set_label(queue_undo_button_, "Undo removing track");
    gtk_widget_set_visible(queue_undo_button_, TRUE);
    g_signal_connect(queue_undo_button_, "clicked",
                     G_CALLBACK(on_queue_undo_clicked), this);
    safe_box_append(GTK_BOX(queue_undo_bar_), queue_undo_button_);

    safe_box_append(GTK_BOX(tabs_box_), queue_undo_bar_);
}

void RightNowPlaying::show_queue_undo(
    const std::string& title,
    const std::string& track_id,
    size_t index,
    const std::vector<std::string>& queue_after) {
    if (!queue_undo_bar_ || track_id.empty()) return;

    pending_undo_track_id_ = track_id;
    pending_undo_title_ = title.empty() ? "Track" : title;
    pending_undo_index_ = index;
    queue_after_removal_ = queue_after;

    const std::string message =
        "Removed " + pending_undo_title_ + " from the queue";
    const std::string tooltip =
        "Put " + pending_undo_title_ + " back at its original place";
    const std::string accessible_label =
        "Undo removing " + pending_undo_title_;

    gtk_label_set_text(GTK_LABEL(queue_undo_label_), message.c_str());
    gtk_widget_set_tooltip_text(queue_undo_button_, tooltip.c_str());
    accessibility::set_label(queue_undo_button_, accessible_label.c_str());
    gtk_widget_set_sensitive(queue_undo_button_, TRUE);
    gtk_widget_set_visible(queue_undo_bar_, TRUE);
}

void RightNowPlaying::hide_queue_undo() {
    pending_undo_track_id_.clear();
    pending_undo_title_.clear();
    queue_after_removal_.clear();
    if (queue_undo_bar_) {
        gtk_widget_set_visible(queue_undo_bar_, FALSE);
    }
}

void RightNowPlaying::schedule_queue_action(std::function<void()> action) {
    auto* deferred = new DeferredQueueAction{this, std::move(action), 0};
    deferred->source_id = g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        &RightNowPlaying::on_deferred_queue_action,
        deferred,
        [](gpointer data) { delete static_cast<DeferredQueueAction*>(data); });
    deferred_queue_actions_.push_back(deferred->source_id);
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
    create_queue_undo_bar();

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

    // Create synced lyrics view with Apple Music-style progressive fill
    synced_lyrics_view_ = std::make_unique<SyncedLyricsView>(
        [this](int position_ms) {
            if (!main_window_) return;
            if (IPlayerService* backend = main_window_->get_backend()) {
                backend->seek(position_ms / 1000.0);
            }
        },
        false  // not fullscreen
    );

    if (main_window_) {
        if (ThemeManager* theme = main_window_->get_theme_manager()) {
            double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
            theme->get_accent_color(r, g, b, a);
            synced_lyrics_view_->set_accent(GdkRGBA{
                static_cast<float>(r), static_cast<float>(g),
                static_cast<float>(b), static_cast<float>(a)});
        }
    }

    GtkWidget* lyrics_widget = synced_lyrics_view_->get_widget();
    safe_stack_add_named(GTK_STACK(tab_content_stack_),
                         lyrics_widget, "lyrics");

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

void RightNowPlaying::adapt_to_allocation(int width, int height) {
    if (width <= 0 || height <= 0) return;

    panel_width_ = width;
    panel_height_ = height;

    // Content margins, the panel border, and slack for wrapped text must stay
    // outside the artwork request. Without the extra slack the artwork becomes
    // the panel's minimum-width driver and the panel grows.
    const bool compact = width < kCompactPanelWidth;
    const int horizontal_chrome = compact ? 32 : 44;
    const int available_width = std::max(120, width - horizontal_chrome);
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

    const int horizontal_margin = compact ? 8 : 12;
    gtk_widget_set_margin_start(content_box_, horizontal_margin);
    gtk_widget_set_margin_end(content_box_, horizontal_margin);
    gtk_widget_set_margin_top(content_box_, compact ? 8 : 10);
    gtk_widget_set_margin_bottom(content_box_, compact ? 8 : 10);

    // Wrapping labels need a narrower character budget in the 500px column.
    const int label_chars = std::clamp(width / 14, 12, 40);
    gtk_label_set_max_width_chars(GTK_LABEL(track_title_label_), label_chars);
    gtk_label_set_max_width_chars(GTK_LABEL(track_artist_label_), label_chars);

    // Tighten control spacing so the transport row keeps its full width.
    gtk_box_set_spacing(GTK_BOX(progress_box_), compact ? 6 : 8);
    gtk_box_set_spacing(GTK_BOX(transport_box_), compact ? 8 : 12);

    if (compact != compact_layout_) {
        compact_layout_ = compact;
        if (compact) {
            gtk_widget_add_css_class(main_box_, "compact");
        } else {
            gtk_widget_remove_css_class(main_box_, "compact");
        }
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
    if (progress_wave_) {
        if (main_window_) {
            if (IPlayerService* backend = main_window_->get_backend()) {
                progress_wave_->set_can_seek(backend->can_seek());
            }
        }
        progress_wave_->set_duration(duration);
        progress_wave_->set_progress(duration > 0.0 ? position / duration : 0.0);
    }

    // Update synced lyrics view position
    if (synced_lyrics_view_ && synced_lyrics_view_->has_lyrics()) {
        synced_lyrics_view_->set_position(static_cast<int>(position * 1000),
                                          static_cast<int>(duration * 1000));
    }
}

void RightNowPlaying::set_playing(bool playing) {
    if (progress_wave_) {
        progress_wave_->set_playing(playing);
    }
}

void RightNowPlaying::set_queue(
    const std::vector<std::string>& queue_track_ids) {
    populate_queue(queue_track_ids);
}

void RightNowPlaying::set_lyrics(const std::string& lyrics) {
    has_lyrics_ = !lyrics.empty();

    if (synced_lyrics_view_) {
        if (has_lyrics_) {
            // Check if it's synced lyrics (contains LRC timestamps)
            if (lyrics.find('[') != std::string::npos && lyrics.find(']') != std::string::npos) {
                // Parse and set synced lyrics
                SyncedLyrics parsed = LrcParser::parse(lyrics);
                if (parsed.valid()) {
                    synced_lyrics_view_->set_lyrics(parsed);
                } else {
                    // Fallback to plain text
                    synced_lyrics_view_->set_message(lyrics);
                }
            } else {
                // Plain lyrics
                synced_lyrics_view_->set_message(lyrics);
            }
        } else {
            synced_lyrics_view_->set_message("No lyrics available.");
        }
    }
    if (lyrics_changed_callback_) lyrics_changed_callback_(lyrics);
    show_active_tab();
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
    gtk_widget_set_sensitive(repeat_button_, sensitive);
    gtk_widget_set_sensitive(loop_button_, sensitive);
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
}

void RightNowPlaying::set_transport_controls_sensitive(bool sensitive) {
    gtk_widget_set_sensitive(prev_button_, sensitive);
    gtk_widget_set_sensitive(next_button_, sensitive);
}

void RightNowPlaying::set_panel_height(int height) {
    adapt_to_allocation(panel_width_ > 0 ? panel_width_ : kPanelWidth, height);
}

void RightNowPlaying::request_lyrics_for_track(const std::string& track_id) {
    current_track_id_ = track_id;
    set_lyrics("");

    if (!lyrics_service_) return;

    // Request synced lyrics (LRC format)
    lyrics_service_->request_synced_lyrics(
        track_id,
        [this](const std::string& id, const std::string& lrc_text) {
            if (id == current_track_id_) {
                set_lyrics(lrc_text);
            }
        });
}

void RightNowPlaying::populate_queue(
    const std::vector<std::string>& queue_track_ids) {
    // Use safe helper to avoid invalid sibling pointers after unparenting
    safe_list_box_remove_all(GTK_LIST_BOX(queue_view_));
    queue_rows_.clear();

    if (queue_track_ids.empty()) {
        GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(empty, "queue-empty-state");
        gtk_widget_set_hexpand(empty, TRUE);
        gtk_widget_set_vexpand(empty, TRUE);
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(empty, TRUE);

        GtkWidget* empty_title = gtk_label_new("Your queue is empty");
        gtk_widget_add_css_class(empty_title, "queue-empty-title");
        gtk_label_set_xalign(GTK_LABEL(empty_title), 0.5);
        gtk_widget_set_halign(empty_title, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(empty_title, TRUE);
        safe_box_append(GTK_BOX(empty), empty_title);

        GtkWidget* empty_hint = gtk_label_new(
            "Add tracks from your library to start listening.");
        gtk_widget_add_css_class(empty_hint, "queue-empty-description");
        gtk_label_set_xalign(GTK_LABEL(empty_hint), 0.5);
        gtk_label_set_wrap(GTK_LABEL(empty_hint), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(empty_hint), 40);
        gtk_widget_set_halign(empty_hint, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(empty_hint, TRUE);
        safe_box_append(GTK_BOX(empty), empty_hint);

        safe_list_box_append(GTK_LIST_BOX(queue_view_), empty);
        return;
    }

    std::map<std::string, Track> tracks_by_id;
    if (library_service_) {
        for (const Track& item :
             library_service_->get_tracks_by_ids(queue_track_ids)) {
            if (!item.id.empty()) {
                tracks_by_id.emplace(item.id, item);
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
            main_window_, track, static_cast<int>(i + 1),
            TrackRowOptions{.show_track_number = true,
                            .show_album = false,
                            .show_duration = false,
                            .enable_queue_actions = true});

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

        row->set_like_callback([this](const Track& item, bool liked) {
            (void)liked;
            if (library_service_) {
                library_service_->toggle_like(item.id);
            }
        });

        row->set_queue_action_callback([this, track](const Track&, const char* action) {
            const std::string action_track_id = track.id;
            auto enqueue_later = [this, action_track_id](bool next) {
                schedule_queue_action([this, action_track_id, next]() {
                    if (!main_window_) return;
                    if (IPlayerService* backend = main_window_->get_backend()) {
                        backend->enqueue_track(action_track_id, next);
                    }
                });
            };

            if (std::strcmp(action, "play_next") == 0) {
                enqueue_later(true);
            } else if (std::strcmp(action, "add_to_queue") == 0) {
                enqueue_later(false);
            } else if (std::strcmp(action, "remove_from_queue") == 0) {
                schedule_queue_action(
                    [this, action_track_id, title = track.title]() {
                        if (!main_window_) return;
                        IPlayerService* backend = main_window_->get_backend();
                        if (!backend) return;

                        const auto queue = backend->get_queue();
                        for (size_t j = 0; j < queue.size(); ++j) {
                            if (queue[j] == action_track_id) {
                                if (backend->remove_from_queue(j)) {
                                    show_queue_undo(title, action_track_id, j,
                                                    backend->get_queue());
                                }
                                return;
                            }
                        }
                    });
            } else if (std::strcmp(action, "play_last") == 0) {
                enqueue_later(false);
            } else if (std::strcmp(action, "add_to_playlist") == 0) {
                // TODO: Show playlist selection dialog; queue is the fallback.
                enqueue_later(false);
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

void RightNowPlaying::on_queue_undo_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<RightNowPlaying*>(user_data);
    if (!self || !self->main_window_) return;

    IPlayerService* backend = self->main_window_->get_backend();
    if (!backend) {
        self->hide_queue_undo();
        return;
    }

    if (self->pending_undo_track_id_.empty() ||
        backend->get_queue() != self->queue_after_removal_) {
        self->hide_queue_undo();
        return;
    }

    const std::string track_id = self->pending_undo_track_id_;
    const size_t index = self->pending_undo_index_;
    if (backend->insert_into_queue(track_id, index)) {
        self->hide_queue_undo();
        return;
    }

    const std::string failure = "Couldn't put the track back.";
    gtk_label_set_text(GTK_LABEL(self->queue_undo_label_), failure.c_str());
    gtk_widget_set_sensitive(self->queue_undo_button_, FALSE);
    accessibility::set_label(self->queue_undo_button_, failure.c_str());
}

gboolean RightNowPlaying::on_deferred_queue_action(gpointer user_data) {
    auto* item = static_cast<DeferredQueueAction*>(user_data);
    if (item->panel) {
        auto& sources = item->panel->deferred_queue_actions_;
        sources.erase(std::remove(sources.begin(), sources.end(),
                                  item->source_id),
                      sources.end());
    }
    if (item->task) item->task();
    return G_SOURCE_REMOVE;
}

} // namespace lollypop
