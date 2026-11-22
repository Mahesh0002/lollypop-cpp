# Music Player — Complete Backend Implementation Plan

**Architecture, implementation roadmap, data structures, concurrency, bug fixes and testing strategy**

| Property | Technology |
|---|---|
| Operating System | Linux / Arch Linux |
| Frontend | GTK 4 |
| Backend | C++ |
| Database | SQLite + FTS5 |
| Audio Engine | GStreamer |
| Metadata Extraction | TagLib |
| External Player Integration | MPRIS / D-Bus |
| Filesystem Monitoring | Linux inotify |
| Audio Visualization | GStreamer appsink + FFT |
| Optional System Visualization | CAVA |
| Build System | CMake |

---

# 1. Project Overview

The application will use a modular C++ backend responsible for:

- Audio playback.
- Music library management.
- Metadata extraction.
- Database storage and querying.
- Playback queue management.
- Internal and external playback control.
- Artwork extraction and caching.
- Lyrics retrieval and synchronization.
- Audio visualization.
- Filesystem monitoring.
- MPRIS integration.
- Playback history.
- Smart recommendations and radio features.

The GTK 4 frontend must communicate with the backend through `BackendController`.

It should not directly communicate with GStreamer, SQLite, TagLib, inotify, or individual MPRIS players.

## 1.1 Core architectural principles

1. Keep frontend and backend independent.
2. Keep `BackendController` as the frontend-facing facade.
3. Keep database operations inside `StorageManager`.
4. Separate internal playback from external MPRIS playback.
5. Ensure only one playback source is active at a time.
6. Use asynchronous processing for expensive operations.
7. Use bounded queues wherever incoming work can exceed processing capacity.
8. Use a single SQLite writer.
9. Deliver frontend events through the GTK main context.
10. Keep real-time audio callbacks free from blocking operations and dynamic allocation.
11. Give every subsystem, thread, callback, and resource an explicit owner.
12. Test individual subsystems before integrating them.
13. Preserve existing working implementations unless a concrete defect requires a change.

---

# 2. Current Implementation Status

The status must distinguish between an API existing and the complete functionality behind that API being implemented.

| Component | Current status | Description |
|---|---|---|
| SQLite Storage | COMPLETE | Existing database implementation |
| Query Delegation | COMPLETE | BackendController delegates database queries to StorageManager |
| BackendController API | IMPLEMENTED — NOT YET VERIFIED COMPLETE | Public API exists; full subsystem integration and tests remain |
| Playback State Machine | PARTIAL | Initial state handling exists; complete source arbitration remains |
| AudioEngine | PENDING | GStreamer playback |
| QueueManager | PENDING | Queue entries, shuffle, repeat and history |
| PlaybackController | PENDING | Playback coordination and source selection |
| LibraryScanner | PENDING / VERIFY EXISTING CODE | TagLib scanning and stable track identity |
| FileWatcher | PENDING / VERIFY EXISTING CODE | inotify directory monitoring |
| LibraryManager | PENDING | Scan coordination and unique work queue |
| ArtworkManager | PENDING | Artwork extraction and caching |
| LyricsManager | PENDING | Lyrics retrieval and synchronization |
| CacheManager | PENDING | Shared cache infrastructure |
| EventBus | PENDING | Thread-safe backend event delivery |
| VisualizerEngine | PENDING | Audio sample processing and FFT |
| CAVA Integration | OPTIONAL / PENDING | System-wide audio visualization |
| MPRIS Client | PENDING | External-player discovery and control |
| MPRIS Server | PENDING | Expose this application as an MPRIS player |
| Position Polling | PENDING | Playback position updates |
| Smart Recommendation Engine | FUTURE | Co-play graph and similarity recommendations |
| Tests | PENDING | Unit, integration and lifecycle testing |

**Completion rule:** A component is COMPLETE only after its implementation, unit tests, lifecycle tests and relevant integration tests pass.

For BackendController, COMPLETE must not mean merely that its method signatures exist.

---

# 3. Overall Backend Architecture

```text
                         GTK 4 FRONTEND
                                |
                                v
                       BackendController
                                |
             +------------------+------------------+
             |                  |                  |
             v                  v                  v
      PlaybackController   LibraryManager    StorageManager
             |                  |                  |
       +-----+-----+       +----+------+           v
       |           |       |           |        SQLite
       v           v       v           v
  QueueManager  AudioEngine  FileWatcher  LibraryScanner
       |           |       |           |       |
       |           |       +-----+-----+       v
       |           |             |            TagLib
       |           |             v
       |           |       Unique Work Queue
       |           |
       |           +------ Audio Output
       |           |
       |           +------ Visualization Branch
       |                         |
       |                         v
       |                  Lock-Free SPSC Ring
       |                         |
       |                         v
       |                    FFT Worker
       |                         |
       |                         v
       |                    Triple Buffer
       |                         |
       |                         v
       |                    GTK Frontend
       |
       +------ MPRIS Server

      MPRIS Client <------> External MPRIS Players

      ArtworkManager / LyricsManager / CacheManager
                    |
                    v
              StorageManager

          All backend modules
                    |
                    v
                 EventBus
                    |
                    v
             GTK Main Context
```

This is a logical architecture. Modules can be implemented in the same process and linked through C++ interfaces.

## 3.1 Suggested directory structure

```text
backend/
|
+-- include/
|   |
|   +-- controller.hpp
|   +-- types.hpp
|   +-- events.hpp
|   |
|   +-- playback/
|   |   +-- playback_controller.hpp
|   |   +-- audio_engine.hpp
|   |   +-- queue_manager.hpp
|   |
|   +-- library/
|   |   +-- library_manager.hpp
|   |   +-- library_scanner.hpp
|   |   +-- file_watcher.hpp
|   |
|   +-- storage/
|   |   +-- storage_manager.hpp
|   |
|   +-- media/
|   |   +-- artwork_manager.hpp
|   |   +-- lyrics_manager.hpp
|   |   +-- cache_manager.hpp
|   |
|   +-- mpris/
|   |   +-- mpris_client.hpp
|   |   +-- mpris_server.hpp
|   |
|   +-- visualization/
|       +-- visualizer_engine.hpp
|       +-- cava_visualizer.hpp
|
+-- src/
|   |
|   +-- controller.cpp
|   +-- playback/
|   +-- library/
|   +-- storage/
|   +-- media/
|   +-- mpris/
|   +-- visualization/
|
+-- tests/
|
+-- CMakeLists.txt
```

Do not reorganize existing working source files unnecessarily. Adapt the structure to the current repository.

---

# 4. Completed Components

## 4.1 StorageManager — SQLite

**Status: COMPLETE**

StorageManager owns database operations.

It should remain independent of:

- GTK widgets.
- GStreamer.
- MPRIS.
- Filesystem monitoring.
- Visualization.

### Existing functionality

- SQLite database.
- Database schema.
- CRUD operations.
- Persistent storage.
- FTS5 search.
- StorageManager interface.

### Database responsibilities

The database should manage:

- Tracks.
- Albums.
- Artists.
- Genres.
- Playlists.
- Playlist tracks.
- Library locations.
- Playback history.
- Favourites.
- Application settings.
- Artwork references.
- Lyrics references.
- Queue persistence, if required.

### Required database hardening

The following should be verified rather than blindly reimplemented:

1. Database migrations and schema versioning.
2. `PRAGMA foreign_keys=ON`.
3. WAL mode.
4. Prepared statements.
5. Transactions.
6. FTS5 synchronization.
7. Read/write connection ownership.
8. Error handling and rollback.
9. Database recovery.
10. Tests for persistence.

### SQLite connection model

Use:

- One writer connection.
- A writer command queue.
- Read-only connections on worker threads.

The writer thread is the only thread that performs database writes.

This avoids concurrent write operations from the scanner, playlists, history manager and other modules.

WAL allows readers to operate alongside the writer.

## 4.2 BackendController

**Status: API IMPLEMENTED; COMPLETION NOT YET VERIFIED**

BackendController is the main interface between GTK and the backend.

### Responsibilities

- Expose playback commands.
- Expose track and album information.
- Expose library queries.
- Expose queue operations.
- Expose playback state.
- Expose playback position.
- Expose volume controls.
- Expose external-player information.
- Expose visualization data.
- Subscribe to backend events.
- Publish frontend-facing state changes.

### Important rule

BackendController coordinates modules. It must not contain their internal implementation.

Example:

```text
GTK
 |
 v
BackendController::play_track(track_id)
 |
 v
PlaybackController
 |
 +--> QueueManager
 |
 +--> AudioEngine
```

The controller must not implement SQL statements, GStreamer pipeline construction or inotify event processing.

### Completion requirements

The existing API must be tested against the actual implemented modules.

Until then, describe it as an implemented interface rather than a fully completed backend controller.

## 4.3 Query Delegation

**Status: COMPLETE**

The current flow is:

```text
GTK
 |
 v
BackendController
 |
 v
StorageManager
 |
 v
SQLite
```

Preserve this implementation.

Do not duplicate SQL queries inside BackendController or GTK.

---

# 5. Playback Source Arbitration

**Priority: CRITICAL**

The application has two possible playback sources:

1. Internal playback through GStreamer.
2. External playback through MPRIS.

The application must never accidentally play both sources simultaneously.

## 5.1 Source states

```cpp
enum class PlaybackSource {
    None,
    Internal,
    External
};
```

There are three source states:

- None.
- Internal.
- External.

These represent the selected playback source, not the detailed playback status.

## 5.2 Source transition table

| Current source | Requested event | New source | Required side effect |
|---|---|---|---|
| None | Select internal track | Internal | Start AudioEngine |
| None | Select external player | External | Select external player |
| Internal | Select external player | External | Pause AudioEngine |
| External | Select internal track | Internal | Send MPRIS Pause, then start AudioEngine |
| Internal | Internal playback ends | Internal / None | Apply queue policy |
| External | External player disappears | External / None | Re-evaluate active external player |
| External | Another player becomes active | External | Update selected external player |
| Any | Shutdown | None | Stop operations and release resources |

### Important handoff rule

When switching from Internal to External:

```cpp
audio_engine.pause();
```

Do not stop the engine. Pausing preserves the internal track's position.

When switching from External to Internal:

```text
1. Send Pause to active MPRIS player.
2. Confirm or handle the command result.
3. Start internal playback.
4. Update the selected source.
```

The exact transition ordering should be implemented as one controlled operation, not scattered callbacks.

## 5.3 Inactive-source event rejection

The playback reducer must ignore events from a source that is no longer active.

```cpp
if (event.source != active_source) {
    return;
}
```

This prevents:

- Delayed Spotify events overwriting internal playback state.
- Old GStreamer events overwriting external playback state.
- Position updates from a previous player changing the current UI.
- Stale callbacks changing the selected track.

This is the structural fix for the source-switching defects associated with Bugs 49 and 52.

---

# 6. Playback State Machine

**Priority: CRITICAL**

The source state and playback state must be separate concepts.

Do not combine source selection, buffering and seeking into one oversized enum.

## 6.1 Playback state

```cpp
enum class PlaybackState {
    Stopped,
    Loading,
    Playing,
    Paused,
    Ended,
    Error
};
```

## 6.2 Orthogonal flags

```cpp
struct PlaybackFlags {
    bool buffering = false;
    bool seeking = false;
};
```

Buffering and Seeking are flags because they can occur alongside the underlying playback state.

For example:

```text
PlaybackState = Playing
buffering = true
seeking = false
```

## 6.3 Transition table

Define allowed playback-state transitions explicitly.

```cpp
constexpr bool kAllowedTransitions[N][N] = {
    // Define every permitted from -> to transition.
};
```

The transition function should reject invalid transitions:

```cpp
bool transition(PlaybackState next)
{
    const auto from = static_cast<std::size_t>(current_state);
    const auto to   = static_cast<std::size_t>(next);

    if (!kAllowedTransitions[from][to]) {
        return false;
    }

    current_state = next;
    return true;
}
```

The actual table must be fully initialized and tested.

### Important rule

Do not manually set the playback state to Playing before GStreamer confirms the corresponding state transition.

Use GStreamer bus messages and actual state changes as the authority for internal playback state.

For external players, use the MPRIS PlaybackStatus property.

---

# 7. AudioEngine — GStreamer

**Priority: CRITICAL**

AudioEngine owns internal playback.

## 7.1 Dependencies

Required:

- `gstreamer-1.0`
- `gstreamer-audio-1.0`
- `gstreamer-pbutils-1.0`

Runtime plugins must be installed for the formats the application supports.

Optional:

- `gstreamer-fft-1.0` if GStreamer's FFT implementation is used.

## 7.2 Pipeline design

The audio pipeline must have two separate branches:

- Output branch.
- Visualization branch.

**Volume must be applied to the output branch, not before the tee.**

Otherwise, visualization amplitude changes with the user's volume setting and can become nearly flat at low volume.

Check whether playbin's own volume property already provides the required output control. Avoid stacking two independent volume stages.

### Recommended topology

```text
                     GStreamer playbin
                             |
                        Audio Sink Bin
                             |
                       Audio Convert
                             |
                       Audio Resample
                             |
                            Tee
                         /       \
                        /         \
                       v           v
                 Output Queue   Visual Queue
                       |           |
                    Volume       Leaky Queue
                       |           |
                       v           v
                  Audio Sink     Appsink
                       |           |
                       v           v
                   PipeWire    SPSC Ring Buffer
                                   |
                                   v
                               FFT Worker
                                   |
                                   v
                              Triple Buffer
                                   |
                                   v
                                GTK UI
```

The exact placement of convert/resample and volume should be validated against the chosen GStreamer sink-bin design.

## 7.3 Critical visualizer isolation

The visualization branch must never delay the audio output branch.

Set the visualization queue to use:

```text
leaky=downstream
```

Configure appsink:

```text
max-buffers=1
drop=true
sync=false
```

This ensures that stale visualization buffers are discarded rather than accumulating and delaying processing.

The output branch must remain independent.

## 7.4 GStreamer pad management

Each tee branch needs its own requested source pad.

Use:

```cpp
gst_element_request_pad_simple(tee, "src_%u");
```

Do not use the deprecated `gst_element_get_request_pad()` API.

The output branch and visualization branch must have separate tee pads.

Release requested pads during teardown.

## 7.5 Explicit pipeline graph

Represent pipeline connections as an explicit edge list:

```cpp
struct PipelineEdge {
    GstElement* source;
    GstElement* destination;
};
```

Link every declared edge in a loop.

```cpp
for (const auto& edge : edges) {
    if (!gst_element_link(edge.source, edge.destination)) {
        // Pipeline construction failure.
    }
}
```

Before moving the pipeline to PLAYING:

1. Verify required elements exist.
2. Verify all links succeeded.
3. Verify both tee branches are connected.
4. Verify the output branch reaches the audio sink.
5. Verify the visualization branch reaches appsink.
6. Verify all requested pads are released correctly on failure.

Use a graph reachability check as an additional structural validation, but remember that successful graph connectivity alone does not prove negotiated caps or runtime playback will succeed.

## 7.6 Pipeline graph inspection

Enable GStreamer's DOT graph output:

```bash
export GST_DEBUG_DUMP_DOT_DIR=/tmp/gst-dot
```

Run the application and inspect the generated graph.

This is particularly useful for finding missing elements and missing links.

## 7.7 AudioEngine responsibilities

- Initialize GStreamer.
- Construct and own the pipeline.
- Load audio files.
- Play.
- Pause.
- Stop.
- Seek.
- Query duration.
- Query position.
- Control volume.
- Handle errors.
- Handle end-of-stream.
- Handle buffering.
- Publish playback events.
- Provide samples to VisualizerEngine.
- Shut down safely.

## 7.8 Known defects and fixes

### Bug A1 — PlaybackState declaration order

Declare the playback-state enum before callback aliases that use it.

### Bug A2 — Volume element not connected

A created volume element must be added and linked into the output path.

### Bug A3 — audioconvert and audioresample not linked

Add both elements to the correct bin and verify their links.

### Bug A4 — Tee pad reused

Request one independent tee pad for each branch.

Use `gst_element_request_pad_simple()`.

### Bug A5 — Appsink callback missing (RETIRED - see R-01 design)

The old approach of connecting `new-sample` with `Impl*`/`AudioEngine*` cast was retired (see critique.md BUG-02).
Current design (R-01): Connect safely with `Impl*`/owner, lifetime token, main-context marshalling.
VisualizerEngine or CavaVisualizer should be constructed in BackendController::initialize() and
connected to the appsink with proper lifetime guards.

### Bug A6 — Element creation failures

Check every element creation result.

If an element cannot be created, stop pipeline construction and clean up everything allocated so far.

### Bug A7 — URI conversion unchecked

Validate the file path and URI conversion before assigning it to playbin.

### Bug A8 — Invalid playback-state transitions

Use the explicit transition table and actual backend state notifications.

### Bug A9 — Buffering and duration handling

Handle relevant bus messages and refresh duration when it becomes available.

### Bug A10 — Commands before initialization

Guard playback operations against uninitialized and shutting-down pipeline states.

### Bug A11 — Incomplete shutdown

Make shutdown idempotent and release every owned resource.

### Bug A12 — Position ownership

AudioEngine is the source of truth for internal playback position.

### Bug A13 — Visualization backpressure

The visualization branch must use a leaky queue and one-buffer appsink.

### Bug A14 — Dynamic allocation in streaming callback

The streaming callback must copy samples into preallocated storage without `malloc` or mutex locking.

---

# 8. VisualizerEngine

**Priority: HIGH**

VisualizerEngine processes audio samples independently of the audio output path.

## 8.1 Audio handoff

The `new-sample` callback must:

1. Pull the sample.
2. Map the buffer.
3. Copy the sample data into a preallocated ring buffer.
4. Unmap the buffer.
5. Release the sample.
6. Return quickly.

It must not:

- Allocate memory dynamically.
- Acquire a mutex.
- Run FFT.
- Call GTK.
- Wait for the UI.
- Perform file operations.

## 8.2 SPSC ring buffer

Use a preallocated, lock-free Single Producer Single Consumer ring buffer.

Properties:

- Power-of-two capacity.
- Atomic head and tail indexes.
- Fixed-size storage.
- No dynamic allocation after initialization.
- No mutex.
- One producer: GStreamer streaming callback.
- One consumer: FFT worker.

Conceptual structure:

```cpp
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    // Preallocated storage.
    // Atomic producer and consumer indexes.
};
```

### Overflow policy

Drop the oldest data when the buffer becomes full.

The implementation must use a correct atomic protocol for producer/consumer coordination. Do not implement this by casually allowing both threads to write the same index.

Use sequence counters or an appropriate compare-and-exchange design to ensure that dropping old data cannot race with the consumer.

The objective is to preserve recent audio samples for visualization rather than allowing a large stale backlog.

## 8.3 FFT worker

The FFT worker:

1. Reads samples from the SPSC buffer.
2. Applies the selected window function.
3. Performs FFT.
4. Calculates frequency magnitudes.
5. Normalizes the spectrum.
6. Applies smoothing.
7. Produces a visualization frame.
8. Publishes the frame.

The FFT worker must not call GTK directly.

## 8.4 Triple buffering

Use a triple buffer for transferring completed spectrum frames to the UI.

```text
FFT Worker
    |
    v
Buffer A / B / C
    |
    v
GTK Draw Callback
```

The worker publishes completed frames.

The GTK draw callback reads the latest completed frame without waiting for FFT processing.

If the UI misses frames, it simply renders the newest available frame.

This prevents visualization processing from blocking drawing.

## 8.5 CAVA integration

CAVA is appropriate for system-wide audio capture.

This makes it useful for visualizing external MPRIS players because their audio is not passing through this application's internal GStreamer pipeline.

For internal playback, use the samples already available through appsink.

Do not use CAVA as the only visualization mechanism for both sources.

Define the visualization source explicitly:

| Playback source | Visualization source |
|---|---|
| Internal | GStreamer appsink + in-process FFT |
| External | CAVA/system audio capture, if enabled |
| None | Empty or idle visualization |

---

# 9. EventBus

**Priority: CRITICAL**

EventBus transports events between backend modules and the GTK frontend.

Use an MPSC queue drained on the GTK main thread.

Possible delivery mechanisms:

- `g_main_context_invoke()`
- `g_idle_add()`

## 9.1 Two event classes

Do not treat every event as an ordinary FIFO event.

Split events into two categories.

### Class A — FIFO events

These events must preserve ordering:

- Playback state changes.
- Track changes.
- Errors.
- Queue mutations.
- Player appearance/disappearance.
- Library scan completion.
- Important user-visible operations.

Use an ordered event queue.

### Class B — Conflated events

These events only need their newest value:

- Position.
- Spectrum frame.
- Buffering percentage.
- Other rapidly changing continuous values.

Maintain one latest-value slot per event type.

If the UI is slow, a newer position replaces the old position rather than building a backlog.

## 9.2 Main-thread delivery

The EventBus must ensure GTK-facing callbacks execute on the GTK main context.

Worker threads should publish events, not invoke widgets directly.

Schedule the main-context drain efficiently. Avoid scheduling a new idle callback for every sample or every position update when one drain is already pending.

## 9.3 Position interpolation

Do not emit position updates at 30–60 Hz merely to animate the seek bar.

Publish:

```cpp
struct PositionUpdate {
    int64_t position_us;
    std::chrono::steady_clock::time_point timestamp;
    double playback_rate;
};
```

The UI interpolates position using its own frame clock:

```text
Displayed Position =
    Received Position +
    Elapsed Time × Playback Rate
```

Reset the interpolation anchor when:

- A new track starts.
- Playback pauses.
- Playback resumes.
- Seeking begins or completes.
- Playback source changes.
- Playback rate changes.

The backend sends occasional authoritative updates. The UI handles smooth visual movement.

---

# 10. LibraryScanner

**Priority: HIGH**

LibraryScanner extracts metadata and discovers supported music files.

It should not perform the entire scan synchronously.

## 10.1 Scan pipeline

Use bounded queues and separate processing stages:

```text
Directory Walker
      |
      v
Bounded MPMC Queue
      |
      v
2–4 TagLib Workers
      |
      v
Bounded Result Queue
      |
      v
Single DB Writer Thread
      |
      v
SQLite
```

### Stage responsibilities

**Directory walker**

- Traverse configured roots.
- Discover candidate files.
- Check cancellation.
- Submit paths to the bounded work queue.

**TagLib workers**

- Read metadata.
- Validate supported formats.
- Extract audio properties.
- Prepare normalized metadata records.
- Extract artwork only according to the album-level strategy.

**Result queue**

- Hold completed metadata records.
- Apply backpressure when the writer cannot keep up.

**Database writer**

- Perform all database writes.
- Batch inserts and updates.
- Commit approximately every 500 records, subject to transaction size and failure behavior.

Workers must never write directly to SQLite.

## 10.2 Bounded queues

Use bounded queues to:

- Limit memory usage.
- Apply backpressure.
- Avoid unbounded pending work.
- Make cancellation manageable.
- Prevent the scanner from overwhelming SQLite.

Queue capacities should be configurable or selected through measurement rather than arbitrary huge values.

## 10.3 TagLib extension allowlist

Derive supported extension candidates from:

```cpp
TagLib::FileRef::defaultFileExtensions()
```

Intersect that list with the application's own supported-format allowlist.

Requirements:

- Compare extensions case-insensitively.
- Normalize extensions before matching.
- Test actual files for each supported format.
- Do not assume TagLib supports raw ADTS `.aac`.

Verify `.aac` using a real sample:

```cpp
TagLib::FileRef file(
    path.c_str(),
    true,
    TagLib::AudioProperties::Fast
);

if (file.isNull()) {
    // Unsupported, unreadable, or invalid.
}
```

The exact overload must match the installed TagLib version.

## 10.4 Metadata extraction

Use:

```cpp
TagLib::AudioProperties::Fast
```

for ordinary library scans.

Extract:

- Title.
- Artist.
- Album.
- Album artist.
- Track number.
- Disc number.
- Genre.
- Year.
- Duration.
- Bitrate.
- Sample rate.
- File path.

Normalize missing values and whitespace.

## 10.5 Stable track identity

The database row ID is the stable track identity.

The path is a mutable attribute.

Do not use the file path as the primary identity.

### Rescan matching order

1. Match by path.
2. Match by `(device, inode)`.
3. Match by content fingerprint.
4. Insert a new track if no match is found.

When a recognized move occurs:

```sql
UPDATE tracks
SET path = ?
WHERE id = ?;
```

Do not delete and recreate the row.

Preserving the row ID protects:

- Favourites.
- Play history.
- Playlist membership.
- Track relationships.
- Other user-specific metadata.

## 10.6 Artist and album ID caching

The writer thread should maintain:

```cpp
std::unordered_map<std::string, ArtistId> artist_cache;
std::unordered_map<std::string, AlbumId> album_cache;
```

Use get-or-create operations.

This avoids performing a separate SELECT for every track.

## 10.7 Artwork extraction

Extract artwork only for the first track encountered for an album key.

Use ArtworkManager for:

- Extraction.
- Storage.
- Stable cache key.
- Lookup.
- Invalidation.

LibraryScanner therefore depends on ArtworkManager for artwork functionality.

## 10.8 Directory traversal

Use explicit-stack DFS rather than recursive calls.

Advantages:

- Controlled memory usage.
- Explicit cancellation checks.
- Easier traversal lifecycle.
- Symlink-loop detection.
- Natural directory-level processing.

Use a visited set of:

```text
(dev, inode)
```

to detect repeated directories and symlink loops.

DFS also tends to keep files from the same album directory close together, helping directory-level artwork discovery and batching. Do not rely on DFS alone to guarantee that all album tracks are contiguous if the filesystem layout does not provide that guarantee.

---

# 11. FileWatcher — inotify

**Priority: HIGH**

FileWatcher monitors configured library roots.

It should forward normalized events to LibraryManager rather than directly rescanning the entire library.

## 11.1 Directory watch representation

Use directory nodes:

```cpp
struct DirectoryNode {
    int wd;
    DirectoryNode* parent;
    std::string name;
};
```

Maintain:

```text
wd -> DirectoryNode*
```

The full directory path can be derived by walking the parent chain.

A directory move can then re-parent one node rather than rewriting the stored paths of every descendant.

A flat `wd -> path` map is insufficient as the sole representation if directory moves must preserve the entire subtree efficiently.

## 11.2 Watch descriptor mapping

Maintain a `wd -> node` or `wd -> path` lookup for event resolution.

Remember that inotify is not recursive. Each directory that must be monitored needs its own watch.

## 11.3 Watch masks

Important correction:

`IN_MOVE` is a valid macro:

```cpp
#define IN_MOVE (IN_MOVED_FROM | IN_MOVED_TO)
```

Do not classify it as an invalid mask.

### Required handling

- Use `IN_CLOSE_WRITE` to identify completed writes.
- `IN_MODIFY` can fire repeatedly during writing.
- Include `IN_DELETE_SELF`.
- Include `IN_MOVE_SELF`.
- Handle `IN_IGNORED`.
- Handle `IN_Q_OVERFLOW`.
- Use `IN_ONLYDIR | IN_DONT_FOLLOW` when adding directory watches.

Example:

```cpp
constexpr uint32_t directory_mask =
    IN_CREATE |
    IN_DELETE |
    IN_CLOSE_WRITE |
    IN_MOVED_FROM |
    IN_MOVED_TO |
    IN_DELETE_SELF |
    IN_MOVE_SELF |
    IN_ONLYDIR |
    IN_DONT_FOLLOW;
```

`IN_ONLYDIR` and `IN_DONT_FOLLOW` are watch-add flags.

`IN_Q_OVERFLOW` and `IN_IGNORED` are kernel-delivered events, not optional ordinary watch-mask bits.

## 11.4 Directory registration race

When registering a directory subtree:

**Add the watch first, then call `readdir`.**

Correct order:

```text
1. Add watch to directory.
2. Enumerate directory.
3. Push child directories onto explicit stack.
4. Repeat.
```

If enumeration occurs first, a file created between enumeration and watch registration can be missed.

Apply the same procedure to directories moved into the watched tree through `IN_MOVED_TO` with `IN_ISDIR`.

## 11.5 Move-cookie pairing

Pair:

```text
IN_MOVED_FROM
IN_MOVED_TO
```

using their cookie.

A paired move must become one logical move event.

For a tracked file, update the existing track's path while preserving its database ID.

For a directory, re-parent the directory node.

Unpaired moves need a timeout or fallback policy because a move into or out of the watched root may only produce one observable side.

## 11.6 Event debounce

Use:

- A min-heap keyed by event deadline.
- A `path -> entry` hash map.
- A configurable debounce interval.

This allows multiple rapid notifications to become one logical update.

An album copy may generate dozens of filesystem events. They should not trigger dozens of redundant scans.

## 11.7 Event coalescing rewrite table

| Pending event | New event | Result |
|---|---|---|
| Created | Modified | Created |
| Created | Deleted | Drop |
| Modified | Deleted | Deleted |
| Deleted | Created | Modified |
| Move A→B | Move B→C | Move A→C |

The Deleted-then-Created case also handles common editors that write a temporary file and rename it over the original.

## 11.8 Unique work queue

FileWatcher must feed LibraryManager through a unique FIFO queue.

Use:

- FIFO work queue.
- Hash set of pending paths.
- One pending scan job per path.
- Controlled retry policy.

Ten events for the same file should produce one pending scan task, not ten.

## 11.9 Queue overflow

On `IN_Q_OVERFLOW`, do not trust the event history.

Schedule a full reconciliation/rescan of the affected library root.

This restores consistency after lost notifications.

## 11.10 Thread lifecycle

- Do not busy-spin on `EAGAIN`.
- Use poll, epoll or a suitable blocking strategy.
- Dispatch callbacks through EventBus.
- Stop the watcher thread before closing its file descriptor.
- Handle shutdown races.
- Make stop idempotent.

---

# 12. QueueManager

**Priority: HIGH**

QueueManager owns queue state.

It must not depend on PlaybackController.

PlaybackController consumes QueueManager results.

## 12.1 Queue data model

Store track IDs only.

Do not store full Track objects inside queue entries.

```cpp
using TrackId = std::int64_t;
using EntryId = std::uint64_t;

struct QueueEntry {
    EntryId entry_id;
    TrackId track_id;
};
```

The track resolver obtains the current track metadata when needed.

## 12.2 Queue structure

Use:

```cpp
std::deque<QueueEntry> queue;
std::size_t cursor;
```

A separate FIFO handles "play next" entries:

```cpp
std::deque<QueueEntry> play_next_queue;
```

The play-next FIFO has priority over the normal queue context.

## 12.3 Unique queue-entry identity

The same track may appear more than once.

Therefore:

- `track_id` identifies the database track.
- `entry_id` identifies one occurrence in the queue.

Use `entry_id` for queue removal and current-entry identity.

## 12.4 MPRIS track object path

Use the queue entry ID for MPRIS `mpris:trackid`.

Example:

```text
/org/lollypop/Track/<entry_id>
```

It must be a valid D-Bus object path.

The MPRIS Server must validate the supplied track object path when receiving `SetPosition`.

## 12.5 Queue result type

Use an explicit result:

```text
Next{entry}
EndOfQueue
Invalid
```

Do not return ambiguous null values for all outcomes.

## 12.6 Repeat modes

Define:

- Repeat Off.
- Repeat One.
- Repeat All.

Document and test behavior at the end of the queue.

## 12.7 Cursor adjustment

When removing an entry:

- If removed index is before cursor, decrement cursor.
- If removed index is current, apply the defined current-track removal policy.
- If removed index is after cursor, leave cursor unchanged.
- If the queue becomes empty, clear cursor.

Add an integration test for deleting the track currently playing.

## 12.8 Shuffle

Do not choose a random track every time the user presses Next.

Pre-generate a Fisher–Yates permutation.

```text
Original IDs
    |
    v
Fisher–Yates Shuffle
    |
    v
Permutation
    |
    v
Shuffle Cursor
```

Walk the permutation with a cursor.

This provides:

- O(1) next/previous cursor movement.
- No repeated tracks during one shuffle cycle.
- Predictable previous-track behavior.
- Reproducible shuffle when a seed is retained.

Rebuild or update the permutation when the queue changes.

## 12.9 Gapless playback

Use GStreamer's `about-to-finish` signal to prepare the next track.

QueueManager should provide the next track ID in O(1) from the appropriate queue context.

The callback must not perform database queries or expensive work.

Resolve and prepare the next URI ahead of time.

The signal callback must obey GStreamer's threading requirements and must not call GTK.

## 12.10 Playback history

Maintain:

- Bounded previous stack.
- Forward stack.

When moving to a previous track:

1. Pop the previous entry.
2. Push the current entry onto the forward stack.
3. Activate the previous entry.

When moving forward:

1. Pop the forward entry.
2. Push the current entry onto previous history.
3. Activate the next entry.

Define how manual queue changes invalidate or preserve forward history.

---

# 13. ArtworkManager, LyricsManager and CacheManager

## 13.1 ArtworkManager

Responsibilities:

- Extract embedded artwork.
- Locate folder artwork.
- Store artwork.
- Generate stable cache keys.
- Return artwork references.
- Invalidate stale artwork.
- Avoid duplicate extraction.

## 13.2 Artwork request priority

Use a priority queue:

```text
Now Playing
     |
     v
Next Track
     |
     v
Background Library Work
```

Priority order:

1. Currently playing track.
2. Next track.
3. Background artwork discovery.

Maintain an in-flight key set.

If multiple requests ask for the same cover, perform only one fetch or extraction operation.

## 13.3 LRU artwork cache

Use an LRU cache implemented with:

- Hash map.
- Doubly linked list.

Set a byte budget rather than a simple item-count limit.

A 3000×3000 image and a 128×128 thumbnail should not count as equal memory usage.

Store scaled thumbnails on disk keyed by:

```text
(hash, size)
```

This allows the same artwork to have multiple cached sizes.

## 13.4 LyricsManager

Responsibilities:

- Read embedded lyrics.
- Retrieve external lyrics.
- Parse timed lyrics.
- Parse untimed lyrics.
- Synchronize lyrics with playback position.
- Cache lyrics.
- Reject stale results after track changes.

Lyrics retrieval should not block playback.

## 13.5 CacheManager

Responsibilities:

- Create cache directories.
- Generate cache keys.
- Track cache metadata.
- Invalidate stale entries.
- Remove expired files.
- Clean orphaned cache entries.
- Protect files currently in use.

---

# 14. MPRIS Client

**Priority: HIGH**

MPRIS Client discovers and controls external Linux media players.

## 14.1 Interfaces

```text
org.mpris.MediaPlayer2
org.mpris.MediaPlayer2.Player
org.freedesktop.DBus.Properties
```

Root properties such as Identity and DesktopEntry belong to `org.mpris.MediaPlayer2`, not the Player interface.

## 14.2 Per-player data

Use:

```cpp
std::unordered_map<std::string, PlayerEntry> players;
```

Each PlayerEntry owns:

- Root proxy.
- Player proxy.
- Signal subscription IDs.
- Cached identity.
- Desktop entry.
- Playback status.
- Metadata.
- Position.
- Volume.
- Request generation.
- Polling state.
- MRU information.

This prevents one player's signal ID from overwriting another player's subscription.

## 14.3 Active-player policy

Maintain an MRU list.

1. When a player's status becomes Playing, move it to the front.
2. Active player is the first Playing player in MRU order.
3. If no player is Playing, use the front of the MRU list.
4. If the list is empty, no external player is active.
5. When a player disappears, remove it and recalculate the active player.

## 14.4 NameOwnerChanged

The signal contains three strings:

```text
name
old_owner
new_owner
```

Interpretation:

- Empty old owner + non-empty new owner: appearance.
- Non-empty old owner + empty new owner: disappearance.

Use one handler.

Prevent duplicate discovery when initial enumeration races with bus notifications.

## 14.5 Metadata parsing

Parse the MPRIS Metadata dictionary.

Relevant fields:

- `xesam:title`
- `xesam:artist`
- `xesam:album`
- `xesam:albumArtist`
- `mpris:artUrl`
- `mpris:length`
- `mpris:trackid`

Handle missing and incorrectly typed values safely.

## 14.6 Async D-Bus replies

Use asynchronous D-Bus calls.

A slow reply from a previously active player must not overwrite the current player's state.

Tag every request with a generation counter.

When the reply arrives:

1. Check that the player still exists.
2. Check that the request generation is current.
3. Check that the selected source/player still matches.
4. Drop the reply if it is stale.

## 14.7 Position polling

Allow only one outstanding Position poll per player.

Stop polling when:

- The player disappears.
- Position is no longer needed.
- The application shuts down.

Do not accumulate overlapping polls.

## 14.8 Seek scrubbing

Use a latest-wins slot.

Rate-limit D-Bus SetPosition requests to approximately 10–20 Hz during slider scrubbing.

Send the final position when scrubbing completes.

Remember:

- `Seek(offset)` is relative.
- `SetPosition(track_id, position)` is absolute.

## 14.9 Other required fixes

- Subscribe to Seeked.
- Handle invalidated properties.
- Disconnect old active-player subscriptions.
- Keep subscriptions per player.
- Avoid synchronous D-Bus calls on GTK's main thread.
- Clean up proxies and subscriptions on player disappearance.
- Make shutdown safe and repeatable.

---

# 15. MPRIS Server

**Priority: HIGH**

MPRIS Server exposes this application as an external media player.

It is separate from MPRIS Client.

Responsibilities:

- Register the application's MPRIS bus name.
- Expose root properties.
- Expose Player properties.
- Expose playback commands.
- Expose metadata.
- Expose position.
- Expose volume.
- Expose queue track identity.
- Emit relevant property and seek signals.

The server must use QueueManager's `entry_id` as the source for valid MPRIS track object paths.

It must not use only the database `track_id`, because the same track can appear multiple times in the queue.

---

# 16. SQLite Schema Improvements

The current SQLite implementation is complete, but the following enhancements should be evaluated and tested.

## 16.1 Foreign keys

Enable:

```sql
PRAGMA foreign_keys = ON;
```

## 16.2 WAL

Enable WAL mode:

```sql
PRAGMA journal_mode = WAL;
```

## 16.3 FTS5 configuration

Recommended configuration:

```sql
tokenize = 'unicode61 remove_diacritics 2'
prefix = '2 3'
```

Use an external-content FTS5 table where appropriate.

Keep it synchronized using database triggers for insert, update and delete operations.

Example conceptual structure:

```sql
CREATE VIRTUAL TABLE tracks_fts USING fts5(
    title,
    artist,
    album,
    content='tracks',
    content_rowid='id',
    tokenize='unicode61 remove_diacritics 2',
    prefix='2 3'
);
```

The actual FTS schema must match the real tracks table.

Add triggers for:

- INSERT.
- UPDATE.
- DELETE.

Test synchronization explicitly.

## 16.4 Keyset pagination

Prefer keyset pagination over OFFSET for large libraries.

Example:

```sql
SELECT id, title, artist
FROM tracks
WHERE (sort_key, id) > (?, ?)
ORDER BY sort_key, id
LIMIT ?;
```

Use a stable sort key and tie-breaker.

This avoids the increasing cost and instability that can occur with large OFFSET values.

## 16.5 Genre hierarchy as a DAG

A genre may have multiple parent genres.

Use a separate relationship table:

```sql
CREATE TABLE genre_edges (
    parent INTEGER NOT NULL,
    child INTEGER NOT NULL,
    PRIMARY KEY (parent, child)
);
```

Query a genre and all its descendants using a recursive CTE:

```sql
WITH RECURSIVE sub(id) AS (
    SELECT ?1

    UNION

    SELECT e.child
    FROM genre_edges e
    JOIN sub ON e.parent = sub.id
)
SELECT t.id
FROM tracks t
JOIN track_genres tg
    ON tg.track_id = t.id
WHERE tg.genre_id IN sub;
```

Use `UNION`, not `UNION ALL`, to prevent repeated traversal of already-seen nodes and protect against cycles.

Validate or reject cycles when inserting genre relationships.

## 16.6 Artist and album string interning

Normalize and reuse artist and album records.

Use writer-thread caches to avoid repeated lookups.

## 16.7 Library root lookup

A sorted vector of library roots plus binary search is sufficient for a small number of roots.

Do not introduce a trie unless measurements demonstrate a need.

---

# 17. Optional Smart Recommendation Structures

These features are not required for the initial playback implementation.

## 17.1 Co-play graph

For smart next-track selection and radio, maintain a weighted graph.

```sql
CREATE TABLE track_edges (
    src INTEGER NOT NULL,
    dst INTEGER NOT NULL,
    weight REAL NOT NULL,
    PRIMARY KEY (src, dst)
) WITHOUT ROWID;
```

Increase the weight when track B is played for more than 30 seconds after track A in the same session or playlist.

Keep only the top-K neighbors per node.

For recommendations:

1. Start from the current track.
2. Retrieve candidate neighbors.
3. Use metadata similarity as a fallback.
4. Perform a random walk with restart.
5. Rank and return recommendations.

### Cold-start handling

The co-play graph is initially empty.

Bootstrap with metadata relationships:

- Same artist.
- Same album.
- Same genre.
- Similar BPM.
- Similar key, if available.

## 17.2 Union-Find for duplicates and artist aliases

Union-Find is useful only when several independent signals must merge transitively.

Possible signals:

- MusicBrainz ID.
- Audio fingerprint.
- Normalized artist and title.
- Duration within approximately two seconds.
- Artist aliases.
- Featured-artist variants.

Examples:

- Beyoncé / Beyonce.
- Different formatting of featured artists.

If grouping is based on one normalized key only, ordinary SQL `GROUP BY` is sufficient.

Do not add Union-Find merely for the sake of using it.

## 17.3 Shortest-path playlist transitions

Dijkstra's algorithm over a similarity graph can produce gradual-transition playlists.

Possible edge weights:

- BPM difference.
- Musical key distance.
- Energy difference.
- Genre similarity.

This is a future feature, not a core backend requirement.

---

# 18. CMake Targets and Dependencies

The previous target layout did not provide homes for all required modules.

Add:

- `lollypop-playback`
- `lollypop-media`
- `lollypop-events`

Suggested mapping:

| Target | Responsibilities |
|---|---|
| `lollypop-storage` | StorageManager, SQLite |
| `lollypop-events` | EventBus and event types |
| `lollypop-playback` | AudioEngine, QueueManager, PlaybackController |
| `lollypop-library` | LibraryManager, LibraryScanner, FileWatcher |
| `lollypop-media` | ArtworkManager, LyricsManager, CacheManager |
| `lollypop-mpris` | MPRIS Client and Server |
| `lollypop-visualization` | VisualizerEngine and optional CAVA |
| Application target | BackendController and integration |

## 18.1 Dependencies to declare

Ensure CMake declares every library actually used:

- GTK 4.
- GLib / GIO.
- SQLite.
- TagLib.
- GStreamer core.
- GStreamer audio.
- GStreamer pbutils.
- GStreamer FFT if used.

In particular, `gstreamer-pbutils-1.0` must appear in the relevant CMake target, not merely in the documentation.

---

# 19. Initialization and Teardown

Use an initialization stack.

Do not rely on scattered constructor calls and manually maintained cleanup paths.

## 19.1 Initialization stack

When a subsystem initializes successfully, push its cleanup operation onto the stack.

Conceptually:

```text
Initialize EventBus
    |
    v
Push EventBus cleanup

Initialize Storage
    |
    v
Push Storage cleanup

Initialize CacheManager
    |
    v
Push CacheManager cleanup

Initialize AudioEngine
    |
    v
Push AudioEngine cleanup
```

If initialization fails:

```text
Pop cleanup stack
    |
    v
Shutdown last initialized subsystem
    |
    v
Continue in reverse order
```

Shutdown follows the same reverse order.

## 19.2 Scope-guard cleanup

Use the same pattern inside GStreamer pipeline construction.

If element creation or linking fails:

- Release allocated elements.
- Release requested tee pads.
- Disconnect signals.
- Release partially created bins.
- Return a useful error.

Only release the cleanup guard when the complete pipeline has been successfully constructed and ownership has transferred.

This structurally fixes partial initialization and resource leaks.

## 19.3 Idempotent shutdown

Every module's shutdown operation should be safe to call more than once.

Examples:

- Watcher already stopped.
- D-Bus subscription already disconnected.
- Pipeline already NULL.
- Worker thread already joined.
- Cache worker already shut down.

Avoid double-free and use-after-free behavior.

## 19.4 Initialization ordering

Maintain a static ordered initialization list.

Do not implement a dynamic dependency DAG yet. For approximately ten to fifteen modules, an explicit order plus a unit test is sufficient.

Add a test that checks every module appears after its declared dependencies.

Shutdown in reverse order.

If the architecture later becomes pluggable, reconsider a dependency DAG and topological sorting.

---

# 20. Testing Strategy

Every subsystem must have tests before integration.

## 20.1 StorageManager

- Initialization.
- CRUD.
- FTS5.
- Transactions.
- Foreign keys.
- WAL.
- Migrations.
- Keyset pagination.
- Genre recursive CTE.
- Persistence.
- Read/write concurrency.

## 20.2 BackendController

- Delegation to StorageManager.
- Delegation to PlaybackController.
- Error propagation.
- Event delivery.
- Lifecycle.
- Frontend-facing API consistency.

## 20.3 QueueManager

- Append and insert.
- Duplicate track entries.
- Unique entry IDs.
- Play-next FIFO.
- Repeat modes.
- Shuffle permutation.
- Previous/next history.
- Cursor adjustment.
- Track deletion during playback.
- Empty queue.
- End-of-queue.
- Gapless next-track lookup.

## 20.4 PlaybackController

- None → Internal.
- None → External.
- Internal → External.
- External → Internal.
- AudioEngine pause during handoff.
- MPRIS Pause during handoff.
- Inactive-source event rejection.
- External player disappearance.
- Stale callback rejection.

## 20.5 AudioEngine

- Pipeline creation.
- Element creation failure.
- Element linking failure.
- Tee branch construction.
- Separate tee pads.
- Output volume.
- Playback.
- Pause.
- Stop.
- Seek.
- Duration.
- Position.
- Buffering.
- End-of-stream.
- Invalid URI.
- Missing decoder.
- Appsink sample callback.
- Leaky visualization queue.
- Shutdown with pending callbacks.

## 20.6 VisualizerEngine

- Ring buffer wraparound.
- Ring buffer overflow.
- Producer/consumer concurrency.
- Drop-oldest behavior.
- No dynamic allocation in sample callback.
- FFT processing.
- Windowing.
- Normalization.
- Smoothing.
- Triple-buffer publication.
- UI reading without blocking.
- Shutdown during processing.

## 20.7 EventBus

- FIFO ordering.
- Conflated latest-value behavior.
- MPSC publishing.
- GTK main-context dispatch.
- No duplicate drain scheduling.
- Subscriber cleanup.
- Pending events during shutdown.
- Position timestamp interpolation.

## 20.8 LibraryScanner

- Supported formats.
- Unsupported formats.
- Uppercase extensions.
- `.aac` verification.
- Fast audio properties.
- Missing metadata.
- Album and artist caching.
- Artwork extraction once per album key.
- Stable identity.
- File move updates path.
- Cancellation.
- Bounded queue backpressure.
- Batched database transactions.

## 20.9 FileWatcher

- Create.
- Modify.
- Close-write.
- Delete.
- Rename.
- Move-cookie pairing.
- Directory move.
- Directory re-parenting.
- Watch-add-before-readdir.
- Symlink handling.
- `IN_DELETE_SELF`.
- `IN_MOVE_SELF`.
- `IN_IGNORED`.
- `IN_Q_OVERFLOW`.
- Debounce.
- Coalescing table.
- Unique work queue.
- Shutdown.
- No busy-spin on EAGAIN.

## 20.10 MPRIS Client

- Initial discovery.
- Player appearance.
- Player disappearance.
- Duplicate discovery prevention.
- Correct interface properties.
- Metadata parsing.
- Per-player subscriptions.
- MRU selection.
- Async generation counters.
- One outstanding Position poll.
- Seeked signal.
- Latest-wins scrubbing.
- Stale response rejection.
- Shutdown.

## 20.11 MPRIS Server

- Bus registration.
- Root properties.
- Player properties.
- Playback methods.
- Metadata.
- Position.
- Seek.
- Valid track object paths.
- Queue entry identity.
- SetPosition validation.
- Shutdown.

## 20.12 ArtworkManager and CacheManager

- Embedded artwork.
- Folder artwork.
- Duplicate fetch prevention.
- Priority scheduling.
- LRU eviction.
- Byte budget.
- Thumbnail sizes.
- Disk cache keys.
- Invalidations.
- Expiration.
- Orphan cleanup.

## 20.13 LyricsManager

- Embedded lyrics.
- External retrieval.
- Timed lyrics.
- Untimed lyrics.
- Invalid timestamps.
- Cache behavior.
- Current-line calculation.
- Track-change stale-result rejection.

---

# 21. Recommended Implementation Order

The order below respects the corrected dependencies.

## Stage 0 — Existing implementation audit

- Inspect current repository.
- Preserve SQLite implementation.
- Preserve query delegation.
- Inspect BackendController API.
- Inventory existing scanner and watcher code.
- Identify existing tests.
- Confirm CMake dependencies.

## Stage 1 — EventBus and AudioEngine topology

- Implement EventBus foundation.
- Implement AudioEngine.
- Build the complete sink topology.
- Add output and visualization branches.
- Add separate tee pads.
- Put volume on the output branch.
- Add leaky visualization queue.
- Configure one-buffer appsink.
- Add no-op sample callback.
- Implement pipeline cleanup stack.
- Add tests.

## Stage 2 — QueueManager

- Track IDs only.
- Unique entry IDs.
- Deque and cursor.
- Play-next FIFO.
- Repeat modes.
- Fisher–Yates shuffle.
- History stacks.
- Gapless next-track preparation.
- Tests.

## Stage 3 — CacheManager and ArtworkManager

- Cache infrastructure.
- Artwork extraction.
- LRU memory cache.
- Disk thumbnail cache.
- Priority queue.
- In-flight key set.
- Tests.

## Stage 4 — LibraryScanner

- Explicit-stack DFS.
- Supported extension allowlist.
- TagLib worker pool.
- Stable track identity.
- Artist/album ID caches.
- Artwork integration.
- Bounded queues.
- Single database writer.
- Batch transactions.
- Tests.

## Stage 5 — FileWatcher and LibraryManager

- inotify initialization.
- Directory-node tree.
- Watch registration.
- Move-cookie pairing.
- Debounce min-heap.
- Event coalescing.
- Unique FIFO work queue.
- Overflow reconciliation.
- Tests.

## Stage 6 — PlaybackController

- Source state.
- Playback state.
- Transition table.
- Source handoff.
- Queue integration.
- Inactive-event rejection.
- Tests.

## Stage 7 — MPRIS Client

- Player discovery.
- Per-player entries.
- MRU policy.
- Async D-Bus calls.
- Generation counters.
- Position polling.
- Seek scrubbing.
- Tests.

## Stage 8 — MPRIS Server

- Bus registration.
- Root interface.
- Player interface.
- Metadata.
- Queue entry object paths.
- Playback methods.
- Tests.

## Stage 9 — VisualizerEngine and LyricsManager

- FFT worker.
- SPSC ring buffer.
- Triple buffering.
- GTK frame-clock integration.
- Lyrics parsing.
- Lyrics retrieval.
- Tests.

## Stage 10 — Integration and hardening

- Full test suite.
- Startup/shutdown tests.
- Rapid file-change tests.
- Large library tests.
- Queue stress tests.
- External-player switching tests.
- Stale D-Bus response tests.
- Visualizer stress tests.
- CMake dependency inspection.

---

# 22. Definition of Done

A subsystem is complete only when:

1. Its public interface is implemented.
2. Its ownership rules are documented.
3. Its threading rules are documented.
4. Its error paths are handled.
5. Its shutdown is safe and idempotent.
6. Its unit tests pass.
7. Its integration tests pass where applicable.
8. It follows the intended module dependency direction.
9. It does not block GTK with expensive work.
10. It does not introduce GTK calls from worker or streaming threads.

The complete backend is ready for frontend integration when:

- SQLite and query delegation remain functional.
- BackendController delegates correctly to implemented modules.
- Internal and external playback cannot run simultaneously.
- Source handoff preserves internal playback position.
- Inactive-source events are discarded.
- Audio output remains independent of visualizer processing.
- The visualizer uses bounded sample buffering.
- EventBus prevents continuous-event backlogs.
- Scanning uses bounded queues and one database writer.
- FileWatcher coalesces events and preserves move identity.
- File moves preserve track row IDs and user relationships.
- Queue entries have unique IDs.
- MPRIS track IDs use valid object paths.
- MPRIS replies cannot overwrite newer state.
- Artwork and lyrics requests are deduplicated.
- Startup and shutdown follow the tested dependency order.
- All required tests pass.

---

# 23. Instructions for OpenCode Implementation

When implementing this document:

1. Inspect the existing repository before modifying anything.
2. Identify what is genuinely implemented and what only has declarations or placeholder code.
3. Do not rewrite the completed SQLite implementation without a demonstrated defect.
4. Do not rewrite query delegation.
5. Treat BackendController as implemented but not verified complete until its tests pass.
6. Do not use file paths as primary track identity.
7. Preserve track IDs when files move.
8. Do not report `IN_MOVE` as invalid.
9. Use `IN_CLOSE_WRITE` for completed-write handling.
10. Add `IN_DELETE_SELF` and `IN_MOVE_SELF` handling.
11. Remember that `IN_Q_OVERFLOW` and `IN_IGNORED` are kernel-delivered events.
12. Add watches before enumerating directories.
13. Pair move events by cookie.
14. Use a directory-node tree for watched directories.
15. Use bounded queues for scanning.
16. Use a single SQLite writer.
17. Batch database writes.
18. Do not make SQLite writes directly from TagLib workers.
19. Use QueueManager independently of PlaybackController.
20. Store queue track IDs rather than full Track objects.
21. Give every queue occurrence a unique `entry_id`.
22. Use Fisher–Yates shuffle rather than choosing a random track every time.
23. Build the appsink branch during the initial AudioEngine implementation.
24. Keep visualization processing isolated from audio output.
25. Use `leaky=downstream` on the visualization queue.
26. Set appsink to one buffer, dropping old buffers, and disable synchronization.
27. Use a preallocated lock-free SPSC ring buffer.
28. Do not allocate memory or acquire a mutex in the streaming callback.
29. Perform FFT on a worker thread.
30. Publish visualization frames using triple buffering.
31. Separate FIFO events from conflated events in EventBus.
32. Interpolate playback position in the UI using timestamps.
33. Use per-player MPRIS state.
34. Use MRU active-player selection.
35. Use generation counters for asynchronous replies.
36. Allow one outstanding Position poll per player.
37. Rate-limit seek scrubbing.
38. Apply volume on the output branch, not before the visualization tee.
39. Use `gst_element_request_pad_simple()`.
40. Use an initialization stack and reverse-order teardown.
41. Use cleanup guards during GStreamer pipeline construction.
42. Add tests before integrating each subsystem.
43. Add the required CMake targets.
44. Include `gstreamer-pbutils-1.0`.
45. Include `gstreamer-fft-1.0` only if it is used.
46. Run the complete test suite after each integration stage.
47. Inspect the CMake dependency graph.
48. Preserve existing working functionality.

**Implementation philosophy:** Build the backend as a collection of independently testable systems. Prioritize correctness, resource ownership, concurrency safety, and playback reliability before adding advanced recommendation features or visual polish.
