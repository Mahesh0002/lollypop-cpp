/**
 * Lollypop C++ - Synced Lyrics View Implementation
 * Apple Music-style anchored scrolling with per-row fade.
 */

#include "ui/components/SyncedLyricsView.hpp"
#include "ui/components/LyricRow.hpp"

#include <gtk/gtk.h>
#include <graphene.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <glib.h>

namespace lollypop {

namespace {

// Mirrors the Python implementation's _line_fraction(): enhanced LRC word
// timings drive the karaoke fill; plain lines interpolate across their span.
double line_fraction(const SyncedLyricLine& line, int position_ms,
                     int end_ms) {
    const int total = static_cast<int>(g_utf8_strlen(line.text.c_str(), -1));
    if (total <= 0) return 0.0;

    if (!line.words.empty()) {
        if (position_ms <= line.words.front().start_ms) return 0.0;

        for (size_t i = 0; i < line.words.size(); ++i) {
            const auto& word = line.words[i];
            const int next_start = i + 1 < line.words.size()
                ? line.words[i + 1].start_ms
                : end_ms;
            if (position_ms < next_start || i + 1 == line.words.size()) {
                if (position_ms <= word.start_ms) {
                    return static_cast<double>(word.char_start) / total;
                }
                const int span = std::max(1, next_start - word.start_ms);
                const double progress = std::clamp(
                    static_cast<double>(position_ms - word.start_ms) / span,
                    0.0, 1.0);
                return static_cast<double>(word.char_start) / total +
                       static_cast<double>(word.char_end - word.char_start) *
                           progress / total;
            }
        }
        return 1.0;
    }

    const int span = std::max(1, end_ms - line.start_ms);
    return std::clamp(
        static_cast<double>(position_ms - line.start_ms) / span, 0.0, 1.0);
}

} // namespace

// The active line sits in the upper third of the visible lyric area, so past
// lines have room to travel upwards before they fade away.
static constexpr double kAnchorFraction = 0.28;
static constexpr double kFallbackAnchorPx = 90.0;
static constexpr double kScrollTimeConstant = 0.22; // seconds
static constexpr double kManualPauseSeconds = 4.0;
static constexpr int LAST_LINE_FALLBACK_MS = 6000;

void SyncedLyricsView::create_widget() {
    widget_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_ref_sink(widget_);
    gtk_widget_add_css_class(widget_, "synced-lyrics-view");
    if (fullscreen_) {
        gtk_widget_add_css_class(widget_, "fullscreen");
    }
    gtk_widget_set_hexpand(widget_, TRUE);
    gtk_widget_set_vexpand(widget_, TRUE);

    // Heading
    heading_ = gtk_label_new("LYRICS");
    gtk_widget_add_css_class(heading_, "lyrics-heading");
    gtk_label_set_xalign(GTK_LABEL(heading_), 0.0);
    gtk_widget_set_margin_start(heading_, 12);
    gtk_widget_set_margin_end(heading_, 12);
    gtk_widget_set_margin_top(heading_, 8);
    gtk_widget_set_margin_bottom(heading_, 4);
    gtk_box_append(GTK_BOX(widget_), heading_);

    // Scrolled window with list box
    scroller_ = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scroller_, "lyrics-scroller");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller_), TRUE);
    gtk_widget_set_vexpand(scroller_, TRUE);
    gtk_widget_set_hexpand(scroller_, TRUE);

    list_box_ = gtk_list_box_new();
    gtk_widget_add_css_class(list_box_, "lyrics-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_), GTK_SELECTION_NONE);
    gtk_widget_set_hexpand(list_box_, TRUE);
    gtk_widget_set_vexpand(list_box_, TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller_), list_box_);
    gtk_box_append(GTK_BOX(widget_), scroller_);

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
    if (vadj) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(on_vadjustment_changed), this);
    }

    widget_owner_.reset(widget_);
    tick_id_ = gtk_widget_add_tick_callback(
        widget_, &SyncedLyricsView::on_follow_tick, this, nullptr);
}

SyncedLyricsView::SyncedLyricsView(SeekCallback on_seek, bool fullscreen)
    : on_seek_(std::move(on_seek)), fullscreen_(fullscreen) {
    create_widget();
}

SyncedLyricsView::~SyncedLyricsView() {
    // The widget can outlive this object (parents keep their own ref), so the
    // tick callback must be detached explicitly.
    if (widget_ && tick_id_) {
        gtk_widget_remove_tick_callback(widget_, tick_id_);
        tick_id_ = 0;
    }
}

void SyncedLyricsView::clear() {
    synced_ = false;
    active_index_ = -1;
    rows_.clear();
    line_starts_.clear();
    current_lyrics_.lines.clear();
    duration_ms_ = 0;
    target_valid_ = false;
    follow_paused_until_ = 0.0;

    // GTK wraps every appended widget in an internal GtkListBoxRow. remove_all()
    // clears both the wrappers and the original children.
    gtk_list_box_remove_all(GTK_LIST_BOX(list_box_));
    head_spacer_ = nullptr;
    tail_spacer_ = nullptr;
    message_label_ = nullptr;
}

void SyncedLyricsView::set_lyrics(const SyncedLyrics& lyrics) {
    clear();

    if (!lyrics.valid()) {
        set_message(lyrics.lines.empty() ? "No lyrics available." : lyrics.lines[0].text);
        return;
    }

    current_lyrics_ = lyrics;
    synced_ = true;
    duration_ms_ = 0;
    reset_follow();

    build_rows();

    gtk_widget_set_visible(scroller_, TRUE);
    if (heading_) gtk_widget_set_visible(heading_, TRUE);
}

void SyncedLyricsView::build_rows() {
    line_starts_.clear();
    line_starts_.reserve(current_lyrics_.lines.size());

    // Leading spacer puts the first line at the anchor, so every line change
    // produces real upward scroll instead of clamping the target to zero.
    head_spacer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(head_spacer_, "lyrics-head-spacer");
    gtk_widget_set_hexpand(head_spacer_, TRUE);
    gtk_list_box_append(GTK_LIST_BOX(list_box_), head_spacer_);

    for (size_t i = 0; i < current_lyrics_.lines.size(); ++i) {
        const auto& line = current_lyrics_.lines[i];
        line_starts_.push_back(line.start_ms);

        auto row = std::make_unique<LyricRow>(line, accent_,
            [this](int pos_ms) {
                if (on_seek_) on_seek_(pos_ms);
            });
        row->set_accent(accent_);

        GtkWidget* row_widget = row->get_widget();
        gtk_list_box_append(GTK_LIST_BOX(list_box_), row_widget);
        rows_.push_back(std::move(row));
    }

    // Extra scrollable space below the last line so it can still reach the
    // upper-third anchor instead of being stuck at the bottom of the list.
    tail_spacer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tail_spacer_, "lyrics-tail-spacer");
    gtk_widget_set_hexpand(tail_spacer_, TRUE);
    gtk_list_box_append(GTK_LIST_BOX(list_box_), tail_spacer_);

    active_index_ = -2;
    set_active_index(-1);
    gtk_widget_show(list_box_);
}

void SyncedLyricsView::set_message(const std::string& message) {
    clear();
    synced_ = false;

    message_label_ = gtk_label_new(message.c_str());
    gtk_widget_add_css_class(message_label_, "lyrics-message");
    gtk_label_set_wrap(GTK_LABEL(message_label_), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(message_label_), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(message_label_), fullscreen_ ? 0.0 : 0.5);
    gtk_label_set_yalign(GTK_LABEL(message_label_), 0.0);
    gtk_widget_set_hexpand(message_label_, TRUE);
    gtk_widget_set_margin_start(message_label_, 12);
    gtk_widget_set_margin_end(message_label_, 12);
    gtk_widget_set_margin_top(message_label_, 24);
    gtk_widget_set_margin_bottom(message_label_, 24);

    gtk_list_box_append(GTK_LIST_BOX(list_box_), message_label_);
    gtk_widget_show(message_label_);
    gtk_widget_set_visible(scroller_, TRUE);
    if (heading_) gtk_widget_set_visible(heading_, TRUE);
}

void SyncedLyricsView::set_accent(const GdkRGBA& accent) {
    accent_ = accent;
    for (auto& row : rows_) {
        row->set_accent(accent);
    }
    if (synced_ && active_index_ >= 0 && active_index_ < static_cast<int>(rows_.size())) {
        rows_[active_index_]->set_gap_phase(0.0);
    }
}

void SyncedLyricsView::set_position(int position_ms, int duration_ms) {
    if (!synced_ || rows_.empty()) return;

    position_ms = std::max(0, position_ms);
    if (duration_ms > 0) {
        duration_ms_ = duration_ms;
    }

    // Binary search for active line
    int index = -1;
    int low = 0, high = static_cast<int>(line_starts_.size()) - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (line_starts_[mid] <= position_ms) {
            index = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (index != active_index_) {
        set_active_index(index);
    }

    if (index < 0) return;

    // Calculate line end time
    int start = line_starts_[index];
    int end;
    if (index + 1 < static_cast<int>(line_starts_.size())) {
        end = line_starts_[index + 1];
    } else if (duration_ms_ > start) {
        end = duration_ms_;
    } else {
        end = start + LAST_LINE_FALLBACK_MS;
    }
    if (end <= start) end = start + 1000;

    // Update karaoke fill for the active row; scrolling is handled by the tick.
    const auto line_index = static_cast<size_t>(index);
    auto& row = rows_[line_index];
    if (current_lyrics_.lines[line_index].is_gap) {
        row->set_gap_phase(static_cast<double>(position_ms) / 420.0);
    } else {
        row->set_progress(line_fraction(current_lyrics_.lines[line_index],
                                        position_ms, end));
    }

    // The follow target is persistent and refreshed with every position
    // update; the tick callback only eases the adjustment toward it.
    update_follow_target();
}

void SyncedLyricsView::set_active_index(int index) {
    if (index == active_index_) return;
    active_index_ = index;

    for (size_t i = 0; i < rows_.size(); ++i) {
        if (static_cast<int>(i) == index) {
            rows_[i]->set_state(LyricRow::State::Active);
        } else if (static_cast<int>(i) < index) {
            rows_[i]->set_state(LyricRow::State::Past);
        } else {
            rows_[i]->set_state(LyricRow::State::Future);
        }
    }
    // Keep target_valid_: ordinary line changes must glide to the new anchor.
    // Seeks and relayouts are handled by the large-delta branch in the tick.
}

void SyncedLyricsView::reset_follow() {
    target_valid_ = false;
    follow_paused_until_ = 0.0;
}

void SyncedLyricsView::update_follow_target() {
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(scroller_));
    if (!adj) return;

    // Resizing the spacers can clamp the adjustment; treat that as ours.
    programmatic_scroll_ = true;
    update_head_spacer(adj);
    update_tail_spacer(adj);
    programmatic_scroll_ = false;

    follow_target_ = compute_scroll_target(adj);
}

double SyncedLyricsView::row_content_y(GtkWidget* row_widget) const {
    // Bounds relative to the list give the row's offset in scrollable content
    // coordinates (GtkListBox wraps each child in an internal GtkListBoxRow).
    graphene_rect_t bounds;
    if (gtk_widget_compute_bounds(row_widget, list_box_, &bounds)) {
        return static_cast<double>(bounds.origin.y);
    }
    return 0.0;
}

double SyncedLyricsView::row_height(GtkWidget* row_widget) {
    return std::max(1, gtk_widget_get_height(row_widget));
}

double SyncedLyricsView::compute_scroll_target(GtkAdjustment* adj) const {
    const double page = gtk_adjustment_get_page_size(adj);
    const double anchor = page > 1.0 ? page * kAnchorFraction : kFallbackAnchorPx;

    double row_y = 0.0;
    if (active_index_ >= 0 && active_index_ < static_cast<int>(rows_.size())) {
        row_y = row_content_y(rows_[static_cast<size_t>(active_index_)]->get_widget());
    }
    // Bounds are unavailable before the first layout; don't snap to zero.
    if (active_index_ > 0 && row_y <= 0.0) {
        return gtk_adjustment_get_value(adj);
    }

    const double maximum = std::max(
        0.0, gtk_adjustment_get_upper(adj) - page);
    return std::clamp(row_y - anchor, 0.0, maximum);
}

void SyncedLyricsView::update_head_spacer(GtkAdjustment* adj) {
    if (!head_spacer_) return;
    const double page = gtk_adjustment_get_page_size(adj);
    if (page <= 1.0) return;

    // Match the scroll anchor so the active line starts at the upper third.
    const int requested = static_cast<int>(std::ceil(page * kAnchorFraction));

    int current = 0;
    gtk_widget_get_size_request(head_spacer_, nullptr, &current);
    if (std::abs(requested - current) > 4) {
        gtk_widget_set_size_request(head_spacer_, -1, requested);
    }
}

void SyncedLyricsView::update_tail_spacer(GtkAdjustment* adj) {
    if (!tail_spacer_) return;
    const double page = gtk_adjustment_get_page_size(adj);
    if (page <= 1.0) return;

    // One viewport minus the anchor offset: enough for the last line to
    // travel up to the anchor without adding dead scroll space.
    const int requested = static_cast<int>(std::ceil(
        page * (1.0 - kAnchorFraction)));

    int current = 0;
    gtk_widget_get_size_request(tail_spacer_, nullptr, &current);
    if (std::abs(requested - current) > 4) {
        gtk_widget_set_size_request(tail_spacer_, -1, requested);
    }
}

void SyncedLyricsView::update_row_prominence(GtkAdjustment* adj) {
    if (active_index_ < 0 ||
        active_index_ >= static_cast<int>(rows_.size())) {
        reset_row_prominence();
        return;
    }

    const double view_top = gtk_adjustment_get_value(adj);
    const double page = gtk_adjustment_get_page_size(adj);
    const double anchor_offset = page > 1.0
        ? page * kAnchorFraction
        : kFallbackAnchorPx;
    const double unit = row_height(
        rows_[static_cast<size_t>(active_index_)]->get_widget());

    for (size_t i = 0; i < rows_.size(); ++i) {
        GtkWidget* row_widget = rows_[i]->get_widget();
        const double top = row_content_y(row_widget);
        const double bottom = top + row_height(row_widget);
        double alpha = 1.0;

        if (static_cast<int>(i) < active_index_) {
            // Past lines keep scrolling at full strength and dissolve only as
            // their bottom edge reaches the top of the viewport.
            alpha = std::clamp((bottom - view_top) / (unit * 1.25), 0.0, 1.0);
        } else if (static_cast<int>(i) > active_index_) {
            // Upcoming lines stay readable, gently receding with distance.
            const double distance = std::max(
                0.0, top - view_top - anchor_offset) / unit;
            alpha = std::clamp(1.0 - distance / 2.0, 0.55, 1.0);
        }

        rows_[i]->set_prominence(alpha);
    }
}

void SyncedLyricsView::reset_row_prominence() {
    for (auto& row : rows_) {
        row->set_prominence(1.0);
    }
}

void SyncedLyricsView::set_adjustment_value(GtkAdjustment* adj, double value) {
    // Programmatic scrolling must not be mistaken for a user scroll and pause
    // line-following.
    programmatic_scroll_ = true;
    gtk_adjustment_set_value(adj, value);
    programmatic_scroll_ = false;
}

void SyncedLyricsView::on_vadjustment_changed(GtkAdjustment*, gpointer user_data) {
    auto* self = static_cast<SyncedLyricsView*>(user_data);
    if (!self->synced_ || self->programmatic_scroll_) return;

    self->follow_paused_until_ =
        static_cast<double>(g_get_monotonic_time()) / 1000000.0 + kManualPauseSeconds;
    self->target_valid_ = false;
    self->reset_row_prominence();
}

gboolean SyncedLyricsView::on_follow_tick(GtkWidget*, GdkFrameClock* clock,
                                          gpointer user_data) {
    auto* self = static_cast<SyncedLyricsView*>(user_data);
    if (!self->synced_ || self->rows_.empty()) return G_SOURCE_CONTINUE;

    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(self->scroller_));
    if (!adj) return G_SOURCE_CONTINUE;

    // Refresh the persistent target every frame; set_position() also does so
    // immediately so karaoke updates never lag the scroll anchor.
    self->update_follow_target();

    const double now = static_cast<double>(g_get_monotonic_time()) / 1000000.0;
    if (now < self->follow_paused_until_) {
        return G_SOURCE_CONTINUE;
    }

    double dt = 0.0;
    if (clock) {
        const gint64 frame_time = gdk_frame_clock_get_frame_time(clock);
        if (self->last_frame_time_ > 0.0) {
            dt = (static_cast<double>(frame_time) - self->last_frame_time_) / 1000000.0;
        }
        self->last_frame_time_ = static_cast<double>(frame_time);
    } else {
        dt = now - self->last_frame_time_;
        self->last_frame_time_ = now;
    }
    dt = std::clamp(dt, 0.0, 0.05);

    const double target = self->follow_target_;
    const double current = gtk_adjustment_get_value(adj);

    if (!self->target_valid_) {
        self->set_adjustment_value(adj, target);
        self->target_valid_ = true;
    } else {
        const double delta = target - current;
        const double page = gtk_adjustment_get_page_size(adj);
        // Seeks and first layout land directly; ordinary line changes glide.
        if (std::abs(delta) > std::max(120.0, page * 0.8)) {
            self->set_adjustment_value(adj, target);
        } else if (std::abs(delta) > 0.5) {
            const double blend = 1.0 - std::exp(-dt / kScrollTimeConstant);
            self->set_adjustment_value(adj, current + delta * blend);
        }
    }

    self->update_row_prominence(adj);
    return G_SOURCE_CONTINUE;
}

void SyncedLyricsView::scroll_to_current(bool animate) {
    if (!synced_) return;
    follow_paused_until_ = 0.0;
    target_valid_ = false;
    (void)animate;
}

} // namespace lollypop
