# Authority Model — Shell-Media-Controls-First

## Decision

**The shell's `MediaControls.qml` (Quickshell) is the sole playback authority.**  
The GTK app's bottom playback bar and right Now Playing panel are **read-only displays** (with optional pass-through controls). They observe `PlaybackState`; they never own it.

This matches how you actually use the system: shell media keys / shell media control panel / shell visualizer drive playback, and the GTK app is a secondary window that reflects the same state.

---

## Who Writes, Who Observes

| Component | Role | Writes to `PlaybackState`? | Observes `PlaybackState`? | Mechanism |
|-----------|------|---------------------------|---------------------------|-----------|
| Shell `MediaControls.qml` | **PRIMARY authority** | Yes (indirectly) | No | MPRIS D-Bus → `lollypop-cpp` `MprisServer` |
| Shell `MprisController.qml` | MPRIS client | Yes (via MPRIS calls) | No | `org.mpris.MediaPlayer2` methods |
| `lollypop-cpp` `MprisServer` | MPRIS server | Yes (translates MPRIS → backend) | No | D-Bus method calls → `BackendController` |
| `BackendController` | State owner | **Yes (single source of truth)** | N/A | Internal `playing_`, `position_`, `repeat_*`, `shuffle_` |
| GTK Bottom Playback Bar | Display + optional pass-through | **No** | Yes | Callbacks from `BackendController` |
| GTK Right Now Playing Panel | Display + optional pass-through | **No** | Yes | Callbacks from `BackendController` |
| GTK `WaveformView` (both panels) | Display | **No** | Yes | Visualization callback → `VisualizerModel` |

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    SHELL (Quickshell)                       │
│  MediaControls.qml ───┐                                     │
│                       ▼                                     │
│              MprisController.qml                            │
│                       ▼                                     │
│              D-Bus (org.mpris.MediaPlayer2)                 │
└───────────────┬─────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────┐
│                 lollypop-cpp (GTK app)                      │
│  MprisServer (D-Bus endpoint) ───────────────────────────►│
│                       ▼                                     │
│              BackendController                              │
│  • PlaybackState (single source of truth)                   │
│  • GStreamer / AudioEngine                                  │
│  • CavaVisualizer / VisualizerEngine                        │
│                       ▼                                     │
│         Callbacks (track/position/state/visualization)      │
│                       ▼                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ Bottom Bar   │  │ Right Panel  │  │ WaveformView │      │
│  │ (read-only)  │  │ (read-only)  │  │ (read-only)  │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

---

## MPRIS as the Bridge

The shell controls `lollypop-cpp` exclusively through MPRIS on the session bus.

| Shell Action | MPRIS Call | lollypop-cpp Handler | Result |
|--------------|------------|----------------------|--------|
| Play / Pause | `PlayPause` | `BackendController::toggle_play_pause()` | `playing_` flips; UI notified |
| Next | `Next` | `BackendController::next_track()` | Track change; UI notified |
| Previous | `Previous` | `BackendController::previous_track()` | Track change; UI notified |
| Seek | `Seek(position)` | `BackendController::seek(position)` | Position jump; UI notified |
| Volume | `Volume = v` | `BackendController::set_volume(v)` | Volume change; UI notified |
| Shuffle | `Shuffle = true/false` | `BackendController::set_shuffle(b)` | Shuffle flag; UI notified |
| Repeat All | `LoopStatus = Playlist` | `BackendController::set_repeat(true)` + `set_repeat_one(false)` | Repeat mode; UI notified |
| Repeat One | `LoopStatus = Track` | `BackendController::set_repeat_one(true)` | Repeat mode; UI notified |
| Repeat Off | `LoopStatus = None` | `set_repeat(false)` + `set_repeat_one(false)` | Normal; UI notified |

`lollypop-cpp`'s `MprisServer` must publish:
- `PlaybackStatus` (Playing / Paused / Stopped)
- `LoopStatus` (None / Track / Playlist)
- `Shuffle`
- `Volume`
- `Metadata` (title, artist, album, artUrl)
- `Position` (updated on track change and seek, not continuously per MPRIS spec)

The `artUrl` must point to a real file-backed artwork cache (see Phase 10 / Section 11.5 of the frontend plan).

---

## Visualizer Data Flow

The shell runs CAVA and renders the wave in `WaveVisualizer.qml`. The GTK app does **not** need to coordinate with the shell's visualizer process.

Two independent paths produce waveform data:

1. **Shell path**: CAVA subprocess → `GlobalStates.visualizerPoints` → `WaveVisualizer.qml` (QML Canvas). This is what you see in the shell.
2. **GTK app path**: `lollypop-cpp` backend's `CavaVisualizer` (CAVA) or `VisualizerEngine` (GStreamer appsink) → `std::vector<float>` points → `VisualizerModel` → GTK `WaveformView`.

Both capture the same system audio (PipeWire), so they show the same spectrum. They run independently; no cross-process sync is required.

If you later want the GTK app to skip its own CAVA instance and reuse the shell's points, that requires a custom IPC channel (not MPRIS). That is a future optimization, not part of the initial model.

---

## GTK Controls: Disabled vs Pass-Through

The plan (Sections 7 & 9) specifies playback controls in the bottom bar and right panel. Under this authority model:

**Option A — Fully read-only (recommended for v1)**
- Bottom bar Play/Pause, Previous, Next, Shuffle, Repeat, Volume buttons are **insensitive** (greyed out) or hidden.
- The bar shows: artwork, title, artist, progress, current state, waveform.
- User controls playback exclusively from the shell.

**Option B — Pass-through controls**
- GTK buttons remain enabled but, when clicked, they issue the equivalent MPRIS call to `lollypop-cpp`'s own `MprisServer` (or directly to `BackendController`), which updates `PlaybackState` and notifies all observers including the shell.
- This keeps shell and GTK in sync because both observe the same MPRIS-backed state.

**Decision:** Start with **Option A** for the initial functional version (simplest, no risk of dual-control desync). Add Option B in a later phase if you want in-app control.

Either way: **GTK UI never writes to `PlaybackState` directly.** It only reads via callbacks.

---

## PlaybackState (Single Source of Truth)

```cpp
struct PlaybackState {
    TrackId    currentTrackId;
    bool       isPlaying;
    double     positionSeconds;
    double     durationSeconds;
    double     volume;
    bool       shuffleEnabled;
    enum class RepeatMode { Off, All, One };
    RepeatMode repeatMode;
};
```

- Owned by `BackendController`.
- `Repeat All` and `Repeat One` are **mutually exclusive** (one enum, not two booleans).
- All GTK panels receive updates through `BackendController` callbacks:
  - `track_changed_cb_`
  - `position_changed_cb_`
  - `playback_state_changed_cb_`
  - `queue_changed_cb_`
  - `visualization_cb_`

---

## Implementation Consequences for Frontend Code

1. **`main_window.cpp`**: Bottom bar height must be ≤112 px (currently 220 px — violates the plan). Fix before building more layout.
2. **`waveform_widget.cpp`**: Currently Cairo-based. The plan mandates `GtkSnapshot`/GSK. Keep Cairo as a prototype if needed, but the final renderer must use `GtkSnapshot`.
3. **`BottomPlayback` / `RightNowPlaying`**: Controls should default to insensitive (read-only). Display track info, progress, artwork, waveform from callbacks.
4. **No duplicate playback state**: Do not create a second `PlaybackState` in the GTK UI. All panels read from `BackendController`.
5. **No SQL / GStreamer in UI**: UI never touches `AudioEngine`, `library_manager`, or SQLite directly.
6. **Service interfaces (Phase 1)**: Before more UI code, extract `IPlayerService` from `BackendController` so the UI depends on an interface, not the concrete class.
7. **MPRIS identity**: `MprisServer` must register with an identity matching your shell's `Config.options.bar.media.preferredPlayer` so `MprisController.qml` discovers `lollypop-cpp` automatically.

---

## Files Affected

| File | Change |
|------|--------|
| `frontend/src/main_window.cpp` | Fix bottom bar height (≤112 px); wire callbacks for read-only display |
| `frontend/src/panels/bottom_playback.cpp` | Make controls insensitive or pass-through; observe only |
| `frontend/src/panels/right_now_playing.cpp` | Make controls insensitive or pass-through; observe only |
| `frontend/src/panels/waveform_widget.cpp` | Observe `VisualizerModel`; plan migration to `GtkSnapshot` |
| `frontend/include/panels/bottom_playback.hpp` | Remove write-methods or mark pass-through |
| `frontend/include/panels/right_now_playing.hpp` | Remove write-methods or mark pass-through |
| `docs/IMPLEMENTATION_AUDIT.md` | Record this authority model (Phase 0 deliverable) |
| New: `src/services/IPlayerService.*` | Phase 1 interface extracted from `BackendController` |
| New: `src/models/PlaybackState.*` | Single authoritative state struct |
| New: `src/models/VisualizerModel.*` | Shared ring buffer + sample data |

---

## Playback Source Authority (Internal vs External MPRIS)

In addition to the shell→GTK authority model, `lollypop-cpp` supports two playback
sources that can be active at any time:

| Source | Description | Control Path |
|--------|-------------|--------------|
| **Internal** | Local `AudioEngine` (GStreamer) | Direct `AudioEngine` calls via `BackendController` |
| **External** | Remote MPRIS player (Spotify, etc.) | MPRIS D-Bus calls via `MprisManager` |

Only **one source is authoritative** at a time, managed by `PlaybackSourceManager`.
The active source is selected via:
- `BackendController::set_playback_source_internal()`
- `BackendController::set_playback_source_external()`
- `BackendController::is_playback_source_internal()`

### Transport Verb Routing

All transport verbs route through `PlaybackSourceManager`:

| Verb | Internal Source | External Source |
|------|-----------------|-----------------|
| `play_pause` | `AudioEngine::play()/pause()` | `MprisManager::play_pause()` |
| `play` | `AudioEngine::play()` | `MprisManager::play()` |
| `pause` | `AudioEngine::pause()` | `MprisManager::pause()` |
| `stop` | `AudioEngine::stop()` + clear track | `MprisManager::stop()` |
| `next` | `QueueManager::next_index()` + `start_queue_index()` | `MprisManager::next()` |
| `previous` | `QueueManager::previous_index()` + `start_queue_index()` (or seek 0) | `MprisManager::previous()` |
| `seek` | `AudioEngine::seek()` (debounced, 200ms) | `MprisManager::seek()` (absolute) |
| `seek_by` | `AudioEngine::seek(current + offset)` | `MprisManager::seek_by()` (relative) |
| `set_volume` | `AudioEngine::set_volume()` | `MprisManager::set_volume()` |

### Capability Queries

Capabilities reflect the active source:

| Capability | Internal | External |
|------------|----------|----------|
| `can_control` | Always true (if AudioEngine exists) | `MprisManager::can_control()` |
| `can_seek` | `AudioEngine::can_seek()` (requires loaded track) | `MprisManager::can_seek()` |
| `can_go_next` | Always true (queue manager handles) | `MprisManager::can_go_next()` |
| `can_go_previous` | Always true (queue manager handles) | `MprisManager::can_go_previous()` |

### Unsupported Verb Behavior

When a verb is not supported by the active source (e.g., `seek` on unseekable stream,
`next` on external player with `CanGoNext=false`), the call:
- **Does not silently fake success**
- Returns early or emits current state without change
- `can_*()` queries allow UI to disable insensitive controls proactively

### Source Switching

- Switching sources stops playback on the previous source
- Queue/track state is preserved for internal source
- External player state is managed by the external player itself

---

## Acceptance Criteria (Authority Model)

- [ ] Shell media keys control `lollypop-cpp` playback via MPRIS.
- [ ] GTK bottom bar and right panel update when shell changes playback state.
- [ ] GTK panels do **not** contain a second `PlaybackState`.
- [ ] GTK panels do **not** call SQL or GStreamer directly.
- [ ] Bottom bar height ≤112 px.
- [ ] `MprisServer` identity discoverable by shell `MprisController.qml`.
- [ ] Transport verbs route through `PlaybackSourceManager` based on active source.
- [ ] `can_seek()` returns false for unseekable sources (no track loaded / external player without CanSeek).
- [ ] Unsupported verbs on external source don't crash; fail gracefully.
- [ ] Source switching via `set_playback_source_internal/external()` works correctly.