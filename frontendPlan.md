# UI_ARCHITECTURE_AND_IMPLEMENTATION.md

> **Project:** Desktop music player  
> **Primary language:** C++20  
> **UI:** GTK 4 + libadwaita  
> **GTK integration:** GTK/libadwaita C APIs called directly from C++  
> **Database:** SQLite  
> **Audio engine:** GStreamer — proposed, subject to audit  
> **Metadata:** TagLib — proposed, subject to audit  
> **Desktop integration:** MPRIS over D-Bus  
> **Target environment:** Arch Linux, Wayland, Hyprland  
> **Purpose:** Definitive UI architecture, component contracts, visual specification, implementation sequence, and acceptance criteria.

---

# 0. Instructions to the implementation agent

This document is the implementation specification for the application's UI and its connection to application services.

Implement the architecture and interactions described here. Treat numerical values marked proposed as initial defaults, not immutable requirements.

## 0.1 Requirement labels

Use these labels consistently:

- **[CONFIRMED]** — explicitly selected or required by the user.
- **[PROPOSED]** — recommended implementation choice; validate during development.
- **[INSPECT]** — inspect the existing repository before deciding.
- **[FALLBACK]** — implement a minimal alternative if the existing component is unavailable.
- **[PROTOTYPE]** — build and test a small implementation before finalizing the design.
- **[OPTIONAL]** — later enhancement, not required for the first functional version.

Do not mark proposed technology choices as confirmed.

## 0.2 Confirmed and proposed technology decisions

| Technology | Status | Decision |
|---|---|---|
| C++20 | [CONFIRMED] | Primary application language |
| GTK 4 | [CONFIRMED] | UI toolkit |
| libadwaita | [CONFIRMED] | GTK application framework |
| GTK C API | [CONFIRMED] | Used directly from C++ |
| libadwaita C API | [CONFIRMED] | Used directly from C++ |
| gtkmm | Not selected | Do not use |
| Peel | Not selected | Do not use |
| SQLite | [PROPOSED] | Local library and settings storage |
| GStreamer | [PROPOSED] | Playback and audio analysis |
| TagLib | [PROPOSED] | Audio metadata |
| MPRIS | [PROPOSED] | Desktop media integration |
| CMake | [INSPECT] | Preserve existing build system if suitable |

## 0.3 Mandatory C++ and GTK binding decision

**Use the GTK 4 and libadwaita C APIs directly from C++20.**

Do not use gtkmm.

Do not use the archived libadwaitamm project.

Do not introduce Peel as a dependency for this implementation.

### Why this decision is necessary

GTK has an official C++ binding, gtkmm, but libadwaita does not have an equivalent maintained official C++ binding selected for this project.

Using gtkmm for GTK and raw C APIs for libadwaita would create a mixed wrapper environment. That is technically possible, but it adds unnecessary object ownership and widget interoperability complexity.

This project will instead use one consistent model:

```text
                 C++20 APPLICATION
                         │
          ┌──────────────┴──────────────┐
          │                             │
     UI IMPLEMENTATION             APP SERVICES
          │                             │
    GTK 4 C API                   C++ interfaces
    libadwaita C API              C++ implementations
          │                             │
     GObject / GTK                  SQLite
          │                         GStreamer
          │                         TagLib
          │                         D-Bus
          └──────────────┬──────────────┘
                         │
                    GLib main loop
```

### C++ ownership requirements

Use C++ RAII wrappers for GObject references and other resources.

Examples of resources that require explicit ownership handling:

- `GObject*`
- `GFile*`
- `GdkTexture*`
- `GtkCssProvider*`
- `GstElement*`
- `GstBus*`
- `GError*`
- `GVariant*`

Use appropriate custom deleters or small RAII wrapper types.

Do not blindly call `g_object_unref()` on borrowed references.

For each API call, inspect whether the returned object is:

- Transfer full.
- Transfer none.
- Floating.
- Owned by a parent widget.

Keep widget parenting and GObject reference ownership explicit.

### Custom GTK widgets

For custom widgets that need `GtkSnapshot` rendering:

- Implement the widget using GTK's GObject-based C API.
- Implement the custom type in C++ using the supported GObject type-registration pattern.
- Keep the custom widget's rendering code isolated from the application services.
- Use `GtkSnapshot` and GSK APIs through the GTK C API.

Do not write a fictitious `Gtk::Widget` subclass or `Gtk::Snapshot` wrapper. This project does not use gtkmm.

The exact custom-widget implementation must be verified with a minimal build prototype before the full waveform renderer is developed.

## 0.4 Repository audit

Before modifying existing code, inspect:

1. Directory structure.
2. Build system.
3. C++ standard.
4. Installed GTK version.
5. Installed libadwaita version.
6. Existing application entry point.
7. Existing widget implementation.
8. Existing player backend.
9. Existing library service.
10. Existing SQLite schema.
11. Existing metadata handling.
12. Existing palette picker.
13. Existing icons.
14. Existing artwork cache.
15. Existing waveform implementation.
16. Existing Quickshell visualizer reference.

Create:

```text
docs/IMPLEMENTATION_AUDIT.md
```

Use this table:

| Component | Exists? | Language | Compatible? | Action |
|---|---|---|---|---|
| Application shell | To inspect | To inspect | To inspect | Reuse or build |
| Playback service | To inspect | To inspect | To inspect | Reuse or interface |
| Library service | To inspect | To inspect | To inspect | Reuse or interface |
| Database | To inspect | To inspect | To inspect | Reuse or migrate |
| Palette picker | To inspect | To inspect | To inspect | Reuse or port |
| Artwork cache | To inspect | To inspect | To inspect | Reuse or implement |
| Waveform renderer | To inspect | To inspect | To inspect | Adapt or implement |
| Icon assets | To inspect | To inspect | To inspect | Reuse or replace |

Do not assume a component exists merely because it existed in an older Lollypop extension.

### Existing Python utilities

Inspect existing files such as:

- `api_fetcher.py`
- `theme_generator.py`
- `color_extractor.py`

Do not automatically import these into the new C++ application.

If a Python utility generates GTK 3 stylesheets, do not use those stylesheets as the GTK 4 theme system.

If its underlying color-extraction logic is useful, port the logic or expose a documented service boundary.

The new application must use an application-scoped GTK CSS provider.

## 0.5 Reference assets

Create:

```text
docs/reference/
├── README.md
├── WaveVisualizer.qml
├── visual-reference.png
└── reference-notes.md
```

Known Quickshell visualizer reference:

```text
/home/mahesh/.config/quickshell/end4-pC/modules/common/widgets/WaveVisualizer.qml
```

Inspect and copy it if it exists.

The screenshot reference has not been supplied. Do not invent its appearance or claim to have inspected it.

---

# 1. Product vision

The application should feel like a polished desktop music player with a strong music-library experience and a distinctive Now Playing interface.

The design direction combines:

- Apple Music-inspired library organization.
- A custom dark desktop appearance.
- Rounded artwork and restrained surfaces.
- Smooth but subtle transitions.
- A prominent waveform visualization.
- Persistent playback controls.
- A dedicated Now Playing panel.
- A future compact floating PiP player.

Do not reproduce Apple's interface pixel-for-pixel or use assets that are licensed exclusively for Apple platforms.

## 1.1 Core user experience

The user must be able to:

1. Browse Home, Albums, Artists, Playlists, Genres, and Songs.
2. Access Favourite Albums and Liked Music.
3. Search the local library.
4. Open album and artist detail pages.
5. Start an album or playlist.
6. Control playback from any page.
7. View the current track in the right panel and bottom bar.
8. Use the waveform visualization.
9. Switch between Lyrics and Queue.
10. Control shuffle, repeat-all, and repeat-one.
11. Change the application accent color.
12. Use the application in floating and tiled Hyprland layouts.

## 1.2 Four-region layout

The application has four persistent regions:

1. Left navigation sidebar.
2. Central library area.
3. Right Now Playing panel.
4. Bottom playback bar.

```text
┌───────────────────────────────────────────────────────────────────────────┐
│                                                                           │
│  ┌──────────────┬────────────────────────────────────┬────────────────┐  │
│  │              │                                    │                │  │
│  │              │                                    │                │  │
│  │   SIDEBAR    │          MAIN LIBRARY              │  NOW PLAYING   │  │
│  │              │                                    │                │  │
│  │              │                                    │                │  │
│  │              │                                    │                │  │
│  │              │                                    │                │  │
│  └──────────────┴────────────────────────────────────┴────────────────┘  │
│                                                                           │
├───────────────────────────────────────────────────────────────────────────┤
│                         BOTTOM PLAYBACK BAR                               │
└───────────────────────────────────────────────────────────────────────────┘
```

The bottom bar spans the full application width.

There is no mandatory full-width top header bar in the initial design. Page titles belong inside the central library area.

This avoids adding unnecessary vertical chrome to the four-region design.

Use libadwaita components where they provide useful behavior, not merely to impose its default layout.

---

# 2. Application architecture

## 2.1 Technology responsibilities

| Layer | Technology | Responsibility |
|---|---|---|
| Application logic | C++20 | Models, services, state |
| UI | GTK 4 C API | Widgets and rendering |
| Application framework | libadwaita C API | Adaptive behavior and GNOME integration |
| Styling | GTK 4 CSS | Application appearance |
| Database | SQLite | Local library and preferences |
| Playback | GStreamer | Proposed playback engine |
| Metadata | TagLib | Proposed metadata reader |
| Desktop integration | D-Bus/MPRIS | Desktop media controls |
| Rendering | GtkSnapshot/GSK | Custom waveform |
| Artwork | GdkTexture | Image presentation |

## 2.2 libadwaita usage

Use libadwaita for:

- `AdwApplication` and application integration.
- `AdwApplicationWindow` where appropriate.
- `AdwBreakpoint` for responsive behavior.
- `AdwAnimation` for suitable transitions.
- Standard dialogs and preferences patterns.
- Adaptive widgets where they fit the design.

Do not use an `AdwHeaderBar` as an additional permanent 47 px region by default.

If a titlebar is later required for client-side decorations, prototype it separately and include its actual height in the vertical layout budget.

The initial application layout must not assume that a full-width header bar exists.

## 2.3 Version requirements

**[PROPOSED] Minimum GTK version: 4.16.**

This provides the CSS custom-property baseline used in this document.

Verify the installed GTK and libadwaita versions during Phase 0.

If a lower version is required, implement and test a deliberate compatibility stylesheet rather than silently using unsupported properties.

## 2.4 Service separation

The UI must never:

- Execute SQL directly.
- Manage GStreamer pipelines directly.
- Decode artwork on the GTK main thread.
- Own an independent playback state.
- Perform long-running library scans.

The UI communicates with service interfaces and receives state updates through a defined main-thread event mechanism.

---

# 3. Window sizing and responsive layout

## 3.1 Initial dimensions

**[PROPOSED]**

| Property | Value |
|---|---:|
| Default width | 1440 px |
| Default height | 900 px |
| Minimum width | 600 px |
| Minimum height | 600 px |
| Expanded sidebar | 220 px |
| Compact sidebar | 72 px |
| Right panel minimum | 300 px |
| Right panel maximum | 430 px |
| Bottom bar target | 104 px |
| Bottom bar compact height | 88 px |

The minimum width must not make the smaller responsive layouts unreachable.

Do not enforce a 900 or 1050 px minimum.

## 3.2 Horizontal allocation

Let:

- `W` = available window content width.
- `S` = sidebar width.
- `R` = right panel width.
- `M` = main library width.

```text
M = W - S - R
```

The right panel target:

```text
R_target = 0.25 × (W - S)
```

Clamp it:

```text
R = clamp(R_target, 300, 430)
```

The right panel width is a proportion of the available content width after the sidebar.

Do not assume GTK `hexpand` or minimum-size settings enforce this proportion.

### Implementation

Prototype a layout using standard GTK containers first.

If those cannot satisfy the required allocation, implement a custom allocation strategy or layout manager.

The chosen implementation must:

- Respect minimum widths.
- Avoid recursive allocation.
- Avoid layout jitter.
- Preserve a usable main library area.
- Respond correctly when the window is resized.
- Work in Hyprland tiled mode.

## 3.3 Responsive behavior

The minimum width is 600 px, so all responsive modes below that threshold must be reachable.

**[PROPOSED]**

| Available width | Layout |
|---|---|
| Above 1200 px | Expanded sidebar, main library, right panel |
| 900–1200 px | Compact sidebar, main library, right panel |
| 720–900 px | Compact sidebar, narrower main library and right panel |
| 600–720 px | Main library prioritized; right panel may become a separate page or overlay |

At narrow widths:

- Do not shrink every component indefinitely.
- Preserve the current track identity.
- Preserve essential playback controls.
- Allow the right panel to become a separate view.
- Keep navigation accessible.

Use libadwaita breakpoints where suitable.

## 3.4 Vertical layout

The initial design does not reserve a permanent full-width header bar.

Page titles are placed inside the library area.

The bottom bar uses:

- 104 px target height.
- 88 px compact height.

At a 600 px window height, the available content height is therefore approximately:

```text
600 px - 88 px = 512 px
```

If a titlebar or additional chrome is introduced later, subtract its actual measured height.

Short-height mode must be designed as a normal responsive state, not an exceptional fallback.

The right panel must be able to collapse its artwork presentation to preserve Lyrics/Queue content.

---

# 4. Navigation sidebar

## 4.1 Expanded sidebar

**[PROPOSED] Width: 220 px.**

```text
┌──────────────────────────┐
│  APP ICON / NAME         │
│                          │
│  ◉  Home                 │
│                          │
│  LIBRARY                 │
│  ▣  Albums               │
│  ♬  Artists              │
│  ▤  Playlists            │
│  ◈  Genres               │
│  ☷  Songs                │
│                          │
│  COLLECTIONS             │
│  ♡  Favourite Albums     │
│  ♥  Liked Music          │
│                          │
│                          │
│  ‹  Collapse             │
│  ☰  More                 │
└──────────────────────────┘
```

## 4.2 Compact sidebar

**[PROPOSED] Width: 72 px.**

```text
┌──────────┐
│    ◉     │
│          │
│    ▣     │
│    ♬     │
│    ▤     │
│    ◈     │
│    ☷     │
│          │
│    ♡     │
│    ♥     │
│          │
│    ›     │
│    ☰     │
└──────────┘
```

Use tooltips for icon-only navigation.

## 4.3 Navigation destinations

| Item | Destination |
|---|---|
| Home | Home dashboard |
| Albums | Album grid |
| Artists | Artist collection |
| Playlists | Playlist collection |
| Genres | Genre collection |
| Songs | Sortable song table |
| Favourite Albums | Favourite album collection |
| Liked Music | Liked track collection |

Home must be a real navigation item and remain accessible after the user visits other pages.

## 4.4 Distinct collection icons

Favourite Albums and Liked Music must not share the same icon.

Use distinct symbols, such as:

- Favourite Albums: album/grid with a heart.
- Liked Music: heart with a music note.

Use an open icon set.

## 4.5 Sidebar collapse control

The collapse control and the menu control are two separate buttons.

### Collapse control

- Expanded mode: show a left-pointing chevron.
- Compact mode: show a right-pointing chevron.
- Clicking it changes sidebar width and label visibility.
- It does not open Settings or Fullscreen.

### Menu control

- Opens a popover.
- Contains Settings and Fullscreen.
- Works in both expanded and compact modes.
- Does not change sidebar width.

Do not assign two unrelated actions to the same button.

## 4.6 Sidebar animation

Use `GtkRevealer` for labels and other content that needs reveal/hide behavior.

Do not rely on CSS width transitions.

When the sidebar collapses:

1. Keep the icon column visible.
2. Hide labels.
3. Animate the label region where supported.
4. Change the allocated sidebar width.
5. Preserve the selected navigation state.

Respect the system's reduced-motion preference, including `gtk-enable-animations`.

## 4.7 Persistence

Persist:

- Sidebar state.
- Last selected page.
- Relevant sidebar preferences.

Use the application's settings layer, GSettings, or the existing persistence solution.

---

# 5. Central library area

## 5.1 Page architecture

Use a page stack such as `AdwViewStack` or an equivalent GTK structure.

Switch pages without recreating the entire application window.

The right panel and bottom bar remain independent of the current library page.

## 5.2 Home

Home is a real navigation destination.

Suggested content:

- Recently Played.
- Recently Added.
- Recently Played Tracks.
- Favourite Albums.
- Suggested or frequently played content, if actual history data exists.

Use actual library and history data.

If unavailable, show a proper empty state or clearly identified development mock data.

## 5.3 Albums

Use a responsive grid of square artwork cards.

Each card contains:

- Artwork.
- Album title.
- Artist.
- Optional year or track count.
- Play action.

### Interaction

- Single click opens the album detail page.
- Hover reveals the Play button.
- Keyboard focus also reveals the Play button.
- Activating the Play button starts album playback.

Do not make the Play action dependent on hover alone.

The hover Play control must be keyboard-accessible.

## 5.4 Artists

Display artists in a responsive grid or list.

Selecting an artist opens the artist detail page.

## 5.5 Playlists

Display:

- Playlist artwork or collage.
- Name.
- Track count.
- Optional description.

Only show actions that the library service supports.

## 5.6 Genres

Display genres derived from actual library metadata.

Selecting a genre opens a filtered view.

Do not invent genre values for tracks with no genre metadata.

## 5.7 Songs

Use `GtkColumnView`.

Do not use `GtkListView` for the sortable multi-column Songs page.

Recommended columns:

| Column | Content |
|---|---|
| Track | Title |
| Artist | Artist |
| Album | Album |
| Duration | Duration |
| Genre | Genre |
| Date Added | Date added |

Support sorting, keyboard navigation, row activation, and selection.

### Sorting with lazy loading

Do not assume `GtkColumnView` sorting automatically sorts the entire database.

Its sorter operates on the model items currently supplied to it. A partially loaded model cannot provide correct global database ordering by sorting only loaded rows.

Use database-side sorting.

Implementation:

1. Receive the requested sort column and direction.
2. Ask `ILibraryService` for a globally sorted query.
3. Retrieve the ordered track IDs.
4. Keep the ordered ID list in memory.
5. Load row details lazily as the user scrolls.
6. Update the visible model.
7. Preserve the current sort order.

The ID list is inexpensive compared with loading every track's complete metadata and artwork.

Do not create thousands of permanent row widgets.

## 5.8 Favourite Albums

Use the same AlbumCard component as the Albums page.

Only display albums marked as favourites.

## 5.9 Liked Music

Display tracks marked as liked.

Use the shared TrackRow component.

## 5.10 Album details

Include:

- Album artwork.
- Album title.
- Artist.
- Year and track count.
- Play album.
- Shuffle.
- Track list.

Use the shared TrackRow component.

## 5.11 Artist details

Include:

- Artist artwork.
- Artist name.
- Albums.
- Tracks, where available.

## 5.12 Search

Search the local library.

Use SQLite FTS5 if appropriate to the inspected database schema.

Do not add an unnecessary long debounce delay.

Run each query on a worker thread.

Use a generation counter:

1. Increment the counter when the search query changes.
2. Start a worker query.
3. Return results to the main context.
4. Compare the result generation with the latest generation.
5. Discard stale results.
6. Display only the latest result.

Never run expensive database queries on the GTK main thread.

---

# 6. Right Now Playing panel

## 6.1 Purpose

The right panel displays:

- Current album artwork.
- Track title.
- Artist name.
- Shared waveform visualization.
- Playback-mode controls.
- Lyrics or Queue.

It is a persistent listening area, not another library page.

## 6.2 Width and padding

**[PROPOSED]**

| Property | Value |
|---|---:|
| Target width | 25% of remaining content width |
| Minimum width | 300 px |
| Maximum width | 430 px |
| Horizontal padding | 18 px |
| Artwork corner radius | 12–16 px |
| Panel corner radius | 18 px |

Use consistent padding. Do not mix a 10 px artwork margin with an 18 px panel padding without a specific design reason.

## 6.3 Vertical sizing

The artwork must not consume all available panel height.

Reserve space for:

- Track title and artist.
- Playback controls.
- Lyrics/Queue tabs.
- Lyrics/Queue content.

Minimum target:

```text
Lyrics/Queue content: 160 px
Tab header:            36 px
```

Use the following proposed artwork rule:

```text
artwork_size = min(
    panel_width - (2 × horizontal_padding),
    0.38 × available_panel_height
)
```

Then constrain it by the actual remaining vertical allocation.

### Adaptive modes

**Normal mode**

- Prominent artwork.
- Track information.
- Waveform.
- Three playback-mode controls.
- Lyrics/Queue.

**Compact mode**

- Smaller artwork.
- Reduced gaps.
- Visible track identity.
- Shorter waveform.
- Usable Lyrics/Queue area.

**Short-height mode**

- Artwork becomes a compact thumbnail or collapses.
- Track title and artist remain visible.
- Playback-mode controls remain accessible.
- Lyrics/Queue receives priority.

**Lyrics-focused mode**

- Artwork can shrink further.
- Lyrics content receives the available space.

**Queue-focused mode**

- Queue content receives priority over artwork size.

Do not merely describe this as a visual preference. Implement it in the layout allocation logic.

## 6.4 Panel structure

```text
┌─────────────────────────────────────┐
│                                     │
│           ALBUM ARTWORK             │
│                                     │
├─────────────────────────────────────┤
│ Track title                         │
│ Artist name                         │
│                                     │
│ ◀          WAVEFORM             ▶   │
│                                     │
│   Shuffle   Repeat All   Repeat One │
├─────────────────────────────────────┤
│   Lyrics                 Queue      │
├─────────────────────────────────────┤
│                                     │
│   Active tab content                │
│                                     │
│                                     │
└─────────────────────────────────────┘
```

## 6.5 Playback modes

There must be three distinct controls:

1. Shuffle.
2. Repeat All.
3. Repeat One.

Repeat All and Repeat One are mutually exclusive.

Shuffle is independent.

### State mapping

| Shuffle | Repeat All | Repeat One | Meaning |
|---|---|---|---|
| Off | Off | Off | Normal playback |
| On | Off | Off | Shuffle |
| Off | On | Off | Repeat entire queue/playlist |
| Off | Off | On | Repeat current track |
| On | On | Off | Shuffle with repeat-all |
| On | Off | On | Shuffle with repeat-one |

The two repeat controls must never be active simultaneously.

Clicking Repeat All while Repeat One is active disables Repeat One.

Clicking Repeat One while Repeat All is active disables Repeat All.

Clicking the active repeat mode again disables repeat.

The three controls must share state with the bottom playback bar and MPRIS service.

## 6.6 ArtworkView

Create a reusable ArtworkView component.

Responsibilities:

- Display album or track artwork.
- Preserve aspect ratio.
- Support appropriate cropping.
- Show fallback artwork.
- Use rounded corners.
- Reuse cached textures.
- Load asynchronously.

Do not repeatedly decode original artwork during playback.

## 6.7 Lyrics

Lyrics view:

- Displays synchronized lyrics where available.
- Highlights the current line.
- Scrolls smoothly.
- Does not jump aggressively.
- Shows a proper empty state when unavailable.

Lyrics data must come through a lyrics service interface.

## 6.8 Queue

Queue view:

- Shows upcoming tracks.
- Identifies the current track.
- Allows selecting a queued track.
- Supports reordering when the player service supports it.
- Supports removal when the player service supports it.

The queue must use the same authoritative player state and queue operations as the bottom bar.

---

# 7. Bottom playback bar

## 7.1 Dimensions

**[PROPOSED]**

| Property | Value |
|---|---:|
| Target height | 104 px |
| Compact height | 88 px |
| Maximum height | 112 px |
| Artwork | 56 px |
| Artwork radius | 8–10 px |
| Horizontal padding | 18–22 px |

Do not use a 200–250 px bottom bar.

## 7.2 Layout

Three logical groups:

1. Track information.
2. Playback controls and waveform.
3. Secondary controls.

```text
┌───────────────────────────────────────────────────────────────────────────┐
│                                                                           │
│  ┌──────┐  Track title      ▶    ◀   ┌────────────────┐   ▶   🔀  ↻  🔊  ▢ │
│  │ ART  │  Artist                Previous WAVEFORM Next                   │
│  └──────┘                                                               │
│                                                                           │
└───────────────────────────────────────────────────────────────────────────┘
```

## 7.3 Control order

The control sequence is:

1. Play/Pause.
2. Previous.
3. Waveform/progress.
4. Next.

Previous and Next flank the waveform.

The waveform component is shared with the right panel.

Do not place the waveform in a different, unrelated position beneath the track information.

## 7.4 Secondary controls

Include:

- Shuffle.
- Repeat All.
- Repeat One.
- Volume.
- PiP launch.
- Optional queue shortcut.

The repeat controls follow the mutual-exclusion rules in Section 6.5.

At narrow widths, move lower-priority controls into a menu instead of compressing every control.

## 7.5 Shared state

The bottom bar must use the same PlaybackState as the right panel.

Do not create a second playback state.

---

# 8. Shared models and service interfaces

## 8.1 PlaybackState

Create one authoritative playback-state model.

Conceptual structure:

```cpp
struct PlaybackState {
    TrackId currentTrackId;

    bool isPlaying;

    double positionSeconds;
    double durationSeconds;
    double volume;

    bool shuffleEnabled;

    enum class RepeatMode {
        Off,
        All,
        One
    };

    RepeatMode repeatMode;
};
```

Adapt the exact types to the inspected project.

Do not represent Repeat All and Repeat One as independent booleans.

## 8.2 IPlayerService

The player interface must support individual tracks and collections.

A `play_track(id)` operation alone is not sufficient for album and playlist playback.

Conceptual contract:

```cpp
class IPlayerService {
public:
    virtual ~IPlayerService() = default;

    virtual PlaybackState get_state() const = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void toggle_play_pause() = 0;

    virtual void next() = 0;
    virtual void previous() = 0;

    virtual void seek(double position_seconds) = 0;
    virtual void set_volume(double volume) = 0;

    virtual void set_shuffle(bool enabled) = 0;
    virtual void set_repeat_mode(RepeatMode mode) = 0;

    virtual void play_track(TrackId id) = 0;

    virtual void play_collection(
        CollectionId id,
        std::size_t start_index
    ) = 0;

    virtual QueueSnapshot get_queue() const = 0;

    virtual void play_queue_item(QueueItemId id) = 0;
    virtual void remove_queue_item(QueueItemId id) = 0;

    virtual void move_queue_item(
        QueueItemId id,
        std::size_t new_index
    ) = 0;

    virtual void clear_queue() = 0;
};
```

This is a conceptual interface. Define the actual collection and queue types after inspecting the existing model.

The `play_collection()` operation must support:

- Albums.
- Playlists.
- Other ordered track collections.

The `start_index` specifies the track at which playback begins.

## 8.3 ILibraryService

The library interface must provide:

- Albums.
- Artists.
- Playlists.
- Genres.
- Tracks.
- Album details.
- Artist details.
- Favourite albums.
- Liked tracks.
- Search.
- Sorted track ID queries.
- Lazy track detail retrieval.
- Artwork references.

Do not let UI components execute SQL directly.

## 8.4 ILyricsService

Create a separate lyrics interface.

Conceptual contract:

```cpp
class ILyricsService {
public:
    virtual ~ILyricsService() = default;

    virtual LyricsResult get_lyrics(TrackId id) = 0;

    virtual void request_lyrics(
        TrackId id,
        LyricsCallback callback
    ) = 0;
};
```

The actual implementation may support:

- Embedded lyrics.
- Local lyric files.
- Cached lyrics.
- An external lyrics provider, if one is configured.

Do not invent a provider or assume online lyrics are available.

The UI must display an appropriate unavailable state when no lyrics source exists.

## 8.5 State updates and GTK main context

All service-originated UI updates must be marshalled onto the GTK main context.

This applies to:

- Playback state.
- Track changes.
- Queue changes.
- Lyrics results.
- Library results.
- Artwork completion.
- Search results.
- Visualizer state changes.

Worker threads and GStreamer streaming threads must not directly modify GTK widgets.

Use the appropriate GLib main-context dispatch mechanism.

Ensure that callbacks cannot access destroyed widgets.

Disconnect signals or invalidate weak references when a widget is destroyed.

## 8.6 Mock implementations

If the real service does not exist:

1. Create the interface.
2. Implement a minimal mock.
3. Mark mock data clearly.
4. Build the UI against the interface.
5. Replace the mock without rewriting UI components.

Do not invent production behavior.

---

# 9. Waveform and visualizer

## 9.1 Prototype requirement

The final waveform data and rendering semantics must be settled during the Phase 8 prototype.

The following is the initial visual proposal, not an already validated implementation.

### Proposed default

Both the right panel and bottom playback bar display the same smoothed live visualizer shape.

- The left side of the current playhead uses the saturated accent color.
- The right side uses a dimmed version of the same color.
- The current playhead is clearly visible.
- Both widgets read the same live sample data.
- The widgets differ only in dimensions and presentation.

This creates a consistent visual identity between the two regions.

Validate whether the visual treatment communicates playback progress clearly enough when applied to live spectrum data.

Do not confuse the displayed progress treatment with a true precomputed whole-track waveform.

## 9.2 Live spectrum versus whole-track waveform

These are different data types.

### Live spectrum

Represents the current frequency energy of the audio.

It changes continuously while playback runs.

### Whole-track waveform

Represents audio amplitude across the entire track duration.

It can be precomputed and used to display a genuine played/unplayed waveform.

The first implementation prototype should test the proposed shared live-spectrum appearance.

If the result is visually misleading, implement a separate whole-track peak-data source.

Do not claim that GStreamer's spectrum element automatically generates a whole-track waveform.

## 9.3 GStreamer spectrum

**[PROPOSED]** Use GStreamer's `spectrum` element for live analysis of this application's own playback pipeline.

The correct official documentation is:

https://gstreamer.freedesktop.org/documentation/spectrum/index.html

The element is part of GStreamer's audio analysis/effects plugins.

Do not use CAVA as the default source. CAVA may capture system-wide audio rather than only this application's playback.

## 9.4 GStreamer message-thread handling

GStreamer bus messages delivered through a normal GLib bus watch are handled through the main loop.

Do not parse large visualizer messages or perform waveform processing in a GTK callback.

For the visualizer prototype:

1. Configure the spectrum element to post messages.
2. Install a bus sync handler for the relevant spectrum messages.
3. Parse and copy the required numeric samples into a thread-safe ring buffer.
4. Keep the sync handler minimal.
5. Do not call GTK APIs from the sync handler.
6. Do not perform expensive smoothing or rendering in the sync handler.
7. Let the GTK frame-clock callback read the latest buffer snapshot.
8. Schedule redraws only when appropriate.

The sync handler runs in the posting thread. Treat it as a streaming-thread callback.

Do not block the audio pipeline with expensive operations.

## 9.5 Shared VisualizerModel

Both waveform widgets consume one shared model.

It should contain:

- Live sample buffer.
- Sample count.
- Timestamp or sequence number.
- Current track identity.
- Playback status.
- Playback position.
- Optional precomputed peak data.
- Optional smoothed output data.

Use a ring buffer.

Do not create separate audio-analysis pipelines or sample buffers for the two widgets.

## 9.6 GtkSnapshot rendering

Use GTK's snapshot and GSK rendering architecture.

Do not define the renderer solely around:

```cpp
render(cairo_t*);
```

The renderer needs to support GTK snapshot composition and effects.

Use the GTK C API for:

- Snapshot creation.
- Render-node construction.
- Clipping.
- Color effects.
- Opacity.
- Blur where supported.
- Rounded clipping.

Verify exact API availability against the target GTK version.

The custom widget implementation must follow the C API/GObject architecture selected in Section 0.3.

Do not write gtkmm-only widget signatures.

## 9.7 Shared WaveformView

Create one reusable component:

```text
WaveformView
```

It accepts:

- Shared VisualizerModel.
- Display dimensions.
- Accent color.
- Progress position.
- Rendering mode.
- Animation state.

The right panel and bottom bar may use different heights and densities, but both consume the same source data.

## 9.8 Rendering frequency

**[PROPOSED]** Target approximately 30 FPS while actively playing.

Redraw when:

- New samples arrive.
- Playback position changes.
- Track changes.
- Accent color changes.
- Widget allocation changes.
- Playback starts or pauses.

Avoid continuous redraws when the data has not changed.

---

# 10. Playback progress and seeking

## 10.1 Frame-clock interpolation

Use the GTK frame clock to interpolate playback progress.

Do not poll the backend independently from each widget.

Maintain:

- Last confirmed backend position.
- Timestamp of confirmation.
- Current playback status.
- Track duration.

While playing:

```text
estimated_position =
    last_confirmed_position
    + elapsed_time_since_confirmation
```

Freeze interpolation when paused.

Reconcile with backend updates.

## 10.2 Optimistic seeking

During a drag:

1. Update the playhead immediately.
2. Update the displayed time locally.
3. Do not send a backend seek for every pointer movement.
4. Send one seek when the user releases.
5. Reconcile with the backend response.

If the seek fails, restore the last confirmed position.

## 10.3 Shared progress

Both waveform widgets must use the same progress state.

Do not create separate timers or independent playback clocks.

---

# 11. Database, library, and artwork

## 11.1 SQLite

**[PROPOSED]** Use SQLite after inspecting the existing database.

Use:

- Prepared statements.
- Transactions.
- Appropriate indexes.
- FTS5 where suitable.
- WAL mode where appropriate.
- Worker-thread queries.

Do not change an existing schema without inspecting it first.

## 11.2 Database-side sorting

The Songs page must support correct global sorting with lazy loading.

Use SQL to determine the global order.

Maintain an in-memory ordered list of track IDs.

Fetch full row details only when needed.

Do not rely on a `GtkColumnView` sorter to sort records that have not yet been loaded.

## 11.3 Lazy models

Use a GTK-compatible `GListModel` or another appropriate model implementation.

The model should support:

- Incremental loading.
- Stable item identity.
- Lazy row-detail retrieval.
- Database-backed sorting.
- Correct item-count handling.

Do not create thousands of permanent widgets.

## 11.4 Artwork thumbnail sizes

The initial proposal is:

| Bucket | Use |
|---|---|
| 64 px | Small thumbnails |
| 160 px | Medium cards and rows |
| 512 px | Large artwork |

However, these values must be validated at actual display scale.

A 430 logical-pixel panel at 2× scale may require an image larger than 512 physical pixels.

### Phase 10 prototype requirement

Test artwork at:

- 1× scale.
- 1.25× scale.
- 1.5× scale.
- 2× scale.

Compare:

- Image sharpness.
- Decode cost.
- Memory use.
- Cache size.
- Texture reuse.

Select the bucket using:

```text
required_physical_size =
    logical_display_size × scale_factor
```

A 1024 px bucket may be necessary.

Do not finalize the cache bucket list before this prototype.

## 11.5 Artwork file cache and MPRIS

MPRIS metadata may expose artwork through an `artUrl`.

The artwork URL must point to a real accessible file.

A `GdkTexture` held only in memory is not sufficient.

The artwork cache must therefore preserve a file-backed representation for MPRIS.

Requirements:

- Use a stable cache location.
- Store or copy the selected artwork file.
- Generate a valid file URI.
- Ensure the file exists when metadata is published.
- Handle missing artwork.
- Avoid exposing a temporary file that is immediately deleted.
- Update the metadata when artwork changes.

Do not invent an `artUrl` pointing to a non-existent cache path.

---

# 12. Theme and styling

## 12.1 Design language

Use:

- Deep neutral backgrounds.
- Soft surface separation.
- Rounded artwork.
- Restrained borders.
- Subtle shadows.
- Consistent typography.
- One dynamic accent color.

Do not make every region a separate card.

## 12.2 Palette picker

**[INSPECT]** Inspect whether the existing palette picker can be reused.

If it does not exist or is incompatible:

**[FALLBACK]** Implement an application-scoped accent-color selector.

The accent color must come from the selected palette.

Purple may be used as a temporary development default, not as a permanent hardcoded value.

## 12.3 libadwaita accent integration

libadwaita has its own accent-color and color-scheme behavior.

The application's custom accent must not disagree with standard libadwaita widgets.

When the selected palette changes:

1. Update the application accent setting.
2. Update the application's CSS custom property.
3. Update the relevant libadwaita accent-color preference or supported application-level styling mechanism.
4. Update custom controls.
5. Update the waveform.
6. Persist the selected palette.

Do not assume that changing one custom CSS variable automatically changes every libadwaita widget.

Prototype the mapping and verify that standard controls and custom components use the same accent.

Do not modify global GTK theme files.

## 12.4 CSS custom properties

Target GTK 4.16 or newer.

Example:

```css
:root {
    --app-bg: #111116;
    --surface-primary: #1a1a22;
    --surface-secondary: #22222c;

    --text-primary: #f5f5f7;
    --text-secondary: #a3a3ad;

    --accent-color: #a78bfa;
    --accent-soft: color-mix(
        in srgb,
        var(--accent-color) 18%,
        transparent
    );
}
```

These are proposed development values.

Use CSS custom properties consistently.

Avoid deprecated `@define-color` declarations and deprecated `alpha()` usage.

GTK CSS is not identical to browser CSS. Verify selectors and functions against the target GTK version.

## 12.5 Application-scoped stylesheet

Load the custom stylesheet through an application-scoped `GtkCssProvider`.

Do not install it globally.

Use application-specific classes:

```text
.music-app
.music-sidebar
.music-library
.music-now-playing
.music-playback-bar
.music-album-card
.music-track-row
.music-waveform
```

## 12.6 Selected states

Do not assume `:selected` applies to every custom widget.

Use explicit state classes or checkable controls.

Example:

```css
.navigation-item.active {
    background: var(--accent-soft);
    color: var(--accent-color);
}
```

## 12.7 Fonts and icons

Use Inter if installed and appropriately licensed; otherwise use the system UI font.

Do not bundle Apple's San Francisco font.

Use an open icon set:

- Phosphor.
- Lucide.
- Tabler.

Do not use SF Symbols as general-purpose Linux assets.

Keep stroke weight, sizing, and alignment consistent.

---

# 13. Animation and reduced motion

Use libadwaita animation helpers where appropriate.

Suggested initial durations:

| Interaction | Duration |
|---|---:|
| Hover feedback | 120–160 ms |
| Button feedback | 100–140 ms |
| Sidebar transition | 180–240 ms |
| Artwork crossfade | 200–300 ms |
| Page transition | 160–220 ms |

Use animations to communicate changes, not to delay interaction.

Respect reduced-motion preferences.

When animations are disabled:

- Change sidebar state immediately.
- Avoid decorative pulsing.
- Avoid artwork transitions.
- Keep playback and progress functional.
- Preserve visible focus and state feedback.

---

# 14. Widget and component architecture

## 14.1 Suggested directory structure

```text
src/
├── app/
│   ├── Application.*
│   ├── MainWindow.*
│   └── AppSettings.*
│
├── ui/
│   ├── sidebar/
│   │   ├── Sidebar.*
│   │   └── NavigationItem.*
│   │
│   ├── library/
│   │   ├── LibraryView.*
│   │   ├── HomeView.*
│   │   ├── AlbumsView.*
│   │   ├── ArtistsView.*
│   │   ├── PlaylistsView.*
│   │   ├── GenresView.*
│   │   ├── SongsView.*
│   │   ├── AlbumDetailView.*
│   │   ├── ArtistDetailView.*
│   │   ├── AlbumCard.*
│   │   └── TrackRow.*
│   │
│   ├── now_playing/
│   │   ├── NowPlayingPanel.*
│   │   ├── ArtworkView.*
│   │   ├── LyricsView.*
│   │   └── QueueView.*
│   │
│   ├── playback/
│   │   ├── PlaybackBar.*
│   │   ├── PlaybackControls.*
│   │   ├── ProgressController.*
│   │   └── WaveformView.*
│   │
│   └── theme/
│       ├── ThemeManager.*
│       └── AccentColorManager.*
│
├── services/
│   ├── IPlayerService.*
│   ├── ILibraryService.*
│   ├── ILyricsService.*
│   ├── PlayerService.*
│   ├── LibraryService.*
│   └── VisualizerService.*
│
├── models/
│   ├── Track.*
│   ├── Album.*
│   ├── Artist.*
│   ├── Playlist.*
│   ├── PlaybackState.*
│   └── VisualizerModel.*
│
├── data/
│   ├── Database.*
│   ├── TrackRepository.*
│   ├── AlbumRepository.*
│   └── ArtworkCache.*
│
└── platform/
    ├── MprisService.*
    └── DesktopIntegration.*
```

Adapt this to the existing repository rather than moving working code unnecessarily.

## 14.2 Main window hierarchy

Conceptual structure:

```text
AdwApplicationWindow
└── Main application content
    ├── Horizontal layout
    │   ├── Sidebar
    │   ├── Library page stack
    │   └── Now Playing panel
    │
    └── Bottom playback bar
```

There is no mandatory full-width header bar in the initial design.

The bottom playback bar spans the entire application width.

## 14.3 Component responsibilities

### Sidebar

Owns navigation presentation and menu interactions.

Does not own library queries or playback state.

### LibraryView

Owns page presentation, page switching, and search presentation.

Does not own SQL or GStreamer.

### AlbumCard

Owns artwork, album text, focus/hover Play action, and album-selection events.

Does not own playback logic.

### TrackRow

Owns track presentation and user interaction.

Does not own track retrieval or queue state.

### NowPlayingPanel

Owns current artwork, metadata, lyrics/queue presentation, and right-side waveform presentation.

Does not own the playback engine.

### PlaybackBar

Owns bottom playback controls, progress presentation, and secondary controls.

Does not own the playback engine.

### WaveformView

Owns rendering and display behavior.

Does not own audio capture, audio decoding, or playback.

---

# 15. PiP integration boundary

PiP is a separate feature.

Create:

`PIP_PLAYER_DESIGN_AND_IMPLEMENTATION.md`

The main application must expose the shared playback and visualizer interfaces required by PiP.

## 15.1 Visual direction

A compact, rounded floating player:

```text
┌───────────────────────────────────────────────────────┐
│                                                       │
│  ┌─────────┐  Track title                              │
│  │         │  Artist name                              │
│  │ ARTWORK │                                           │
│  │         │      Previous   Play/Pause   Next         │
│  └─────────┘                                           │
│                                                       │
└───────────────────────────────────────────────────────┘
```

Reuse the same PlaybackState, artwork cache, and accent-color source.

## 15.2 Wayland and Hyprland

GTK 4 does not provide a portable replacement for GTK 3's old keep-above behavior.

Wayland stacking order is controlled by the compositor.

For Hyprland:

1. Give PiP a unique application window class.
2. Document the required Hyprland rules.
3. Configure it as floating.
4. Use the compositor's pin behavior if required.
5. Test against the installed Hyprland version.
6. Do not silently edit the user's configuration.

The application must still function as an ordinary separate window without these rules.

A layer-shell alternative may be investigated later, but it introduces a separate dependency and must be tested on the target compositor.

---

# 16. MPRIS and desktop integration

## 16.1 Application role

The application is the music player.

It should expose an MPRIS service over D-Bus.

It is not merely an MPRIS client.

## 16.2 Supported operations

Implement the relevant MPRIS methods and properties:

- Play.
- Pause.
- PlayPause.
- Next.
- Previous.
- Seek.
- SetPosition where supported.
- Volume.
- Shuffle.
- LoopStatus.
- PlaybackStatus.
- Metadata.
- Track identity.

## 16.3 Repeat mapping

Map internal modes as follows:

| Internal mode | MPRIS LoopStatus |
|---|---|
| Off | None |
| Repeat One | Track |
| Repeat All | Playlist |

Repeat One and Repeat All are mutually exclusive.

## 16.4 Mute

MPRIS does not define a standard mute property.

If the application provides mute:

- Store mute state internally.
- Save the previous volume.
- Set volume to zero when muted.
- Restore the previous volume when unmuted.
- Expose volume normally.

Do not advertise a non-standard mute property as if it were part of MPRIS.

## 16.5 Position

MPRIS Position is not a continuously signaled property.

The application must:

- Maintain its own playback position.
- Answer position queries.
- Emit `Seeked` when seeking.
- Update relevant properties when playback status or metadata changes.

Do not emit continuous Position property changes as if the specification required them.

## 16.6 Artwork URL

MPRIS artwork metadata must point to a real accessible file.

Use the file-backed artwork cache described in Section 11.5.

Do not publish a URI for an in-memory texture or a file that has already been deleted.

## 16.7 Shared player service

All desktop controls must use the same IPlayerService.

```text
Media keys ──────┐
Desktop widget ──┼── MPRIS ── IPlayerService
Bottom bar ──────┤
Right panel ─────┤
PiP ─────────────┘
```

Do not create separate playback state for MPRIS.

---

# 17. Persistence

Persist:

| Setting | Persistence |
|---|---|
| Sidebar state | Yes |
| Last page | Yes |
| Window dimensions | Yes |
| Accent color | Yes |
| Color scheme | Yes |
| Right panel visibility | Yes |
| Selected Lyrics/Queue tab | Optional |
| Volume | Yes |
| Shuffle | Optional |
| Repeat mode | Optional |

Use the existing settings layer, GSettings, or the established application persistence mechanism.

Do not store artwork binaries in the settings database.

---

# 18. Empty states and error handling

Implement deliberate states for:

- Empty library.
- No search results.
- Missing artwork.
- Missing lyrics.
- Playback failure.
- Missing audio device.
- Unavailable media file.
- Database error.
- Loading.

Do not leave blank panels when data is unavailable.

Do not crash because a track has invalid metadata or missing artwork.

Mock data must be visibly identified as development data.

---

# 19. Accessibility

Support:

- Keyboard navigation.
- Visible focus.
- Accessible labels for icon-only buttons.
- Tooltips for compact navigation.
- Reduced motion.
- Sufficient text contrast.
- Logical focus order.
- Screen-reader labels.

Suggested shortcuts:

| Action | Shortcut |
|---|---|
| Play/Pause | Space when not editing text |
| Next | Ctrl + Right |
| Previous | Ctrl + Left |
| Search | Ctrl + F |
| Toggle sidebar | Ctrl + B |
| Escape | Close popover/dialog or clear transient selection |

Avoid intercepting Space when a text input or another interactive widget has focus.

Hover-only actions must also appear on keyboard focus.

---

# 20. Implementation phases and dependency order

The phase order is mandatory.

Every component must exist before a phase that consumes it.

## Phase 0 — Repository audit

Tasks:

1. Inspect repository structure.
2. Inspect CMake/Meson configuration.
3. Verify C++ version.
4. Verify GTK and libadwaita versions.
5. Confirm direct C API integration.
6. Inspect existing services.
7. Inspect database.
8. Inspect palette picker.
9. Inspect icons.
10. Inspect artwork handling.
11. Inspect visualizer references.

Deliverable:

`docs/IMPLEMENTATION_AUDIT.md`

Acceptance:

- Binding decision recorded.
- Existing code identified.
- Missing services identified.
- No existing code overwritten.

## Phase 1 — Models and interfaces

Create:

- Track.
- Album.
- Artist.
- Playlist.
- PlaybackState.
- QueueSnapshot.
- VisualizerModel.
- IPlayerService.
- ILibraryService.
- ILyricsService.

Create mock implementations where real services do not exist.

Define main-context delivery for all service-originated UI events.

Acceptance:

- UI does not depend on concrete backend classes.
- Playback state has one owner.
- Queue operations exist in the interface.
- Collection playback can specify a start index.
- Lyrics have a service interface.

## Phase 2 — Application shell and layout

Create:

- AdwApplication.
- Main application window.
- Four-region layout.
- Sidebar container.
- Library page stack.
- Right panel container.
- Bottom playback bar container.
- Responsive layout.
- Initial width and height behavior.

**Create a WaveformView placeholder/stub in this phase.**

The stub must:

- Be a valid GTK widget.
- Accept the shared VisualizerModel.
- Reserve the correct layout space.
- Display a static placeholder.
- Expose the same public component interface intended for the real renderer.

This prevents Phase 7 from depending on a component that does not exist until Phase 8.

Acceptance:

- Four-region shell works.
- Bottom bar spans full width.
- Minimum window width is 600 px.
- Responsive modes are reachable.
- WaveformView placeholder can be embedded in both panels.

## Phase 3 — Theme and shared visual primitives

Create:

- Application-scoped CSS.
- ThemeManager.
- AccentColorManager.
- Palette-picker integration or fallback.
- ArtworkView.
- EmptyState.
- LoadingState.
- ErrorState.
- Shared button styles.
- WaveformView styling and placeholder behavior.

Acceptance:

- Accent color changes at runtime.
- Standard libadwaita controls and custom widgets use consistent accent styling.
- No global theme files are modified.
- The waveform placeholder has stable dimensions.

## Phase 4 — Navigation and page framework

Create:

- Sidebar.
- NavigationItem.
- Collapse button.
- Separate menu button.
- Settings/Fullscreen popover.
- Home page.
- Page stack.
- Page persistence.

Acceptance:

- Home is reachable.
- Collapse and menu actions are independent.
- Compact navigation is usable.
- Favourite Albums and Liked Music are distinct.

## Phase 5 — Library components

Create:

- AlbumCard.
- TrackRow.
- Artist item.
- Playlist item.
- Albums.
- Artists.
- Playlists.
- Genres.
- Songs using GtkColumnView.
- Favourite Albums.
- Liked Music.
- Album details.
- Artist details.
- Lazy data models.

Acceptance:

- Album single-click opens details.
- Play is available on hover and keyboard focus.
- Songs sorting is database-backed.
- Lazy loading does not break global sort order.

## Phase 6 — Playback service

Create or integrate:

- IPlayerService implementation.
- Playback engine.
- PlaybackState updates.
- Track playback.
- Collection playback.
- Queue operations.
- Seeking.
- Volume.
- Shuffle.
- Repeat All.
- Repeat One.

GStreamer is proposed. Validate the playback pipeline during this phase.

Acceptance:

- All controls use the same service.
- Collection playback starts at a specified index.
- Queue operations work through the interface.
- Repeat modes are mutually exclusive.

## Phase 7 — Bottom playback bar

Implement:

- Track summary.
- Play/Pause.
- Previous.
- WaveformView placeholder.
- Next.
- Progress controller.
- Volume.
- Shuffle.
- Repeat All.
- Repeat One.
- PiP launch placeholder.

Use the WaveformView stub from Phase 2.

Acceptance:

- Bar height is approximately 104 px.
- Compact height is approximately 88 px.
- Previous and Next flank the waveform.
- Progress is synchronized.
- Seeking is optimistic and sends one final seek request.

## Phase 8 — Waveform prototype and final renderer

This phase must begin with a focused prototype.

Prototype:

1. GTK custom widget implementation using the C API.
2. GObject lifecycle and C++ ownership.
3. GtkSnapshot rendering.
4. GStreamer spectrum element.
5. Bus sync handler.
6. Thread-safe sample ring buffer.
7. Main-thread/frame-clock consumption.
8. Smoothing behavior.
9. Progress/playhead appearance.
10. Accent-color integration.

Decide whether the proposed shared live waveform appearance is visually effective.

If not, define a separate whole-track waveform peak-data source.

Then replace the Phase 2 placeholder with the final renderer without changing the public WaveformView interface.

Acceptance:

- Both panels use one VisualizerModel.
- No random waveform generation.
- No GTK calls from streaming threads.
- Redraws target approximately 30 FPS while playing.
- No unnecessary redraws while paused.

## Phase 9 — Right Now Playing panel

Implement:

- Artwork.
- Track information.
- WaveformView.
- Shuffle.
- Repeat All.
- Repeat One.
- Lyrics tab.
- Queue tab.
- Adaptive artwork sizing.
- Short-height layout.

Acceptance:

- The panel preserves Lyrics/Queue space.
- Artwork shrinks or collapses when necessary.
- Queue uses actual player-service queue operations.
- Lyrics use ILyricsService.
- Playback modes remain synchronized with the bottom bar.

## Phase 10 — Search, database, and artwork performance

Implement:

- FTS5 where suitable.
- Worker-thread search.
- Generation-counter stale-result handling.
- SQL sorting.
- Lazy row data.
- SQLite indexes.
- WAL where appropriate.
- Thumbnail generation.
- Texture LRU cache.
- File-backed artwork cache for MPRIS.

Prototype the artwork bucket sizes at actual scale factors before finalizing them.

Acceptance:

- Search does not block the UI.
- Global sorting remains correct.
- Large libraries load incrementally.
- Artwork is sharp at 2× and fractional scale.
- MPRIS artUrl points to an existing file.

## Phase 11 — MPRIS

Implement:

- MPRIS service.
- Metadata.
- Playback status.
- Volume.
- Shuffle.
- LoopStatus.
- Seek.
- Seeked.
- Desktop control synchronization.
- Artwork URL.

Acceptance:

- MPRIS and UI share IPlayerService.
- Repeat mapping is correct.
- No unsupported mute property.
- Position handling follows MPRIS semantics.

## Phase 12 — PiP

Create the separate PiP specification and implementation.

Reuse:

- PlaybackState.
- IPlayerService.
- ArtworkCache.
- AccentColorManager.
- VisualizerModel.

Document Hyprland floating/pin behavior.

Acceptance:

- PiP does not create another playback engine.
- PiP displays the same current track.
- Wayland limitations are documented.
- App remains usable without compositor rules.

## Phase 13 — Final testing and polish

Test:

- All navigation.
- All library pages.
- Playback.
- Queue.
- Lyrics.
- Search.
- Sorting.
- Artwork.
- Waveform.
- MPRIS.
- Accent color.
- Reduced motion.
- Narrow windows.
- Short windows.
- Hyprland tiled mode.
- Floating mode.
- Error handling.
- Memory and CPU usage.

---

# 21. Performance requirements

## 21.1 GTK main thread

Do not run these operations on the GTK main thread:

- Full library scans.
- Large SQLite queries.
- Artwork decoding.
- Thumbnail generation.
- Audio analysis.
- Blocking network requests.
- Large metadata parsing.

Deliver results to the GTK main context safely.

## 21.2 Search

Run search queries on worker threads.

Use a generation counter to discard stale results.

Do not add a long debounce delay without evidence that it is necessary.

## 21.3 Library

Use lazy data models.

Use SQL for global ordering.

Keep the ordered track ID list in memory and fetch row details as required.

## 21.4 Artwork

Generate thumbnails in the background.

Use a size-bucketed GdkTexture LRU cache.

Choose bucket sizes according to physical pixel requirements.

Keep a file-backed artwork cache for MPRIS.

## 21.5 Waveform

Use one shared ring buffer.

Do not run audio analysis per widget.

Target approximately 30 FPS during playback.

Do not redraw when data has not changed.

## 21.6 Playback progress

Use frame-clock interpolation.

Do not create independent polling loops for the right panel and bottom bar.

## 21.7 Sidebar

Use GtkRevealer for reveal/hide behavior.

Do not rely on CSS width animation.

Respect reduced motion.

## 21.8 Grid and carousel prototype

Prototype the album grid early.

Test:

- Square artwork in allocated grid cells.
- Different window widths.
- Fractional scaling.
- Nested scroll behavior.
- Mouse-wheel behavior.
- Touchpad behavior.

Do not assume `GtkGridView` cells are automatically square.

Use a dedicated artwork container that preserves the required aspect ratio.

---

# 22. Acceptance checklist

## Architecture

- [ ] C++20 is used.
- [ ] GTK 4 C API is used directly from C++.
- [ ] libadwaita C API is used directly from C++.
- [ ] gtkmm is not used.
- [ ] Peel is not used.
- [ ] GObject ownership is explicit.
- [ ] Service interfaces exist.
- [ ] UI events are marshalled to the GTK main context.

## Layout

- [ ] Four-region layout exists.
- [ ] Bottom bar spans the full width.
- [ ] Minimum width is 600 px.
- [ ] Responsive breakpoints are reachable.
- [ ] No mandatory full-width header bar.
- [ ] Short-height mode is usable.

## Sidebar

- [ ] Collapse and menu buttons are separate.
- [ ] Home is reachable.
- [ ] Compact sidebar works.
- [ ] Settings and Fullscreen are accessible.
- [ ] Collection icons are distinct.
- [ ] Reduced motion works.

## Library

- [ ] Albums grid works.
- [ ] Album single-click opens details.
- [ ] Play appears on hover and focus.
- [ ] Artists works.
- [ ] Playlists works.
- [ ] Genres works.
- [ ] Songs uses GtkColumnView.
- [ ] Sorting is database-backed.
- [ ] Lazy loading preserves global ordering.
- [ ] Favourite Albums works.
- [ ] Liked Music works.

## Playback

- [ ] Play/Pause works.
- [ ] Previous works.
- [ ] Next works.
- [ ] Collection playback works.
- [ ] Queue operations work.
- [ ] Seek works.
- [ ] Volume works.
- [ ] Shuffle works.
- [ ] Repeat All works.
- [ ] Repeat One works.
- [ ] Repeat modes are mutually exclusive.

## Right panel

- [ ] Artwork loads.
- [ ] Track metadata updates.
- [ ] Lyrics tab works.
- [ ] Queue tab works.
- [ ] Artwork adapts to available height.
- [ ] Short-height mode preserves useful tab content.
- [ ] Three playback-mode controls are visible and functional.

## Waveform

- [ ] Phase 2 stub exists.
- [ ] Final implementation uses GtkSnapshot.
- [ ] Both widgets share VisualizerModel.
- [ ] No random waveform generation.
- [ ] Spectrum and whole-track waveform are distinguished.
- [ ] No GTK calls from streaming threads.
- [ ] Paused playback does not wastefully animate.

## Theme

- [ ] CSS is application-scoped.
- [ ] Accent color changes at runtime.
- [ ] Standard libadwaita controls match the selected accent.
- [ ] GTK 3 CSS is not reused.
- [ ] Global theme files are not modified.
- [ ] Icons are appropriately licensed.

## MPRIS

- [ ] MPRIS service exists.
- [ ] Desktop controls work.
- [ ] Repeat mapping is correct.
- [ ] Seeked behavior works.
- [ ] Position handling is correct.
- [ ] artUrl points to a real file.

## Hyprland

- [ ] Tiled mode works.
- [ ] Floating mode works.
- [ ] Narrow mode works.
- [ ] Short-height mode works.
- [ ] PiP compositor instructions are documented.
- [ ] No false always-on-top guarantee exists.

---

# 23. Non-negotiable implementation rules

1. Do not use gtkmm.
2. Do not use Peel.
3. Use GTK 4 and libadwaita C APIs from C++20.
4. Use RAII for GObject and other owned resources.
5. Do not mix GTK UI code with SQL.
6. Do not mix GTK UI code with GStreamer pipeline management.
7. Do not duplicate playback state.
8. Do not duplicate waveform sample buffers.
9. Do not use random waveform data.
10. Do not use GtkListView for the sortable Songs table.
11. Do not rely on client-side sorting for unloaded database records.
12. Do not make Home unreachable.
13. Do not assign collapse and menu actions to the same button.
14. Do not make album Play hover-only; keyboard focus must reveal it too.
15. Do not omit Repeat All or Repeat One.
16. Do not allow Repeat All and Repeat One simultaneously.
17. Do not let artwork consume the right panel.
18. Do not make the bottom bar 200–250 px tall.
19. Do not run expensive work on the GTK main thread.
20. Do not call GTK from GStreamer streaming callbacks.
21. Do not invent backend functionality.
22. Do not overwrite working code without backups.
23. Do not modify global GTK theme files.
24. Do not claim GTK guarantees always-on-top behavior on Wayland.
25. Do not publish a nonexistent MPRIS artwork URL.
26. Do not proceed to a phase before its prerequisites exist.
27. Do not mark a phase complete until its acceptance criteria pass.

---

# 24. Reference documentation

## GTK 4

- GTK 4: https://docs.gtk.org/gtk4/
- GTK C API from C++: https://docs.gtk.org/gtk4/question_index.html
- GTK CSS properties: https://docs.gtk.org/gtk4/css-properties.html
- GtkSnapshot: https://docs.gtk.org/gtk4/class.Snapshot.html
- GtkColumnView: https://docs.gtk.org/gtk4/class.ColumnView.html
- GtkGridView: https://docs.gtk.org/gtk4/class.GridView.html
- GtkListView: https://docs.gtk.org/gtk4/class.ListView.html

## libadwaita

- libadwaita documentation: https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/
- Adaptive layouts: https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/adaptive-layouts.html
- Breakpoints: https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/migrating-to-breakpoints.html

## C++ and GTK binding background

- Official gtkmm overview: https://www.gtk.org/docs/language-bindings/cpp/
- gtkmm maintainer discussion about libadwaita bindings: https://discourse.gnome.org/t/questions-about-gtkmm/27887
- GTK and C++: https://docs.gtk.org/gtk4/question_index.html

The project does not use gtkmm, but these references explain the binding landscape and C API integration choices.

## GStreamer

- GStreamer documentation: https://gstreamer.freedesktop.org/documentation/
- Spectrum element: https://gstreamer.freedesktop.org/documentation/spectrum/index.html

## MPRIS

- Official MPRIS specification: https://specifications.freedesktop.org/mpris-spec/latest/

## Icons

- Phosphor: https://phosphoricons.com/
- Lucide: https://lucide.dev/
- Tabler: https://tabler.io/icons

---

# 25. Final implementation objective

The finished application must feel like one cohesive desktop music player.

The library, sidebar, right panel, bottom playback bar, lyrics, queue, waveform, and future PiP player must be coordinated through shared services and shared state.

The most important architectural rule is:

**The UI is a presentation layer over shared services.**

The implementation order is:

1. Audit the repository.
2. Establish C++/GTK integration and ownership.
3. Define models and service interfaces.
4. Build the four-region shell.
5. Build the WaveformView placeholder.
6. Build navigation and library pages.
7. Integrate playback.
8. Build the bottom bar using the placeholder.
9. Prototype and finalize the waveform.
10. Build the right panel.
11. Optimize search, database, and artwork.
12. Integrate MPRIS.
13. Implement PiP separately.

Do not skip the audit, interfaces, or waveform prototype.

Prioritize correctness, responsive behavior, shared state, and performance before decorative polish.