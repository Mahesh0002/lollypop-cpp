# Frontend-Backend Integration Plan

> **Project:** Lollypop C++  
> **Status:** Planning  
> **Target:** Connect all frontend UI components to the BackendController storage and services

---

## 1. Current Architecture Overview

### 1.1 Backend (BackendController)
The `BackendController` in `backend/src/controller.cpp` implements **three service interfaces**:
- `IPlayerService` — Playback control, state, queue management
- `ILibraryService` — Library queries (albums, artists, tracks, playlists, search, sorting)
- `ILyricsService` — Lyrics retrieval and storage

**Key subsystems owned by BackendController:**
| Subsystem | Purpose |
|-----------|---------|
| `StorageManager` | SQLite database (tracks, albums, artists, playlists, lyrics, preferences) |
| `AudioEngine` | GStreamer-based playback |
| `LibraryScanner` + `LibraryManager` | Async library scanning with unique work queue |
| `MprisServer` | MPRIS D-Bus service (this app as a player) |
| `MprisManager` | MPRIS D-Bus client (external players) |
| `LyricsManager` | Embedded/local/external lyrics fetch, parse, sync |
| `CacheManager` + `ArtworkManager` | Thumbnail cache with size buckets |
| `VisualizerEngine` | GStreamer spectrum element → EventBus |
| `FileWatcher` | inotify-based library change detection |
| `QueueManager` | Shuffle, repeat, next/previous logic |

**Callback marshaling:** All UI callbacks (`track_changed_cb_`, `position_changed_cb_`, `playback_state_changed_cb_`, `queue_changed_cb_`, `visualization_cb_`, `library_changed_cb_`) are emitted from worker threads and marshaled to the GTK main context via `Glib::Dispatcher`.

---

### 1.2 Frontend (Current State)
The frontend in `frontend/src/` uses **GTK 4 + libadwaita C APIs** directly from C++.

**Key UI panels:**
| Panel | File | Current Backend Connection |
|-------|------|----------------------------|
| `MainWindow` | `main_window.cpp` | ✅ Connected to BackendController callbacks |
| `LeftSidebar` | `left_sidebar.cpp` | Navigation only, no data |
| `MiddleLibrary` | `middle_library.cpp` | ✅ Passes `ILibraryService*` to pages |
| `RightNowPlaying` | `right_now_playing.cpp` | ✅ Track info, position, waveform, lyrics, queue |
| `BottomPlayback` | `bottom_playback.cpp` | ✅ Track info, position, waveform, controls |

**Library pages** (in `frontend/src/ui/pages/`):
- `HomePage` — Uses `ILibraryService` for recently added/played, favourite albums
- `AlbumsPage` — Uses `ILibraryService::get_all_albums()`
- `ArtistsPage` — Uses `ILibraryService::get_all_artists()`
- `PlaylistsPage` — Uses `ILibraryService::get_all_playlists()`
- `GenresPage` — Uses `ILibraryService::get_all_genres()`
- `SongsPage` — Uses `ILibraryService::get_sorted_track_ids()` + `get_tracks_by_ids()` (lazy)
- `FavouriteAlbumsPage` — Uses `ILibraryService::get_favourite_albums()`
- `LikedMusicPage` — Uses `ILibraryService::get_liked_tracks()`
- `AlbumDetailPage` — Uses `ILibraryService::get_album()` + `get_tracks_by_ids()`
- `ArtistDetailPage` — Uses `ILibraryService::get_artist()` + `get_tracks_by_artist()`

**Service implementations:**
- `MockLibraryService` — Returns empty data (development only)
- `MockLyricsService` — Returns `std::nullopt` (development only)

---

## 2. Integration Gaps Analysis

### 2.1 Library Pages → BackendController (ILibraryService)
**Status:** Pages accept `ILibraryService*` but some may still use mock data internally.

| Page | Required ILibraryService Methods | Gap |
|------|----------------------------------|-----|
| `HomePage` | `get_recently_added()`, `get_recently_played()`, `get_favourite_albums()` | Verify no mock fallbacks |
| `AlbumsPage` | `get_all_albums()` | Verify artwork loading via `get_artwork_path_for_album()` |
| `ArtistsPage` | `get_all_artists()` | Verify artwork loading via `get_artwork_path_for_artist()` |
| `PlaylistsPage` | `get_all_playlists()`, `get_playlist_tracks()` | Verify playlist track counts |
| `GenresPage` | `get_all_genres()`, `get_tracks_by_genre()` | Verify genre filtering |
| `SongsPage` | `get_sorted_track_ids()`, `get_tracks_by_ids()` | **Critical:** Lazy loading + DB-backed sort |
| `FavouriteAlbumsPage` | `get_favourite_albums()` | Verify favourite toggle |
| `LikedMusicPage` | `get_liked_tracks()` | Verify like toggle |
| `AlbumDetailPage` | `get_album()`, `get_tracks_by_ids()` | Verify track list + play actions |
| `ArtistDetailPage` | `get_artist()`, `get_tracks_by_artist()` | Verify albums + tracks |

### 2.2 RightNowPlaying → BackendController
| Feature | Current | Required |
|---------|---------|----------|
| Track info | ✅ Via `track_changed_cb_` | ✅ |
| Playback position | ✅ Via `position_changed_cb_` | ✅ |
| Waveform | ✅ Via `visualization_cb_` | ✅ |
| Playback modes (shuffle/repeat/loop) | ❌ UI buttons disabled | Connect `set_shuffle`, `set_repeat`, `set_repeat_one` callbacks |
| Queue | ❌ Manual `populate_queue()` | Connect `queue_changed_cb_` → auto-update |
| Lyrics | ❌ Manual `request_lyrics_for_track()` | Connect `track_changed_cb_` → auto-request lyrics |
| Previous/Next buttons | ✅ Connected to backend | ✅ |

### 2.3 BottomPlayback → BackendController
| Feature | Current | Required |
|---------|---------|----------|
| Track info | ✅ Via `track_changed_cb_` | ✅ |
| Playback position | ✅ Via `position_changed_cb_` | ✅ |
| Waveform | ✅ Via `visualization_cb_` | ✅ |
| Play/Pause/Prev/Next | ✅ Buttons disabled (shell authority) | ✅ Keep disabled |
| Volume | ❌ Not connected | Add volume slider callback |
| Playback modes | ❌ Not shown | Add shuffle/repeat/loop buttons |

### 2.4 Search → BackendController
| Component | Current | Required |
|-----------|---------|----------|
| Search entry (MiddleLibrary header) | UI only | Connect to `ILibraryService::search_tracks()` with debounce + generation counter |
| Search results page | Not implemented | Create `SearchResultsPage` using lazy model |

### 2.5 Artwork Pipeline
| Stage | Current | Required |
|-------|---------|----------|
| Backend: `ArtworkManager` + `CacheManager` | ✅ Implemented | Verify bucket sizes (64/160/512/1024) |
| Backend: `get_artwork_path_for_*` | ✅ Implemented | Returns cached file path for MPRIS `artUrl` |
| Frontend: `ArtworkView` | Loads from path | Use `GdkTexture` from file, async decode, cache reuse |
| Frontend: Fallback artwork | Placeholder | Implement generated fallback (colored gradient) |

---

## 3. Detailed Pipeline Design

### 3.1 Library Data Pipeline (Read Path)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           LIBRARY READ PIPELINE                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  UI COMPONENT                    ILibraryService              BACKEND       │
│  ────────────                    ──────────────              ───────       │
│                                                                              │
│  AlbumsPage.get_all_albums() ──────► get_all_albums() ──────► StorageManager│
│       │                              │                            │          │
│       │                              │                            ▼          │
│       │                              │                     SQLite Query       │
│       │                              │                            │          │
│       │◄─────────────────────────────┤                            │          │
│       │       vector<Album>          │                            │          │
│       │                              │                            │          │
│       ▼                              │                            │          │
│  For each album:                    │                            │          │
│  get_artwork_path_for_album() ─────► get_artwork_path_for_album()──► Cache/DB │
│       │                              │                            │          │
│       │◄─────────────────────────────┤                            │          │
│       │       string (file path)     │                            │          │
│       │                              │                            │          │
│       ▼                              │                            │          │
│  ArtworkView.set_artwork_path()      │                            │          │
│       │                              │                            │          │
│       ▼                              │                            │          │
│  Async decode → GdkTexture → cache   │                            │          │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  SongsPage (LAZY LOADING WITH DB-BACKED SORT)                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  User clicks sort column "title" ASC                                        │
│       │                                                                      │
│       ▼                                                                      │
│  SongsPage: get_sorted_track_ids("title", true) ──────────────────────────► │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     SQL: SELECT id FROM tracks                       │
│       │                     ORDER BY title ASC LIMIT 10000                   │
│       │                              │                                      │
│       │◄─────────────────────────────┤                                      │
│       │       vector<string> track_ids (globally sorted)                    │
│       │                              │                                      │
│       ▼                              │                                      │
│  Store track_ids in memory                                              │
│       │                                                                      │
│       ▼                                                                      │
│  GtkColumnView requests row N                                             │
│       │                                                                      │
│       ▼                                                                      │
│  LazyListModel::get_item(N)                                               │
│       │                                                                      │
│       ▼                                                                      │
│  get_tracks_by_ids({track_ids[N]}) ─────────────────────────────────────► │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     SQL: SELECT * FROM tracks                        │
│       │                     WHERE id = ?                                     │
│       │                              │                                      │
│       │◄─────────────────────────────┤                                      │
│       │       Track (full metadata)                                         │
│       │                              │                                      │
│       ▼                              │                                      │
│  TrackRow renders with artwork via get_artwork_path_for_track()             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key Implementation Details:**

1. **Database-backed sorting (SongsPage):**
   - `ILibraryService::get_sorted_track_ids(sortColumn, ascending)` → `BackendController::get_sorted_track_ids()` → `StorageManager::get_tracks_paginated(limit, sortColumn, "", "")`
   - Returns **globally ordered track IDs** (not just visible rows)
   - Frontend stores ID vector in memory (~10k IDs = ~80 KB)
   - `ILibraryService::get_tracks_by_ids(ids)` → batch fetch full metadata for visible rows only

2. **Lazy model for GtkColumnView:**
   - Implement `GListModel` wrapper (`LazyTrackListModel`)
   - `get_item(position)` → if cached return, else `get_tracks_by_ids({id})` async
   - Use `GTask` + thread pool for async fetch, `g_list_model_items_changed()` on completion

3. **Artwork loading:**
   - `ArtworkView.set_artwork_path(path)` → spawn worker thread → `gdk_texture_new_from_file()` → `g_main_context_invoke()` → `gtk_image_set_from_paintable()`
   - Cache `GdkTexture*` in `ArtworkView` with LRU (max 100 textures)
   - Fallback: generate colored gradient from album ID hash

### 3.2 Library Write Pipeline (Like/Favourite/Playlist)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           LIBRARY WRITE PIPELINE                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  UI ACTION                      ILibraryService              BACKEND       │
│  ──────────                     ──────────────              ───────       │
│                                                                              │
│  User clicks ♡ on AlbumCard                                               │
│       │                                                                      │
│       ▼                                                                      │
│  AlbumCard.toggle_favorite()                                                │
│       │                                                                      │
│       ▼                                                                      │
│  library_service->toggle_favorite_album(album_id) ───────────────────────► │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     BackendController::toggle_favorite_album()      │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     storage_->toggle_favorite_album(album_id)       │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     SQLite: UPDATE albums SET favorite = NOT        │
│       │                     favorite WHERE id = ?                           │
│       │                              │                                      │
│       │                              ▼                                      │
│       │                     emit_library_changed()                          │
│       │                              │                                      │
│       │◄─────────────────────────────┤  (via Glib::Dispatcher)             │
│       │                              │                                      │
│       ▼                              │                                      │
│  MiddleLibrary.refresh_active_page()                                        │
│       │                                                                      │
│       ▼                                                                      │
│  AlbumsPage/FavouriteAlbumsPage refresh() → re-query get_all_albums()       │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  Playlist Operations                                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  create_playlist(name) ──────► StorageManager::create_playlist()            │
│  add_track_to_playlist() ────► StorageManager::add_track_to_playlist()      │
│  delete_playlist() ─────────► StorageManager::delete_playlist()             │
│  All emit library_changed → UI refresh                                       │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 Playback Pipeline (Read Path)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PLAYBACK READ PIPELINE                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  BACKEND (BackendController)              GTK MAIN CONTEXT           UI    │
│  ────────────────────────────              ─────────────────           ──    │
│                                                                              │
│  AudioEngine position callback                                            │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::emit_position_changed(pos, dur)                         │
│       │                                                                      │
│       ▼                                                                      │
│  Glib::Dispatcher → main context                                            │
│       │                                                                      │
│       ▼                                                                      │
│  MainWindow::connect_backend() lambda                                       │
│       │                                                                      │
│       ├──► bottom_playback_->update_playback_position(pos, dur)             │
│       │       │                                                              │
│       │       ├──► Update time label "M:SS / M:SS"                          │
│       │       │                                                              │
│       │       └──► waveform_view_->set_progress(pos/dur)                    │
│       │                                                                      │
│       └──► right_now_playing_->update_playback_position(pos, dur)           │
│               │                                                              │
│               ├──► Update time label                                        │
│               │                                                              │
│               └──► waveform_view_->set_progress(pos/dur)                    │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  Track Change                                                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  BackendController::start_queue_index()                                     │
│       │                                                                      │
│       ├──► Load track in AudioEngine                                        │
│       │                                                                      │
│       ├──► emit_track_changed(current_track) ──────► GTK main context ────►│
│       │                                                                          │
│       │          ├──► bottom_playback_->update_track_info(title, artist,    │
│       │          │                    artwork_path)                          │
│       │          │                    │                                     │
│       │          │                    ├──► Set labels                       │
│       │          │                    │                                     │
│       │          │                    └──► ArtworkView.set_artwork_path()   │
│       │          │                                                              │
│       │          └──► right_now_playing_->update_track_info(...)            │
│       │                      │                                              │
│       │                      ├──► Set labels                                │
│       │                      │                                              │
│       │                      ├──► ArtworkView.set_artwork_path()            │
│       │                      │                                              │
│       │                      └──► request_lyrics_for_track(track.id) ──────►│
│       │                                                                          │
│       ├──► emit_queue_changed(queue_ids) ──────► GTK main context ────────►│
│       │                                                                          │
│       │          └──► right_now_playing_->set_queue(queue_ids)              │
│       │                                                                          │
│       └──► emit_playback_state_changed(true) ──────► GTK main context ────►│
│                                                                              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.4 Playback Pipeline (Write Path — Shell Authority)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PLAYBACK WRITE PIPELINE                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  SHELL (media keys, sound menu)         MPRIS D-BUS            BACKEND     │
│  ──────────────────────────         ──────────────            ───────      │
│                                                                              │
│  User presses "Play" media key                                              │
│       │                                                                      │
│       ▼                                                                      │
│  MPRIS D-Bus: org.mpris.MediaPlayer2.Player.Play                            │
│       │                                                                      │
│       ▼                                                                      │
│  MprisServer::on_play()                                                     │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::mpris_server_callback (set_playback_state_changed)     │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::toggle_play_pause()                                     │
│       │                                                                      │
│       ▼                                                                      │
│  AudioEngine::play() / pause()                                              │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::set_playing(true/false)                                 │
│       │                                                                      │
│       ├──► mpris_server_->update_playback_status()                          │
│       │                                                                      │
│       └──► emit_playback_state_changed() ──────► GTK main context ────────►│
│                                                                              │
│  GTK UI buttons: set_controls_sensitive(FALSE) — read-only display          │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  Seek (MPRIS SetPosition / Seek)                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  MPRIS SetPosition(track_id, position) ──────► BackendController::seek()   │
│  MPRIS Seek(offset) ─────────────────────────► BackendController::seek()   │
│                                                                              │
│  Frontend (waveform drag):                                                  │
│  1. Drag → optimistic local playhead update                                 │
│  2. Release → BackendController::seek(position)                             │
│  3. Backend → AudioEngine::seek() → position callback → UI reconcile        │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.5 Queue Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              QUEUE PIPELINE                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  BACKEND (QueueManager)              GTK MAIN CONTEXT            RIGHT PANEL│
│  ──────────────────────────              ─────────────────            ──────│
│                                                                              │
│  User clicks "Play Album" on AlbumCard                                      │
│       │                                                                      │
│       ▼                                                                      │
│  AlbumCard.play_callback → main_window_->get_backend()->play_tracks(ids, 0) │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::play_tracks(track_ids, start_index)                     │
│       │                                                                      │
│       ├──► queue_manager_->set_queue(track_ids, start_index)                │
│       │                                                                      │
│       ├──► persist_queue() (storage preference)                             │
│       │                                                                      │
│       ├──► start_queue_index(clamped_index)                                 │
│       │       │                                                              │
│       │       ├──► Load track → AudioEngine                                 │
│       │       │                                                              │
│       │       ├──► emit_track_changed() ──────► UI panels                   │
│       │       │                                                              │
│       │       ├──► emit_queue_changed() ──────► RightNowPlaying.set_queue() │
│       │       │                                                              │
│       │       └──► emit_playback_state_changed() ──────► UI panels          │
│       │                                                                      │
│       ▼                                                                      │
│  RightNowPlaying::set_queue(queue_track_ids)                                │
│       │                                                                      │
│       ├──► Clear existing GtkListBox rows                                   │
│       │                                                                      │
│       └──► For each track_id:                                               │
│               library_service->get_track(track_id) → TrackRow               │
│               TrackRow.play_callback → backend->play_queue_index(index)     │
│               TrackRow.like_callback → library_service->toggle_like()       │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  Queue Mutations (Reorder, Remove, Clear)                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  RightNowPlaying queue drag-drop / remove button                            │
│       │                                                                      │
│       ▼                                                                      │
│  backend->move_queue_item(from, to) / remove_from_queue(index)              │
│       │                                                                      │
│       ▼                                                                      │
│  QueueManager mutates internal deque                                         │
│       │                                                                      │
│       ├──► persist_queue()                                                  │
│       │                                                                      │
│       └──► emit_queue_changed() ──────► RightNowPlaying.set_queue()         │
│               (full rebuild — acceptable for queue sizes < 500)             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.6 Lyrics Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              LYRICS PIPELINE                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  TRACK CHANGE                    ILyricsService              BACKEND       │
│  ────────────                    ──────────────              ───────       │
│                                                                              │
│  BackendController::emit_track_changed(track) ──────► GTK main context ────►│
│       │                                                                          │
│       ▼                                                                          │
│  RightNowPlaying::update_track_info()                                           │
│       │                                                                          │
│       └──► request_lyrics_for_track(track.id)                                   │
│               │                                                                   │
│               ▼                                                                   │
│  lyrics_service->request_lyrics(track_id, callback) ───────────────────────►  │
│               │                                                                   │
│               ▼                                                                   │
│  BackendController::request_lyrics()                                            │
│               │                                                                   │
│               ▼                                                                   │
│  LyricsManager::request_lyrics(track_id, artist, title, callback)              │
│               │                                                                   │
│               ├──► Check storage_->get_lyrics(track_id) → callback if cached    │
│               │                                                                   │
│               ├──► Check embedded lyrics (TagLib) → callback if found           │
│               │                                                                   │
│               ├──► Check local .lrc file → callback if found                    │
│               │                                                                   │
│               └──► (Future) External provider API → callback                    │
│               │                                                                   │
│               ▼                                                                   │
│  LyricsManager stores result → storage_->set_lyrics()                           │
│               │                                                                   │
│               ▼                                                                   │
│  callback(track_id, Lyrics) → Glib::Dispatcher → GTK main context              │
│               │                                                                   │
│               ▼                                                                   │
│  RightNowPlaying callback → set_lyrics(text)                                    │
│               │                                                                   │
│               ├──► If text empty: show lyrics_empty_state                       │
│               │                                                                   │
│               └──► If text exists: GtkTextBuffer set_text, show lyrics view     │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.7 Visualization Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           VISUALIZATION PIPELINE                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  GSTREAMER AUDIO PIPELINE          EVENTBUS            GTK MAIN CONTEXT   │
│  ───────────────────────────          ───────            ───────────────   │
│                                                                              │
│  AudioEngine pipeline includes:                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ source → decode → audioconvert → spectrum → audiosink               │   │
│  │                          │                                           │   │
│  │                          ▼                                           │   │
│  │              spectrum element posts "spectrum" message              │   │
│  │              (magnitudes: vector<float>, 64 bands)                  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                           │                                                  │
│                           ▼                                                  │
│  VisualizerEngine (bus sync handler)                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 1. Parse spectrum message → magnitudes vector                       │   │
│  │ 2. Apply smoothing (exponential moving average)                     │   │
│  │ 3. Conflate: keep only latest frame (ring buffer of 1)              │   │
│  │ 4. EventBus::publish("spectrum", SpectrumFrame{magnitudes})         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                           │                                                  │
│                           ▼                                                  │
│  EventBus delivers to subscribers on main context                           │
│                           │                                                  │
│                           ▼                                                  │
│  BackendController subscription:                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ EventBus::subscribe("spectrum", [this](Event e) {                   │   │
│  │     if (auto* frame = get<SpectrumFrame>(e.payload)) {             │   │
│  │         if (visualization_cb_) visualization_cb_(frame->magnitudes);│   │
│  │     }                                                               │   │
│  │ });                                                                 │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                           │                                                  │
│                           ▼                                                  │
│  MainWindow callbacks:                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ backend->set_visualization_changed_callback([this](points) {       │   │
│  │     bottom_playback_->set_points(points);                           │   │
│  │     right_now_playing_->set_points(points);                         │   │
│  │ });                                                                 │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                           │                                                  │
│                           ▼                                                  │
│  WaveformView (both panels):                                                │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ VisualizerModel.samples = points                                    │   │
│  │ VisualizerModel.sequenceNumber++                                    │   │
│  │ gtk_widget_queue_draw(waveform_widget)                              │   │
│  │                                                                     │   │
│  │ snapshot() {                                                        │   │
│  │     // GSK render: bars, accent color left of playhead,             │   │
│  │     // dimmed color right of playhead, playhead indicator           │   │
│  │ }                                                                   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.8 Library Scan & File Watcher Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         LIBRARY SCAN & WATCH PIPELINE                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  INITIAL SCAN                                                                │
│  ────────────                                                                │
│                                                                              │
│  Application::on_activate()                                                 │
│       │                                                                      │
│       ▼                                                                      │
│  backend_->scan_default_music_library()                                     │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::scan_default_music_library()                            │
│       │                                                                      │
│       ├──► storage_->add_library_root(path)                                 │
│       │                                                                      │
│       └──► library_manager_->scan_directory(path, true)                     │
│               │                                                              │
│               ▼                                                              │
│  LibraryManager worker thread:                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ while (running) {                                                   │   │
│  │     job = queue.pop()                                               │   │
│  │     storage_->begin_transaction()                                   │   │
│  │     scanner_->scan_directory(job.path, job.recursive)               │   │
│  │     // scanner calls track_cb_(track) for each found track          │   │
│  │     // BackendController track_cb_ → storage_->add_track(track)     │   │
│  │     storage_->commit_transaction()                                  │   │
│  │     finished_cb_(success, cancelled)                                │   │
│  │ }                                                                   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│               │                                                              │
│               ▼                                                              │
│  BackendController::dispatch_scan_updates() (Glib::Dispatcher)              │
│               │                                                              │
│               ├──► scan_progress_cb_(progress) → UI progress indicator      │
│               │                                                              │
│               └──► scan_finished_cb_(success, cancelled)                    │
│                       │                                                      │
│                       ▼                                                      │
│               emit_library_changed() → MiddleLibrary.refresh_active_page()  │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  FILE WATCHER (inotify)                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  FileWatcher thread detects: Created/Modified/Deleted/Moved                │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::handle_file_event() → queue event → Glib::Dispatcher   │
│       │                                                                      │
│       ▼                                                                      │
│  BackendController::dispatch_file_events() (main context)                  │
│       │                                                                      │
│       ├──► Created: library_manager_->scan_file() / scan_directory()       │
│       │                                                                      │
│       ├──► Modified: library_manager_->scan_file() + update current track  │
│       │                                                                      │
│       ├──► Deleted: storage_->delete_track_by_path() / delete_tracks...    │
│       │           if current track affected → stop()                        │
│       │                                                                      │
│       └──► Moved: delete old + scan new                                     │
│       │                                                                      │
│       ▼                                                                      │
│  emit_library_changed() → MiddleLibrary.refresh_active_page()              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Implementation Tasks

### Phase 1: Connect Library Pages to Backend (Week 1)

| Task | File(s) | Description |
|------|---------|-------------|
| 1.1 | `AlbumsPage.cpp`, `ArtistsPage.cpp`, `PlaylistsPage.cpp`, `GenresPage.cpp`, `FavouriteAlbumsPage.cpp`, `LikedMusicPage.cpp` | Verify all `refresh()` methods call `library_service->get_*()` and populate UI. Remove any mock fallbacks. |
| 1.2 | `SongsPage.cpp` | Implement lazy loading with `LazyTrackListModel`:<br>- `get_sorted_track_ids()` on sort change<br>- Store ID vector<br>- `GListModel` wrapper with async `get_tracks_by_ids()`<br>- Artwork via `get_artwork_path_for_track()` |
| 1.3 | `AlbumDetailPage.cpp`, `ArtistDetailPage.cpp` | Verify `set_album()`/`set_artist()` fetch details and tracks via `library_service`. Connect play/shuffle buttons to `backend->play_tracks()`. |
| 1.4 | `HomePage.cpp` | Verify `refresh()` calls `get_recently_added()`, `get_recently_played()`, `get_favourite_albums()`. |
| 1.5 | `ArtworkView.cpp` | Implement async artwork loading:<br>- Worker thread: `gdk_texture_new_from_file()`<br>- Main context: `gtk_image_set_from_paintable()`<br>- LRU cache (100 textures)<br>- Fallback gradient generation |
| 1.6 | `MiddleLibrary.cpp` | Connect search entry → debounced `search_tracks()` with generation counter → `SearchResultsPage` (new). |

### Phase 2: Connect Playback & Queue UI (Week 2)

| Task | File(s) | Description |
|------|---------|-------------|
| 2.1 | `RightNowPlaying.cpp` | Connect `queue_changed_cb_` → `set_queue()` auto-update (remove manual `populate_queue` call). |
| 2.2 | `RightNowPlaying.cpp` | Connect `track_changed_cb_` → auto `request_lyrics_for_track()` (already partially done). |
| 2.3 | `RightNowPlaying.cpp` | Enable playback mode buttons (shuffle/repeat/loop) — currently `set_controls_sensitive(false)`. Connect toggled callbacks to `backend->set_shuffle/repeat/repeat_one()`. Update button states via `queue_changed_cb_` or dedicated mode callbacks. |
| 2.4 | `BottomPlayback.cpp` | Add volume slider connected to `backend->set_volume()` + `get_volume()`. Add playback mode buttons (shuffle/repeat/loop) synced with right panel. |
| 2.5 | `MainWindow.cpp` | Add `BackendController` mode callbacks: `set_shuffle_callback`, `set_repeat_callback`, `set_repeat_one_callback` (extend `IPlayerService` if needed). |

### Phase 3: Lyrics & Search (Week 3)

| Task | File(s) | Description |
|------|---------|-------------|
| 3.1 | `LyricsManager.cpp` | Verify embedded lyrics (TagLib), local `.lrc` files, storage cache. Implement `request_lyrics()` async flow. |
| 3.2 | `RightNowPlaying.cpp` | Handle synced lyrics (LRC parsing) → highlight current line, auto-scroll. |
| 3.3 | `MiddleLibrary.cpp` + new `SearchResultsPage.cpp` | Implement search with debounce (300ms) + generation counter. Results page uses lazy model like SongsPage. |
| 3.4 | `BackendController.cpp` | Add `set_shuffle_callback`, `set_repeat_callback`, `set_repeat_one_callback` to `IPlayerService` interface and implementation. |

### Phase 4: Artwork & Polish (Week 4)

| Task | File(s) | Description |
|------|---------|-------------|
| 4.1 | `ArtworkManager.cpp`, `CacheManager.cpp` | Validate bucket sizes (64, 160, 512, 1024). Test at 1×, 1.25×, 1.5×, 2× scale. |
| 4.2 | `ArtworkView.cpp` | Implement crossfade transition on artwork change (200-300ms). |
| 4.3 | `BackendController.cpp` | Ensure MPRIS `artUrl` points to cached artwork file (not memory-only texture). |
| 4.4 | All pages | Test empty states, loading states, error states. |
| 4.5 | `VisualizerEngine.cpp` | Verify spectrum smoothing, conflation, 30 FPS target. |

---

## 5. Data Flow Summary Tables

### 5.1 Read Operations (UI → Backend)

| UI Component | Service Method | Backend Method | Storage Method | Thread |
|--------------|----------------|----------------|----------------|--------|
| AlbumsPage | `get_all_albums()` | `BackendController::get_all_albums()` | `StorageManager::get_all_albums()` | Worker (via LibraryManager) |
| ArtistsPage | `get_all_artists()` | `BackendController::get_all_artists()` | `StorageManager::get_all_artists()` | Worker |
| SongsPage (sort) | `get_sorted_track_ids(col, asc)` | `BackendController::get_sorted_track_ids()` | `StorageManager::get_tracks_paginated()` | Worker |
| SongsPage (lazy) | `get_tracks_by_ids(ids)` | `BackendController::get_tracks_by_ids()` | `StorageManager::get_track()` (batched) | Worker |
| AlbumDetail | `get_album(id)` | `BackendController::get_album()` | `StorageManager::get_album()` | Worker |
| Search | `search_tracks(query)` | `BackendController::search_tracks()` | `StorageManager::search_tracks()` (FTS5) | Worker |
| Artwork | `get_artwork_path_for_*` | `BackendController::get_artwork_path_for_*` | `StorageManager::get_*` + CacheManager | Main (path) → Worker (decode) |

### 5.2 Write Operations (UI → Backend → Storage)

| UI Action | Service Method | Backend Method | Storage Method | Callback |
|-----------|----------------|----------------|----------------|----------|
| Toggle like | `toggle_like(track_id)` | `BackendController::toggle_like()` | `StorageManager::toggle_like()` | `emit_library_changed()` |
| Toggle favourite | `toggle_favorite_album(album_id)` | `BackendController::toggle_favorite_album()` | `StorageManager::toggle_favorite_album()` | `emit_library_changed()` |
| Create playlist | `create_playlist(name)` | `BackendController::create_playlist()` | `StorageManager::create_playlist()` | `emit_library_changed()` |
| Add to playlist | `add_track_to_playlist()` | `BackendController::add_track_to_playlist()` | `StorageManager::add_track_to_playlist()` | `emit_library_changed()` |
| Play album | `play_tracks(ids, idx)` | `BackendController::play_tracks()` | `QueueManager::set_queue()` + persist | `emit_track_changed`, `emit_queue_changed`, `emit_playback_state_changed` |
| Seek | `seek(pos)` | `BackendController::seek()` | `AudioEngine::seek()` | `emit_position_changed` |
| Volume | `set_volume(vol)` | `BackendController::set_volume()` | `AudioEngine::set_volume()` + pref | `mpris_server_->update_volume()` |
| Shuffle/Repeat | `set_shuffle/repeat/repeat_one()` | `BackendController::set_shuffle/repeat/repeat_one()` | `QueueManager` + pref | `emit_queue_changed` + MPRIS |

### 5.3 Push Notifications (Backend → UI)

| Callback | Trigger | UI Consumers |
|----------|---------|--------------|
| `track_changed_cb_` | Track load, track change, stop | BottomPlayback, RightNowPlaying |
| `position_changed_cb_` | AudioEngine position (every ~200ms) | BottomPlayback, RightNowPlaying |
| `playback_state_changed_cb_` | Play/pause/stop | BottomPlayback, RightNowPlaying |
| `queue_changed_cb_` | Queue set, reorder, remove, clear | RightNowPlaying (queue view) |
| `visualization_cb_` | Spectrum frame (EventBus) | BottomPlayback, RightNowPlaying (waveform) |
| `library_changed_cb_` | Scan finish, file watcher, like/fav change | MiddleLibrary (refresh active page) |

---

## 6. Threading & Main-Context Rules

| Operation | Thread | Main-Context Marshalling |
|-----------|--------|--------------------------|
| SQLite queries (read) | LibraryManager worker / thread pool | Results via `Glib::Dispatcher` → callback |
| SQLite writes (scan, like, playlist) | LibraryManager worker (batched transactions) | `emit_library_changed()` via `Glib::Dispatcher` |
| AudioEngine callbacks | GStreamer streaming thread | `Glib::Dispatcher` (position, state, EOS) |
| MPRIS D-Bus callbacks | D-Bus thread (GLib main loop) | Direct (already on GLib main context) |
| FileWatcher events | inotify thread | Queue → `Glib::Dispatcher` → `dispatch_file_events()` |
| LyricsManager fetch | Worker thread | `Glib::Dispatcher` → callback |
| Artwork decode | Worker thread (GTask) | `g_main_context_invoke()` → `gtk_image_set_from_paintable()` |
| Visualization (spectrum) | VisualizerEngine (bus sync handler) | EventBus → main context subscription |
| UI callbacks (GTK signals) | GTK main thread | N/A (already on main context) |

**Golden Rule:** Never touch GTK widgets from non-main threads. All backend-to-UI callbacks **must** go through `Glib::Dispatcher` or `g_main_context_invoke()`.

---

## 7. Acceptance Criteria

| Criterion | Verification |
|-----------|--------------|
| Library pages show real data | Scan Music folder → Albums/Artists/Songs pages populate |
| Songs page sorts correctly | Click column headers → global DB sort, lazy load works |
| Play album from AlbumsPage | Click Play → playback starts, queue populated, right panel updates |
| Queue in RightNowPlaying | Play album → queue shows tracks, click track → plays that track |
| Lyrics display | Play track with embedded lyrics → Lyrics tab shows synced text |
| Search works | Type in search entry → results appear in SearchResultsPage |
| Artwork loads async | Scroll AlbumsPage → artwork appears without UI freeze |
| MPRIS controls work | Media keys / sound menu → play/pause/next/prev/seek/volume |
| File watcher works | Add/remove file in Music folder → library updates automatically |
| Visualization runs | Play audio → waveform animates in both panels at ~30 FPS |
| No main-thread blocking | Profile: no >16ms spikes on main thread during scan/playback |

---

## 8. File Map for Implementation

```
frontend/src/
├── application.cpp                 # ✅ Creates BackendController
├── main_window.cpp                 # ✅ Connects backend callbacks
├── panels/
│   ├── middle_library.cpp          # 🔄 Connect search, verify pages
│   ├── right_now_playing.cpp       # 🔄 Connect queue_changed, lyrics, modes
│   ├── bottom_playback.cpp         # 🔄 Add volume, mode buttons
│   └── left_sidebar.cpp            # Navigation only
├── ui/pages/
│   ├── AlbumsPage.cpp              # 🔄 Verify get_all_albums + artwork
│   ├── ArtistsPage.cpp             # 🔄 Verify get_all_artists + artwork
│   ├── SongsPage.cpp               # ⬜ Implement LazyTrackListModel
│   ├── PlaylistsPage.cpp           # 🔄 Verify get_all_playlists
│   ├── GenresPage.cpp              # 🔄 Verify get_all_genres
│   ├── FavouriteAlbumsPage.cpp     # 🔄 Verify get_favourite_albums
│   ├── LikedMusicPage.cpp          # 🔄 Verify get_liked_tracks
│   ├── AlbumDetailPage.cpp         # 🔄 Verify get_album + play actions
│   ├── ArtistDetailPage.cpp        # 🔄 Verify get_artist + tracks
│   ├── HomePage.cpp                # 🔄 Verify recently added/played/fav
│   └── SearchResultsPage.cpp       # ⬜ NEW: lazy search results
├── ui/components/
│   ├── ArtworkView.cpp             # ⬜ Async load, cache, fallback
│   ├── AlbumCard.cpp               # 🔄 Connect play_callback to backend
│   ├── TrackRow.cpp                # 🔄 Connect play/like callbacks
│   └── ...
├── services/
│   ├── MockLibraryService.cpp      # 🗑️ Remove once all pages use real service
│   ├── MockLyricsService.cpp       # 🗑️ Remove once LyricsManager works
│   └── ...
├── models/
│   └── LazyTrackListModel.cpp      # ⬜ NEW: GListModel wrapper for lazy loading
└── data/
    └── ArtworkCache.cpp            # 🔄 Integrate with BackendController cache

backend/src/
├── controller.cpp                  # ✅ Core implementation
├── library_manager.cpp             # ✅ Scan coordination
├── storage_manager.cpp             # ✅ SQLite operations
├── lyrics_manager.cpp              # 🔄 Verify embedded/local fetch
├── artwork_manager.cpp             # 🔄 Verify bucket sizes
├── visualizer_engine.cpp           # 🔄 Verify smoothing/conflation
└── ...
```

---

## 9. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| `SongsPage` lazy model complexity | High | Prototype `LazyTrackListModel` separately with 1000 dummy IDs first |
| Artwork decode blocking main thread | Medium | Use `GTask` + thread pool, verify with `GDK_DEBUG=texture-cache` |
| MPRIS `artUrl` pointing to deleted cache file | Medium | `ArtworkManager` must persist files, not use temp files |
| Spectrum visualization jitter | Low | Conflate frames in VisualizerEngine, target 30 FPS |
| Search debounce too aggressive | Low | Start with 300ms, tune based on UX testing |
| Queue rebuild on every change | Low | Acceptable for <500 tracks; optimize later if needed |

---

## 10. Next Steps

1. **Immediate:** Implement `LazyTrackListModel` for `SongsPage` (Phase 1.2)
2. **Immediate:** Connect `queue_changed_cb_` in `RightNowPlaying` (Phase 2.1)
3. **Immediate:** Enable playback mode buttons in `RightNowPlaying` (Phase 2.3)
4. **Week 1:** Complete all library page verifications (Phase 1.1, 1.3, 1.4)
5. **Week 1:** Implement `ArtworkView` async loading (Phase 1.5)
6. **Week 2:** Complete playback UI connections (Phase 2.2, 2.4, 2.5)
7. **Week 3:** Lyrics + Search (Phase 3)
8. **Week 4:** Polish + Artwork validation (Phase 4)

---

*Document generated from codebase analysis on 2026-11-21*