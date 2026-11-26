/**
 * Lollypop C++ - Fullscreen Queue Panel Implementation
 */

#include "panels/fullscreen_queue_panel.hpp"

#include "main_window.hpp"
#include "services/IPlayerService.hpp"
#include "services/ILibraryService.hpp"
#include "types.hpp"
#include "ui/components/PlaylistDialogs.hpp"
#include "ui/components/TrackRow.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lollypop {
namespace {

constexpr guint kQueueAnimMs = 340;

GtkWidget* create_queue_scroller(GtkWidget* child) {
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(scroller, "fullscreen-queue-scroll");
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_halign(scroller, GTK_ALIGN_FILL);
    gtk_widget_set_valign(scroller, GTK_ALIGN_FILL);
    gtk_widget_set_visible(scroller, TRUE);
    safe_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), child);
    return scroller;
}

} // namespace

FullscreenQueuePanel::FullscreenQueuePanel(MainWindow* main_window,
                                           ILibraryService* library_service)
    : main_window_(main_window),
      library_service_(library_service),
      revealer_(gtk_revealer_new()),
      frame_(gtk_frame_new(nullptr)),
      queue_view_(gtk_list_box_new()) {
    gtk_widget_add_css_class(frame_, "fullscreen-queue-frame");
    gtk_widget_set_hexpand(frame_, TRUE);
    gtk_widget_set_vexpand(frame_, TRUE);
    gtk_widget_set_size_request(frame_, 480, -1);
    gtk_widget_set_visible(frame_, TRUE);
    last_width_ = 480;

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(queue_view_),
                                    GTK_SELECTION_NONE);
    gtk_widget_add_css_class(queue_view_, "fullscreen-queue-list");
    gtk_widget_set_hexpand(queue_view_, TRUE);
    gtk_widget_set_vexpand(queue_view_, TRUE);
    gtk_widget_set_visible(queue_view_, TRUE);
    gtk_frame_set_child(GTK_FRAME(frame_), create_queue_scroller(queue_view_));

    gtk_widget_add_css_class(revealer_, "fullscreen-queue");
    gtk_revealer_set_transition_type(
        GTK_REVEALER(revealer_), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer_), kQueueAnimMs);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer_), FALSE);
    gtk_widget_set_hexpand(revealer_, FALSE);
    gtk_widget_set_vexpand(revealer_, TRUE);
    gtk_widget_set_halign(revealer_, GTK_ALIGN_END);
    gtk_widget_set_valign(revealer_, GTK_ALIGN_FILL);
    gtk_widget_set_visible(revealer_, TRUE);
    gtk_revealer_set_child(GTK_REVEALER(revealer_), frame_);
}

FullscreenQueuePanel::~FullscreenQueuePanel() {
    *alive_ = false;
}

void FullscreenQueuePanel::set_visible(bool visible) {
    if (revealer_) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(revealer_), visible);
    }
}

void FullscreenQueuePanel::set_width(int width) {
    if (!frame_ || width <= 0 || width == last_width_) return;
    last_width_ = width;
    gtk_widget_set_size_request(frame_, width, -1);
}

void FullscreenQueuePanel::set_right_margin(int margin) {
    if (!revealer_ || margin < 0 || margin == last_right_margin_) return;
    last_right_margin_ = margin;
    gtk_widget_set_margin_end(revealer_, margin);
}

void FullscreenQueuePanel::set_queue(
    const std::vector<std::string>& queue_track_ids) {
    safe_list_box_remove_all(GTK_LIST_BOX(queue_view_));
    queue_rows_.clear();

    if (queue_track_ids.empty()) {
        GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(empty, "queue-empty-state");
        gtk_widget_add_css_class(empty, "fullscreen-queue-empty");
        gtk_widget_set_hexpand(empty, TRUE);
        gtk_widget_set_vexpand(empty, TRUE);
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(empty, TRUE);

        GtkWidget* title = gtk_label_new("Your queue is empty");
        gtk_widget_add_css_class(title, "queue-empty-title");
        gtk_label_set_xalign(GTK_LABEL(title), 0.5);
        gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(title, TRUE);
        safe_box_append(GTK_BOX(empty), title);

        GtkWidget* hint = gtk_label_new(
            "Add tracks from your library to start listening.");
        gtk_widget_add_css_class(hint, "queue-empty-description");
        gtk_label_set_xalign(GTK_LABEL(hint), 0.5);
        gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(hint), 40);
        gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(hint, TRUE);
        safe_box_append(GTK_BOX(empty), hint);

        safe_list_box_append(GTK_LIST_BOX(queue_view_), empty);
        return;
    }

    std::map<std::string, Track> tracks_by_id;
    if (library_service_) {
        for (const Track& item :
             library_service_->get_tracks_by_ids(queue_track_ids)) {
            if (!item.id.empty()) tracks_by_id.emplace(item.id, item);
        }
    }

    queue_rows_.reserve(queue_track_ids.size());
    for (size_t i = 0; i < queue_track_ids.size(); ++i) {
        Track track;
        auto it = tracks_by_id.find(queue_track_ids[i]);
        if (it != tracks_by_id.end()) {
            track = it->second;
        } else {
            track.id = queue_track_ids[i];
        }
        const std::string track_id =
            track.id.empty() ? queue_track_ids[i] : track.id;

        auto row = std::make_unique<TrackRow>(
            main_window_, track, static_cast<int>(i + 1),
            TrackRowOptions{.show_track_number = false,
                            .show_album = false,
                            .show_duration = false,
                            .enable_queue_actions = true,
                            .stack_title_artist = true});

        row->set_play_callback([this, track_id](const Track&) {
            schedule_action([this, track_id]() {
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
        });

        row->set_like_callback([this](const Track& item, bool liked) {
            (void)liked;
            if (library_service_) library_service_->toggle_like(item.id);
        });

        row->set_queue_action_callback(
            [this, track](const Track&, const char* action) {
                const std::string action_track_id = track.id;
                if (std::strcmp(action, "add_to_playlist") == 0) {
                    playlist_dialogs::show_add_to_playlist_dialog(
                        main_window_->get_window(), library_service_,
                        {action_track_id});
                    return;
                }

                if (std::strcmp(action, "remove_from_queue") == 0) {
                    schedule_action([this, action_track_id]() {
                        if (!main_window_) return;
                        IPlayerService* backend = main_window_->get_backend();
                        if (!backend) return;
                        const auto queue = backend->get_queue();
                        for (size_t j = 0; j < queue.size(); ++j) {
                            if (queue[j] == action_track_id) {
                                backend->remove_from_queue(j);
                                return;
                            }
                        }
                    });
                    return;
                }

                const bool play_next = std::strcmp(action, "play_next") == 0;
                const bool enqueue =
                    play_next || std::strcmp(action, "add_to_queue") == 0 ||
                    std::strcmp(action, "play_last") == 0;
                if (!enqueue) return;
                schedule_action([this, action_track_id, play_next]() {
                    if (!main_window_) return;
                    if (IPlayerService* backend = main_window_->get_backend()) {
                        backend->enqueue_track(action_track_id, play_next);
                    }
                });
            });

        GtkWidget* row_widget = row->get_widget();
        gtk_widget_add_css_class(row_widget, "right-panel-queue-row");
        gtk_widget_add_css_class(row_widget, "fullscreen-queue-row");
        gtk_widget_set_hexpand(row_widget, TRUE);
        safe_list_box_append(GTK_LIST_BOX(queue_view_), row_widget);
        queue_rows_.push_back(std::move(row));
    }
}

void FullscreenQueuePanel::schedule_action(std::function<void()> action) {
    std::shared_ptr<bool> alive = alive_;
    auto* task = new std::function<void()>(
        [alive, action = std::move(action)]() {
            if (*alive) action();
        });
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto* pending = static_cast<std::function<void()>*>(data);
            (*pending)();
            return G_SOURCE_REMOVE;
        },
        task,
        [](gpointer data) {
            delete static_cast<std::function<void()>*>(data);
        });
}

} // namespace lollypop
