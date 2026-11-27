/**
 * Lollypop C++ - TrackRow
 *
 * Shared track row component used by Songs, Home, Album details, Artist
 * details, Liked Music, and Queue pages. The whole row is the play control;
 * only the trailing star is a separate action. No hover feedback: rows are
 * separated by a centered hairline instead.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <functional>
#include <unordered_set>

#include "types.hpp"
#include "ui/animation/SpringAnimation.hpp"

namespace lollypop {

class MainWindow;
class ArtworkView;
class EqualizerIndicator;

// Direct children tagged with one of these roles are laid out by
// create_track_columns_layout(). Year, Genre, Date Added, Duration and
// Favourite have fixed widths; Title/Artist/Album split what remains.
enum class TrackColumnRole : int {
    Fixed = 0,
    Title = 1,
    Artist = 2,
    Album = 3,
    Year = 4,
    Genre = 5,
    Duration = 6,
    Favourite = 7,
    DateAdded = 8,
};

struct TrackRowOptions {
    bool show_track_number = false;
    bool show_album = true;
    bool show_duration = true;
    bool show_year = false;
    bool show_genre = false;
    bool show_date_added = false;
    bool enable_queue_actions = false;  // For queue page: show context menu
    // Right-panel queue rows: artwork first, title over artist, both centered.
    bool stack_title_artist = false;
};

using QueueActionCallback = std::function<void(const Track&, const char* action)>;
// action: "play_next", "add_to_queue", "remove_from_queue", "play_last", "add_to_playlist"

void set_track_column_role(GtkWidget* widget, TrackColumnRole role);
GtkLayoutManager* create_track_columns_layout();

class TrackRow {
public:
    using PlayCallback = std::function<void(const Track&)>;
    using LikeCallback = std::function<void(const Track&, bool)>;
    using QueueActionCallback = std::function<void(const Track&, const char* action)>;

    TrackRow(MainWindow* main_window, const Track& track, int track_number,
             TrackRowOptions options = {});
    ~TrackRow();

    TrackRow(const TrackRow&) = delete;
    TrackRow& operator=(const TrackRow&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    void set_play_callback(PlayCallback cb) { play_cb_ = std::move(cb); }
    void set_like_callback(LikeCallback cb) { like_cb_ = std::move(cb); }
    void set_queue_action_callback(QueueActionCallback cb) {
        queue_action_cb_ = std::move(cb);
    }

    void set_playing(bool playing);
    void set_liked(bool liked);
    void set_queue_position(int position);  // For queue page: update track number display

    /// Refresh every live row when playback changes. Safe on the main context.
    static void update_playing_state(const std::string& track_id, bool playing);

    /// Refresh every live row for a track after its like state changes.
    static void update_liked_state(const std::string& track_id, bool liked);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<EqualizerIndicator> equalizer_;
    std::unique_ptr<GtkWidget, WidgetDeleter> number_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> title_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> artist_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> album_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> year_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> genre_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> date_added_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> duration_label_;
    std::unique_ptr<GtkWidget, WidgetDeleter> like_button_;

    MainWindow* main_window_;
    Track track_;
    TrackRowOptions options_;
    int display_track_number_ = 0;
    PlayCallback play_cb_;
    LikeCallback like_cb_;
    QueueActionCallback queue_action_cb_;
    bool playing_ = false;
    bool liked_ = false;
    gint64 last_like_press_time_ = 0;
    gint64 last_row_play_time_ = 0;
    static std::unordered_set<TrackRow*> live_rows_;
    static void on_widget_destroyed(gpointer data);

    void setup_ui();
    void create_context_menu();
    GMenu* create_menu_model();
    static void on_like_clicked(GtkButton* button, gpointer user_data);
    static void on_like_pressed(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static void on_row_activated(GtkListBoxRow* row, gpointer user_data);
    static gboolean on_pressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static gboolean on_released(GtkGestureClick* gesture, int n_press, double x, double y, gpointer user_data);
    static void on_context_menu(GtkGestureClick* gesture, int n_press,
                                double x, double y, gpointer user_data);
    static void on_play_next(GSimpleAction* action, GVariant* parameter, gpointer user_data);
    static void on_add_to_queue(GSimpleAction* action, GVariant* parameter, gpointer user_data);
    static void on_remove_from_queue(GSimpleAction* action, GVariant* parameter, gpointer user_data);
    void animate_press(bool press);
};

} // namespace lollypop
