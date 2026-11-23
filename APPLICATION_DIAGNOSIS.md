# Application diagnosis: startup delay, broken behaviour, and navigation bugs

Date: 2026-11-23  
Scope: code review of the current working tree plus a short runtime measurement of `build/lollypop-cpp`. No source files were changed.

> Note: several older audit documents in this repo (`critique.md`, `BUG_FIXES_SUMMARY.md`, `docs/FRONTEND_GAP_ANALYSIS.md`) mark some of the items below as "FIXED". The current code still contains the defects described here. Treat this document as the current state.

---

## TL;DR

The application has three separate classes of problems:

1. **Real correctness/crash bugs.** The most serious one is that `GWeakRef` is used on plain C++ objects that are **not GObjects**. Typing in the search box calls `g_weak_ref_init()` with a `MiddleLibrary*`, which is undefined behaviour and segfaults immediately in a local GLib repro. The same mistake exists in search-result lazy loading. Several visible navigation controls are also dead stubs.

2. **Everything expensive happens on the GTK main thread before the first frame.** Backend init performs GStreamer plugin/audio-sink creation, two synchronous D-Bus connections, synchronous MPRIS proxy creation, SQLite migrations/FTS/backfill, queue restoration with one SQL query per saved track ID, and then the whole UI (11 pages, HomePage data fetch, queue rows) is built before `gtk_window_present()`. A full recursive library rescan is started immediately after that. With a large music folder this easily produces a 10–20 s "launch" even though the process is technically alive.

3. **Navigation is mostly cosmetic.** The history stack stores only page IDs, not page parameters or scroll offsets; the middle panel uses one shared scroller whose offset is never reset; detail pages are single shared instances; the sidebar loses its active highlight on detail/search pages; search runs synchronously; and the Home "View all" buttons never invoke the callbacks that were wired up for them.

In this workspace, with 132 files in `~/Music`, the current binary reaches a fully idle state in roughly two seconds (see "Runtime observation" below). The 10–20 s delay is therefore most likely caused by the scaling factors listed in section 3 (large library, embedded artwork extraction, slow/unavailable PulseAudio or session bus, cold GStreamer plugin cache, running from a directory where `resources/style.css` is not found).

---

## 1. Runtime observation

Command used (22 s sample):

```sh
timeout 22s stdbuf -oL -eL ./build/lollypop-cpp 2>&1 | awk '{print strftime("%H:%M:%S"), $0}'
```

Output:

```text
11:45:36 BackendController: Initializing...
11:45:36 MprisServer: Exported as org.mpris.MediaPlayer2.lollypop
11:45:36 BackendController: Initialized successfully
11:45:36 MainWindow: Backend callbacks connected
11:45:36 BackendController: Scanning Music folder /home/mahesh/Music
11:45:36 LibraryManager: scanning directory /home/mahesh/Music (recursive=1)
```

Sampling `/proc/<pid>/stat` once per second showed:

- t=1 s: 1 thread, ~5.6 MB RSS (still in dynamic loading / `adw_init()`).
- t=2 s: 16 threads, ~207 MB RSS, 0.25 s user + 0.05 s system CPU.
- t=3–15 s: CPU time flat, RSS flat → main loop idle.

So the code path completes quickly with a small library, but the architecture is O(library size) and O(external service latency) on the UI thread. The ~207 MB RSS is also high for 132 tracks and is a symptom of constructing every page and loading GStreamer at startup.

---

## 2. Why the application is "broken" (correctness bugs)

### P0-1 — `GWeakRef` used on non-GObject objects → crash

| Location | Problem |
| --- | --- |
| `frontend/include/panels/middle_library.hpp:71` | `GWeakRef self_weak_ref_;` is a POD member with no initialization. |
| `frontend/src/panels/middle_library.cpp:89-96` | Destructor calls `g_weak_ref_clear()` on it. |
| `frontend/src/panels/middle_library.cpp:508-515` | `on_search_changed()` calls `g_weak_ref_get()` on an uninitialized `GWeakRef`, then `g_weak_ref_init(&self_weak_ref_, self)` where `self` is a `MiddleLibrary*` (plain C++, **not** a GObject). |
| `frontend/src/panels/middle_library.cpp:514-524` | The timeout callback again calls `g_weak_ref_get()` on that invalid weak ref. It also never `g_object_unref()`s the returned strong reference. |
| `frontend/src/ui/pages/SearchResultsPage.cpp:20-25, 324-343` | `SearchResultsIdleData::page_ref` is initialized with `this`, a `SearchResultsPage*` which is also **not** a GObject. |

`GWeakRef` only works with `GObject` instances. I verified locally with a minimal program:

```c
struct Foo { int x; };
GWeakRef wr = { NULL };
g_weak_ref_init(&wr, &foo);   // foo is not a GObject
// result: SIGSEGV (exit 139)
```

Impact: typing a single character in the library search box can segfault the app; scrolling search results past the first page can do the same.

**Mitigation:** never weak-ref `this` here. Use the same pattern already used in `SongsPage`: weak-ref the actual GObject widget (`widget_.get()`) and tag it with owner data, or use a `std::shared_ptr<bool>` lifetime guard owned by the C++ page and cancelled in the destructor. Initialize and clear weak refs correctly if they are kept. Always `g_object_unref()` the result of `g_weak_ref_get()`.

---

### P0-2 — Home page "View all" buttons are dead

| Location | Problem |
| --- | --- |
| `frontend/src/ui/components/HomePage.cpp:94-99, 165-170` | Each button's `clicked` handler gets a **heap copy** of the `view_all_cb` that existed at construction time. That lambda reads `recently_added_cb_` / `recently_played_cb_` / `favourite_albums_cb_`. |
| `frontend/src/ui/components/HomePage.cpp:382-398` | The setters assign the new callback to `sections_data_[i]->view_all_cb` (a *different* copy) and never set the member lambdas at `include/ui/components/HomePage.hpp:92-94`. |
| `frontend/src/panels/middle_library.cpp:340-346` | Wiring happens after HomePage construction, so the button copies never see it. |

Impact: the three "View all" affordances on Home do nothing; users cannot jump to Songs or Favourites from Home.

**Mitigation:** set the member callbacks in the setters (`recently_added_cb_ = std::move(cb); ...`), and make the button handler call `data->view_all_cb` by reference to the stored `SectionData`, or connect the signal after the callbacks are assigned.

---

### P1 — Search results never load their second page / stale-result bugs

- `frontend/src/ui/pages/SearchResultsPage.cpp:304-343`: lazy loading hits the non-GObject `GWeakRef` bug above.
- `frontend/src/ui/pages/SearchResultsPage.cpp:137-171`: `library_service_->search_tracks(query)` runs synchronously on the GTK thread, holding `load_mutex_`. On a large FTS index this freezes typing.
- `frontend/src/ui/pages/SongsPage.cpp:161-188`: `reload()` increments `generation_` but `append_tracks()` (`217-232`) never checks it, so results from an older sort/filter can be appended after a new one.
- `frontend/src/ui/pages/SongsPage.cpp:190-193`: `load_page()` returns early if `loading_` is still true. A quick second sort/filter click can leave the list empty until the stale idle callback fires.
- `frontend/src/ui/pages/SongsPage.cpp:270-273`: `set_genre_filter()` reloads data but does not update the genre dropdown selection, so the visible filter contradicts the list when `show_genre()` is used.

**Mitigation:** move FTS to a worker with generation/cancellation and marshal results with `g_idle_add`; check the generation in `append_tracks`; reset `loading_` before starting a new load; set the dropdown selection when applying a genre from `show_genre()`.

---

### P1 — Decorative controls and missing CSS

- `frontend/src/panels/middle_library.cpp:259-275`: the popover buttons "View Options", "Sort By", "Group By", "Settings" have no `clicked` handlers.
- `frontend/src/panels/left_sidebar.cpp:302-304`: Settings only does `std::cout << "Settings clicked"`.
- `frontend/src/application.cpp:92`: `theme_manager_->load_stylesheet("resources/style.css")` uses a **relative path**. `frontend/src/ui/theme/ThemeManager.cpp:35-56` ignores the `GError`. Launch from any directory other than the repo root or `build/` and the app silently runs unstyled.
- `resources/lollypop.gresource.xml` bundles only the Inter font, not the stylesheet.
- `src/main.cpp:56-64`: if `LOLLYPOP_HIDE_GTK_CRITICALS` is set, real GTK assertions are hidden; this can mask the bugs above.

**Mitigation:** compile an absolute data path into the binary (`LOLLYPOP_DATA_DIR`) or bundle `style.css` into the gresource and load `resource:///.../style.css`; log and surface the error on failure. Wire the menu buttons to real actions or remove them.

---

## 3. Why launch takes 10–20 s

### The serial pre-first-frame path

`Application::on_activate()` (`frontend/src/application.cpp:71-87`) does this on the GTK main thread, in order:

1. `BackendController()` construction — dispatchers, queue manager, source manager.
2. `backend_->initialize()` — see below.
3. `new MainWindow(...)` — builds **all** panels and **all** pages.
4. `main_window_->present()`.
5. `backend_->scan_default_music_library()` — starts a full recursive rescan.

Only after step 4 can the user see a window. Steps 1–3 are synchronous.

### 3.1 Backend initialization is synchronous and touches slow external services

`backend/src/controller.cpp:53-213`:

- **Storage** (`:57-61`): `StorageManager::initialize()` (`storage/src/manager.cpp:109-149`) runs `PRAGMA journal_mode=WAL`, version checks, five migration steps, `create_fts5_index()`, `create_triggers()`, and `backfill_library_index()` on **every launch**. `backfill_library_index()` (`storage/src/manager.cpp:406-452`) executes multi-table `INSERT ... SELECT ... NOT EXISTS` statements on every startup even when nothing changed. This is I/O on the UI thread.
- **GStreamer** (`backend/src/audio_engine.cpp:169-203`): `gst_init()` scans/loads the plugin registry; `build_pipeline()` creates ~13 elements including a `pulsesink` (`:350-355`). If PulseAudio/PipeWire is slow or unreachable, sink creation can block. This happens before any audio is needed.
- **D-Bus, twice** (`backend/src/mpris_manager.cpp:696-721` and `backend/src/mpris_server.cpp:439-471`): two separate `Gio::DBus::Connection::get_sync(SESSION)` calls. `MprisManager::Impl::refresh_players()` additionally creates several `Gio::DBus::Proxy::create_sync()` proxies (`:355-364`) for every detected MPRIS player. An unresponsive external player can delay startup.
- **Queue restore** (`backend/src/controller.cpp:175-199`): validates every saved queue ID with an individual `get_track(id)` SQL query. For a 1,000-track saved queue that is 1,000 round trips on the main thread.
- `CacheManager` and `LyricsManager` are also initialized here (`controller.cpp:137-154`), creating `/tmp/lollypop-cpp-cache` directories.

### 3.2 The entire UI is built eagerly

`frontend/src/main_window.cpp:118-155` constructs `MiddleLibrary`, `LeftSidebar`, `RightNowPlaying`, and `BottomPlayback`. `MiddleLibrary::create_placeholder_pages()` (`frontend/src/panels/middle_library.cpp:278-350`) eagerly constructs ten pages:

- `HomePage` immediately runs three library queries (`frontend/src/ui/components/HomePage.cpp:41-64`) and creates up to 24 `TrackRow`/`AlbumCard` widgets with artwork, each with a staggered spring animation (`:249-275`, `:304-329`).
- `SongsPage` queries all genres in its constructor (`frontend/src/ui/pages/SongsPage.cpp:87-104`).
- `RightNowPlaying::set_queue()` (`frontend/src/panels/right_now_playing.cpp:517-593`) resolves every queued track ID through `get_tracks_by_ids()` and builds a row per track, synchronously, from `MainWindow::connect_backend()` (`frontend/src/main_window.cpp:233`).

GTK then has to measure/style this whole tree against a 1,303-line stylesheet full of custom properties, shadows, and transitions (`resources/style.css`) before the first frame. This is the main reason RSS jumps to ~207 MB in the measurement.

### 3.3 A full library rescan starts immediately after the window appears

`backend/src/controller.cpp:386-447` resolves `~/Music` and calls `library_manager_->scan_directory(path, true)` right after `present()`.

- `LibraryScanner::scan_recursive()` (`backend/src/library_scanner.cpp:226-265`) walks everything with **no mtime/size/hash check**. Every file is re-read on every launch.
- `process_file()` (`:278-324`) constructs a `TagLib::FileRef` per file, reads all tags, and `extract_artwork()` writes embedded cover art to disk for every album (`:119-145`, `:147-200`).
- Progress is emitted **twice per file** (`:446, :449`) and each emission calls `scan_dispatcher_->emit()` (`backend/src/controller.cpp:429-443`), marshalling an event to the GTK main loop.
- At completion, `emit_library_changed()` (`controller.cpp:483`) causes `MainWindow`'s callback to call `middle_library_->refresh_active_page()` **synchronously** (`frontend/src/main_window.cpp:240-243`), rebuilding HomePage and all its animations on the UI thread.

With thousands of tracks and large embedded artwork, this keeps the main loop busy for many seconds after the window is mapped — exactly the "10–20 s to launch" symptom.

### 3.4 Always-on animation/tick sources

- `RightNowPlaying` (`frontend/src/panels/right_now_playing.cpp:102`, `:595-607`) and `BottomPlayback` (`frontend/src/panels/bottom_playback.cpp:43`) install permanent GTK tick callbacks just to detect size changes.
- `ProgressWave` starts a timer (`frontend/src/panels/progress_wave.cpp:160`), `WaveformWidget` runs a 16 ms timer (`frontend/src/panels/waveform_widget.cpp:55`), and `WaveformView` has a tick callback (`frontend/src/panels/waveform_view.cpp:48`) — several of these run even when nothing is playing or the widget is hidden.

### Mitigations for startup (ordered by impact)

| Priority | Change | Where |
| --- | --- | --- |
| P0 | Present the window first with a lightweight loading state; move `BackendController::initialize()` to a worker, or at minimum defer MPRIS, cache, lyrics and queue restore to `g_idle_add_full(G_PRIORITY_LOW, ...)` after the first frame. | `frontend/src/application.cpp:71-87` |
| P0 | Make audio lazy: do not call `gst_init()` / create the pipeline / create `pulsesink` until the first play. Use `gst_init_check()` on a worker if plugin scanning is slow. | `backend/src/audio_engine.cpp:169-203, 350-355` |
| P0 | Make MPRIS async/deferred: use `Gio::DBus::Connection::get()` callbacks, avoid `create_sync()` proxies during startup, and start both MPRIS objects after the UI is interactive. | `mpris_manager.cpp:696-721`, `mpris_server.cpp:439-471` |
| P1 | Skip schema/FTS/trigger/backfill work when `user_version == schema_version`; batch queue restoration into one `get_tracks_by_ids()` call. | `storage/src/manager.cpp:109-149, 406-452`, `controller.cpp:175-199` |
| P1 | Replace the full startup rescan with an incremental scan of persisted library roots using mtime+size; delay it ~1–2 s after first frame; throttle progress callbacks to every N files or ~100 ms; coalesce `refresh_active_page()` to one idle rebuild. | `controller.cpp:386-447`, `library_scanner.cpp:226-265, 446-449`, `main_window.cpp:240-243` |
| P1 | Create pages lazily on first navigation (Home + initial page only). Cache fetched data per page instead of rebuilding on every library change. | `middle_library.cpp:278-350` |
| P2 | Replace permanent allocation tick callbacks with `size-allocate` signals; run wave timers only while playing/visible. | `right_now_playing.cpp:102,595-607`, `bottom_playback.cpp:43`, `progress_wave.cpp:160`, `waveform_widget.cpp:55` |
| P2 | Load CSS from an absolute/gresource path and check the `GError`. | `application.cpp:92`, `ThemeManager.cpp:35-56` |

---

## 4. Navigation bugs

| # | Bug | Evidence | Fix |
| --- | --- | --- | --- |
| N1 | Search typing can crash the app. | `middle_library.cpp:508-524`, `middle_library.hpp:71` (invalid `GWeakRef`) | See P0-1: use widget-based weak refs or a lifetime token; unref strong refs. |
| N2 | Home "View all" buttons do nothing. | `HomePage.cpp:94-99, 165-170, 382-398` | See P0-2: assign the member callback lambdas. |
| N3 | Search results stop loading / can crash on scroll. | `SearchResultsPage.cpp:41-49, 304-343` | Remove nested scroller, use shared adjustment, fix weak-ref usage. |
| N4 | Shared scroll offset leaks between pages. `navigate_to_page()` never touches `scrolled_window_`'s adjustment. | `middle_library.cpp:119-162, 359-383` | Reset adjustment to 0 on top-level navigation, or store/restore per-page offsets in history entries. |
| N5 | Back/forward restore only a page ID, not its state. Detail pages are single shared instances, so going back to `album_detail` shows whichever album was opened last; genre filters and search queries are lost. | `middle_library.cpp:359-383, 467-495, 440-465` | Make history entries `{page_id, params, scroll_offset}`; call `set_album()`/`set_genre_filter()`/`search()` when restoring. |
| N6 | Sidebar highlight disappears on detail and search pages because `page_change_cb_` forwards IDs such as `album_detail`, `artist_detail`, `search` that have no nav row. | `main_window.cpp:133-138`, `left_sidebar.cpp:209-223, 274-282` | Map detail/search IDs to a parent nav item (Albums/Artists/Songs) or add explicit nav state; ignore unknown IDs. |
| N7 | Clearing the search box manipulates history directly and can yank the user to the search page from an unrelated page; the search entry retains its text after navigating away. | `middle_library.cpp:545-561` | Treat search as a normal page with its own history entry; clear the entry when leaving; never pop history while on a different page. |
| N8 | Search query runs on the GTK thread; large libraries freeze input. | `SearchResultsPage.cpp:163-171` | Worker thread + generation + idle marshal (like `SongsPage` intends). |
| N9 | `SongsPage` can show stale/empty results after sort or filter changes. | `SongsPage.cpp:161-232, 270-273` | Check generation in `append_tracks`, clear `loading_` before a new load, sync dropdown selection. |
| N10 | Toolbar popover items and sidebar Settings are non-functional. | `middle_library.cpp:259-275`, `left_sidebar.cpp:302-304` | Implement actions or remove the controls. |
| N11 | Initial navigation happens before `page_change_cb_` is connected, and `LeftSidebar` calls `set_active_page("home")` with `add_to_history=true` immediately after `MiddleLibrary` already navigated to home with `false`. | `main_window.cpp:125-138`, `left_sidebar.cpp:74-81`, `middle_library.cpp:348-349` | Connect the callback before initial navigation and make one place own the initial page selection. |
| N12 | `refresh_active_page()` rebuilds a page unconditionally after a scan, discarding user filters/selection. | `main_window.cpp:240-243` | Refresh data models in place and preserve filter/selection/scroll state. |

---

## 5. Suggested implementation order

1. **Crash/stability (P0):** fix the `GWeakRef` misuse in `MiddleLibrary` and `SearchResultsPage`; fix Home "View all"; make CSS load path robust; verify with a manual launch plus `ctest` and the ASan build already present in `build-asan/`.
2. **Perceived launch (P0/P1):** defer MPRIS + audio + queue restore; present the window before backend work; lazy-create pages. Measure time-to-first-frame, not just time-to-process-start.
3. **Scan behaviour (P1):** incremental scan with mtime/size, throttled progress, debounced single UI refresh.
4. **Navigation model (P1):** introduce a structured history entry and a single `navigate(page, params)` API; remove nested scrollers; sync sidebar state through a parent-page mapping.
5. **Search/sort correctness (P1):** worker-thread queries with generation tokens; fix `SongsPage` stale loads and filter state.
6. **Polish (P2):** wire or remove decorative controls; replace permanent tick callbacks; reduce startup memory.

### Acceptance checks

- Typing, deleting, and re-typing a search query never crashes; results update and lazy-load past the first 50 tracks.
- Home "View all" navigates to Songs / Favourite Albums.
- Opening an album, pressing Back, and pressing Forward returns to the same album and scroll position.
- Sidebar always highlights a valid item; detail pages highlight their parent section.
- Changing sort/filter rapidly produces only the newest result set.
- Launching `./build/lollypop-cpp` from `/tmp` still loads `style.css` (or logs a clear error).
- Cold launch with a several-thousand-file library shows a responsive window in < 2 s; scan progress does not block input.
- `ctest` and the ASan binary report no new errors.

---

## 6. Caveat

I did not modify code or commit anything. The 10–20 s figure was not reproducible with the current 132-file library in this environment (the process was idle after ~2 s), so the startup fixes are prioritised by which code paths scale with library size or block on external services. If the slow launch happens only on a specific machine, check `DBUS_SESSION_BUS_ADDRESS`, PulseAudio/PipeWire availability, and the size of `~/Music` / embedded artwork first; those directly affect the synchronous paths listed in section 3.1 and 3.3.
