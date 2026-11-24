# PiP Window Implementation Plan — Apple Music Mini-Player Parity

## Overview
Transform the current PiP window from a basic horizontal control bar into a **compact, Liquid Glass-styled mini-player** matching Apple Music's mini-player UI.

---

## Phase 0: Prerequisites (Already Done)
- `SyncedLyricsLabel` — compact single-line progressive-fill lyrics (in `BottomPlayback`)
- `ProgressWave` — wavy progress bar with seek callback
- `ArtworkView` — async artwork loading with corner radius
- `IPlayerService` callbacks: track_changed, position_changed, playback_state_changed

---

## Phase 1: Liquid Glass Visual Foundation

### 1.1 CSS Variables (resources/style.css)
Add to `:root`:
```css
/* PiP-specific Liquid Glass tokens */
--pip-glass-bg: rgba(28, 28, 30, 0.72);
--pip-glass-border: rgba(255, 255, 255, 0.1);
--pip-glass-blur: 20px;
--pip-glass-shadow: 0 24px 64px rgba(0, 0, 0, 0.4);
--pip-radius: 16px;
--pip-radius-sm: 10px;
```

Dark mode (already covered by existing glass variables).

### 1.2 Window Configuration (pip_window.cpp)
```cpp
// In setup_ui():
gtk_window_set_decorated(GTK_WINDOW(window_.get()), FALSE);
gtk_window_set_resizable(GTK_WINDOW(window_.get()), TRUE);  // Allow resize
gtk_window_set_default_size(GTK_WINDOW(window_.get()), 400, 200);
gtk_window_set_default_widget(window_.get());  // For focus
gtk_window_set_keep_above(GTK_WINDOW(window_.get()), TRUE);  // Always on top
gtk_window_set_type_hint(GTK_WINDOW(window_.get()), GDK_WINDOW_TYPE_HINT_DIALOG);

// Enable drag-to-move on undecorated window
auto* drag = gtk_gesture_drag_new();
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);
g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), this);
g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), this);
gtk_widget_add_controller(window_.get(), GTK_EVENT_CONTROLLER(drag));
```

### 1.3 Main Container Style
Replace `.pip-content` with:
```css
.pip-window {
    background-color: var(--pip-glass-bg);
    backdrop-filter: blur(var(--pip-glass-blur)) saturate(180%);
    border: 1px solid var(--pip-glass-border);
    border-radius: var(--pip-radius);
    box-shadow: var(--pip-glass-shadow);
}
.pip-window:focus-within {
    border-color: var(--accent-color);
    box-shadow: var(--pip-glass-shadow), 0 0 0 2px var(--accent-color);
}
```

---

## Phase 2: Layout Restructure

### 2.1 Target Layout (Apple Music Mini-Player)
```
┌─────────────────────────────────────────────────────────────────┐
│  .pip-header                                                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ [Artwork 64×64]  Title / Artist / Album      [≡] [×]     │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  .pip-transport                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │        [Prev]  [Play/Pause ●]  [Next]                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  .pip-progress-row                                              │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ 0:00 ████████████████████████████░░░░░░░░░░ 3:45          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  .pip-lyrics-row                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ ♪ "Current lyric line with progressive fill..."          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  .pip-footer                                                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ [🔀] [🔁] [🔂]                                    [🔊]   │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Widget Hierarchy (pip_window.cpp)
```cpp
// Root: GtkBox (vertical, spacing 8, margins 12)
GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

// 1. Header Row: GtkCenterBox
GtkWidget* header = gtk_center_box_new();
//   Start: Artwork + Info (GtkBox horizontal)
//   Center: (empty - for future album name on wide window)
//   End: MenuButton (≡) + CloseButton (×)

// 2. Transport Row: GtkCenterBox
GtkWidget* transport = gtk_center_box_new();
//   Center: GtkBox horizontal (Prev | PlayPause | Next)
//   PlayPause: circular, prominent, 48×48

// 3. Progress Row: GtkBox horizontal
//   TimeLabel (0:00) | ProgressWave (hexpand) | TimeLabel (3:45)

// 4. Lyrics Row: SyncedLyricsLabel (single line, hexpand)

// 5. Footer Row: GtkCenterBox
//   Start: Toggle buttons (Shuffle, Repeat, Loop) in .linked box
//   End: Volume MenuButton
```

### 2.3 Member Variables to Add (pip_window.hpp)
```cpp
// Header
std::unique_ptr<GtkWidget, WidgetDeleter> header_box_;
std::unique_ptr<GtkWidget, WidgetDeleter> menu_button_;      // ≡
std::unique_ptr<GtkWidget, WidgetDeleter> close_button_;     // ×

// Transport
std::unique_ptr<GtkWidget, WidgetDeleter> transport_box_;
std::unique_ptr<GtkWidget, WidgetDeleter> prev_button_;
std::unique_ptr<GtkWidget, WidgetDeleter> play_pause_button_;  // NEW
std::unique_ptr<GtkWidget, WidgetDeleter> next_button_;

// Progress
std::unique_ptr<GtkWidget, WidgetDeleter> progress_box_;
std::unique_ptr<GtkWidget, WidgetDeleter> time_elapsed_label_;
std::unique_ptr<GtkWidget, WidgetDeleter> time_remaining_label_;

// Lyrics
std::unique_ptr<SyncedLyricsLabel> compact_lyrics_label_;

// Footer
std::unique_ptr<GtkWidget, WidgetDeleter> footer_box_;
std::unique_ptr<GtkWidget, WidgetDeleter> shuffle_button_;    // NEW (toggle)
std::unique_ptr<GtkWidget, WidgetDeleter> repeat_button_;     // NEW (toggle)
std::unique_ptr<GtkWidget, WidgetDeleter> loop_button_;       // NEW (toggle)
std::unique_ptr<GtkWidget, WidgetDeleter> volume_button_;     // Existing

// State
bool shuffle_active_ = false;
bool repeat_active_ = false;
bool loop_active_ = false;
```

### 2.4 Callbacks to Add (pip_window.hpp)
```cpp
static void on_play_pause_clicked(GtkButton*, gpointer);
static void on_shuffle_toggled(GtkToggleButton*, gpointer);
static void on_repeat_toggled(GtkToggleButton*, gpointer);
static void on_loop_toggled(GtkToggleButton*, gpointer);
static void on_menu_clicked(GtkMenuButton*, gpointer);  // ≡ menu
static void on_drag_begin(GtkGestureDrag*, double, double, gpointer);
static void on_drag_update(GtkGestureDrag*, double, double, gpointer);
```

### 2.5 Transport Row Detail
```cpp
// Play/Pause button - prominent circular
play_pause_button_ = gtk_toggle_button_new();
gtk_widget_add_css_class(play_pause_button_.get(), "pip-play-pause");
gtk_widget_set_size_request(play_pause_button_.get(), 48, 48);
gtk_button_set_has_frame(GTK_BUTTON(play_pause_button_.get()), FALSE);
// Icon: "media-playback-start-symbolic" / "media-playback-pause-symbolic"
g_signal_connect(play_pause_button_.get(), "clicked", G_CALLBACK(on_play_pause_clicked), this);
```

---

## Phase 3: Synced Lyrics Integration

### 3.1 Reuse Existing SyncedLyricsLabel
```cpp
// In setup_ui():
compact_lyrics_label_ = std::make_unique<SyncedLyricsLabel>("", 0.0);
compact_lyrics_label_->set_line_wrap(false);
compact_lyrics_label_->set_width_chars(50);
compact_lyrics_label_->set_ellipsize(PANGO_ELLIPSIZE_END);
gtk_widget_add_css_class(compact_lyrics_label_->get_widget(), "pip-lyrics");
gtk_box_append(GTK_BOX(lyrics_box), compact_lyrics_label_->get_widget());
```

### 3.2 Update from Backend
```cpp
void PiPWindow::set_lyrics(const std::string& lyrics) {
    has_lyrics_ = !lyrics.empty();
    if (compact_lyrics_label_) {
        if (has_lyrics_ && lyrics.find('[') != std::string::npos) {
            current_lyrics_ = LrcParser::parse(lyrics);
            if (current_lyrics_.valid()) {
                compact_lyrics_label_->set_active(true);
            }
        } else {
            compact_lyrics_label_->set_active(false);
            compact_lyrics_label_->set_text(lyrics.empty() ? "No lyrics" : lyrics);
        }
    }
}

void PiPWindow::update_playback_position(double position, double duration) {
    // ... existing progress_wave_ update ...
    
    // Update compact lyrics
    if (compact_lyrics_label_ && has_lyrics_ && current_lyrics_.valid()) {
        int pos_ms = static_cast<int>(position * 1000);
        compact_lyrics_label_->set_position(pos_ms);
    }
}
```

---

## Phase 4: Mode Buttons (Shuffle/Repeat/Loop)

### 4.1 Toggle Buttons in Linked Box
```cpp
GtkWidget* modes_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
gtk_widget_add_css_class(modes_box, "linked");

auto make_mode_toggle = [](const char* icon, const char* tooltip) {
    GtkWidget* btn = gtk_toggle_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_add_css_class(btn, "pip-mode-button");
    GtkWidget* icon_w = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(icon_w), 16);
    gtk_button_set_child(GTK_BUTTON(btn), icon_w);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_set_size_request(btn, 32, 32);
    return btn;
};

shuffle_button_ = make_mode_toggle("media-playlist-shuffle-symbolic", "Shuffle");
repeat_button_ = make_mode_toggle("media-playlist-repeat-symbolic", "Repeat");
loop_button_ = make_mode_toggle("media-playlist-repeat-song-symbolic", "Repeat One");

gtk_box_append(GTK_BOX(modes_box), shuffle_button_.get());
gtk_box_append(GTK_BOX(modes_box), repeat_button_.get());
gtk_box_append(GTK_BOX(modes_box), loop_button_.get());

g_signal_connect(shuffle_button_.get(), "toggled", G_CALLBACK(on_shuffle_toggled), this);
g_signal_connect(repeat_button_.get(), "toggled", G_CALLBACK(on_repeat_toggled), this);
gtk_signal_connect(loop_button_.get(), "toggled", G_CALLBACK(on_loop_toggled), this);
```

### 4.2 Callbacks
```cpp
void PiPWindow::on_shuffle_toggled(GtkToggleButton* btn, gpointer data) {
    auto* self = static_cast<PiPWindow*>(data);
    if (self->backend_) self->backend_->set_shuffle(gtk_toggle_button_get_active(btn));
}
void PiPWindow::on_repeat_toggled(GtkToggleButton* btn, gpointer data) {
    auto* self = static_cast<PiPWindow*>(data);
    if (self->backend_) self->backend_->set_repeat(gtk_toggle_button_get_active(btn));
}
void PiPWindow::on_loop_toggled(GtkToggleButton* btn, gpointer data) {
    auto* self = static_cast<PiPWindow*>(data);
    if (self->backend_) self->backend_->set_repeat_one(gtk_toggle_button_get_active(btn));
}
```

### 4.3 Sync from Backend (set_backend)
```cpp
void PiPWindow::set_backend(IPlayerService* backend) {
    // ... existing callbacks ...
    
    // Add state sync callbacks
    backend->add_shuffle_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_.get()), enabled);
    });
    backend->add_repeat_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_.get()), enabled);
    });
    backend->add_repeat_one_changed_callback([this, alive](bool enabled) {
        if (!*alive) return;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_.get()), enabled);
    });
    
    // Initial sync
    if (shuffle_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shuffle_button_.get()), backend->get_shuffle());
    if (repeat_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(repeat_button_.get()), backend->get_repeat());
    if (loop_button_) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(loop_button_.get()), backend->get_repeat_one());
}
```

---

## Phase 5: Progress Bar Enhancement

### 5.1 Time Labels
```cpp
time_elapsed_label_ = gtk_label_new("0:00");
gtk_widget_add_css_class(time_elapsed_label_.get(), "pip-time");
time_remaining_label_ = gtk_label_new("0:00");
gtk_widget_add_css_class(time_remaining_label_.get(), "pip-time");

// In update_playback_position():
int elapsed = static_cast<int>(position);
int remaining = static_cast<int>(duration - position);
gtk_label_set_text(GTK_LABEL(time_elapsed_label_.get()), format_time(elapsed).c_str());
gtk_label_set_text(GTK_LABEL(time_remaining_label_.get()), "-" + format_time(remaining).c_str());
```

### 5.2 Hover Scrubber (ProgressWave already supports seek)
Add tooltip on hover:
```cpp
auto* motion = gtk_event_controller_motion_new();
g_signal_connect(motion, "enter", G_CALLBACK(on_progress_enter), this);
g_signal_connect(motion, "leave", G_CALLBACK(on_progress_leave), this);
gtk_widget_add_controller(progress_wave_->get_widget(), GTK_EVENT_CONTROLLER(motion));
```

---

## Phase 6: Keyboard Shortcuts

### 6.1 Shortcut Controller
```cpp
// In setup_ui():
auto* shortcuts = gtk_shortcut_controller_new();

// Space = Play/Pause
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_space, 0),
                     gtk_callback_action_new("pip.play-pause", nullptr)));

// Left/Right = Seek ±5s
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Left, 0),
                     gtk_callback_action_new("pip.seek-backward", nullptr)));
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Right, 0),
                     gtk_callback_action_new("pip.seek-forward", nullptr)));

// Up/Down = Volume ±5%
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Up, 0),
                     gtk_callback_action_new("pip.volume-up", nullptr)));
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Down, 0),
                     gtk_callback_action_new("pip.volume-down", nullptr)));

// Escape = Close
gtk_shortcut_controller_add_shortcut(shortcuts,
    gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, 0),
                     gtk_callback_action_new("pip.close", nullptr)));

gtk_widget_add_controller(window_.get(), GTK_EVENT_CONTROLLER(shortcuts));

// Register actions
GActionEntry entries[] = {
    {"play-pause", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->on_play_pause_clicked(nullptr, self);
    }, nullptr, nullptr, nullptr},
    {"seek-backward", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->seek_relative(-5.0);
    }, nullptr, nullptr, nullptr},
    {"seek-forward", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->seek_relative(5.0);
    }, nullptr, nullptr, nullptr},
    {"volume-up", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->adjust_volume(0.05);
    }, nullptr, nullptr, nullptr},
    {"volume-down", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->adjust_volume(-0.05);
    }, nullptr, nullptr, nullptr},
    {"close", [](GSimpleAction*, GVariant*, gpointer self) {
        static_cast<PiPWindow*>(self)->on_close_clicked(nullptr, self);
    }, nullptr, nullptr, nullptr},
};
g_action_map_add_action_entries(G_ACTION_MAP(app_), entries, 6, this);
```

---

## Phase 7: Menu Button (≡) Popover

### 7.1 Menu Model
```cpp
GMenu* menu = g_menu_new();
g_menu_append(menu, "Open Main Window", "app.show-main-window");
g_menu_append(menu, "Show Lyrics", "pip.show-lyrics");
g_menu_append_separator(menu);
g_menu_append(menu, "Add to Queue", "pip.add-to-queue");
g_menu_append(menu, "Add to Playlist…", "pip.add-to-playlist");
g_menu_append_separator(menu);
g_menu_append(menu, "Sleep Timer", "pip.sleep-timer");
g_menu_append_separator(menu);
g_menu_append(menu, "Settings", "app.preferences");
gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button_.get()), G_MENU_MODEL(menu));
```

---

## Phase 8: CSS Classes (resources/style.css)

### 8.1 New Classes
```css
/* Window */
.pip-window { /* defined in Phase 1 */ }

/* Header */
.pip-header { margin-bottom: 4px; }
.pip-artwork { border-radius: 8px; box-shadow: 0 4px 16px var(--shadow-strong); }
.pip-info-box { min-width: 0; }  /* Allow ellipsis */
.pip-title { font-size: 14px; font-weight: 600; color: var(--text-primary); }
.pip-subtitle { font-size: 12px; color: var(--text-secondary); }
.pip-menu-button, .pip-close-button {
    min-width: 32px; min-height: 32px; border-radius: 50%; padding: 4px;
}
.pip-menu-button:hover { background: var(--bg-hover); }
.pip-close-button { color: var(--text-tertiary); }
.pip-close-button:hover { background: var(--destructive-bg); color: white; }

/* Transport */
.pip-transport { margin: 4px 0; }
.pip-control-button {
    min-width: 40px; min-height: 40px; border-radius: 50%; padding: 8px;
    color: var(--text-primary);
}
.pip-control-button:hover { background: var(--bg-hover); }
.pip-play-pause {
    min-width: 48px; min-height: 48px; border-radius: 50%;
    background: var(--accent-color); color: white;
    box-shadow: 0 4px 16px var(--accent-shadow);
}
.pip-play-pause:hover { background: var(--accent-hover); transform: scale(1.05); }
.pip-play-pause:active { transform: scale(0.95); }

/* Progress */
.pip-progress-row { margin: 4px 0; }
.pip-time { font-size: 11px; color: var(--text-tertiary); font-variant-numeric: tabular-nums; min-width: 40px; }
.pip-progress { /* ProgressWave */ }

/* Lyrics */
.pip-lyrics-row { margin-top: 4px; }
.pip-lyrics {
    font-size: 12px; color: var(--text-secondary);
    font-style: italic; overflow: hidden;
}

/* Footer */
.pip-footer { margin-top: 8px; padding-top: 8px; border-top: 1px solid var(--border-subtle); }
.pip-mode-button {
    min-width: 32px; min-height: 32px; border-radius: 50%; padding: 4px;
    color: var(--text-tertiary);
}
.pip-mode-button:hover { background: var(--bg-hover); color: var(--text-primary); }
.pip-mode-button:checked { color: var(--accent-color); background: var(--accent-bg); }
.pip-volume-button { /* same as mode */ }

/* Linked box for mode buttons */
.linked > *:first-child { border-top-right-radius: 0; border-bottom-right-radius: 0; }
.linked > *:last-child { border-top-left-radius: 0; border-bottom-left-radius: 0; }
.linked > *:not(:first-child):not(:last-child) { border-radius: 0; }
```

---

## Phase 9: Window Drag Handling

### 9.1 Implementation
```cpp
double drag_start_x_ = 0, drag_start_y_ = 0;

static void on_drag_begin(GtkGestureDrag* gesture, double x, double y, gpointer data) {
    auto* self = static_cast<PiPWindow*>(data);
    int wx, wy;
    gtk_window_get_position(GTK_WINDOW(self->window_.get()), &wx, &wy);
    self->drag_start_x_ = wx - x;
    self->drag_start_y_ = wy - y;
}

static void on_drag_update(GtkGestureDrag* gesture, double x, double y, gpointer data) {
    auto* self = static_cast<PiPWindow*>(data);
    int nx = static_cast<int>(self->drag_start_x_ + x);
    int ny = static_cast<int>(self->drag_start_y_ + y);
    gtk_window_move(GTK_WINDOW(self->window_.get()), nx, ny);
}
```

---

## Phase 10: Build & Test Checklist

### 10.1 Compile
```bash
cmake --build build-asan --target lollypop-cpp
```

### 10.2 Run Tests
```bash
./build-asan/tests/test_backend
./build-asan/tests/test_queue_manager
./build-asan/tests/test_seek
```

### 10.3 Manual Verification
- [ ] PiP opens from BottomPlayback button
- [ ] Window is draggable anywhere on background
- [ ] Play/Pause toggles playback
- [ ] Prev/Next navigate queue
- [ ] Progress bar seeks on click
- [ ] Time labels update correctly
- [ ] Lyrics show progressive fill
- [ ] Shuffle/Repeat/Loop toggles sync with backend
- [ ] Volume popover works
- [ ] Close button hides PiP
- [ ] Keyboard shortcuts work (Space, ←→, ↑↓, Esc)
- [ ] Window stays on top
- [ ] Respects dark/light theme

---

## File Change Summary

| File | Type | Description |
|------|------|-------------|
| `frontend/include/panels/pip_window.hpp` | Edit | Add member variables, callback declarations |
| `frontend/src/panels/pip_window.cpp` | Rewrite | Complete `setup_ui()`, new callbacks, state sync |
| `resources/style.css` | Edit | Replace `.pip-*` with new Liquid Glass classes |

---

## Dependencies
- `SyncedLyricsLabel` ✅ (from `BottomPlayback`)
- `LrcParser` ✅
- `ProgressWave` ✅
- `ArtworkView` ✅
- `IPlayerService` shuffle/repeat callbacks ✅ (need to add to interface if missing)

---

## Estimated Timeline
| Phase | Time |
|-------|------|
| 1: Liquid Glass CSS + Window | 30 min |
| 2: Layout Restructure | 60 min |
| 3: Lyrics Integration | 20 min |
| 4: Mode Buttons | 30 min |
| 5: Progress Enhancement | 15 min |
| 6: Keyboard Shortcuts | 20 min |
| 7: Menu Popover | 15 min |
| 8: CSS Polish | 30 min |
| 9: Drag Handling | 15 min |
| 10: Test & Iterate | 30 min |
| **Total** | **~4.5 hours** |