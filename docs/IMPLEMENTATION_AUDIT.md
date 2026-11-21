# Implementation Audit

> **Project:** Lollypop C++  
> **Date:** 21 November 2026  
> **Status:** Phase 0 Complete — Baseline established

---

## Component Audit

| Component | Exists? | Language | Compatible? | Action |
|-----------|---------|----------|-------------|--------|
| Application shell | ✅ | C++ | ✅ | Reuse |
| Playback service (IPlayerService) | ✅ | C++ | ✅ | Reuse + connect UI |
| Library service (ILibraryService) | ✅ | C++ | ✅ | Reuse + connect UI |
| Database (SQLite) | ✅ | C++ | ✅ | Reuse + add FTS5 |
| Artwork cache | ✅ | C++ | ✅ | Reuse + async decode |
| Waveform renderer | ✅ | C++ | ✅ | Reuse + fix double-buffer |
| Icon assets | ✅ | System | ✅ | Reuse (symbolic icons) |
| MockLibraryService | ✅ | C++ | ❌ | Remove from production |
| MockLyricsService | ✅ | C++ | ❌ | Remove from production |

---

## Library Pages — Current State

| Page | Backend Queries | Play Callback | Like Callback | Artwork | Notes |
|------|-----------------|---------------|---------------|---------|-------|
| AlbumsPage | `get_all_albums()` | ❌ TODO | N/A | Sync (blocking) | Uses AlbumCard |
| ArtistsPage | `get_all_artists()` | N/A | N/A | Sync | Uses ArtistItem |
| PlaylistsPage | `get_all_playlists()` | ❌ No action | N/A | Sync (placeholder) | Uses PlaylistItem |
| GenresPage | `get_all_genres()`, `get_tracks_by_genre()` | ❌ View only | N/A | N/A | View button not connected |
| SongsPage | `get_sorted_track_ids()`, `get_tracks_by_ids()` | ❌ TODO | ❌ TODO | Sync | **Loads ALL tracks** (not lazy) |
| FavouriteAlbumsPage | `get_favourite_albums()` | ❌ TODO | N/A | Sync | Uses AlbumCard |
| LikedMusicPage | `get_liked_tracks()` | ❌ TODO | ⚠️ Refresh only | Sync | Uses TrackRow |
| AlbumDetailPage | `get_album()`, `get_tracks_by_ids()` | ❌ TODO | ❌ TODO | Sync | Uses TrackRow |
| ArtistDetailPage | `get_artist()`, `get_tracks_by_artist()` | ❌ TODO | ❌ TODO | Sync | Uses TrackRow |
| HomePage | — | — | — | — | **Missing** (file not found) |
| SearchResultsPage | — | — | — | — | **Missing** |

---

## Playback Panels — Current State

| Panel | Track Info | Position | Waveform | Queue | Lyrics | Playback Modes | Volume | Controls |
|-------|------------|----------|----------|-------|--------|----------------|--------|----------|
| RightNowPlaying | ✅ | ✅ | ✅ | ❌ Manual | ❌ Manual | ❌ Disabled | ❌ | ❌ Disabled |
| BottomPlayback | ✅ | ✅ | ✅ | ❌ | N/A | ❌ Missing | ❌ | ❌ Disabled |

---

## Integration Gaps

### 1. Playback Actions (TODO → Backend)
- AlbumCard play → `IPlayerService::play_tracks(album.track_ids, 0)`
- TrackRow play → `IPlayerService::play_queue_index(index)` or `play_track(track_id)`
- AlbumDetailPage play → `play_tracks(album.track_ids, 0)`
- AlbumDetailPage shuffle → `play_tracks(shuffled_ids, 0)`
- PlaylistsPage play → `play_tracks(playlist.track_ids, 0)`
- GenresPage view → navigate to filtered view

### 2. Like/Favourite Actions (TODO → Backend)
- TrackRow like → `ILibraryService::toggle_like(track_id)`
- AlbumCard favourite → `ILibraryService::toggle_favorite_album(album_id)`

### 3. Artwork Loading (Sync → Async)
- AlbumCard, ArtistItem, PlaylistItem, AlbumDetailPage, ArtistDetailPage, ArtworkView
- All use `gtk_picture_set_file()` synchronously on main thread
- Need: `GTask` + `gdk_texture_new_from_file()` + thread pool

### 4. SongsPage (Eager → Lazy)
- Currently: `get_sorted_track_ids()` → `get_tracks_by_ids(ALL)` → populate all
- Need: `GListStore` + paged retrieval + prefetch

### 5. Search (Missing)
- Search entry exists in MiddleLibrary header
- No debounce, no generation counter, no SearchResultsPage

### 6. Queue Auto-Update (Missing)
- `queue_changed_cb_` connected in MainWindow but not forwarded to RightNowPlaying
- RightNowPlaying has `set_queue()` but never called automatically

### 6. Lyrics Auto-Request (Missing)
- `track_changed_cb_` connected but doesn't trigger lyrics request
- RightNowPlaying has `request_lyrics_for_track()` but never called

### 7. Playback Mode Buttons (Disabled)
- RightNowPlaying: `set_controls_sensitive(false)` — shuffle/repeat/loop disabled
- BottomPlayback: no mode buttons at all
- Need: enable + connect to `IPlayerService::set_shuffle/repeat/repeat_one`

### 8. Volume Slider (Missing)
- BottomPlayback: no volume control
- Need: connect to `IPlayerService::set_volume/get_volume`

### 9. HomePage (Missing)
- File not found: `frontend/src/ui/pages/HomePage.cpp`
- Should show: Recently Added, Recently Played, Favourite Albums

### 10. SearchResultsPage (Missing)
- Need: lazy-loading search results page

---

## Threading Verification

| Callback | Source Thread | Marshalled? | Target |
|----------|---------------|-------------|--------|
| `track_changed_cb_` | AudioEngine (GStreamer) | ✅ Glib::Dispatcher | MainWindow → panels |
| `position_changed_cb_` | AudioEngine | ✅ Glib::Dispatcher | MainWindow → panels |
| `playback_state_changed_cb_` | AudioEngine | ✅ Glib::Dispatcher | MainWindow → panels |
| `queue_changed_cb_` | BackendController (main context) | ✅ Direct | MainWindow → panels |
| `visualization_cb_` | EventBus (main context) | ✅ Direct | MainWindow → panels |
| `library_changed_cb_` | LibraryManager (worker) | ✅ Glib::Dispatcher | MiddleLibrary |
| `scan_progress_cb_` | LibraryManager (worker) | ✅ Glib::Dispatcher | — |
| `scan_finished_cb_` | LibraryManager (worker) | ✅ Glib::Dispatcher | — |

**All callbacks correctly marshalled to GTK main context.**

---

## BackendController Implementation Status

| Interface | Method | Implemented | Notes |
|-----------|--------|-------------|-------|
| IPlayerService | play_track/play_tracks | ✅ | |
| IPlayerService | play_queue_index | ✅ | |
| IPlayerService | toggle_play_pause | ✅ | |
| IPlayerService | next/previous/seek | ✅ | |
| IPlayerService | set_volume | ✅ | |
| IPlayerService | set_shuffle | ✅ | |
| IPlayerService | set_repeat/set_repeat_one | ✅ | |
| IPlayerService | enqueue/remove/move/clear | ✅ | QueueManager |
| ILibraryService | get_all_albums/artists/playlists/genres | ✅ | StorageManager |
| ILibraryService | get_sorted_track_ids | ✅ | Keyset pagination |
| ILibraryService | get_tracks_by_ids | ✅ | Batched |
| ILibraryService | search_tracks | ✅ | FTS5 ready |
| ILibraryService | toggle_like/favorite | ✅ | |
| ILyricsService | get_lyrics/request_lyrics | ✅ | LyricsManager |

---

## Next Steps (Phase 1)

Priority order:
1. Create HomePage
2. Connect all play/like callbacks to backend
3. Implement async artwork loading (ArtworkView + components)
4. Connect queue_changed_cb → RightNowPlaying.set_queue()
5. Connect track_changed_cb → auto-request lyrics
6. Enable playback mode buttons in RightNowPlaying
7. Add volume slider + mode buttons to BottomPlayback
8. Implement lazy SongsPage (GListStore + paged)
9. Connect search with debounce + generation counter
10. Create SearchResultsPage
11. Add FTS5 to StorageManager schema