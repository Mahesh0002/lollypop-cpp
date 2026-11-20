/**
 * Lollypop C++ - Right Now Playing Implementation
 */

#include "panels/right_now_playing.hpp"
#include "main_window.hpp"
#include "controller.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

// Simple waveform widget placeholder - will be replaced with actual CAVA-based implementation
class WaveformWidget {
public:
    explicit WaveformWidget(GtkWidget* parent) {
        drawing_area_ = gtk_drawing_area_new();
        gtk_widget_add_css_class(drawing_area_, "waveform-widget");
        gtk_widget_set_size_request(drawing_area_, -1, 70);
        gtk_widget_set_hexpand(drawing_area_, TRUE);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area_), draw_callback, this, nullptr);
        gtk_box_append(GTK_BOX(parent), drawing_area_);
    }

    GtkWidget* get_widget() const { return drawing_area_; }

    void set_progress(double progress) {
        progress_ = progress;
        gtk_widget_queue_draw(drawing_area_);
    }

private:
    GtkWidget* drawing_area_;
    double progress_ = 0.0;

    static void draw_callback(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
        WaveformWidget* self = static_cast<WaveformWidget*>(user_data);
        double progress = self->progress_;

        // Background
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.3);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        // Waveform bars (placeholder - will be replaced with CAVA data)
        int bar_count = 60;
        int bar_width = width / (bar_count * 2);
        int spacing = bar_width;

        for (int i = 0; i < bar_count; ++i) {
            double x = i * (bar_width + spacing) + spacing / 2;
            // Simulate waveform with sine wave
            double bar_height = (sin(i * 0.3 + progress * 10) * 0.5 + 0.5) * height * 0.8;
            double y = (height - bar_height) / 2;

            // Color based on progress
            if (i < progress * bar_count) {
                cairo_set_source_rgb(cr, 0.2, 0.7, 1.0);  // Played portion - accent color
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);  // Unplayed portion
            }

            cairo_rectangle(cr, x, y, bar_width, bar_height);
            cairo_fill(cr);
        }

        // Progress indicator line
        double progress_x = progress * width;
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, progress_x, 0);
        cairo_line_to(cr, progress_x, height);
        cairo_stroke(cr);
    }
};

RightNowPlaying::RightNowPlaying(MainWindow* main_window)
    : main_window_(main_window),
      main_box_(nullptr),
      scrolled_window_(nullptr),
      content_box_(nullptr),
      artwork_box_(nullptr),
      artwork_image_(nullptr),
      track_title_label_(nullptr),
      track_artist_label_(nullptr),
      waveform_box_(nullptr),
      waveform_widget_(nullptr),
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
      queue_view_(nullptr) {
    setup_ui();
}

RightNowPlaying::~RightNowPlaying() {
    delete waveform_widget_;
}

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

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_), content_box_);
    gtk_box_append(GTK_BOX(main_box_), scrolled_window_);

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

    // Artwork image with rounded corners via CSS
    artwork_image_ = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(artwork_image_), GTK_CONTENT_FIT_COVER);
    gtk_widget_add_css_class(artwork_image_, "now-playing-artwork-image");
    gtk_widget_set_size_request(artwork_image_, 280, 280);  // Will be constrained by panel width

    gtk_box_append(GTK_BOX(artwork_box_), artwork_image_);
    gtk_box_append(GTK_BOX(content_box_), artwork_box_);
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

    gtk_box_append(GTK_BOX(info_box), track_title_label_);
    gtk_box_append(GTK_BOX(info_box), track_artist_label_);
    gtk_box_append(GTK_BOX(content_box_), info_box);
}

void RightNowPlaying::create_waveform_section() {
    waveform_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(waveform_box_, "now-playing-waveform");
    gtk_widget_set_margin_top(waveform_box_, 8);

    // Time label
    time_label_ = gtk_label_new("0:00 / 0:00");
    gtk_widget_add_css_class(time_label_, "now-playing-time");
    gtk_widget_set_halign(time_label_, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(waveform_box_), time_label_);

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
    gtk_button_set_child(GTK_BUTTON(prev_button_), prev_icon);
    g_signal_connect(prev_button_, "clicked", G_CALLBACK(on_prev_clicked), this);

    // Waveform widget
    waveform_widget_ = new WaveformWidget(waveform_controls);

    // Next button
    next_button_ = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(next_button_), FALSE);
    gtk_widget_add_css_class(next_button_, "now-playing-control-button");
    GtkWidget* next_icon = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(next_icon), 24);
    gtk_button_set_child(GTK_BUTTON(next_button_), next_icon);
    g_signal_connect(next_button_, "clicked", G_CALLBACK(on_next_clicked), this);

    gtk_box_append(GTK_BOX(waveform_box_), waveform_controls);
    gtk_box_append(GTK_BOX(content_box_), waveform_box_);
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
    gtk_button_set_child(GTK_BUTTON(shuffle_button_), shuffle_icon);
    g_signal_connect(shuffle_button_, "toggled", G_CALLBACK(on_shuffle_toggled), this);

    // Repeat
    repeat_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(repeat_button_), FALSE);
    gtk_widget_add_css_class(repeat_button_, "now-playing-mode-button");
    GtkWidget* repeat_icon = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(repeat_icon), 20);
    gtk_button_set_child(GTK_BUTTON(repeat_button_), repeat_icon);
    g_signal_connect(repeat_button_, "toggled", G_CALLBACK(on_repeat_toggled), this);

    // Loop (repeat one)
    loop_button_ = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(loop_button_), FALSE);
    gtk_widget_add_css_class(loop_button_, "now-playing-mode-button");
    GtkWidget* loop_icon = gtk_image_new_from_icon_name("media-playlist-repeat-song-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(loop_icon), 20);
    gtk_button_set_child(GTK_BUTTON(loop_button_), loop_icon);
    g_signal_connect(loop_button_, "toggled", G_CALLBACK(on_loop_toggled), this);

    gtk_box_append(GTK_BOX(modes_box_), shuffle_button_);
    gtk_box_append(GTK_BOX(modes_box_), repeat_button_);
    gtk_box_append(GTK_BOX(modes_box_), loop_button_);
    gtk_box_append(GTK_BOX(content_box_), modes_box_);
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

    gtk_box_append(GTK_BOX(tab_buttons), lyrics_tab_);
    gtk_box_append(GTK_BOX(tab_buttons), queue_tab_);

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
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lyrics_scroll), lyrics_view_);
    gtk_stack_add_named(GTK_STACK(tab_content_stack_), lyrics_scroll, "lyrics");

    // Queue view
    queue_view_ = gtk_list_box_new();
    gtk_widget_add_css_class(queue_view_, "now-playing-queue");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(queue_view_), GTK_SELECTION_NONE);
    GtkWidget* queue_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(queue_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(queue_scroll), queue_view_);
    gtk_stack_add_named(GTK_STACK(tab_content_stack_), queue_scroll, "queue");

    // Connect tab buttons
    g_signal_connect(lyrics_tab_, "clicked", G_CALLBACK(on_lyrics_tab_clicked), this);
    g_signal_connect(queue_tab_, "clicked", G_CALLBACK(on_queue_tab_clicked), this);

    // Default to lyrics
    gtk_stack_set_visible_child_name(GTK_STACK(tab_content_stack_), "lyrics");
    gtk_widget_add_css_class(lyrics_tab_, "active");

    gtk_box_append(GTK_BOX(tabs_box_), tab_buttons);
    gtk_box_append(GTK_BOX(tabs_box_), tab_content_stack_);
    gtk_box_append(GTK_BOX(content_box_), tabs_box_);
}

void RightNowPlaying::update_track_info(const std::string& title, const std::string& artist, const std::string& artwork_path) {
    gtk_label_set_text(GTK_LABEL(track_title_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(track_artist_label_), artist.c_str());

    if (!artwork_path.empty() && artwork_path != current_artwork_path_) {
        current_artwork_path_ = artwork_path;
        GFile* file = g_file_new_for_path(artwork_path.c_str());
        gtk_picture_set_file(GTK_PICTURE(artwork_image_), file);
        g_object_unref(file);
    }
}

void RightNowPlaying::update_playback_position(double position, double duration) {
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

void RightNowPlaying::set_playing(bool playing) {
    // Update play/pause button in bottom panel via main window
    if (main_window_) {
        BottomPlayback* bottom = main_window_->get_bottom_playback();
        if (bottom) {
            bottom->set_playing(playing);
        }
    }
}

void RightNowPlaying::set_queue(const std::vector<std::string>& queue) {
    // Clear existing queue items - iterate through list box rows
    GtkWidget* child = gtk_widget_get_first_child(queue_view_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(queue_view_), child);
        child = next;
    }

    // Add new items (placeholder)
    for (const auto& item : queue) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);

        GtkWidget* art = gtk_picture_new();
        gtk_widget_set_size_request(art, 40, 40);
        gtk_widget_add_css_class(art, "queue-item-art");

        GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* title = gtk_label_new(item.c_str());
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_widget_add_css_class(title, "queue-item-title");
        GtkWidget* artist = gtk_label_new("Artist");
        gtk_label_set_xalign(GTK_LABEL(artist), 0.0);
        gtk_widget_add_css_class(artist, "queue-item-artist");

        gtk_box_append(GTK_BOX(info), title);
        gtk_box_append(GTK_BOX(info), artist);
        gtk_box_append(GTK_BOX(row), art);
        gtk_box_append(GTK_BOX(row), info);

        gtk_list_box_append(GTK_LIST_BOX(queue_view_), row);
    }
}

void RightNowPlaying::set_lyrics(const std::string& lyrics) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lyrics_view_));
    gtk_text_buffer_set_text(buffer, lyrics.c_str(), -1);
}

void RightNowPlaying::on_prev_clicked(GtkButton* button, gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->previous_track();
        }
    }
}

void RightNowPlaying::on_next_clicked(GtkButton* button, gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    if (self->main_window_) {
        BackendController* backend = self->main_window_->get_backend();
        if (backend) {
            backend->next_track();
        }
    }
}

void RightNowPlaying::on_shuffle_toggled(GtkToggleButton* button, gpointer user_data) {
    // TODO: Implement shuffle toggle
}

void RightNowPlaying::on_repeat_toggled(GtkToggleButton* button, gpointer user_data) {
    // TODO: Implement repeat toggle
}

void RightNowPlaying::on_loop_toggled(GtkToggleButton* button, gpointer user_data) {
    // TODO: Implement loop toggle
}

void RightNowPlaying::on_lyrics_tab_clicked(GtkButton* button, gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    gtk_stack_set_visible_child_name(GTK_STACK(self->tab_content_stack_), "lyrics");
    gtk_widget_add_css_class(self->lyrics_tab_, "active");
    gtk_widget_remove_css_class(self->queue_tab_, "active");
}

void RightNowPlaying::on_queue_tab_clicked(GtkButton* button, gpointer user_data) {
    RightNowPlaying* self = static_cast<RightNowPlaying*>(user_data);
    gtk_stack_set_visible_child_name(GTK_STACK(self->tab_content_stack_), "queue");
    gtk_widget_add_css_class(self->queue_tab_, "active");
    gtk_widget_remove_css_class(self->lyrics_tab_, "active");
}

} // namespace lollypop