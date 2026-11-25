# Component state matrix

Companion to `docs/design-rules.yaml` section `components` and the component contracts in
`docs/APPLE_DESIGN_EXTRACTION_ACTION_PLAN.md` section 4.

This matrix records which states are represented in code/CSS and which still need a runtime
check with a real library. It is intentionally descriptive: a "runtime pending" row is not a
claim that the state is broken, only that it has not been visually verified from the shell.

## Legend

- **Code** — the state is produced by widget logic or a controller callback.
- **CSS** — the state has a styling hook.
- **Runtime** — needs a mouse/keyboard pass in the running app.
- **Pending** — the state is documented but not yet exposed by a visible control.

## Matrix

| Component | Contract states | Implemented hooks | Coverage | Notes |
| --- | --- | --- | --- | --- |
| `AlbumCard` | idle, hover, focus-visible, pressed, active, disabled | CSS: `.album-card:hover`, `.album-card:active`, `.album-card:focus-visible`; generic `button:disabled`; code: focusable widget + key controller | Code + CSS; runtime pending | Focus ring uses `--border-focus`; accessible name includes album title. |
| `AppleAlbumTile` | idle, hover, focus-within, focus-visible, favorited, pressed | CSS: `.apple-album-tile.hover`, `:focus-within`, `:focus-visible`, `.favorited`; scrim/favourite transitions use `--motion-*`; code: favourite button toggles and updates tooltip/accessible name | Code + CSS; runtime pending | Favourite is revealed by keyboard focus, not hover alone. |
| `TrackRow` | idle, hover, focus-visible, playing, pressed, liked, disabled | CSS: `.track-row:focus-visible`, `.track-row-like-button:focus-visible`, generic hover/active/disabled; code: like callback and live-row registry | Code + CSS; runtime pending | Like button keeps a stable allocation when state changes. |
| `SidebarRows` | idle, hover, active, focus-visible, compact | CSS: `.sidebar-nav-item:hover`, `:focus-visible`, compact layout rules; code: click and keyboard activation | Code + CSS; runtime pending | Active label is semibold; inactive label regular. |
| `PiPWindow` | idle, focus-visible, dragging, playing, paused, loading | CSS: `.pip-*` hover/focus/disabled rules; code: transport callbacks, drag via `gdk_toplevel_begin_move()`, playback-state observer | Code + CSS; runtime pending | Close affordance is visible and accessible; loading state should be checked with slow artwork. |
| `StateComponents` | empty, loading, error, retry | CSS: `.empty-state`, `.loading-state`, `.error-state`, `.error-state-retry`; code: `EmptyState.cpp`, `LoadingState.cpp`, `ErrorState.cpp` | Code + CSS; usage pending | Shared copy contract: explanation, plain language, and retry where applicable. |
| `QueueList` | empty, populated, removed, undo available | Code: `RightNowPlaying::populate_queue()`, deferred row actions, undo bar; CSS: `.queue-empty-state`, `.queue-undo-bar` | Code + CSS; runtime pending | Empty copy now names the next user action: add tracks from the library. |
| `QueueUndoBar` | hidden, visible, undo available, stale, failed | Code: `show_queue_undo()`, `hide_queue_undo()`, `on_queue_undo_clicked()`; CSS: `.queue-undo-bar`, `.queue-undo-label`, `.queue-undo-button` | Code + CSS; runtime pending | Undo message is ellipsized with a zero minimum width so long track titles cannot widen the right panel. Stale state hides the bar if the queue changed after removal. Failure disables Undo and says the track could not be put back. |

## Explicitly out of scope for this stage

- **Clear queue** has a backend API (`BackendController::clear_queue()`) but no user-visible control, so there is no destructive UI action to confirm or undo yet.
- **Reorder queue** has a backend API (`move_queue_item()`) but no drag/arrow UI, so there is no visible destructive action to guard.
- **Dynamic favourite menu label** ("Favourite" vs "Remove from favourites") remains a copy polish item for a later stage; the current label is consistent across album menus.

## Runtime check checklist

1. Tab to an album tile: favourite button appears and has an album-specific accessible name.
2. Open Queue, remove a track: undo bar appears below the Queue tab.
3. Activate Undo: the track returns to its original position, not the end.
4. Remove a track, then add another track before undoing: undo bar closes instead of inserting into a changed queue.
5. Remove the last queue track: empty copy appears and Undo still restores the track.
6. Enable reduced motion: transitions collapse to 1 ms and no layout jumps occur.
