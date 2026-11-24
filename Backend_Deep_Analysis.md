# Lollypop C++ Backend - Deep Architecture Analysis

## Executive Summary

The backend is a well-structured, multi-threaded C++17 codebase built around GStreamer (audio), TagLib (metadata), Gio/DBus (MPRIS), and GLib (main loop integration). It follows a **component-based architecture** with a central `BackendController` orchestrating independent subsystems. Key design principles: thread confinement, single-writer patterns, generation-based async safety, and GTK main-loop marshaling.

**Thread Model:**
- **Main/GLib thread**: UI callbacks, D-Bus signal handling, event dispatch
- **Audio streaming thread** (GStreamer): Real-time sample processing
- **Worker threads** (per subsystem): LibraryManager, ArtworkManager, LyricsManager, FileWatcher, VisualizerEngine FFT
- **EventBus**: Thread-safe delivery to GTK main context via Glib::Dispatcher

---

## 1. Core Architecture - BackendController (controller.hpp/cpp)

### 1.1 Role & Responsibilities
`BackendController` is the **central orchestrator** implementing three service interfaces:
- `IPlayerService` - Playback control, queue, state
- `ILibraryService` - Track/album/artist queries, scanning
- `ILyricsService` - Lyrics retrieval

It owns all subsystems via `unique_ptr`/`shared_ptr` and manages their lifecycles.

### 1.2 Initialization Stages (Staged with Rollback)
```cpp
Stage 1: StorageManager (SQLite) - MUST succeed
Stage 2: AudioEngine (GStreamer) - lazy pipeline creation
Stage 2b: VisualizerEngine - connects to AudioEngine appsink
Stage 3: LibraryScanner (TagLib)
Stage 4: FileWatcher (inotify)
Stage 5: LibraryManager (coordinates scanner + storage)

Deferred (G_PRIORITY_LOW idle):
  → MprisManager (DBus player discovery)
  → CacheManager (LRU byte-budgeted)
  → LyricsManager (fetch worker)
  → MprisServer (export Lollypop as MPRIS player)
  → CavaVisualizer (optional external)
  → Queue restoration from preferences
```

**Rollback**: Each stage cleans up previous on failure (`storage_.reset()`, etc.)

### 1.3 Playback State Machine - Single Source of Truth
`playing_` (bool) is **owned exclusively by BackendController**. Subsystems report state changes; only `set_playing()` updates it.

**Guard Flags Preventing Re-entrancy:**
```cpp
changing_playback_state_  // Set during set_playing()
just_started_             // Ignore transient Stopped during startup
pausing_                  // Ignore transient Playing during PLAYING→PAUSED
seek_pending_ + seek_generation_  // Debounced seek coordination
```

**State Transition Logic (audio_engine.cpp:103-158):**
```
Allowed: Stopped↔Playing, Stopped↔Paused, Playing↔Paused, any→Error
Rejected: Error→Playing/Paused/Buffering, Buffering→Error
```

### 1.4 Callback Multicast System
Supports **multiple observers** (UI + MPRIS server + tests):
```cpp
std::vector<TrackChangedCallback> track_changed_callbacks_;
void add_track_changed_callback(cb) { push_back }  // multicast
void set_track_changed_callback(cb) { clear + push }  // legacy single
```
Emission iterates all callbacks: `emit_track_changed()`

### 1.5 Thread Marshaling - Glib::Dispatcher Pattern
All worker→UI communication uses `Glib::Dispatcher`:
```cpp
// Worker thread
scan_dispatcher_ = make_unique<Glib::Dispatcher>();
scan_dispatcher_->connect([this] { dispatch_scan_updates(); });
// In worker: scan_dispatcher_->emit();

// GTK main thread: dispatch_scan_updates() calls UI callbacks
```

**Three dispatchers:**
1. `scan_dispatcher_` - Library scan progress/results
2. `file_event_dispatcher_` - inotify events (debounced)
3. `lyrics_dispatcher_` - Async lyrics fetch results

### 1.6 Playback Source Abstraction
`PlaybackSourceManager` routes commands to active source:
- **Internal**: AudioEngine (GStreamer playbin alternative)
- **External**: MprisManager (controls Spotify, etc.)

Controller checks `is_playback_source_internal()` for queue-aware logic (next/prev, repeat-one).

### 1.7 Queue Persistence
```cpp
persist_queue():  // Called on every mutation
  encode track IDs as newline-separated string → storage preference
  save queue_index
restore_saved_queue():  // On deferred init
  batch lookup via storage_->get_tracks_by_ids()  // Single query
  validate IDs still exist → QueueManager::set_queue()
```

### 1.8 Seek Debouncing (200ms)
```cpp
seek(position):
  pending_seek_position_ = clamped
  seek_pending_ = true
  ++seek_generation_  // Invalidate in-flight responses
  position_ = pending_seek_position_  // Optimistic UI update
  emit_position_changed()
  g_timeout_add(200ms, seek_debounce_timeout) → actual pipeline seek
```
**Why**: Prevents pipeline flooding during scrubber drag; UI stays responsive.

---

## 2. EventBus (event_bus.hpp/cpp)

### 2.1 Dual Event Classes
| Class | Semantics | Use Cases |
|-------|-----------|-----------|
| **FIFO** | Ordered, every event delivered | State changes, track changes, errors, queue mutations, scan completion |
| **Conflated** | Latest value per type wins | Position updates, spectrum frames, buffering % |

### 2.2 Thread-Safe Delivery
```cpp
publish_fifo(event):
  lock → fifo_queue_.push_back → dispatcher.emit()

drain() (on GTK main context via dispatcher):
  swap queues under lock
  deliver conflated first (freshest), then FIFO in order
```

### 2.3 Handler Safety
```cpp
deliver(event):
  copy handler list under lock  // Prevents unsubscribe during iteration
  invoke each with try/catch
```

---

## 3. AudioEngine (audio_engine.hpp/cpp)

### 3.1 Pipeline Architecture (No playbin)
```
filesrc → decodebin → audioconvert → audioresample → tee
    ├── viz_queue (leaky=downstream) → viz_audioconvert → viz_audioresample → capsfilter(S16LE/44.1k/2ch) → appsink (max-buffers=1, drop=true, sync=false)
    └── audio_queue → volume → pulsesink/autoaudiosink
```

**Why custom pipeline**: playbin wrapper caused heap-use-free crashes. This design isolates visualization branch.

### 3.2 Visualization Sample Path (Streaming Thread → VisualizerEngine)
```cpp
appsink "new-sample" callback (GStreamer streaming thread):
  pull sample → map buffer (S16LE interleaved stereo)
  mix down to mono float: (L+R)/2 / 32768.0
  call viz_cb(samples, count)  // AudioEngine::Impl::viz_cb
    → VisualizerEngine::Impl::ring.push(sample)  // Lock-free SPSC
```

### 3.3 State Management
- **Generation tokens**: `load_generation`, `seek_generation` invalidate stale async operations
- **State transition table** (compile-time): Rejects invalid transitions
- **Bus watch** (GLib main context): Handles STATE_CHANGED, ASYNC_DONE, EOS, ERROR, BUFFERING, DURATION_CHANGED

### 3.4 Seek Implementation
```cpp
seek(position):
  ++seek_generation
  pending_seek_position = target_ns
  seek_pending = true
  gst_element_seek_simple(FLUSH | KEY_UNIT | ACCURATE)
  // position_timeout() reports pending_seek_position until ASYNC_DONE clears seek_pending
```

### 3.5 Position Polling
100ms `g_timeout_add` → `update_position()` → queries pipeline → `position_cb`
Reports `pending_seek_position` during seek for smooth scrubber.

---

## 4. PlaybackSourceManager (playback_source_manager.hpp/cpp)

### 4.1 Responsibility
Single active source: `Internal` (AudioEngine) or `External` (MprisManager).

### 4.2 Capability Queries (Delegated)
```cpp
can_seek() → active_source->can_seek()
can_go_next() → active_source->can_go_next()
```

### 4.3 Control Routing
All `play()`, `pause()`, `seek()`, `set_volume()` forward to active source.

---

## 5. MprisManager (mpris_manager.hpp/cpp)

### 5.1 Player Discovery
- **Single `NameOwnerChanged` subscription** on session bus (well-known names only)
- `ListNames` async → `apply_player_list()` filters:
  - Excludes Lollypop itself
  - Filters `playerctld` mirrors
  - Filters non-instance mpd buses
  - Hides native browser buses when plasma-browser-integration active

### 5.2 Active Player Selection (Priority Order)
1. Manual (`set_active_player()`)
2. Preferred (config substring match on identity/desktop_entry/dbus_name)
3. MRU playing (most recently played that's currently playing)
4. Tracked (last seen playing)
5. First available

### 5.3 Async D-Bus with Generation Tokens
```cpp
query_position_async(name, gen, cb):
  props->call("Get", ...)  // Async
  on reply: if gen != current_generation → drop
  cb(position)

call_active_async(method):
  ++request_generation_
  player->call(method, ...)  // Fire-and-forget
```

**Critical**: Prevents stale replies from previous player or superseded request.

### 5.4 Position Polling (500ms)
```cpp
poll_position():
  if position_poll_outstanding_.exchange(true) return;  // One at a time
  gen = ++position_poll_generation_
  query_position_async(active_player, gen, [this, gen](pos) {
    if gen != current → drop
    position_poll_outstanding_ = false
    update entry + position_cb
  })
```

### 5.5 Property Change Handling
`PropertiesChanged` signal on active player → `read_props()` → callbacks:
- `track_cb` (metadata changed)
- `playback_state_cb` (Playing/Paused/Stopped)
- `volume_cb`, `loop_state_cb`, `shuffle_cb`
- `active_player_cb` (always emitted)

---

## 6. MprisServer (mpris_server.hpp/cpp)

### 6.1 Exported Interface
`org.mpris.MediaPlayer2.lollypop` on session bus with:
- Root interface: Identity, DesktopEntry, SupportedUriSchemes/MimeTypes
- Player interface: PlaybackStatus, LoopStatus, Shuffle, Metadata, Volume, Position, Capabilities (CanGoNext, CanPlay, etc.)
- Methods: Next, Previous, Pause, Play, PlayPause, Stop, Seek (relative), SetPosition (absolute + track_id), OpenUri, SetVolume
- Signal: Seeked(position_us)

### 6.2 Track Identity for Queue Entries
```cpp
track_object_path(track):  // Sanitizes track.id to valid D-Bus object path
  /org/lollypop/track/<sanitized_id>

build_metadata(track, entry_id):  // entry_id = queue entry identity
  mpris:trackid = entry_id ? /org/lollypop/entry/<entry_id> : track_object_path(track)
```
**Why**: Same track appearing multiple times in queue gets distinct MPRIS paths.

### 6.3 Property Updates (Backend → MPRIS)
BackendController calls:
- `update_playback_status(bool)` → emits PropertiesChanged(PlaybackStatus)
- `update_track(track, entry_id)` → emits PropertiesChanged(Metadata) + Duration
- `update_position()` → caches only (no emit - would flood 10x/sec)
- `notify_seeked()` → emits Seeked signal + Position property
- `update_capabilities()` → emits 5 boolean properties

### 6.4 Remote Control Callbacks (MPRIS → Backend)
```cpp
set_playback_state_changed_callback:  // Media keys Play/Pause
  if playing: handle resume/start logic
  else: pause()

set_seek_callback: seek(position_ + offset)  // Relative
set_set_position_callback: seek(absolute)    // Absolute with track_id validation
set_volume_callback: set_volume()
set_next/previous/stop/open_uri_callbacks: forward to controller
```

**Feedback Loop Prevention**: `on_set_property` for Shuffle/LoopStatus/Volume does **not** emit PropertiesChanged — backend will call `update_shuffle()` etc. which emits.

---

## 7. LibraryManager (library_manager.hpp/cpp)

### 7.1 Single-Writer Pattern
All database writes happen on **one worker thread**. `StorageManager` only touched from this thread.

### 7.2 Unique Work Queue (No Duplicate Paths)
```cpp
scan_directory(path):
  lock queue_mutex
  if pending_paths_.count(path) return true  // Coalesce
  queue_.push(ScanJob{Directory, path})
  pending_paths_.insert(path)
```

### 7.3 Batch Transactions
```cpp
process_job(job):
  storage_->begin_transaction()
  scanner_->scan_directory(...)  // Emits TrackFoundCallback per track
  if cancelled: storage_->rollback_transaction()
  else: storage_->commit_transaction()
  finished_cb(success, cancelled)
```

### 7.4 Job Types
- `Directory` (recursive)
- `File` (single)
- `Reconcile` (full rescan after IN_Q_OVERFLOW)

### 7.5 Callbacks to Controller
- `track_cb` → `storage_->add_track(track)` (under storage_mutex_)
- `progress_cb` → dispatcher emit
- `finished_cb` → dispatcher emit with result

---

## 8. LibraryScanner (library_scanner.hpp/cpp)

### 8.1 Incremental Scanning (mtime + size cache)
```cpp
is_file_unchanged(file_path):
  stat file → mtime, size
  lock cache_mutex
  return cache[path].mtime == mtime && cache[path].size == size
```
Cache persisted to disk (`path|mtime|size` lines) on scan complete.

### 8.2 Metadata Extraction (TagLib)
- **Format-specific artwork**: ID3v2 APIC (MP3), FLAC PICTURE, MP4 covr, Ogg METADATA_BLOCK_PICTURE (base64 FLAC)
- **Album artist**: PropertyMap "ALBUMARTIST", "ALBUM ARTIST", Vorbis "ALBUM ARTIST"
- **Embedded lyrics**: LYRICS, UNSYNCEDLYRICS, SYNCEDLYRICS, Vorbis "UNSYNCED LYRICS"

### 8.3 Stable Track ID (Deterministic UUID v4)
```cpp
generate_id(canonical_path):
  hash1 = fnv1a(path)
  hash2 = fnv1a(path + "|lollypop")
  Format as UUID v4 with version=4, variant=10xx bits
```
Same file → same ID across scans/restarts.

### 8.4 Progress Throttling
100ms minimum interval between progress callback emits.

---

## 9. QueueManager (queue_manager.hpp/cpp)

### 9.1 Data Structures
```cpp
deque<string> queue_        // Main queue (track IDs)
deque<string> play_next_    // "Play next" FIFO (inserted at current_index+1)
size_t current_index_       // Cursor into queue_
vector<size_t> shuffle_order_  // Fisher-Yates permutation
size_t shuffle_pos_         // Cursor in shuffle_order_
bool shuffle_dirty_         // Rebuild on next access
```

### 9.2 Next/Previous Logic
```cpp
next_index():
  if repeat_track: return current
  if play_next not empty: insert at current+1, return it
  if shuffle:
    ++shuffle_pos_
    if at end:
      if repeat_playlist: reshuffle, wrap
      else: return nullopt
    return shuffle_order[shuffle_pos_]
  // Sequential
  if current+1 < size: return ++current
  if repeat_playlist: return 0
  return nullopt

previous_index():
  // 3-second restart logic handled in controller
  if shuffle: --shuffle_pos_, wrap on repeat
  else: --current, wrap on repeat
```

### 9.3 Shuffle (Fisher-Yates)
```cpp
ensure_shuffle_order():
  for i in 0..size-1: shuffle_order[i] = i
  for i = size; i > 1; --i:
    j = random(0, i-1)
    swap(shuffle_order[i-1], shuffle_order[j])
  shuffle_pos_ = 0
```

---

## 10. VisualizerEngine (visualizer_engine.hpp/cpp)

### 10.1 Pipeline
```
GStreamer appsink (streaming thread)
    → lock-free SPSC ring buffer (8192 float samples)
    → FFT worker thread (1024-frame Hann window → FFT → log bands)
    → mutex-protected latest_spectrum snapshot (EMA smoothing, α=0.6)
    → GTK draw callback polls get_latest_spectrum() ~30 FPS
```

### 10.2 Lock-Free Ring Buffer (SPSC)
```cpp
template<T, Capacity>  // Power-of-two capacity
push():  wr = write_index.load(relaxed); rd = read_index.load(acquire)
         if (wr+1)%Cap == rd: return false (full)
         buffer[wr] = sample; write_index.store((wr+1)%Cap, release)
pop():   rd = read_index.load(relaxed); wr = write_index.load(acquire)
         if rd == wr: return false (empty)
         sample = buffer[rd]; read_index.store((rd+1)%Cap, release)
```

### 10.3 FFT Worker (~30 FPS)
```cpp
while running:
  fill frame[1024] from ring (sleep 200µs if empty)
  apply Hann window
  fft_in_place()  // Iterative radix-2
  magnitudes = |fft| * (2/N)
  band_map() → 64 log bands (40Hz - Nyquist)
  ema_smooth(current, previous, 0.6)
  publish latest_spectrum (lock data_mutex)
  EventBus.publish_conflated("spectrum", frame)
  sleep to maintain 33ms frame interval
```

### 10.4 Lifetime Safety
```cpp
shared_ptr<atomic<bool>> alive = make_shared<atomic<bool>>(true);
// appsink callback captures alive:
engine->set_viz_samples_callback([this, alive](samples, count) {
  if (!alive->load() || !running || !enabled) return;
  ring.push(samples);
});
// Destructor: alive->store(false) before join
```

---

## 11. CavaVisualizer (cava_visualizer.hpp/cpp)

### 11.1 Subprocess Management
- Runs `cava -p config` via `Gio::Subprocess`
- STDOUT_PIPE + STDERR_SILENCE (prevents stderr blocking)
- Config: `method=raw`, `data_format=ascii`, `channels=mono`, `raw_target=/dev/stdout`

### 11.2 Output Parsing
```
CAVA output: "0;0;1;2;5;2;1;0;\n"
Reader thread: read_line() → parse semicolon floats → clamp [0, 1000]
                lock points_mutex, swap pending_points, dispatcher.emit()
GTK thread: dispatch_points() → points_cb(points)
```

### 11.3 Secure Config Generation
- `mkstemp()` in XDG_RUNTIME_DIR or ~/.cache/lollypop-cpp
- Validates bars (1-512), framerate (1-240)
- Auto-cleanup on stop

---

## 12. FileWatcher (file_watcher.hpp/cpp)

### 12.1 inotify with Recursive Watches
```cpp
add_watch_locked(path, recursive):
  wd = inotify_add_watch(fd, path, CREATE|DELETE|CLOSE_WRITE|MOVED_FROM|MOVED_TO|DELETE_SELF|MOVE_SELF|Q_OVERFLOW)
  if recursive: recursive_directory_iterator → add watch for each subdirectory
```

### 12.2 Event Debouncing (150ms)
```cpp
debounce_or_emit(events, debounce_map, path, type):
  debounce_map[path] = {path, type, now}
// On poll timeout or event loop:
flush_debounce():
  for entries older than 150ms: emit, erase
```

### 12.3 Move Event Pairing (Cookie)
```cpp
IN_MOVED_FROM: pending_moves[cookie] = {path, now, is_dir}
IN_MOVED_TO: if pending_moves[cookie] exists:
              emit Moved(from, to)
            else: emit Created(to)
```

### 12.4 IN_Q_OVERFLOW Handling
Emits `Rescan` event → Controller triggers full library reconciliation.

### 12.5 Clean Shutdown
`eventfd` + `poll()` → write to eventfd wakes poll → thread exits cleanly.

---

## 13. ArtworkManager (artwork_manager.hpp/cpp)

### 13.1 Priority Queue
```cpp
ArtworkRequest::Priority: Background=0, NextTrack=1, NowPlaying=2
priority_queue<QueuedRequest>  // Highest priority extracted first
```

### 13.2 In-Flight Deduplication
```cpp
request_artwork(req, cb):
  if cached: cb immediately
  lock in_flight_mutex
  if key in in_flight_: pending_callbacks[key].push(cb); return
  in_flight_.insert(key); pending_callbacks[key] = {cb}
  lock queue_mutex; queue_.push(queued); notify
```

### 13.3 Extraction & Caching
```cpp
process_request(queued):
  if cached: use it
  else:
    extract_fn(file_path, key, max_size)  // TagLib embedded
    or extract_folder_artwork()  // cover.jpg, folder.jpg, etc.
    copy to cache_path_for_key(key)
    cache_->put(key, cached_path, byte_size)
  // Fire ALL pending callbacks
  lock in_flight_mutex; cbs = pending_callbacks[key]; erase; in_flight_.erase
  for cb in cbs: cb(key, artwork_path)
```

### 13.4 CacheManager Integration
- Byte-budgeted LRU (hash map + doubly linked list)
- Disk thumbnails keyed by (hash, size)
- TTL support, orphan cleanup

---

## 14. LyricsManager (lyrics_manager.hpp/cpp)

### 14.1 Priority Sources
1. **Embedded** (audio file tags) - highest priority
2. **Local .lrc** file next to audio
3. **External fetch** (caller-supplied `FetchFn` - web API)

### 14.2 LRC Parsing
```cpp
parse_lrc(text):
  for each line:
    extract all [mm:ss.xx] timestamps
    text = remainder after last timestamp
    create LyricLine for each timestamp with same text
  sort by start_time_seconds
current_line(lines, position):
  return last line with start_time <= position
```

### 14.3 Worker Thread + Cache/Storage
```cpp
worker_loop():
  pop request
  try embedded → local lrc → fetch_fn
  if found: set_lyrics() → persist to storage + cache
  callback(req.track_id, lyrics)
```

### 14.4 Dual Cache (CacheManager + StorageManager)
- `get_lyrics()`: checks CacheManager first (LRU), falls back to StorageManager
- `set_lyrics()`: writes to both

---

## 15. CacheManager (cache_manager.hpp/cpp)

### 15.1 LRU Implementation
```cpp
struct Node { key, entry, prev, next }
unordered_map<string, Node*> map_
head_ (MRU) ↔ tail_ (LRU) doubly linked list
```

### 15.2 Byte Budget Eviction
```cpp
put(key, file_path, byte_size):
  if exists: update, byte_used -= old_size, detach
  evict_to_fit(byte_size): while tail && byte_used + needed > budget: evict tail
  attach_head(new_node); byte_used += byte_size
```

### 15.3 TTL & Expiry
```cpp
get(key):
  if ttl > 0 && now - created_at > ttl: evict, return false
  promote to head, update accessed_at
```

### 15.4 Orphan Cleanup
`clean_orphans()`: scan cache_dir, remove files not in `map_`.

---

## 16. Cross-Cutting Concerns

### 16.1 Thread Confinement Rules
| Subsystem | Thread | Notes |
|-----------|--------|-------|
| BackendController | Main (GLib) | Owns `playing_`, emits UI callbacks |
| AudioEngine | Main (control) + Streaming (GStreamer) | Streaming thread → appsink callback |
| VisualizerEngine | FFT worker + Main (poll) | SPSC ring between them |
| MprisManager | Main (GLib/DBus) | Async DBus calls with generation tokens |
| MprisServer | Main (GLib/DBus) | D-Bus method handlers |
| LibraryManager | Worker | Single writer to Storage |
| LibraryScanner | Worker (via LibraryManager) | |
| FileWatcher | Watch thread | Emits via callback → dispatcher |
| ArtworkManager | Worker | Priority queue + in-flight dedup |
| LyricsManager | Worker | Sequential fetch priority |
| CacheManager | Any (mutex-protected) | |
| EventBus | Any (publish) → Main (drain) | |

### 16.2 Synchronization Primitives
- **mutex + condition_variable**: Worker loops (LibraryManager, ArtworkManager, LyricsManager)
- **atomic**: Generation tokens, flags (`playing_`, `seek_pending_`, `running_`)
- **shared_mutex**: FileWatcher callback replacement
- **Glib::Dispatcher**: Worker → Main marshaling
- **Lock-free SPSC**: VisualizerEngine ring buffer

### 16.3 Generation Token Pattern (Async Safety)
Used in: AudioEngine (`load_generation`, `seek_generation`), MprisManager (`request_generation_`, `position_poll_generation_`), MprisServer (implicit via track validation).
**Principle**: Tag async request with incrementing counter; drop response if counter changed.

### 16.4 Callback Safety
All callback vectors use `if (cb) cb(...)` guards. EventBus copies handler list before invocation. ArtworkManager/LyricsManager wrap callbacks in try/catch.

### 16.5 Error Handling
- TagLib errors → `error_cb(file, message)` → logged
- GStreamer errors → `error_cb` → `set_playing(false)` → MPRIS stopped
- D-Bus errors → logged, async calls fire-and-forget with generation drop
- Cache/storage failures → logged, operation returns false

---

## 17. Data Flow Summary

### 17.1 Play Track (Internal)
```
Controller.play_tracks(ids, start)
  → QueueManager.set_queue()
  → start_queue_index()
    → AudioEngine.load_track(path)
      → Builds pipeline, prerolls to PAUSED
    → AudioEngine.play()
      → set_playing(true) → emits playback_state_changed
      → MprisServer.update_playback_status(true)
      → MprisServer.update_track(current_track, entry_id)
    → AudioEngine bus: ASYNC_DONE → position_cb → emit_position_changed
    → AudioEngine appsink → VisualizerEngine ring → FFT → spectrum → EventBus → UI
    → EOS → next_track() → start_queue_index()
```

### 17.2 Library Scan
```
Controller.start_scan_library(path)
  → LibraryManager.scan_directory(path)
    → Worker thread: scanner.scan_directory()
      → TagLib extraction per file
      → track_cb(track) → storage.add_track() (batched transaction)
      → progress_cb → dispatcher → Controller UI callback
    → finished_cb → dispatcher → Controller
      → storage.add_library_root(path)
      → FileWatcher.add_watch(path)
      → emit_library_changed()
```

### 17.3 File Change (inotify)
```
FileWatcher thread: inotify event
  → debounce/pair moves
  → emit(FileEvent) → Controller.handle_file_event()
    → lock file_events_mutex, push, dispatcher.emit()
Main thread: dispatch_file_events()
  → process_file_event()
    → Created/Modified: LibraryManager.scan_file/directory
    → Deleted/Moved: storage.delete_track_by_path
    → emit_library_changed()
```

### 17.4 External MPRIS Control
```
User presses media key
  → DBus → MprisServer.on_method_call("PlayPause")
    → playback_state_cb(true)
      → Controller: audio_engine.play(); set_playing(true)
  → MprisServer.update_playback_status(true) → PropertiesChanged
  → Sound menu updates
```

---

## 18. Build System (CMakeLists.txt)

**Key dependencies**: gstreamer-1.0, gstreamer-audio-1.0, gstreamer-app-1.0, taglib, glibmm-2.68, giomm-2.68, sqlite3

**Targets**: `lollypop-backend` (static library), links all `.cpp` files in `src/`

---

## 19. Identified Strengths

1. **Clean separation**: Each subsystem has single responsibility
2. **Thread safety**: Explicit confinement, generation tokens, lock-free where needed
3. **Async correctness**: D-Bus async with generation drops, seek debouncing, conflated events
4. **Resource management**: RAII throughout, proper shutdown sequences
5. **Extensibility**: Callback multicast, interface-based services
6. **Performance**: Incremental scanning, byte-budgeted cache, SPSC ring, batched DB writes

---

## 20. Potential Improvement Areas

1. **VisualizerEngine**: Hardcoded 44.1kHz sample rate; should query pipeline caps
2. **AudioEngine**: No gapless playback support (pipeline rebuild per track)
3. **LibraryManager**: No incremental scan scheduling (manual only)
4. **MPRIS**: No support for `Raise` method or `CanControl`=false edge cases
5. **Error Recovery**: AudioEngine shutdown on error could attempt pipeline rebuild
6. **Testing**: No visible unit/integration test infrastructure

---

*Analysis based on source code review of all 15 backend modules (headers + implementations) as of the current repository state.*