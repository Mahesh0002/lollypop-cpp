# Middle Panel Plan — Apple Music Direction

## Goal

Turn the middle library panel into a compact, artwork-forward Apple Music-style
track list while keeping the app GTK4-native and consistent with the rest of the
theme.

Reference target: dense but readable song rows, small square artwork, quiet
column headers, accent-tinted playing state, and subtle hover/selected states.

## References used

These are the GitHub references that informed the plan:

- `CassetteLab/cassette` — Swift-native Subsonic client explicitly based on
  Apple Music UI. Good reference for the three-zone layout and artwork-forward
  lists.
- `sebjvidal/Apple-Music-UI-Demo` — UIKit recreation of the Apple Music Now
  Playing transition. Useful for motion/accent polish.
- `saulsharma/apple-music-sheet-ui` — Expo/React Native Apple Music sheet UI.
  Useful for layered soft backgrounds and sheet/card treatment.
- `falcon78/Vusic`, `antrancs/maeve` — Apple Music-style web players. Useful
  for desktop middle-panel structure, sticky column headers, and right-aligned
  duration columns.
- `regularhunter/gumdrop` — GTK4 music player (Amberol fork). Useful for
  GTK-native list and CSS patterns.
- In-repo: `APPLE_HIG_DESIGN_PLAN.md`, `APPLE_HIG_DESIGN_PLAN_GTK.md`.

## Current state (as of `673b082`)

- `TrackRow` uses `ArtworkView` with async loading from `Track::artwork_path`.
- Row height is fixed at **20px**.
- Artwork is **18×18px** with a 3px corner radius, leaving 1px vertical
  breathing room inside the row.
- Like button is 20px with a 16px icon and 4px horizontal margins.
- Columns: artwork (18px), Title (20 share), Artist (30 share), Album (25
  share), Duration (40px), Like (28px), with 12px gaps and 12px side margins.
- Header first column is blank and matches the 18px artwork column.
- Hover uses `.track-row.hover`; playing uses `.track-row.playing`.
- Custom layout and header live in:
  - `frontend/src/ui/components/TrackRow.cpp`
  - `frontend/src/ui/pages/SongsPage.cpp`
  - `resources/style.css`

## Target wireframe

```text
┌──────────────────────────────────────────────────────────────┐
│ [ All Genres ▾ ]                                             │  filter row
│      TITLE          ARTIST          ALBUM           DURATION │  sticky header
├──────┬───────────────────────────────────────────────────────┤
│ [■]  │ Song title    Artist name    Album name         3:42 │  20px row
│ [■]  │ Another song  Other artist   Other album       4:10 │  20px row
└──────┴───────────────────────────────────────────────────────┘
  18px artwork
```

## Metrics

| Element | Target | Current |
| --- | --- | --- |
| Row height | 20px | 20px |
| Artwork | 18×18px, radius 3px | 18×18px, radius 3px |
| Column gap | 12px | 12px |
| Side margins | 12px | 12px |
| Duration column | 40px, tabular | 40px, tabular |
| Like column | 28px total | 28px total |
| Header height | 44px (filter + columns) | 44px |

## Phased plan

### Phase 1 — Lock the metrics (done)

- Artwork replaces track numbers.
- 20px row height.
- Header/like spacer aligned with the new columns.

### Phase 2 — Centralize layout constants

Create a small shared `TrackListMetrics` header (or namespace in
`TrackRow.hpp`) holding row height, artwork size, gap, side margin, duration
width, and like width. Use it from both `TrackRow` and `SongsPage` so the
header and rows cannot drift apart.

Files:
- `include/ui/components/TrackRow.hpp`
- `frontend/src/ui/components/TrackRow.cpp`
- `frontend/src/ui/pages/SongsPage.cpp`

### Phase 3 — Typography and hierarchy

- Title: primary text, medium weight, 13px.
- Artist: secondary text, 12px.
- Album: tertiary text, 12px.
- Duration: tertiary text, 12px, tabular numerals (already present).
- Column headers: 11px uppercase-ish, semibold, letter-spacing 0.5px (mostly
  present; verify actual rendered size).

Files:
- `resources/style.css`
- `frontend/src/ui/pages/SongsPage.cpp`

### Phase 4 — Sticky column header

Today the sort header scrolls with the list because it is inside the shared
scrolled window. Move the columns row outside the scrolled area (or use an
overlay) so it stays pinned like Apple Music's list headers, while the genre
filter remains above it.

Files:
- `frontend/src/ui/pages/SongsPage.cpp`
- `frontend/src/panels/middle_library.cpp`

### Phase 5 — Playing and hover states

- Keep artwork visible; do not replace it with a number.
- Playing row: accent title and soft accent row background (background already
  present; add title accent).
- Optional later: a tiny animated equalizer badge overlaid on the artwork for
  the playing row, matching Apple Music's "now playing" marker.
- Add explicit `:focus-visible` outline for keyboard navigation.

Files:
- `resources/style.css`
- `frontend/src/ui/components/TrackRow.cpp`

### Phase 6 — Actions and accessibility

- Add tooltips/accessible labels to the like button ("Like"/"Unlike").
- Consider switching star → heart to match Apple Music semantics.
- Consider a trailing ellipsis action for "Add to playlist / Go to album /
  Info" after the core look is settled.

Files:
- `frontend/src/ui/components/TrackRow.cpp`
- `resources/style.css`

### Phase 7 — Optional structural upgrade

If layout complexity keeps growing, evaluate migrating the list to
`GtkColumnView` for native sorting, accessibility, and fixed/flex column
behavior. This is intentionally deferred: the current custom `GtkLayoutManager`
is small and already works.

## Acceptance criteria

- Rows measure exactly 20px and artwork measures 18×18px.
- Header columns line up pixel-for-pixel with row columns.
- Playing row is identifiable without replacing artwork.
- Hover, pressed, focus, and selected states are all visible.
- No CSS parse warnings in the runtime log.
- `build` and `build-asan` both pass all tests.

## Open questions

1. Should the playing indicator be an overlay on the artwork, or only an
   accent title/row tint?
2. Should the like icon become a heart now, or stay a star to preserve current
   library semantics?
3. Should the column header be sticky immediately, or after typography is
   finalized?
