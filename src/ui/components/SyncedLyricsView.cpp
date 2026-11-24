/**
 * Lollypop C++ - Synced Lyrics View Implementation
 * Auto-scrolling lyric list with progressive fill
 */

#include "ui/components/SyncedLyricsView.hpp"
#include "ui/components/LyricRow.hpp"

#include <gtk/gtk.h>
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

static constexpr double SCROLL_PAUSE_SECONDS = 4.0;
static constexpr double SCROLL_DURATION = 0.32;
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

    // Connect scroll handling
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
    if (vadj) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(on_vadjustment_changed), this);
    }

    widget_owner_.reset(widget_);
}

SyncedLyricsView::SyncedLyricsView(SeekCallback on_seek, bool fullscreen)
    : on_seek_(std::move(on_seek)), fullscreen_(fullscreen) {
    create_widget();
}

SyncedLyricsView::~SyncedLyricsView() {
    cancel_scroll();
}

void SyncedLyricsView::clear() {
    cancel_scroll();
    synced_ = false;
    active_index_ = -1;
    rows_.clear();
    line_starts_.clear();
    current_lyrics_.lines.clear();
    duration_ms_ = 0;
    first_sync_ = true;
    follow_paused_until_ = 0.0;

    // GTK wraps every appended widget in an internal GtkListBoxRow. Walking
    // gtk_widget_get_first_child() removes those wrappers, not the original
    // children, and can leave stale widgets behind. remove_all() clears both.
    gtk_list_box_remove_all(GTK_LIST_BOX(list_box_));
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
    first_sync_ = true;
    follow_paused_until_ = 0.0;

    build_rows();

    // Show the scroller
    gtk_widget_set_visible(scroller_, TRUE);
    if (heading_) gtk_widget_set_visible(heading_, TRUE);
}

void SyncedLyricsView::build_rows() {
    line_starts_.clear();
    line_starts_.reserve(current_lyrics_.lines.size());

    for (size_t i = 0; i < current_lyrics_.lines.size(); ++i) {
        const auto& line = current_lyrics_.lines[i];
        line_starts_.push_back(line.start_ms);

        auto row = std::make_unique<LyricRow>(line, accent_,
            [this](int pos_ms) {
                if (on_seek_) on_seek_(pos_ms);
            }, 0.5);
        row->set_accent(accent_);

        GtkWidget* row_widget = row->get_widget();
        gtk_list_box_append(GTK_LIST_BOX(list_box_), row_widget);
        rows_.push_back(std::move(row));
    }

    // Set initial state
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

    // Update progress for active row
    auto& row = rows_[index];
    if (current_lyrics_.lines[index].is_gap) {
        row->set_gap_phase(static_cast<double>(position_ms) / 420.0);
    } else {
        row->set_progress(line_fraction(current_lyrics_.lines[index],
                                        position_ms, end));
    }
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

    if (index < 0) {
        schedule_top(!first_sync_);
        return;
    }

    // Check if user manually scrolled recently
    double now = g_get_monotonic_time() / 1000000.0;
    if (now < follow_paused_until_) return;

    bool animate = !first_sync_;
    first_sync_ = false;
    schedule_scroll(index, animate);
}

void SyncedLyricsView::on_vadjustment_changed(GtkAdjustment* adjustment, gpointer user_data) {
    (void)adjustment;
    auto* self = static_cast<SyncedLyricsView*>(user_data);
    if (!self->synced_) return;

    double now = g_get_monotonic_time() / 1000000.0;
    self->follow_paused_until_ = now + SCROLL_PAUSE_SECONDS;
    self->cancel_scroll();
}

namespace {

struct ScrollMeasureData {
    SyncedLyricsView* self;
    int index;
    bool animate;
    int token;
    int attempts;
};

} // namespace

gboolean SyncedLyricsView::on_scroll_measure(gpointer user_data) {
    auto* data = static_cast<ScrollMeasureData*>(user_data);
    auto* self = data->self;

    auto finish = [&]() {
        if (self->scroll_data_ == data) self->scroll_data_ = nullptr;
        self->scroll_source_ = 0;
        delete data;
    };

    if (data->token != self->scroll_token_ || !self->synced_) {
        finish();
        return G_SOURCE_REMOVE;
    }
    if (data->index >= static_cast<int>(self->rows_.size())) {
        finish();
        return G_SOURCE_REMOVE;
    }

    auto* row_widget = self->rows_[data->index]->get_widget();
    int row_height = gtk_widget_get_allocated_height(row_widget);
    if (gtk_widget_get_allocated_height(self->scroller_) <= 1 || row_height <= 1) {
        ++data->attempts;
        if (data->attempts >= 20) {
            finish();
            return G_SOURCE_REMOVE;
        }
        // Keep the same data alive; this replaces the expired source id.
        self->scroll_source_ = g_timeout_add(25, &SyncedLyricsView::on_scroll_measure, data);
        return G_SOURCE_REMOVE;
    }

    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scroller_));
    GtkAllocation alloc;
    gtk_widget_get_allocation(row_widget, &alloc);
    double target = alloc.y + row_height / 2.0
                    - gtk_adjustment_get_page_size(adj) / 2.0;
    double maximum = std::max(0.0, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    target = std::max(0.0, std::min(target, maximum));
    if (target < row_height) target = 0.0;

    // Clear our own source bookkeeping before anything can trigger
    // cancel_scroll() through the adjustment change callback.
    if (self->scroll_data_ == data) self->scroll_data_ = nullptr;
    self->scroll_source_ = 0;
    self->follow_paused_until_ = g_get_monotonic_time() / 1000000.0 + SCROLL_PAUSE_SECONDS;

    if (data->animate) {
        self->animate_to(target);
        delete data;
    } else {
        gtk_adjustment_set_value(adj, target);
        delete data;
    }
    return G_SOURCE_REMOVE;
}

void SyncedLyricsView::schedule_scroll(int index, bool animate) {
    cancel_scroll();
    ++scroll_token_;
    auto* data = new ScrollMeasureData{this, index, animate, scroll_token_, 0};
    scroll_data_ = data;
    scroll_source_ = g_timeout_add(25, &SyncedLyricsView::on_scroll_measure, data);
}

void SyncedLyricsView::schedule_top(bool animate) {
    cancel_scroll();
    if (animate) {
        animate_to(0.0);
    } else {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
        if (adj) gtk_adjustment_set_value(adj, 0.0);
    }
}

struct AnimateData {
    SyncedLyricsView* self;
    GtkAdjustment* adj;
    double start_value;
    double delta;
    double start_time;
};

gboolean SyncedLyricsView::on_animate_step(gpointer user_data) {
    auto* data = static_cast<AnimateData*>(user_data);
    auto* self = data->self;
    self->follow_paused_until_ = g_get_monotonic_time() / 1000000.0 + SCROLL_PAUSE_SECONDS;
    double elapsed = g_get_monotonic_time() / 1000000.0 - data->start_time;
    double progress = elapsed / SCROLL_DURATION;
    if (progress >= 1.0) {
        gtk_adjustment_set_value(data->adj, data->start_value + data->delta);
        if (self->anim_data_ == data) self->anim_data_ = nullptr;
        self->anim_source_ = 0;
        delete data;
        return G_SOURCE_REMOVE;
    }
    // Cubic ease-out: 1 - (1 - t)^3
    double eased = 1.0 - std::pow(1.0 - progress, 3);
    gtk_adjustment_set_value(data->adj, data->start_value + data->delta * eased);
    return G_SOURCE_CONTINUE;
}

void SyncedLyricsView::animate_to(double target) {
    cancel_scroll();
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
    if (!adj) return;

    double start_value = gtk_adjustment_get_value(adj);
    double delta = target - start_value;
    if (std::abs(delta) < 1.0) {
        gtk_adjustment_set_value(adj, target);
        return;
    }

    double start_time = g_get_monotonic_time() / 1000000.0;
    follow_paused_until_ = start_time + SCROLL_PAUSE_SECONDS;

    auto* data = new AnimateData{this, adj, start_value, delta, start_time};
    anim_data_ = data;
    anim_source_ = g_timeout_add(16, &SyncedLyricsView::on_animate_step, data);
}

void SyncedLyricsView::cancel_scroll() {
    if (scroll_source_) {
        g_source_remove(scroll_source_);
        scroll_source_ = 0;
    }
    if (scroll_data_) {
        delete static_cast<ScrollMeasureData*>(scroll_data_);
        scroll_data_ = nullptr;
    }
    if (anim_source_) {
        g_source_remove(anim_source_);
        anim_source_ = 0;
    }
    if (anim_data_) {
        delete static_cast<AnimateData*>(anim_data_);
        anim_data_ = nullptr;
    }
    ++scroll_token_;
}

void SyncedLyricsView::scroll_to_current(bool animate) {
    if (!synced_) return;
    if (active_index_ >= 0) {
        schedule_scroll(active_index_, animate);
    } else {
        schedule_top(animate);
    }
}

} // namespace lollypop