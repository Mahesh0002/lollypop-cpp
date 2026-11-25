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
| `AlbumsPage` | populated, empty, unavailable, sorting | Code: `AlbumsPage::refresh()` swaps grid/`EmptyState`/`ErrorState`; `GtkMenuButton` sort menu with radio actions; responsive FlowBox columns | Code; runtime pending | Sort is a direct menu, not a cycling button. Loading state is intentionally omitted because refresh is synchronous. |
| `ArtistsPage` | populated, empty, unavailable, sorting | Code: `refresh()` swaps grid/`EmptyState`/`ErrorState`; Albums-style header, sort menu, responsive 3–8 column FlowBox, and count subtitle | Code; runtime pending | Empty copy names the next action; retry re-reads the library. |
| `AppleArtistTile` | idle, hover, focus-visible, pressed | CSS: shared `.apple-album-tile` hover/focus/scrim rules; code: square overlay tile with album-count detail, lazy artwork, play button, and context menu | Code + CSS; runtime pending | Reuses album tile styling so Artists matches Albums visually. |
| `ArtistDetailPage` | populated, artist missing, unavailable, no tracks | Code: `PageStateHost` swaps content/empty/error states; album-style artwork-and-info hero with Play/Shuffle; inline copy when the artist has no tracks; `ArtworkView` keeps a visible fallback | Code; runtime pending | Missing-artist state replaces the whole page; no-tracks state keeps artwork and metadata visible. |
| `PageStateHost` | populated, empty, unavailable, retry | Code: shared box wrapper attaches content once and swaps `EmptyState`/`ErrorState`; page-specific copy supplied by each page | Code; runtime pending | Gives every browse page the same state contract without duplicating visibility bookkeeping. |
| `SongsPage` | populated, empty, unavailable, sorting, genre filter | Code: `refresh()`/`reload()` swap content via `PageStateHost`; database-backed sort and genre filter | Code; runtime pending | Empty copy points to the filter and library; retry re-reads songs. |
| `GenresPage` | populated, empty, unavailable | Code: `PageStateHost` swap in `refresh()`; "View" buttons keep standard button semantics | Code; runtime pending | Empty copy names the metadata needed to populate the page. |
| `LikedMusicPage` | populated, empty, unavailable | Code: `PageStateHost` swap in `refresh()`; unliking the last row reveals the empty state | Code; runtime pending | Uses the shared `TrackRow` states and accessible naming. |
| `PlaylistsPage` | populated, empty, unavailable | Code: `PageStateHost` swap in `refresh()`; responsive 3–8 column FlowBox; rename/delete dialogs | Code; runtime pending | Playlist cards keep the same hover/focus treatment as album cards. |
| `PlaylistDialogs` | add to playlist, new playlist, rename, confirm delete | Code: modal dialogs wired to `ILibraryService` playlist mutations and library-change notifications | Code; runtime pending | Replaces the queue fallback for Add to Playlist across albums, artists, favourites, and queue rows. |
| `PlaylistItem` | idle, hover, focus-visible, pressed, loading, fallback | CSS: shared `.album-card` rules; code: width-driven square tile, `ArtworkView` fallback, tooltip, accessible name, context menu | Code + CSS; runtime pending | Focus uses the canonical accent ring; artwork fills the square tile edge to edge. |
| `FavouriteAlbumsPage` | populated, empty, unavailable | Code: `PageStateHost` swap in `refresh()`; count subtitle and shared `AlbumCard` grid | Code; runtime pending | Empty copy names the heart action that populates the page. |
| `AlbumDetailPage` | populated, album missing, no tracks, unavailable | Code: `PageStateHost` swap in `refresh()`; inline no-tracks copy; `ArtworkView` fallback | Code; runtime pending | Missing album replaces the detail content; no-tracks keeps the title/artwork visible. |
| `ArtworkView` | idle, loading, fallback, square, rounded | Code: `set_preferred_size()` applies square requests to sizer/children and preserves caller alignment on the overlay; cached async decode | Code; runtime pending | Card and right-pane tiles are width-driven squares; artwork uses `FILL` alignment and `GtkPicture` crops with `COVER`. |
| `TrackRow` | idle, hover, focus-visible, playing, pressed, liked, disabled | CSS: `.track-row:focus-visible`, `.track-row-like-button:focus-visible`, generic hover/active/disabled; code: like callback and live-row registry | Code + CSS; runtime pending | Like button keeps a stable allocation when state changes. |
| `SidebarRows` | idle, hover, active, focus-visible, compact | CSS: `.sidebar-nav-item:hover`, `:focus-visible`, compact layout rules; code: click and keyboard activation | Code + CSS; runtime pending | Active label is semibold; inactive label regular. |
| `PiPWindow` | idle, focus-visible, dragging, playing, paused, loading | CSS: `.pip-*` hover/focus/disabled rules; code: transport callbacks, drag via `gdk_toplevel_begin_move()`, playback-state observer | Code + CSS; runtime pending | Close affordance is visible and accessible; loading state should be checked with slow artwork. |
| `StateComponents` | empty, loading, error, retry | CSS: `.empty-state`, `.loading-state`, `.error-state`, `.error-state-retry`; code: `EmptyState.cpp`, `LoadingState.cpp`, `ErrorState.cpp`, `PageStateHost.hpp` | Code + CSS; runtime pending | Shared copy contract now covers Albums, Artists, Songs, Genres, Liked Music, Playlists, Favourites, and both detail pages; retry appears where applicable. |
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
