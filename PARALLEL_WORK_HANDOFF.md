# Parallel work split: Backend vs Frontend/Wiring

Date: 2026-11-23  
Companion docs: `APPLICATION_DIAGNOSIS.md`, `IMPLEMENTATION_PRIORITY_AND_HANDOFF.md`

Yes — this can be done in parallel. The key is **strict file ownership** and a **frozen shared contract**. Do not let both agents edit the same headers or CMake files.

---

## 1. Split strategy

| Track | Owner | Files owned exclusively |
| --- | --- | --- |
| A — Backend | Nemotron 550b / max-thinking | `backend/src/**`, `tests/test_backend.cpp`, `tests/test_seek.cpp`, `tests/test_scan_incremental.cpp`, `tests/test_authority.cpp`, `docs/AUTHORITY_MODEL.md` |
| B — Frontend / wiring | You, or Nemotron 120b / max-thinking | `frontend/src/**`, `frontend/include/**`, `include/ui/**`, `src/ui/**`, `include/application.hpp`, `src/main.cpp`, `resources/**`, root `CMakeLists.txt` |
| Frozen | Nobody edits in this wave | `include/services/**`, `include/models/**`, `backend/include/**`, `tests/CMakeLists.txt` |

If a change is needed in a frozen file, **stop and record it as a follow-up** rather than editing it. Exception: backend may update `docs/AUTHORITY_MODEL.md`.

Use separate branches or git worktrees. Merge in this order:

1. Merge whichever track finishes first.
2. Run full build + `ctest`.
3. Merge the second track.
4. Run full build + `ctest` + ASan + manual launch from `/tmp`.

---

## 2. Frozen shared contract

Both tracks code against this contract. Do not change it during the parallel wave.

### EventBus

Already defined in `backend/include/event_bus.hpp`:

- `"spectrum"` — conflated, payload `SpectrumFrame { std::vector<float> magnitudes; }`
- `"position"` — conflated, published via `publish_position(...)`, payload `PositionUpdate`
- FIFO events: state change, track change, queue mutation, scan finished, player appeared/disappeared, error

Handlers are invoked on the GTK main context.

Important: `VisualizerEngine` already publishes `"spectrum"` at `backend/src/visualizer_engine.cpp:246-249`. Backend must **construct/connect** it, not duplicate publishing.

### BackendController public API

Frontend may use existing methods only:

- `initialize()`, `shutdown()`
- playback: `play_track`, `play_tracks`, `toggle_play_pause`, `pause`, `stop`, `next_track`, `previous_track`, `seek`, `set_volume`, `get_volume`
- state: `is_playing`, `get_current_track`, `get_position`, `get_duration`, `get_queue`, `get_queue_index`, `can_seek`
- source: `set_playback_source_internal`, `set_playback_source_external`, `is_playback_source_internal`
- callbacks: `add_track_changed_callback`, `add_queue_changed_callback`, `add_library_changed_callback`
- library/queries and lyrics as already declared

Frontend must not add new virtual methods to `IPlayerService` in this wave.

---

## 3. Track A — Backend prompt

Paste this to Nemotron 550b / max-thinking.

```text
You are working in /home/mahesh/Projects/lollypop-cpp, a C++20 / GTK4 / libadwaita music player.

Read first:
- AGENTS.md
- APPLICATION_DIAGNOSIS.md
- IMPLEMENTATION_PRIORITY_AND_HANDOFF.md
- PARALLEL_WORK_HANDOFF.md

ASSIGNMENT: Backend Track only. Do not touch frontend/, include/ui/, resources/, root CMakeLists.txt, or frozen headers under include/services/, include/models/, backend/include/.

Hard constraints:
1. Preserve all uncommitted user work. Do not revert or delete docs, backups, build dirs, or planning files.
2. Do not change the shared EventBus topic names or payload types.
3. Do not duplicate spectrum publishing; VisualizerEngine already publishes "spectrum" as a conflated EventBus event.
4. Do not introduce mock services.
5. Do not edit public API headers in this wave. If you need a new API, write a short follow-up note in docs/AUTHORITY_MODEL.md instead.
6. backend/src/** is yours. frontend/src/** is not.

Tasks, in order:

B1 — R-01 visualizer pipeline
- Construct VisualizerEngine and CavaVisualizer inside BackendController::initialize().
- Wire appsink new-sample into the visualizer with a lifetime token owned by the backend/audio engine.
- Ensure spectrum events are published on EventBus topic "spectrum" with SpectrumFrame.
- Ensure shutdown unsubscribes and tears down audio before visualizer objects die.
- Confirm no use-after-free under ASan.

B2 — R-02 seek correctness
- Add a seek generation counter and debounce seek commits to 150-250 ms.
- Ignore stale seek responses.
- Use can_seek() so unseekable sources never issue real seeks.
- Keep position updates flowing while scrubbing.
- Add tests/test_seek.cpp for generation/debounce behaviour.

B3 — R-03 source authority
- Route all transport verbs (play/pause, next, previous, seek, volume) through PlaybackSourceManager using existing set_playback_source_internal/external and is_playback_source_internal().
- Unsupported verbs must report/disable cleanly through existing state, not silently fake success.
- Update docs/AUTHORITY_MODEL.md to match actual code.
- Add tests/test_authority.cpp for the routing matrix.

B4 — Incremental scanning
- In LibraryScanner, skip unchanged files using mtime + size before TagLib parsing.
- Throttle progress emission to ~100 ms.
- Emit library_changed once per completed scan, not per file.
- Add tests/test_scan_incremental.cpp.

B5 — Lazy audio / safe shutdown
- Avoid eager GStreamer/pulsesink setup during initialize().
- Create audio pipeline lazily on first playback.
- Make shutdown idempotent and ASan-clean.

Verification after each task:
  cmake --build build -j$(nproc)
  ctest --test-dir build --output-on-failure
  cmake --build build-asan -j$(nproc)

Report format:
1. Changed files with one-line rationale each.
2. Build/test summary.
3. Any frozen-file changes you deliberately avoided and what follow-up they need.
4. Any behaviour you could not verify.
Stop and ask if unsure about an API.
```

**Backend done when:** visualizer data reaches `"spectrum"`, seek scrubbing is stable, transport verbs follow source authority, scans are incremental, and ASan shutdown is clean.

---

## 4. Track B — Frontend / wiring prompt

This is your UI track. If you hand it to a model, use 120b / max-thinking.

```text
You are working in /home/mahesh/Projects/lollypop-cpp, a C++20 / GTK4 / libadwaita music player.

Read first:
- AGENTS.md
- APPLICATION_DIAGNOSIS.md
- IMPLEMENTATION_PRIORITY_AND_HANDOFF.md
- PARALLEL_WORK_HANDOFF.md

ASSIGNMENT: Frontend / wiring Track only. Do not touch backend/src/**, backend/include/**, include/services/**, include/models/**, docs/**, or tests/**.

Hard constraints:
1. Preserve all uncommitted user work. Do not revert or delete docs, backups, build dirs, or planning files.
2. Never call g_weak_ref_init/get/clear on a non-GObject C++ object. Use a shared lifetime token or weak-ref the actual GObject widget.
3. No relative resource paths. Load CSS from compiled-in absolute path or gresource and check the GError.
4. No nested GtkScrolledWindow in the middle panel.
5. Do not add mock fallbacks. Use the real BackendController through the existing interfaces.
6. Subscribe to EventBus topic "spectrum" for waveform data. Do not publish it; backend owns publication.
7. Do not change root CMakeLists.txt unless adding CSS/gresource/shader wiring. Backend agent must not edit root CMakeLists.txt.

Tasks, in order:

U1 — Search lifetime crash
- Fix invalid GWeakRef usage in frontend/include/panels/middle_library.hpp, frontend/src/panels/middle_library.cpp, frontend/src/ui/pages/SearchResultsPage.cpp.
- Cancel pending search/lazy-load sources in destructors.
- Unref every g_weak_ref_get() result if you keep any GObject weak refs.

U2 — Home "View all"
- Fix HomePage.cpp so recently added, recently played, and favourite albums buttons call the stored section callbacks.
- Ensure setters update the callbacks actually used by buttons.

U3 — CSS path
- Bundle resources/style.css into resources/lollypop.gresource.xml or use a compiled-in absolute install path.
- Load it from application/ThemeManager.
- Log/check the GError.
- Verify launch from /tmp is still styled.

U4 — Window-first startup
- In frontend/src/application.cpp, create and present MainWindow before any slow backend work.
- Move backend_->initialize() to a low-priority idle source.
- Show a loading state; clear it when backend callbacks fire.
- Do not edit backend/src/controller.cpp.

U5 — Navigation history
- Replace flat page ID history with struct NavEntry { page_id, param, scroll }.
- Push detail pages with their IDs/filters.
- Back/forward restore page + param + scroll.
- Map detail pages to valid sidebar items before calling set_active_item_visual().

U6 — Single scroller / search results
- Remove both ScrolledWindow instances from SearchResultsPage.
- Return Box + ListBox like SongsPage.
- Connect lazy load to MiddleLibrary's shared scroll adjustment.

U7 — Search + SongsPage generation guards
- Add monotonic generation counter for search requests.
- Marshal results back to GTK main context and ignore stale generations.
- Reset loading_ in SongsPage before a new load.

U8 — Visual work (only after U1-U7)
- Responsive album grid 2-6 columns, square artwork.
- Detail hero headers and search grouping/empty state.
- ShaderManager loading .gskshader files from resources.
- Typography and reduced-motion support.

Verification after each task:
  cmake --build build -j$(nproc)
  ctest --test-dir build --output-on-failure

Manual checks:
- Launch from /tmp: styled window appears immediately.
- Type/delete search 10x fast: no crash, newest results only.
- Open album detail -> back -> forward: same album and scroll restored.
- One vertical scrollbar in middle panel.
- Waveform accepts synthetic "spectrum" events until backend lands.

Report format:
1. Changed files with one-line rationale each.
2. Build/test summary.
3. Manual verification results.
4. UI behaviours waiting on backend.
Stop and ask if unsure about an API.
```

**Frontend done when:** search never crashes, startup presents immediately, CSS works from any CWD, back/forward restores state, one scrollbar exists, and waveform can consume `"spectrum"` events.

---

## 5. Integration gate after both tracks merge

Run once both tracks are merged:

```sh
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
cmake --build build-asan -j$(nproc)
(cd /tmp && /home/mahesh/Projects/lollypop-cpp/build/lollypop-cpp)
```

Then manually verify:

1. Window appears before scan/GStreamer/MPRIS work.
2. CSS applies when launched from `/tmp`.
3. Search typing/deleting does not crash.
4. Album detail → back → forward restores the same album and scroll.
5. Waveform reacts once backend publishes `"spectrum"`.
6. Progress scrubbing works and does not fight playback.
7. Next/previous behave correctly for internal vs external MPRIS source.
8. Library scan does not replay per-file progress storms.

---

## 6. Conflict rules

If either track needs a frozen file:

- Write a note in `PARALLEL_FOLLOWUPS.md` with: file, reason, proposed change, which track should do it.
- Do not edit the frozen file in the parallel wave.
- Resolve follow-ups in a short integration session after both tracks are green.
