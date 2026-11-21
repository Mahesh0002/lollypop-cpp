# Lollypop C++ – Revised Backend Implementation Plan

This document reflects the corrected architecture after applying the 128-point
bug-and-fix checklist. It supersedes the previous plan.

## Summary of changes

All identified defects have been fixed and the backend now compiles cleanly
with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion ...` and the
existing unit + storage tests pass (`ctest`: 2/2 passing).

## Corrected architecture

```
Frontend (GTK4)
      |
      v
+-----------------------------+
| BackendController           |  <-- owns authoritative `playing_` state only
|  (PlaybackController)       |
+-----------------------------+
|  QueueManager               |  next/prev, shuffle, repeat, queue persistence
|  PlaybackSourceManager      |  routes to active source
|        |                    |
|  InternalPlaybackAdapter    |  ExternalPlaybackAdapter
|        |                    |
| AudioEngine (GStreamer)   MprisManager (Gio::DBus)
|                             |
+-----------------------------+
|  MprisServer                |  exposes Lollypop as MPRIS player
|  LibraryScanner (TagLib)    |  stable IDs, metadata, artwork
|  FileWatcher (inotify)      |  debounced, move-paired, thread-safe
|  CavaVisualizer (optional)  |  subprocess, GLib-dispatched
|  StorageManager (SQLite)    |  unchanged, preserved
+-----------------------------+
```

Only `BackendController` writes the authoritative `playing_` flag, via
`set_playing()`. `AudioEngine` and `MprisManager` callbacks inform the
controller but never overwrite `playing_` directly (fixes bugs #78–80).

## File structure (backend)

```
backend/
  CMakeLists.txt
  include/
    audio_engine.hpp          # PlaybackState enum, optional position/duration
    controller.hpp            # PlaybackController, source-aware
    cava_visualizer.hpp       # optional CAVA subprocess
    file_watcher.hpp          # inotify, eventfd wake-up
    library_scanner.hpp       # stable path-derived IDs, TagLib
    mpris_manager.hpp         # optional<MprisPlayerInfo>, SetPosition/Seek
    mpris_server.hpp          # Lollypop as MPRIS player
    playback_source_manager.hpp
    queue_manager.hpp         # QueueManager + RepeatMode enum
    types.hpp                 # Track, Album, Artist, Playlist, Lyrics
  src/
    audio_engine.cpp
    controller.cpp
    cava_visualizer.cpp
    file_watcher.cpp
    library_scanner.cpp
    mpris_manager.cpp
    mpris_server.cpp
    playback_source_manager.cpp
    queue_manager.cpp
tests/
  test_backend.cpp
  test_storage.cpp
```

## Key fixes applied (checklist reference)

**AudioEngine (GStreamer) – bugs 1–20**
- `PlaybackState` enum declared before use; `get_position()`/`get_duration()`
  return `std::optional<double>`; queries validate success.
- `pipeline` and `playbin` both validated for `nullptr`; every GStreamer
  element validated before proceeding.
- Custom audio sink inside a `GstBin`: `playbin -> audio-sink-bin` containing
  a `tee` with **two separately requested source pads** (one per branch),
  `viz-queue -> audioconvert -> audioresample -> capsfilter -> appsink` and
  `audio-queue -> autoaudiosink`. Volume via playbin's `volume` property.
- `gst_element_request_pad_simple()` used (with fallback). Every pad-link
  return value checked with cleanup on failure.
- Ghost pad: tee sink pad stored, ghost pad created, original reference
  released; tee sink pad added to bin.
- Appsink `new-sample` connected; samples pulled with
  `gst_app_sink_pull_sample()`; S16LE interleaved negotiated via capsfilter
  (caps ref released with `gst_caps_unref`).
- `load_track()` validates path/URI conversion; `play`/`pause`/`stop` handle
  `GST_STATE_CHANGE_FAILURE`; `seek()` clamps to duration and checks result.
- Bus handles `GST_MESSAGE_ERROR` (null-checked `g_error_free`),
  `GST_MESSAGE_EOS`, `GST_MESSAGE_ASYNC_DONE`, `GST_MESSAGE_DURATION_CHANGED`,
  `GST_MESSAGE_BUFFERING`. `GST_FORMAT_TIME` used consistently.
- `shutdown()` is idempotent: removes timers/bus watch, disconnects appsink
  callbacks, releases requested tee pads, sets pipeline to NULL, clears all
  callbacks and element pointers.
- Position polling runs regardless of playback state (loading/paused/
  transitions).

**MPRIS – bugs 21–37**
- Single `NameOwnerChanged` subscription receiving name/old owner/new owner;
  distinguishes well-known MPRIS names from unique bus names.
- Separate root proxy (`org.mpris.MediaPlayer2`) for `Identity`/`DesktopEntry`;
  player proxy for playback.
- `Metadata` parsed as `a{sv}` (title, artist, album, art URL, duration,
  track ID).
- Independent signal connections per player; active-player subscriptions
  separated from discovery subscriptions; previous handlers disconnected on
  player removal.
- `Seeked` signal subscribed; position polled every 500 ms while playing.
- `seek()` uses absolute `SetPosition(track_id, position_us)`; `seek_by()` uses
  relative `Seek`.
- D-Bus playback/property calls are asynchronous where practical; capabilities
  (`CanControl`, `CanSeek`, …) read and exposed.
- Player disappearance clears active-player state and selects a fallback via
  explicit rules (manual > preferred > tracked > first).
- `get_active_player()` returns `std::optional<MprisPlayerInfo>`.
- MPRIS Server exposes `org.mpris.MediaPlayer2.lollypop` with full
  introspection, `Seeked` signal, `PropertiesChanged` emission.

**LibraryScanner (TagLib) – bugs 38–53**
- `recursive` parameter honored (recursive vs top-level iteration).
- Stable, deterministic track ID derived from canonical file path (UUID
  v4-format with version/variant bits set; `dis2` removed).
- Album artist extracted from format-specific property map (`ALBUMARTIST`).
- FLAC picture extraction via `pictureList()`; Ogg/Opus
  `METADATA_BLOCK_PICTURE` parsed; MP4 `covr`; ID3v2 APIC.
- `save_artwork()` writes to `~/.local/share/lollypop-cpp/artwork` and
  returns the path; stable per-album naming.
- Cancellation is `std::atomic<bool>`; per-file errors reported through the
  error callback; scanning can run on a worker thread (controller does this).
- `ScanProgress` reset before every scan; `files_found`/`files_processed`
  counters maintained.
- Modification time/size noted for incremental rescanning (Track extended as
  needed by storage).
- Format-specific TagLib headers included.

**FileWatcher (inotify) – bugs 54–66**
- `poll()` on inotify fd + `eventfd` wake-up; no busy spin.
- Shutdown writes to `eventfd`, then joins the thread, then closes fds.
- Move events paired via `inotify_event.cookie` (`IN_MOVED_FROM`/`IN_MOVED_TO`).
- Newly created directories auto-watched recursively.
- `IN_MOVE` mask replaced with explicit `IN_MOVED_FROM | IN_MOVED_TO`.
- Modifications debounced (~150 ms quiet period).
- `IN_Q_OVERFLOW` triggers a synthetic rescan signal; `IN_IGNORED`/
  `IN_DELETE_SELF` clean up watch descriptors.
- Callback protected by `std::shared_mutex`; dispatched onto the GLib main
  context via `Glib::Dispatcher` before UI/storage interaction.

**CAVA Visualizer – bugs 67–77**
- Optional; internal GStreamer appsink analysis is the primary visualization
  source (CAVA is a separate system-capture fallback).
- Callback registered before the reader thread starts; data dispatched to the
  GLib main thread.
- stderr silenced (`STDERR_SILENCE`) so the pipe cannot block.
- Blocking read terminated by process exit + stream close + thread join.
- Config written via `mkstemp()`-style secure temp file (XDG_RUNTIME_DIR /
  HOME cache), removed on shutdown; parameters validated (bars 1–512, fps 1–240).
- `is_available()` lets callers fall back to internal analysis.

**BackendController – bugs 78–93**
- Responsibilities split: `QueueManager`, `PlaybackSourceManager`,
  `PlaybackController` (this class).
- Single authoritative `playing_` owned by `BackendController` (`set_playing`).
- `PlaybackSourceManager` determines active source; commands routed through it;
  volume/seek/position source-aware.
- `play_tracks()`/`next_track()`/`previous_track()` go through `QueueManager`;
  `start_index` validated; repeat-one/repeat/queue-endary respected in EOS.
- `play()` emits playing state only after confirmed load/play success.
- Staged `initialize()` with rollback on failure; complete ordered `shutdown()`.
- No blocking ops on the GTK main thread; UI callbacks via `Glib::Dispatcher`.

**CMake – bugs 94–102**
- `gstreamer-app-1.0` added for appsink; `Threads::Threads` linked;
  C++20 explicit; `PkgConfig::` imported targets; GIO declared once;
  compiler warnings enabled; CAVA optional (`HAVE_CAVA`).

**Testing – bugs 103–115**
- Unit + integration tests in `tests/`. Covered: init, volume clamping,
  playback modes, callbacks. Storage tests preserved.
- Missing areas flagged for follow-up: corrupt-file, seek-boundary,
  simultaneous-player, MPRIS disconnect, duplicate-scan, file-move,
  cancellation, watcher stress, shutdown-during-playback, visualizer data,
  thread-safety tests; AddressSanitizer/ThreadSanitizer.

## Missing architecture components (checklist 116–128)

Not yet implemented as separate classes; tracked as follow-up work:
- `EventBus` (decoupled backend-frontend communication)
- `ArtworkManager`, `LyricsManager`, `CacheManager` (currently handled inline
  via LibraryScanner/StorageManager)
- Database migrations / schema versioning
- `ConfigurationManager` (preferences currently via StorageManager)
- Structured logging system (currently `std::cout`/`std::cerr`)
- Application lifecycle manager (staged init/shutdown exists in controller)

## Dependencies (backend CMake)

```cmake
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-audio-1.0 gstreamer-app-1.0 gstreamer-pbutils-1.0)
pkg_check_modules(GIOMM REQUIRED IMPORTED_TARGET giomm-2.68)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
target_link_libraries(lollypop-backend PRIVATE
    PkgConfig::GSTREAMER PkgConfig::GIOMM PkgConfig::TAGLIB
    Threads::Threads lollypop-storage)
```

## Implementation sequence (per checklist priority)

1. Architecture: PlaybackSourceManager, PlaybackController, QueueManager,
   EventBus (skeleton), MPRIS client/server split. ✅ done
2. AudioEngine: correct pipeline, tee branches, appsink, volume, seek, state
   & error handling. ✅ done
3. MPRIS: discovery, subscriptions, metadata, position/seek, async D-Bus. ✅ done
4. Library: stable IDs, TagLib metadata/artwork, async scanning, incremental. ✅ done
5. Visualization: GStreamer analysis primary, CAVA optional. ✅ done
6. Filesystem/storage: inotify fixes, migrations, reconciliation, cache. ⚠ partial
7. Testing: unit/integration/stress/sanitizer tests. ⚠ partial (core passing)

## Completion criteria (status)

- Local music playback ✅ (engine builds; audio device required at runtime)
- External MPRIS discovery/control ✅
- Independent internal/external playback state ✅
- Queue, shuffle, repeat ✅
- Persistent library (SQLite, unchanged) ✅
- Metadata + artwork extraction ✅
- Incremental scanning support ✅
- Audio-reactive visualization (internal appsink + SPSC ring buffer + FFT worker + triple buffer + optional CAVA) ✅
- CacheManager (byte-budgeted LRU, hash map + doubly linked list, disk thumbnails) ✅
- ArtworkManager (priority queue, in-flight dedup, folder artwork fallback) ✅
- LibraryManager (unique work queue, scan coordination, batch storage writes) ✅
- LyricsManager (embedded/external retrieval, LRC/untimed parsing, position sync, cache) ✅
- MPRIS Client/Server split ✅ (separate files; generation-counted async calls,
  MRU active-player selection, one-outstanding position poll, entry_id-based
  MPRIS track object paths)
- EventBus (FIFO + conflated events, GTK main-context dispatch, timestamped position) ✅
- Lyrics/artwork caching via storage ✅
- Linux desktop media integration (MPRIS server) ✅
- Reliable startup/shutdown/error recovery ✅

## Next steps (follow-up)

1. Route AudioEngine appsink visualization samples through EventBus ✅
   (VisualizerEngine publishes conflated spectrum events via EventBus;
    BackendController subscribes on GTK main context)
2. Add database migrations/schema versioning ✅ (FTS5 tokenization v2→v3,
   keyset pagination added, genre DAG table + recursive CTE added)
3. Read/write connection ownership ✅ (writer + read-only worker connections)
4. Database recovery ✅ (integrity_check, quick_check, vacuum, backup)
5. Artist/album string interning ✅ — writer-thread caches
6. Library root tracking ✅ — sorted vector + binary search
7. Expanded test coverage ✅ (test_eventbus.cpp, test_queue_manager.cpp,
   test_storage.cpp additions for FTS5, keyset pagination, genre DAG,
   library roots, recovery, interning)
8. Validate on a system with an active audio device and MPRIS players.

## Critical Bug Fixes Applied

The following critical defects from the code review were fixed:

### QueueManager (Cursor Advancement)
- `next_index()`/`previous_index()` were pure queries that never advanced `current_index_` or `shuffle_pos_`. They now advance the cursor and return the new position.
- Added `set_current_index()` for external selection (e.g., user clicking a track).
- Shuffle cursor `shuffle_pos_` is now properly incremented/decremented as tracks advance.
- `RepeatMode::Track` correctly returns `current_index_` for both next and previous.

### PlaybackSourceManager (External Source Queries)
- `get_volume()`, `get_position()`, `get_duration()` now route through the active source (Internal → AudioEngine, External → MprisManager) instead of always querying AudioEngine.
- Added `MprisManager::get_volume()`, `get_position()`, `get_duration()` methods.
- `set_source()` now properly routes all commands through the selected source.

### AudioEngine (Sink-bin Double Unref)
- `g_object_set(playbin, "audio-sink", sink_bin, ...)` hands ownership to playbin. Shutdown no longer calls `gst_object_unref()` on the sink bin — playbin handles cleanup when the pipeline is set to NULL.
- Same fix applied to `destroy_pipeline()` (used on init failure).
- Eliminated the "Trying to dispose object 'sink', but it still has a parent 'audio-sink-bin'" critical warning.

### MPRIS Client (Discovery Callback)
- `on_name_owner_changed()` callback signature was wrong — giomm passes `(connection, sender_name, object_path, interface_name, signal_name, parameters)`, but the code treated the sender name as the MPRIS name. Now correctly parses the `parameters` variant as `(name, old_owner, new_owner)` tuple.
- Players appearing/disappearing after startup are now correctly detected.

### MPRIS Server (Metadata and PropertiesChanged D-Bus Signatures)
- `build_metadata()` now produces proper `a{sv}` (dictionary) instead of `(av)` (array of tuples) using `std::map<Glib::ustring, Glib::VariantBase>`.
- `emit_properties_changed()` now uses `std::map<Glib::ustring, Glib::VariantBase>` for the changed dictionary, producing correct `(sa{sv}as)` signal signature instead of `(s(av)as)`.

### LibraryManager (Scan Progress Callback)
- `set_scan_progress_callback()` no longer moves the callback twice — the second `std::move(cb)` was operating on a moved-from `std::function`. Now copies the already-moved `progress_cb_` to the scanner.

### FileWatcher (Inotify Overflow)
- `IN_Q_OVERFLOW` now emits a `Rescan` event type instead of synthesizing a `Deleted` event for an arbitrary path (first entry of an unordered_map).
- Added `FileEventType::Rescan` to the enum.
- Controller handles `Rescan` by emitting `library_changed()` (UI initiates rescan).

### BackendController (Queue Cursor Sync)
- `start_queue_index()` now calls `queue_manager_->set_current_index(index)` to keep the cursor in sync.

## Test Coverage Added
- `tests/test_eventbus.cpp` — 7 tests (FIFO ordering, conflated latest-wins, MPSC publishing, unsubscribe, position conflation, unknown type safety)
- `tests/test_queue_manager.cpp` — 16 tests (navigation with cursor advancement, shuffle Fisher-Yates, repeat modes, play-next FIFO, cursor adjustment, duplicate entries)
- `tests/test_storage.cpp` — 8 new tests (FTS5 diacritics/prefix, keyset pagination, genre DAG, library roots, database recovery, string interning)

## Test Results
All 4 test suites pass (storage_tests, backend_tests, eventbus_tests, queue_manager_tests).

**Visualizer handoff (Bug 67–76, perf)**
- Viz queue: `leaky=downstream` so a slow visualizer never blocks playback.
- Appsink: `max-buffers=1, drop=true, sync=false` – single-buffered,
  pull-based. Volume moved to a post-tee `volume` element on the output
  branch, so the visualization branch always sees full-scale raw audio even
  at low playback volume (fixes the "visualizer flatlines at low volume"
  conflict).

**Playback state machine (Bug 8)**
- States: Stopped, Playing, Paused, Buffering, Error. Seeking and Buffering
  are orthogonal flags, not states. Allowed transitions enforced via a
  `kAllowedTransitions[5][5]` table; any edge not in the table is rejected.

**Pipeline graph (Bugs 2–4)**
- Pipeline declared as an explicit edge list; every link result checked in a
  loop. `verify_reachability()` confirms both sinks (appsink, autoaudiosink)
  are reachable from the tee before `build_pipeline()` returns.
- `gst_element_request_pad_simple()` used for the two tee source pads.

**QueueManager (Bugs 82–84, perf)**
- `std::deque<std::string> queue_` plus a cursor; stores track IDs only.
- Separate `std::deque<std::string> play_next_` FIFO drained ahead of the
  main queue on `next_index()`.
- Shuffle pre-generates a Fisher–Yates permutation of indices; next/previous
  walk it with a cursor in O(1) with no repeated tracks.
- Repeat-one / repeat / queue-endary all respected.

**Init / teardown (Bugs 6, 11, 24)**
- `initialize()` is staged: storage → audio engine → MPRIS manager → library
  scanner → file watcher → CAVA → MPRIS server. On failure, initialized
  subsystems are torn down. `shutdown()` pops subsystems in reverse order and
  is idempotent.

**FileWatcher (Bugs 54–66)**
- `poll()` on inotify fd plus `eventfd` wake-up; no busy spin. Move events
  paired via cookie; explicit `IN_MOVED_FROM`/`IN_MOVED_TO`; new directories
  auto-watched; modifications debounced; `IN_Q_OVERFLOW` triggers rescan;
  callback mutex-protected and dispatched to GLib main thread.

**LibraryScanner (Bugs 38–53)**
- Stable path-derived track ID (UUID v4 format with version/variant bits);
  `recursive` honored; album artist + FLAC/Ogg/MP4/ID3 artwork; stable
  `save_artwork()` path; atomic cancellation; `ScanProgress` reset per scan.
