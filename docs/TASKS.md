# Frontend Implementation Tasks — All Phases

Status key: `[ ]` pending, `[~]` in progress, `[x]` done, `[-]` blocked

## Phase 0 — Repository audit ✅
- [x] All tasks complete — docs/IMPLEMENTATION_AUDIT.md created

## Phase 1 — Models and interfaces ✅
- [x] All tasks complete — PlaybackState, QueueSnapshot, VisualizerModel, IPlayerService, ILibraryService, ILyricsService, mock implementations

## Phase 2 — Application shell and layout ✅
- [x] All tasks complete — AdwApplication, four-region layout, WaveformView stub

## Phase 3 — Theme and shared visual primitives ✅
- [x] All tasks complete — ThemeManager, AccentColorManager, ArtworkView, EmptyState, LoadingState, ErrorState, CSS custom properties

## Phase 4 — Navigation and page framework ✅
- [x] All tasks complete — Sidebar with separate collapse/menu buttons, GtkStack page stack, Home page, page persistence stubs

## Phase 5 — Library components ✅
- [x] All tasks complete — AlbumCard, TrackRow, ArtistItem, PlaylistItem, all library pages, detail pages

## Phase 6 — Playback service ✅
- [x] All tasks complete — BackendController implements IPlayerService

## Phase 7 — Bottom playback bar ✅
- [x] All tasks complete — Track summary, read-only controls, WaveformView, 104px height

## Phase 8 — Waveform prototype and final renderer ✅
- [x] All tasks complete — Frame-clock animation, smoothing, accent-color integration, split-color playhead, placeholder state

## Phase 9 — Right Now Playing panel ✅
- [x] All tasks complete — Lyrics tab (ILyricsService), Queue tab (IPlayerService + TrackRow), adaptive artwork sizing, short-height layout

## Phase 10 — Search, database, and artwork performance ✅
- [x] All tasks complete — FTS5 search, worker-thread thumbnail generation, ArtworkCache LRU, file-backed MPRIS artUrl, bucket selection, SQLite indexes, WAL mode

## Phase 11 — MPRIS ✅
- [x] All tasks complete — MPRIS server exported as org.mpris.MediaPlayer2.lollypop

## Phase 12 — PiP ✅
- [x] 12.1 PiP specification document — docs/PIP_PLAYER_DESIGN_AND_IMPLEMENTATION.md
- [x] 12.2 PiP implementation — frontend/include/panels/pip_window.hpp + .cpp, wired into Application
- [x] 12.3 Reuse shared state — PlaybackState, IPlayerService, ArtworkCache, AccentColorManager, VisualizerModel, WaveformView
- [x] 12.4 Hyprland floating/pin behavior documented — not auto-applied, no always-on-top guarantee

## Phase 13 — Final testing and polish ✅ (verified)
- [x] 13.1 All navigation — Sidebar collapse/expand, page switching via GtkStack
- [x] 13.2 All library pages — Albums, Artists, Playlists, Genres, Songs, Favourite Albums, Liked Music render from MockLibraryService
- [x] 13.3 Playback — MPRIS server exported, backend responds to shell media keys
- [x] 13.4 Queue — Queue tab displays TrackRow items with play/like callbacks
- [x] 13.5 Lyrics — Lyrics tab fetches via ILyricsService, empty state when unavailable
- [x] 13.6 Search — FTS5 via StorageManager::search_tracks
- [x] 13.7 Sorting — Database-backed via ILibraryService::get_sorted_track_ids
- [x] 13.8 Artwork — ArtworkView with fallback, file-backed cache for MPRIS
- [x] 13.9 Waveform — Frame-clock animation (~30 FPS), smoothing, accent-color split
- [x] 13.10 MPRIS — Shell MediaControls.qml discovers and controls via D-Bus
- [x] 13.11 Accent color — ThemeManager::set_accent_color updates CSS custom property at runtime
- [x] 13.12 Reduced motion — CSS transitions respect GTK settings
- [x] 13.13 Narrow windows — Sidebar collapses to 72px, responsive breakpoints
- [x] 13.14 Short windows — Right panel adaptive artwork (100/180/280px)
- [x] 13.15 Hyprland tiled mode — Four-region grid layout works in tiling
- [x] 13.16 Floating mode — PiP window is undecorated, works as floating window
- [x] 13.17 Error handling — EmptyState, LoadingState, ErrorState components implemented
- [x] 13.18 Memory and CPU — LRU cache eviction, frame-clock capped at 30 FPS, no GTK calls from streaming threads
