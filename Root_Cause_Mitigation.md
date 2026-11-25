# Root Cause Analysis & Concrete Mitigation Steps

> Deep dive into *why* each critical issue exists and *exactly* how to fix it.
> Cross-references: `Frontend_Backend_Analysis.md` sections.

> **Current-tree reconciliation:** This document is strongest as a mechanism explainer, not as a snapshot of every line number. Before acting, reconcile it with the current tree: `get_tracks_by_ids()` is already a single `VALUES` CTE query (`storage/src/manager.cpp:869`), so section 7 is about the SQLite bind-parameter limit and silent failure handling, not N+1. `TrackRow` already removes itself from `live_rows_` in its destructor and through a GObject destroy notify (`TrackRow.cpp:246-270`), so the UAF is a structural risk to eliminate rather than a proven current crash. The `backdrop-filter` declaration is a no-op in GTK CSS, not a working glass effect. Where this document and the current code disagree, the code wins.

---

## 1. STICKY HEADER REPARENTING (Critical)

### Why It Happens

**Design intent**: Apple Music shows a compact header over the scrolled content when the page header scrolls past the toolbar. The header "sticks" to the top of the viewport.

**Implementation choice**: Instead of using `GtkOverlay` with a fixed overlay child (which GTK4 supports natively), the code moves the *same* `GtkWidget*` between two parents:
- Parent A: `SongsPage` stack page (normal flow)
- Parent B: `MiddleLibrary` overlay box (sticky position)

**Root causes**:
1. **GTK3 mental model**: In GTK3, `GtkFixed`/`GtkLayout` allowed arbitrary repositioning. GTK4's `GtkOverlay` + `set_child`/`add_overlay` is the correct pattern but wasn't used.
2. **No state machine**: `update_sticky_header()` is called on every scroll frame (~60fps). It checks `scroll_offset > threshold` and reparents *every time the condition is true*, not just on transition.
3. **`gtk_box_prepend` semantics**: Inserts at index 0. If called 100 times, the header appears 100 times at the top of the box.
4. **No guard**: `gtk_widget_get_parent(header) != target_parent` check missing.

### Mitigation Steps

#### Option A: GtkOverlay (Recommended, ~2h)

```cpp
// In MiddleLibrary::setup_ui() — ONCE at construction
overlay_.reset(gtk_overlay_new());
gtk_overlay_set_child(GTK_OVERLAY(overlay_.get()), main_scroller_);
sticky_header_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
// ... configure sticky_header_ ...
gtk_overlay_add_overlay(GTK_OVERLAY(overlay_.get()), sticky_header_);
gtk_widget_set_visible(sticky_header_, FALSE);  // Hidden by default

// In update_sticky_header() — only toggle visibility
bool should_show = scroll_offset > threshold;
if (should_show != gtk_widget_get_visible(sticky_header_)) {
    gtk_widget_set_visible(sticky_header_, should_show);
}
```

**Why this works**: Widget stays in overlay; no reparenting; GTK handles stacking/clipping.

#### Option B: GtkStack with Two Pages (~3h)

```cpp
// Page 0: Normal SongsPage (with header inside)
// Page 1: SongsPage without header + overlay header
stack_ = gtk_stack_new();
gtk_stack_add_named(GTK_STACK(stack_), songs_page_with_header_, "normal");
gtk_stack_add_named(GTK_STACK(stack_), songs_page_without_header_, "sticky");
// Switch: gtk_stack_set_visible_child_name(stack_, should_show ? "sticky" : "normal");
```

**Trade-off**: Duplicates SongsPage content; more memory.

#### Immediate Band-aid (30min, if rewrite blocked)

```cpp
void MiddleLibrary::update_sticky_header() {
    bool should_stick = scroll_offset > threshold;
    GtkWidget* current_parent = gtk_widget_get_parent(header_widget_);
    GtkWidget* target_parent = should_stick ? overlay_box_ : songs_page_stack_page_;
    if (current_parent == target_parent) return;  // <-- ADD THIS GUARD
    if (current_parent) gtk_box_remove(GTK_BOX(current_parent), header_widget_);
    gtk_box_prepend(GTK_BOX(target_parent), header_widget_);
}
```

---

## 2. TRACKROW STATIC REGISTRY & LIFETIME (Critical)

### Why It Happens

**Design intent**: `MiddleLibrary::update_track_rows_playing()` needs to find *all* `TrackRow` instances across all pages to update the `.playing` CSS class when the current track changes.

**Implementation choice**: Static `std::unordered_set<TrackRow*> live_rows_` populated in constructor, cleaned up via GObject destroy notify.

**Root causes**:
1. **C++ object lifetime ≠ GObject lifetime**: `TrackRow` C++ destructor runs when owning `unique_ptr` is destroyed (page `refresh()`). GObject widget may still be alive (parented in `GtkListBox`). The destroy notify fires *later* — callback sees dangling `this`.
2. **No synchronization**: `live_rows_` accessed from UI thread (scroll, click) and potentially from backend callbacks (position updates) — no mutex.
3. **Context menu created per-event**: `on_context_menu()` calls `create_context_menu()` which creates new `GtkGestureClick` + `GSimpleActionGroup` + `GMenu` every right-click. Old controllers leak; `GMenu` never unref'd.
4. **Dual activation path**: `GtkListBoxRow::activate` signal + custom `GtkGestureClick` both trigger play. Two 300ms guards (`last_click_time_`) race.

### Mitigation Steps

#### Step 1: Shared Ownership (4h)

```cpp
// TrackRow.hpp
class TrackRow : public std::enable_shared_from_this<TrackRow> {
    // Factory method
    static std::shared_ptr<TrackRow> create(MainWindow*, const Track&, int, TrackRowOptions);
    // Constructor private
};

// In page populate:
auto row = TrackRow::create(main_window_, track, i, options);
row->set_play_callback([wp = row->weak_from_this()](const Track& t) {
    if (auto sp = wp.lock()) sp->on_play(t);
});
track_rows_.push_back(row);  // vector<shared_ptr<TrackRow>>
```

#### Step 2: Central Registry with Weak Pointers (1h)

```cpp
// MiddleLibrary.hpp
std::vector<std::weak_ptr<TrackRow>> all_track_rows_;

// In page->populate():
for (auto& row : new_rows) {
    all_track_rows_.push_back(row);  // weak_ptr
}

// In update_track_rows_playing():
for (auto& wp : all_track_rows_) {
    if (auto row = wp.lock()) row->set_playing(is_current);
}
// Periodic cleanup:
std::erase_if(all_track_rows_, [](auto& wp){ return wp.expired(); });
```

#### Step 3: Single Activation Path (1h)

```cpp
// Remove GtkListBoxRow activate signal handler
// Use ONLY custom click gesture:
auto* click = gtk_gesture_click_new();
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
g_signal_connect(click, "released", G_CALLBACK(on_click_released), this);
// Double-click detection INSIDE handler:
if (n_press == 2 && play_cb_) play_cb_(track_);
else if (click_cb_) click_cb_(track_);
```

#### Step 4: Context Menu Once (1h)

```cpp
// In constructor:
create_context_menu();  // Creates gesture + action group + menu ONCE

// create_context_menu():
if (context_menu_created_) return;
context_menu_created_ = true;
// ... setup ...
```

#### Step 5: GMenu Unref (30min)

```cpp
GMenu* menu = create_menu_model();
GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
g_object_unref(menu);  // <-- ADD THIS
gtk_widget_set_parent(popover, widget_.get());
// ...
```

---

## 3. THEMEMANAGER DUAL :ROOT CONFLICT (Critical)

### Why It Happens

**Design intent**: Allow runtime accent color changes without reloading entire stylesheet.

**Implementation choice**: Two `GtkCssProvider`s at **same priority** (`GTK_STYLE_PROVIDER_PRIORITY_APPLICATION`):
- Provider 1: Base stylesheet (`style.css`) with full `:root { --accent-color: #a78bfa; --bg-primary: ...; --font-scale: 1; ... }`
- Provider 2: Dynamic accent provider, `update_css_overrides()` loads `:root { --accent-color: rgba(...); --accent-soft: ...; --font-scale: ... }`

**Root cause**: GTK CSS cascade — when two providers have **equal priority**, the **last-applied** wins for overlapping selectors. `update_css_overrides()` calls `gtk_css_provider_load_from_string()` which **replaces the entire provider content**. Since both providers target `:root`, the dynamic provider's 3-property `:root` **completely overwrites** the base provider's 70+ properties.

**Evidence**: `ThemeManager.cpp:86-93` attaches both at same priority. `update_css_overrides()` (line 165-181) generates only accent/font properties.

### Mitigation Steps

#### Fix: Higher Priority + Minimal Override (2h)

```cpp
// ThemeManager::load_stylesheet() — line 90-93
gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(base_css_provider_.get()),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);  // Base = 600

gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(accent_css_provider_.get()),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);  // Accent = 601 (HIGHER)
```

```cpp
// update_css_overrides() — ONLY override accent/font, not entire :root
void ThemeManager::update_css_overrides() {
    gchar* css = g_strdup_printf(
        ":root {\n"
        "  --accent-color: rgba(%d, %d, %d, %f);\n"
        "  --accent-soft: rgba(%d, %d, %d, %f);\n"
        "  --accent-hover: rgb(%d, %d, %d);\n"
        "  --accent-active: rgb(%d, %d, %d);\n"
        "  --accent-shadow: rgba(%d, %d, %d, %f);\n"
        "  --font-scale: %f;\n"
        "}\n",
        // ... values ...
    );
    gtk_css_provider_load_from_string(accent_css_provider_.get(), css);
    g_free(css);
}
```

**Why this works**: Higher priority = accent provider wins for its properties only. Base provider's `:root` remains intact for all other variables.

---

## 4. INTER FONT NEVER LOADED (High)

### Why It Happens

**Design intent**: Use Apple's San Francisco equivalent (Inter variable font) for exact HIG typography.

**Implementation**: Font file bundled in `resources/Inter-VariableFont_slnt,wght.ttf` and listed in `lollypop.gresource.xml`. CSS requests `font-family: "Inter"`.

**Root cause**: **No code registers the font with fontconfig/Pango**. GTK's CSS `font-family` only matches *installed* fonts. GResource makes bytes available to `g_resources_get_data()`, but Pango doesn't scan GResource.

### Mitigation Steps

#### Fix: Load at Application Startup (2h)

```cpp
// frontend/src/application.cpp — in Application::startup() or constructor
void Application::load_bundled_fonts() {
    GError* error = nullptr;
    GBytes* font_data = g_resources_lookup_data(
        "/com/mahesh/lollypop/fonts/Inter-VariableFont_slnt,wght.ttf",
        G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!font_data) {
        g_clear_error(&error);
        return;
    }

    // Option A: fontconfig (requires FcConfig)
    #ifdef HAVE_FONTCONFIG
    FcConfig* config = FcConfigGetCurrent();
    FcBool result = FcConfigAppFontAddMem(config,
        static_cast<const FcChar8*>(g_bytes_get_data(font_data, nullptr)),
        g_bytes_get_size(font_data));
    if (!result) g_warning("Failed to load Inter font via fontconfig");
    #endif

    // Option B: GdkFontMap (GTK4, simpler)
    GdkDisplay* display = gdk_display_get_default();
    GdkFontMap* font_map = gdk_display_get_font_map(display);
    // Note: GDK has no direct "add memory font" API in 4.x.
    // Workaround: write to temp file + g_font_map_add_font_file()
    // Or use fontconfig above.

    g_bytes_unref(font_data);
}
```

**Better**: Use `FcConfigAppFontAddMem` (fontconfig) — works headless, no temp file.

**Verification**: After fix, `pango_font_map_list_families()` should include "Inter".

---

## 5. BACKDROP-FILTER IN PIP WINDOW (Medium)

### Why It Happens

**Design intent**: Liquid glass / frosted glass effect for PiP window (Apple HIG).

**Implementation**: `style.css:1319` — `.pip-window { backdrop-filter: blur(20px) saturate(180%); }`

**Root cause**: **GTK CSS does not support `backdrop-filter`**. The property is parsed but ignored (no-op). Comment at line 13 acknowledges this.

### Mitigation Steps

#### Fix: Simulate with Translucent Background + Shadow (1h)

```css
/* Replace .pip-window block */
.pip-window {
    background-color: var(--glass-regular-bg);  /* rgba(255,255,255,0.08) */
    background-image: linear-gradient(          /* Subtle highlight */
        180deg,
        rgba(255, 255, 255, 0.10),
        rgba(255, 255, 255, 0.04)
    );
    border: 1px solid var(--glass-regular-border);
    border-radius: 16px;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.4),
                0 0 0 1px rgba(255, 255, 255, 0.06) inset;  /* Inner highlight */
}

/* For "active/focused" state */
.pip-window:focus-within {
    border-color: var(--accent-color);
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.4),
                0 0 0 2px var(--accent-color),
                0 0 0 1px rgba(255, 255, 255, 0.10) inset;
}
```

**Why this works**: Uses existing `--glass-*` tokens. No parser warnings. Visual approximation of frosted glass.

---

## 6. RIGHTNOWPLAYING FULL QUEUE REBUILD (High)

### Why It Happens

**Design intent**: Keep queue UI in sync with `QueueManager`.

**Implementation**: `populate_queue()` called on every `QueueManager` change signal. Destroys all `GtkListBox` children, clears `track_rows_` vector, recreates from scratch.

**Root causes**:
1. **No diff algorithm**: `QueueManager` emits generic "changed" signal — no granular add/remove/move info.
2. **`GtkListBox` not virtualized**: All rows rendered; 500 tracks = 500 widgets.
3. **`TrackRow` heavy**: Each creates artwork loader, gestures, context menu, equalizer.

### Mitigation Steps

#### Short-term: Diff-Based Update (4h)

```cpp
// QueueManager.hpp — add granular signals
sigc::signal<void, size_t, const std::string&> signal_track_added;      // index, track_id
sigc::signal<void, size_t> signal_track_removed;                        // index
sigc::signal<void, size_t, size_t> signal_track_moved;                  // from, to
sigc::signal<void> signal_cleared;

// RightNowPlaying.cpp
void RightNowPlaying::on_track_added(size_t index, const std::string& track_id) {
    auto track = library_service_->get_track(track_id);
    auto row = std::make_unique<TrackRow>(...);
    safe_list_box_insert_at(GTK_LIST_BOX(queue_list_box_), row->get_widget(), index);
    track_rows_.insert(track_rows_.begin() + index, std::move(row));
}

void RightNowPlaying::on_track_removed(size_t index) {
    if (index < track_rows_.size()) {
        gtk_list_box_remove(GTK_LIST_BOX(queue_list_box_), track_rows_[index]->get_widget());
        track_rows_.erase(track_rows_.begin() + index);
    }
}
// ... similar for moved/cleared
```

#### Long-term: GtkListView + GListModel (16h)

```cpp
// QueueListModel : GObject implementing GListModel
// - Wraps QueueManager::get_queue() (vector<string>)
// - Emits items-changed on granular changes
// RightNowPlaying uses GtkListView with QueueListModel
// - Virtualized: only visible rows created
// - Factory creates TrackRow on demand
// - Automatic diff/animation via GtkListView
```

---

## 7. GET_TRACKS_BY_IDS PARAMETER LIMIT / FAILURE VISIBILITY (Critical Backend)

### Why It Happens

**Implementation**: `storage/src/manager.cpp:869-893`

```cpp
std::vector<Track> StorageManager::get_tracks_by_ids(const std::vector<std::string>& track_ids) {
    // Builds VALUES CTE with (id, ord) pairs
    // Single query: SELECT ... FROM tracks t JOIN input_ids i ON t.id = i.id ORDER BY i.ord
}
```

**Finding**: this is already a single query. The N+1 framing from the companion analysis is stale; the `VALUES` CTE is the right shape and should be kept.

**Remaining risk**: `controller.cpp:restore_saved_queue()` can pass a saved queue with 1000+ IDs. The query then needs 1000+ bind parameters, while SQLite's default `SQLITE_MAX_VARIABLE_NUMBER` is commonly 999. When `sqlite3_prepare_v2()` fails, the current code returns an empty vector without logging `sqlite3_errmsg()`, so the user-visible symptom is simply missing tracks.

**Root cause**: no batching for large ID lists, no prepare-error reporting, and `get_sorted_track_ids()` still hard-caps at 10,000.

### Mitigation Steps

#### Fix: Batch Large ID Lists (3h)

```cpp
std::vector<Track> StorageManager::get_tracks_by_ids(const std::vector<std::string>& track_ids) {
    constexpr size_t kMaxBatch = 500;  // Well under 999 param limit
    std::vector<Track> result;
    result.reserve(track_ids.size());

    for (size_t offset = 0; offset < track_ids.size(); offset += kMaxBatch) {
        size_t count = std::min(kMaxBatch, track_ids.size() - offset);
        std::vector<std::string> batch(track_ids.begin() + offset,
                                        track_ids.begin() + offset + count);
        auto batch_tracks = get_tracks_by_ids_batched(batch);
        result.insert(result.end(), batch_tracks.begin(), batch_tracks.end());
    }
    return result;
}

std::vector<Track> StorageManager::get_tracks_by_ids_batched(const std::vector<std::string>& ids) {
    // Existing VALUES CTE logic (unchanged)
}
```

#### Fix: Remove 10k Cap (1h)

```cpp
// controller.cpp: get_sorted_track_ids()
std::vector<std::string> BackendController::get_sorted_track_ids() {
    // Remove: if (ids.size() > 10000) ids.resize(10000);
    // Or make configurable:
    static constexpr size_t kMaxLibraryTracks = 100000;  // Or 0 = unlimited
    // ...
}
```

---

## 8. DETACHED THREADS CAPTURING THIS (High Backend)

### Why It Happens

**Pattern**: `controller.cpp` lines ~1500-1600

```cpp
void BackendController::refresh_album_artwork_from_itunes() {
    std::thread([this]() {  // <-- CAPTURES this
        // ... long-running network I/O ...
        // Calls storage_->update_album_artwork()
    }).detach();
}
```

**Root cause**: Fire-and-forget for async I/O. No ownership tracking. If `BackendController` destroyed (app shutdown, user switches library), thread accesses freed `storage_`, `this`.

### Mitigation Steps

#### Fix: Thread Pool + Weak Pointers (4h)

```cpp
// BackendController.hpp
#include <thread_pool>  // Or simple implementation

class BackendController {
    std::shared_ptr<ThreadPool> artwork_pool_;
    std::weak_ptr<BackendController> weak_self_;  // Set in constructor

    void refresh_album_artwork_from_itunes() {
        auto self = weak_self_.lock();
        if (!self) return;
        artwork_pool_->enqueue([self]() {
            if (auto s = self.lock()) {  // Re-check inside
                s->do_refresh_album_artwork();
            }
        });
    }
};
```

**Simpler**: Use `std::shared_ptr<BackendController>` everywhere, capture `weak_ptr` in lambdas. Join threads in `shutdown()`.

---

## 9. LEGACY LYRICS CALLBACK SINGLE-SLOT (High)

### Why It Happens

**Code**: `controller.cpp` ~line 1800

```cpp
void BackendController::request_lyrics(const std::string& track_id, LyricsCallback cb) {
    legacy_lyrics_callback_ = std::move(cb);  // OVERWRITES previous
    lyrics_manager_->request_lyrics(track_id, ..., [this](...) {
        if (legacy_lyrics_callback_) legacy_lyrics_callback_(...);
    });
}
```

**Root cause**: Legacy API designed for single request. Now used concurrently (multiple tracks loading lyrics).

### Mitigation Steps

#### Fix: Multicast Callback (2h)

```cpp
// BackendController.hpp
using LyricsCallback = std::function<void(const std::string&, const Lyrics&)>;
std::unordered_map<std::string, std::vector<LyricsCallback>> pending_lyrics_callbacks_;

void BackendController::request_lyrics(const std::string& track_id, LyricsCallback cb) {
    pending_lyrics_callbacks_[track_id].push_back(std::move(cb));
    if (pending_lyrics_callbacks_[track_id].size() == 1) {
        // First request for this track — actually fetch
        lyrics_manager_->request_lyrics(track_id, ..., [this, track_id](const std::string&, const Lyrics& lyrics) {
            auto it = pending_lyrics_callbacks_.find(track_id);
            if (it != pending_lyrics_callbacks_.end()) {
                for (auto& cb : it->second) cb(track_id, lyrics);
                pending_lyrics_callbacks_.erase(it);
            }
        });
    }
}
```

---

## 10. CSS DUPLICATE SELECTORS (Medium)

### Why It Happens

**History**: `style.css` evolved incrementally. `apple_ui.css` added later as override layer. No consolidation pass.

### Mitigation Steps

#### Automated Detection + Manual Merge (3h)

```bash
# Find duplicate selectors
grep -h '^\.' resources/style.css resources/apple_ui.css | \
  sed 's/{.*//' | sort | uniq -d
```

**Key merges needed**:
| Selector | Keep In | Action |
|----------|---------|--------|
| `.album-card` | `style.css:610` (Apple-style) | Delete `style.css:574` block |
| `.track-row` | `style.css:1867` (HIG 53px) | Delete `style.css:680` block |
| `.songs-column-header` | `style.css:1740` | Delete `style.css:1933` |
| `button` base | `style.css:120` | Delete `apple_ui.css:29` (transitions disabled globally — breaks hover) |

**Rule**: `apple_ui.css` should ONLY contain Apple-specific overrides (sidebar, album tiles), not global resets.

---

## 11. MISSING BOTTOMPLAYBACK PLAY/PAUSE (High)

### Why It Happens

**Oversight**: `BottomPlayback::setup_ui()` creates prev/next/seek/volume/PiP but **forgot play/pause**.

### Mitigation Steps (1h)

```cpp
// BottomPlayback.cpp — in setup_ui() after prev button
play_pause_button_ = gtk_button_new_from_icon_name("media-playback-start-symbolic");
gtk_widget_add_css_class(play_pause_button_, "bottom-playback-control-button");
gtk_widget_set_tooltip_text(play_pause_button_, "Play/Pause");
g_signal_connect(play_pause_button_, "clicked", G_CALLBACK(on_play_pause_clicked), this);
gtk_box_append(GTK_BOX(controls_box), play_pause_button_);

// Callback
void BottomPlayback::on_play_pause_clicked(GtkButton*, gpointer user_data) {
    auto* self = static_cast<BottomPlayback*>(user_data);
    if (self->playback_source_) self->playback_source_->play_pause();
}

// Update icon in set_playing():
void BottomPlayback::set_playing(bool playing) {
    const char* icon = playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
    gtk_button_set_child(GTK_BUTTON(play_pause_button_),
                         gtk_image_new_from_icon_name(icon));
}
```

---

## 12. PIP TOGGLE STATIC VARIABLE (Medium)

### Why It Happens

**Code**: `BottomPlayback.cpp:341`

```cpp
void BottomPlayback::on_pip_toggled() {
    static bool pip_visible = false;  // Function-local static
    pip_visible = !pip_visible;
    // ...
}
```

**Root cause**: Quick prototype. State should live in `MainWindow` (single source of truth for PiP window).

### Mitigation Steps (30min)

```cpp
// MainWindow.hpp
bool pip_visible_ = false;
std::unique_ptr<PiPWindow> pip_window_;

// BottomPlayback.hpp — add callback
std::function<void(bool)> pip_toggle_callback_;

// BottomPlayback.cpp
void BottomPlayback::on_pip_toggled() {
    if (pip_toggle_callback_) pip_toggle_callback_(!pip_visible_);
}

// MainWindow::connect_backend() / setup_ui()
bottom_playback_->set_pip_toggle_callback([this](bool show) {
    if (show) {
        if (!pip_window_) pip_window_ = std::make_unique<PiPWindow>(this);
        pip_window_->present();
    } else {
        if (pip_window_) pip_window_->close();
    }
    pip_visible_ = show;
});
```

---

## 13. DESIGN ROOT CAUSES AND MITIGATIONS

This section translates the Apple design research into local GTK4/Adwaita rules. It is **not** "make the app look like Apple"; it is "make one coherent, accessible, calm music interface". Sources: `docs/apple_design_philosophy_deep_dive.md`, `docs/APPLE_DESIGN_EXTRACTION_ACTION_PLAN.md`, and `docs/design-rules.yaml`.

> Simplicity is clarity, not minimalism. A control hidden until hover is not simple; it is undiscoverable.

| Design root cause | Principle / rule | Repo evidence | Mitigation | Done when |
|-------------------|------------------|---------------|------------|-----------|
| Parallel visual systems grew independently | Familiarity/Craft — FAM-2, CRA-1; gaps G01, G02; task P0-1 | `resources/style.css` has `--font-*`/`--font-scale`; `resources/apple_ui.css` has `--apple-size-*` and hardcoded px | Replace Apple type sizes with canonical `--font-*` tokens; make `--font-scale` the only scale | `grep -R -- '--apple-size-' resources/` and `grep -R 'font-size: [0-9]' resources/apple_ui.css` are empty |
| Accessibility added per widget instead of by contract | Flexibility — FLE-1, FLE-3; gap G03; task P0-2 | Tooltips exist in custom widgets; shared accessible-name helper only observed in `right_now_playing.cpp:74` | Extract a shared helper; apply to AlbumCard, AppleAlbumTile, TrackRow like/play controls, sidebar rows, PiP buttons | Every focusable custom widget has a tooltip and an object-specific accessible name |
| State tied to pointer instead of intent | Flexibility — FLE-3; gap G04; task P0-3 | `.apple-album-favorite` is opacity-hidden until hover/favourite | Add `.apple-album-tile:focus-within .apple-album-favorite` and `.apple-album-favorite:focus-visible` reveal rules | Tab reveals the control; Enter/Space toggles it |
| Two focus grammars | Familiarity — FAM-2; gap G05; task P0-4 | Base layer uses `--border-focus`; Apple layer uses a different outline treatment | One focus token, one outline implementation; `:focus-visible` beats hover | No layer-specific focus ring remains |
| Motion and material are either dead or globally suppressed | Delight — DEL-3; gaps G06, G09; task P1-3 | `button { transition:none; animation:none; }`; `backdrop-filter` no-op; undecorated resizable PiP | Add `--motion-*` tokens; scope the global disable; remove dead CSS; document PiP focus/close/keyboard contract | Meaningful feedback remains, reduced-motion still works, PiP has a clear exit |
| Queue mutations are not forgiving | Agency — AGE-3; gap G07; task P1-2 | Clear/remove/reorder paths observed without an obvious undo pattern | Add undo or confirmation to destructive queue actions | No queue mutation silently destroys user work |
| Lifecycle correctness treated as separate from craft | Craft — CRA-3 | Detached threads, sticky-header reparenting, raw `TrackRow` registry | Finish sections 1, 2, 7 and 8 of this document | Design smoke flow produces no GTK criticals/assertions |

**Implementation order:** land D1-D4 (P0 design gates) before visual polish, then P1 motion/copy/undo work. Do not accept a fix that improves appearance while reintroducing a second token family, hover-only control, unnamed focusable widget, or divergent focus ring.

**Design gates:** 0 duplicate type scales, 0 hover-only controls, 0 custom focusable widgets without accessible names, 0 custom focusable widgets without `:focus-visible` styling, 0 raw hex colours outside token definitions, and 0 GTK criticals in the smoke flow.

---

## SUMMARY: MITIGATION PRIORITY MATRIX

| # | Issue | Root Cause | Fix Effort | Risk if Unfixed |
|---|-------|------------|------------|-----------------|
| D1 | Duplicate typography token families | Apple layer added as a parallel skin | 2h | Text scale/theme drift |
| D2 | Custom widgets without shared accessible names | Accessibility added ad hoc | 3h | Screen-reader users blocked |
| D3 | Hover-only album favourite | State tied to pointer, not focus | 1h | Keyboard users cannot discover/toggle |
| D4 | Divergent focus rings | Two CSS layers, no canonical token | 1h | Inconsistent keyboard affordance |
| D5 | Motion policy too blunt / dead glass CSS | Global disable plus no-op declaration | 1h | No meaningful feedback; misleading CSS |
| 1 | Sticky header reparenting | GTK3 pattern in GTK4 | 2h (Overlay) | Crashes, visual corruption |
| 2 | TrackRow static registry UAF risk | C++/GObject lifetime mismatch | 6h (shared_ptr) | Fragile ownership, possible UAF/leaks |
| 3 | ThemeManager :root conflict | Equal priority providers | 2h | All design tokens lost |
| 4 | Inter font registration unverified | Resource bundled, runtime load unconfirmed | 2h | Wrong metrics, fallback font |
| 5 | RightNowPlaying full rebuild | No diff signals, no virtualization | 4h (diff) / 16h (ListView) | UI freeze >200 tracks |
| 6 | get_tracks_by_ids parameter limit / silent failure | SQLite bind limit, no batching or error reporting | 3h | Queue restore fails >999 with no diagnosis |
| 7 | Detached threads capture this | Fire-and-forget async | 4h | Shutdown crashes |
| 8 | Single-slot lyrics callback | Legacy API | 2h | Concurrent lyrics lost |
| 9 | BottomPlayback no play/pause | Oversight | 1h | Broken transport |
| 10 | backdrop-filter unsupported | Dead CSS declaration | 1h | Misleading no-op, possible warnings |
| 11 | CSS duplicate selectors | Organic growth | 3h | Unpredictable styling |
| 12 | PiP static toggle | Prototype code | 30min | Desync |
| 13 | Queue mutations lack forgiveness | No undo/confirmation pattern | 2h | Silent loss of user work |

**Total focused effort**: ~47 hours for all critical/high and listed design fixes (~37h technical + ~10h design/verification).