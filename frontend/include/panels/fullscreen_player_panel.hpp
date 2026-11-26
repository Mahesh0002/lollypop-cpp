/**
 * Lollypop C++ - Fullscreen Player Panel
 *
 * Owns the immersive player surface: artwork, track metadata, wavy progress
 * strip, and transport buttons. The close button and lyrics toggle are owned by
 * FullscreenPlayer; the lyrics toggle is inserted into the controls row through
 * get_controls_row(). The synced lyrics panel is a separate overlay sibling, so
 * this panel keeps its width and only slides when lyrics open.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace lollypop {

class ArtworkView;
class ProgressWave;

class FullscreenPlayerPanel {
public:
    using ActionCallback = std::function<void()>;
    using ToggleCallback = std::function<void(bool)>;
    using SeekCallback = std::function<void(double seconds)>;

    FullscreenPlayerPanel();
    ~FullscreenPlayerPanel();

    FullscreenPlayerPanel(const FullscreenPlayerPanel&) = delete;
    FullscreenPlayerPanel& operator=(const FullscreenPlayerPanel&) = delete;

    GtkWidget* get_widget() const { return root_; }
    GtkWidget* get_player_box() const { return center_box_; }
    GtkWidget* get_controls_row() const { return controls_row_; }

    void set_play_callback(ActionCallback callback) {
        play_callback_ = std::move(callback);
    }
    void set_prev_callback(ActionCallback callback) {
        prev_callback_ = std::move(callback);
    }
    void set_next_callback(ActionCallback callback) {
        next_callback_ = std::move(callback);
    }
    void set_shuffle_callback(ToggleCallback callback) {
        shuffle_callback_ = std::move(callback);
    }
    void set_repeat_callback(ToggleCallback callback) {
        repeat_callback_ = std::move(callback);
    }
    void set_repeat_one_callback(ToggleCallback callback) {
        repeat_one_callback_ = std::move(callback);
    }
    void set_seek_callback(SeekCallback callback) {
        seek_callback_ = std::move(callback);
    }

    void set_track_info(const std::string& title,
                        const std::string& artist,
                        const std::string& album,
                        const std::string& artwork_path);
    void set_playback_position(double position,
                               double duration,
                               bool can_seek);
    void set_playing(bool playing);
    void set_shuffle(bool enabled);
    void set_repeat(bool enabled);
    void set_repeat_one(bool enabled);

    void set_width(int width);
    void set_slide_offset(int margin_end);
    void set_artwork_side(int side);

private:
    GtkWidget* root_ = nullptr;
    GtkWidget* center_box_ = nullptr;
    GtkWidget* controls_row_ = nullptr;
    GtkWidget* title_label_ = nullptr;
    GtkWidget* subtitle_label_ = nullptr;
    GtkWidget* play_button_ = nullptr;
    GtkWidget* play_icon_ = nullptr;
    GtkWidget* shuffle_button_ = nullptr;
    GtkWidget* repeat_button_ = nullptr;
    GtkWidget* loop_button_ = nullptr;

    std::unique_ptr<ArtworkView> artwork_view_;
    std::unique_ptr<ProgressWave> progress_wave_;

    ActionCallback play_callback_;
    ActionCallback prev_callback_;
    ActionCallback next_callback_;
    ToggleCallback shuffle_callback_;
    ToggleCallback repeat_callback_;
    ToggleCallback repeat_one_callback_;
    SeekCallback seek_callback_;

    bool updating_controls_ = false;
    bool playing_ = false;
    int last_artwork_side_ = -1;

    void setup_ui();
    void create_artwork();
    void create_track_info();
    void create_progress();
    void create_controls();

    static void on_play_clicked(GtkButton* button, gpointer user_data);
    static void on_prev_clicked(GtkButton* button, gpointer user_data);
    static void on_next_clicked(GtkButton* button, gpointer user_data);
    static void on_shuffle_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_repeat_toggled(GtkToggleButton* button, gpointer user_data);
    static void on_loop_toggled(GtkToggleButton* button, gpointer user_data);
};

} // namespace lollypop
