/**
 * Lollypop C++ - TrackRow Implementation
 * Shared track row with play/like actions.
 * Spring animations for hover/press states.
 */

#include "ui/components/TrackRow.hpp"
#include "ui/components/ArtworkView.hpp"
#include "ui/components/EqualizerIndicator.hpp"
#include "ui/components/TrackListMetrics.hpp"
#include "services/IPlayerService.hpp"
#include "main_window.hpp"
#include "ui/animation/SpringAnimation.hpp"

#include <gtk/gtk.h>
#include <algorithm>
#include <iostream>
#include <vector>

namespace lollypop {

std::unordered_set<TrackRow*> TrackRow::live_rows_;

namespace {

constexpr int kColumnSpacing = track_list_metrics::kColumnGap;
constexpr const char* kTrackColumnRoleKey = "track-column-role";
constexpr int kTitleShare = 20;
constexpr int kArtistShare = 30;
constexpr int kAlbumShare = 25;
constexpr int kTotalShare = kTitleShare + kArtistShare + kAlbumShare;

int role_index(TrackColumnRole role) {
    switch (role) {
        case TrackColumnRole::Title:  return 0;
        case TrackColumnRole::Artist: return 1;
        case TrackColumnRole::Album:  return 2;
        case TrackColumnRole::Fixed:  break;
    }
    return -1;
}

TrackColumnRole column_role(GtkWidget* widget) {
    return static_cast<TrackColumnRole>(
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                          kTrackColumnRoleKey)));
}

GtkSizeRequestMode track_columns_request_mode(GtkWidget*) {
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

void track_columns_measure(GtkWidget* widget, GtkOrientation orientation,
                           int for_size, int* minimum, int* natural,
                           int* minimum_baseline, int* natural_baseline) {
    int min_size = 0;
    int nat_size = 0;
    int min_baseline = -1;
    int nat_baseline = -1;
    int visible_children = 0;

    for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        if (!gtk_widget_get_visible(child)) continue;
        ++visible_children;

        int child_min = 0;
        int child_nat = 0;
        int child_min_baseline = -1;
        int child_nat_baseline = -1;
        gtk_widget_measure(child, orientation, for_size,
                           &child_min, &child_nat,
                           &child_min_baseline, &child_nat_baseline);

        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
            min_size += child_min;
            nat_size += child_nat;
        } else {
            min_size = std::max(min_size, child_min);
            nat_size = std::max(nat_size, child_nat);
        }
        min_baseline = std::max(min_baseline, child_min_baseline);
        nat_baseline = std::max(nat_baseline, child_nat_baseline);
    }

    if (visible_children > 1) {
        const int gaps = (visible_children - 1) * kColumnSpacing;
        min_size += gaps;
        nat_size += gaps;
    }

    if (minimum) *minimum = min_size;
    if (natural) *natural = nat_size;
    if (minimum_baseline) {
        *minimum_baseline = orientation == GTK_ORIENTATION_HORIZONTAL
                                ? -1 : min_baseline;
    }
    if (natural_baseline) {
        *natural_baseline = orientation == GTK_ORIENTATION_HORIZONTAL
                                ? -1 : nat_baseline;
    }
}

void track_columns_allocate(GtkWidget* widget, int width, int height,
                            int baseline) {
    std::vector<GtkWidget*> children;
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        if (gtk_widget_get_visible(child)) children.push_back(child);
    }
    if (children.empty()) return;

    const int gaps = static_cast<int>(children.size() - 1) * kColumnSpacing;
    std::vector<int> widths(children.size(), 0);
    int fixed_total = 0;
    int flex_minimum[3] = {0, 0, 0};

    for (size_t i = 0; i < children.size(); ++i) {
        int child_min = 0;
        int child_nat = 0;
        gtk_widget_measure(children[i], GTK_ORIENTATION_HORIZONTAL, -1,
                           &child_min, &child_nat, nullptr, nullptr);
        const int idx = role_index(column_role(children[i]));
        if (idx < 0) {
            widths[i] = std::max(child_min, child_nat);
            fixed_total += widths[i];
        } else {
            flex_minimum[idx] = child_min;
        }
    }

    int available = width - fixed_total - gaps;
    int shares[3] = {0, 0, 0};
    if (available > 0) {
        shares[0] = available * kTitleShare / kTotalShare;
        shares[1] = available * kArtistShare / kTotalShare;
        shares[2] = available - shares[0] - shares[1];
    }
    for (int i = 0; i < 3; ++i) {
        shares[i] = std::max(shares[i], flex_minimum[i]);
    }

    int flex_total = shares[0] + shares[1] + shares[2];
    while (available >= 0 && flex_total > available) {
        int largest = -1;
        int largest_excess = 0;
        for (int i = 0; i < 3; ++i) {
            const int excess = shares[i] - flex_minimum[i];
            if (excess > largest_excess) {
                largest_excess = excess;
                largest = i;
            }
        }
        if (largest < 0) break;
        const int reduction = std::min(largest_excess, flex_total - available);
        shares[largest] -= reduction;
        flex_total -= reduction;
    }

    int x = 0;
    for (size_t i = 0; i < children.size(); ++i) {
        const int idx = role_index(column_role(children[i]));
        if (idx >= 0) widths[i] = shares[idx];
        const int child_width = std::max(0, widths[i]);
        graphene_point_t position = {static_cast<float>(x), 0.0f};
        gtk_widget_allocate(children[i], child_width, height, baseline,
                            gsk_transform_translate(nullptr, &position));
        x += child_width + kColumnSpacing;
    }
    (void)widget;
}

}  // namespace

void set_track_column_role(GtkWidget* widget, TrackColumnRole role) {
    if (!widget) return;
    g_object_set_data(G_OBJECT(widget), kTrackColumnRoleKey,
                      GINT_TO_POINTER(static_cast<int>(role)));
}

GtkLayoutManager* create_track_columns_layout() {
    return gtk_custom_layout_new(track_columns_request_mode,
                                 track_columns_measure,
                                 track_columns_allocate);
}

TrackRow::TrackRow(MainWindow* main_window, const Track& track, int track_number)
    : main_window_(main_window), track_(track) {
    (void)track_number;
    setup_ui();
    live_rows_.insert(this);

    // Seed from the authoritative backend so rows created after playback
    // started still show the equalizer immediately.
    if (main_window_) {
        if (auto* backend = main_window_->get_backend()) {
            set_playing(backend->is_playing() &&
                        backend->get_current_track().id == track_.id);
        }
    }
}

TrackRow::~TrackRow() {
    live_rows_.erase(this);
}

void TrackRow::setup_ui() {
    widget_.reset(gtk_list_box_row_new());
    g_object_ref_sink(widget_.get());
    gtk_widget_add_css_class(widget_.get(), "track-row");
    gtk_widget_set_hexpand(widget_.get(), TRUE);
    gtk_widget_set_size_request(widget_.get(), -1,
                                track_list_metrics::kRowHeight);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(widget_.get()), TRUE);
    gtk_widget_set_visible(widget_.get(), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,
                                 track_list_metrics::kColumnGap);
    gtk_widget_set_layout_manager(box, create_track_columns_layout());
    gtk_widget_set_margin_start(box, track_list_metrics::kSideMargin);
    gtk_widget_set_margin_end(box, track_list_metrics::kSideMargin);
    gtk_widget_set_margin_top(box, 0);
    gtk_widget_set_margin_bottom(box, 0);
    gtk_widget_set_visible(box, TRUE);

    // Artwork thumbnail replaces the track number for an Apple Music look.
    artwork_view_ = std::make_unique<ArtworkView>();
    artwork_view_->set_preferred_size(track_list_metrics::kArtworkSize,
                                      track_list_metrics::kArtworkSize);
    artwork_view_->set_corner_radius(
        track_list_metrics::kArtworkCornerRadius);
    gtk_widget_add_css_class(artwork_view_->get_widget(), "track-row-artwork");
    artwork_view_->set_artwork_path(track_.artwork_path);
    gtk_widget_set_visible(artwork_view_->get_widget(), TRUE);

    GtkWidget* artwork_stack = gtk_overlay_new();
    gtk_widget_set_size_request(artwork_stack,
                                track_list_metrics::kArtworkSize,
                                track_list_metrics::kArtworkSize);
    gtk_widget_set_visible(artwork_stack, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(artwork_stack),
                          artwork_view_->get_widget());

    equalizer_ = std::make_unique<EqualizerIndicator>();
    gtk_overlay_add_overlay(GTK_OVERLAY(artwork_stack),
                            equalizer_->get_widget());
    gtk_overlay_set_measure_overlay(GTK_OVERLAY(artwork_stack),
                                    equalizer_->get_widget(), FALSE);
    gtk_box_append(GTK_BOX(box), artwork_stack);

    // Title
    title_label_.reset(gtk_label_new(track_.title.c_str()));
    g_object_ref_sink(title_label_.get());
    gtk_widget_add_css_class(title_label_.get(), "track-row-title");
    gtk_label_set_xalign(GTK_LABEL(title_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_label_.get(), FALSE);
    gtk_widget_set_valign(title_label_.get(), GTK_ALIGN_CENTER);
    set_track_column_role(title_label_.get(), TrackColumnRole::Title);
    gtk_widget_set_visible(title_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), title_label_.get());

    // Artist
    artist_label_.reset(gtk_label_new(track_.artist.c_str()));
    g_object_ref_sink(artist_label_.get());
    gtk_widget_add_css_class(artist_label_.get(), "track-row-artist");
    gtk_label_set_xalign(GTK_LABEL(artist_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(artist_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(artist_label_.get(), FALSE);
    gtk_widget_set_valign(artist_label_.get(), GTK_ALIGN_CENTER);
    set_track_column_role(artist_label_.get(), TrackColumnRole::Artist);
    gtk_widget_set_visible(artist_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), artist_label_.get());

    // Album
    album_label_.reset(gtk_label_new(track_.album.c_str()));
    g_object_ref_sink(album_label_.get());
    gtk_widget_add_css_class(album_label_.get(), "track-row-album");
    gtk_label_set_xalign(GTK_LABEL(album_label_.get()), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(album_label_.get()),
                            PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(album_label_.get(), FALSE);
    gtk_widget_set_valign(album_label_.get(), GTK_ALIGN_CENTER);
    set_track_column_role(album_label_.get(), TrackColumnRole::Album);
    gtk_widget_set_visible(album_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), album_label_.get());

    // Duration
    int dur_min = track_.duration / 60;
    int dur_sec = track_.duration % 60;
    char dur_str[16];
    snprintf(dur_str, sizeof(dur_str), "%d:%02d", dur_min, dur_sec);
    duration_label_.reset(gtk_label_new(dur_str));
    g_object_ref_sink(duration_label_.get());
    gtk_widget_add_css_class(duration_label_.get(), "track-row-duration");
    gtk_label_set_xalign(GTK_LABEL(duration_label_.get()), 1.0);
    gtk_widget_set_size_request(duration_label_.get(),
                                track_list_metrics::kDurationWidth, -1);
    gtk_widget_set_visible(duration_label_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), duration_label_.get());

    // Like button
    like_button_.reset(gtk_button_new());
    g_object_ref_sink(like_button_.get());
    gtk_button_set_has_frame(GTK_BUTTON(like_button_.get()), FALSE);
    gtk_widget_add_css_class(like_button_.get(), "track-row-like-button");
    gtk_widget_set_tooltip_text(like_button_.get(),
                                liked_ ? "Unlike" : "Like");
    GtkWidget* like_icon = gtk_image_new_from_icon_name(
        liked_ ? "starred-symbolic" : "non-starred-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(like_icon),
                              track_list_metrics::kLikeIconSize);
    gtk_button_set_child(GTK_BUTTON(like_button_.get()), like_icon);
    gtk_widget_set_halign(like_button_.get(), GTK_ALIGN_END);
    // Consistent spacing on all four sides.
    gtk_widget_set_margin_start(like_button_.get(),
                                track_list_metrics::kLikeMargin);
    gtk_widget_set_margin_end(like_button_.get(),
                              track_list_metrics::kLikeMargin);
    gtk_widget_set_margin_top(like_button_.get(), 0);
    gtk_widget_set_margin_bottom(like_button_.get(), 0);
    g_signal_connect(like_button_.get(), "clicked",
                     G_CALLBACK(on_like_clicked), this);
    gtk_widget_set_visible(like_button_.get(), TRUE);
    gtk_box_append(GTK_BOX(box), like_button_.get());

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(widget_.get()), box);

    g_signal_connect(widget_.get(), "activate",
                     G_CALLBACK(on_row_activated), this);

    // Use GtkEventControllerMotion for hover detection (GTK4 compatible)
    // instead of enter-notify-event/leave-notify-event which don't work
    // on GtkListBoxRow.
    auto* motion_ctrl = gtk_event_controller_motion_new();
    g_object_set_data_full(G_OBJECT(motion_ctrl), "track-row-this", this,
                           nullptr);
    g_signal_connect(motion_ctrl, "enter", G_CALLBACK(on_enter), this);
    g_signal_connect(motion_ctrl, "leave", G_CALLBACK(on_leave), this);
    gtk_widget_add_controller(widget_.get(), motion_ctrl);

    // Press gesture for spring animation
    auto* click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 0); // All buttons
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_pressed), this);
    g_signal_connect(click_gesture, "released", G_CALLBACK(on_released), this);
    gtk_widget_add_controller(widget_.get(), GTK_EVENT_CONTROLLER(click_gesture));
}

void TrackRow::update_playing_state(const std::string& track_id, bool playing) {
    for (TrackRow* row : live_rows_) {
        if (row) row->set_playing(playing && row->track_.id == track_id);
    }
}

void TrackRow::set_playing(bool playing) {
    playing_ = playing;
    if (equalizer_) equalizer_->set_playing(playing);
    if (playing) {
        gtk_widget_add_css_class(widget_.get(), "playing");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "playing");
    }
}

void TrackRow::set_liked(bool liked) {
    liked_ = liked;
    gtk_widget_set_tooltip_text(like_button_.get(),
                                liked_ ? "Unlike" : "Like");
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(like_button_.get()));
    if (GTK_IS_IMAGE(child)) {
        // Reuse existing image widget, just change the icon name
        gtk_image_set_from_icon_name(GTK_IMAGE(child),
                                     liked_ ? "starred-symbolic" : "non-starred-symbolic");
    } else {
        // Fallback: create new icon if child is not an image
        GtkWidget* new_icon = gtk_image_new_from_icon_name(
            liked_ ? "starred-symbolic" : "non-starred-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(new_icon),
                                  track_list_metrics::kLikeIconSize);
        gtk_button_set_child(GTK_BUTTON(like_button_.get()), new_icon);
        // gtk_button_set_child() automatically unrefs the old child
    }
}

void TrackRow::on_like_clicked(GtkButton* button, gpointer user_data) {
    (void)button;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    if (self->like_cb_) self->like_cb_(self->track_, !self->liked_);
}

void TrackRow::on_row_activated(GtkListBoxRow* row, gpointer user_data) {
    (void)row;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    if (self->play_cb_) self->play_cb_(self->track_);
}

gboolean TrackRow::on_enter(GtkEventControllerMotion* controller, double x,
                            double y, gpointer user_data) {
    (void)controller; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_hover(true);
    return FALSE;
}

gboolean TrackRow::on_leave(GtkEventControllerMotion* controller,
                            gpointer user_data) {
    (void)controller;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_hover(false);
    return FALSE;
}

gboolean TrackRow::on_pressed(GtkGestureClick* gesture, int n_press,
                              double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_press(true);
    return FALSE;
}

gboolean TrackRow::on_released(GtkGestureClick* gesture, int n_press,
                               double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    TrackRow* self = static_cast<TrackRow*>(user_data);
    self->animate_press(false);
    return FALSE;
}

void TrackRow::animate_hover(bool hover) {
    // Use CSS class for hover effect (lighter than spring animation for list rows)
    if (hover) {
        gtk_widget_add_css_class(widget_.get(), "hover");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "hover");
    }
}

void TrackRow::animate_press(bool press) {
    // Use CSS class for press effect
    if (press) {
        gtk_widget_add_css_class(widget_.get(), "pressed");
    } else {
        gtk_widget_remove_css_class(widget_.get(), "pressed");
    }
}

} // namespace lollypop
