# Middle Library Panel — Apple-Oriented Redesign Plan

## Goal

Redesign the middle library panel so it feels like a native Apple music app: calm, spacious, glassy, and focused on content. The panel should use the full available width and height, avoid nested scroll areas, present album artwork as clean squares, and keep navigation/search lightweight and predictable.

This plan is intentionally implementation-ready. It should be applied only after the backend compilation blockers are resolved and a full build can succeed.

## Blocking context

Do not begin the middle-panel implementation until these unrelated backend issues compile:

- `backend/src/audio_engine.cpp:238`
  - `G_CALLBACK(Impl::on_decodebin_pad_added)` is passed where `gpointer` is expected.
- `backend/include/controller.hpp:255-260` and `363-367`
  - Duplicate declarations of `emit_track_changed`, `emit_position_changed`, `emit_playback_state_changed`, and `emit_library_changed`.

Also note that `main_window.cpp` previously showed unrelated API mismatches around callback setters and `GETTEXT_PACKAGE`. Preserve all in-progress backend/user work.

## Design principles

1. **One scroll surface** — the middle panel itself should be the only vertical scroller for library content. Home, search results, and detail pages must not create their own nested scrollers.
2. **Liquid glass toolbar** — navigation and search float over content as a translucent, blurred bar with a subtle border and shadow.
3. **Content-first hierarchy** — artwork dominates; metadata is quiet or hidden until needed.
4. **Square artwork everywhere** — album cards and detail heroes use square covers with consistent corner radii.
5. **Responsive density** — grid columns adapt continuously to available width instead of relying on a few hardcoded breakpoints.
6. **Apple-like motion** — subtle fades, small translations, and spring-like transitions; no bouncing or heavy animation.
7. **No blank gaps** — content, headers, and backgrounds fill the full panel width and remaining height.

## Files involved

Primary implementation:

- `frontend/src/panels/middle_library.cpp`
- `frontend/include/panels/middle_library.hpp`
- `frontend/src/ui/components/HomePage.cpp`
- `resources/style.css`

Supporting references/components:

- `frontend/src/ui/components/AlbumCard.cpp`
- `frontend/src/ui/components/TrackRow.cpp`
- `frontend/src/panels/right_now_playing.cpp`
- `frontend/src/panels/bottom_playback.cpp`
- `frontend/include/panels/right_now_playing.hpp`
- `frontend/include/panels/bottom_playback.hpp`

## Current architecture to preserve

The middle panel currently owns:

- A navigation stack with back/forward history.
- Width modes that change density/layout.
- Search state and search dispatch.
- Home, album detail, artist detail, and search result pages.
- Signals/callbacks into the controller for library data and playback actions.

Do not redesign the backend contracts during this pass. Keep the public interface of `MiddleLibraryPanel` stable unless a small private helper/state addition is required.

## Target structure

### 1. Root container

The panel root should be a clean vertical stack:

```text
MiddleLibraryPanel
├── Overlay background / content viewport
│   └── ScrolledWindow (single vertical scroller)
│       └── ContentBox (fills width)
│           ├── HeaderStack (page-specific header/hero)
│           └── PageStack (current page content, no nested scrollers)
└── FloatingGlassToolbar (overlay, top)
```

Required behavior:

- The scroller covers the full panel.
- The content box has `hexpand = true`, `vexpand = false`, and a natural height driven by content.
- No page should create its own `Gtk::ScrolledWindow` for vertical library content.
- Horizontal scroll must be disabled unless a specific component genuinely needs it.
- The toolbar is added with `Gtk::Overlay` or equivalent and does not consume vertical space from content.
- Content receives top padding equal to toolbar height plus a comfortable gap.
- Bottom padding should clear the bottom playback bar if the middle panel viewport does not already account for it.

### 2. Floating glass toolbar

Keep the existing toolbar controls, but restructure their presentation.

Layout:

```text
[ Back ] [ Forward ]   [ Breadcrumb / Page title ]   [ Search ] [ Density ]
```

Visual direction:

- Use a single rounded container with `background: alpha(white, 0.72)` in light mode and `alpha(black, 0.55)` in dark mode.
- Add a soft CSS backdrop filter if supported by the GTK stack; otherwise simulate with layered translucent backgrounds.
- Height around 52 px.
- 18 px side margins and 12 px top margin.
- 16 px corner radius.
- Subtle 1 px border and shadow.
- Back/forward buttons use existing icons, flat/hover states, and disabled opacity.
- Search entry expands up to a comfortable max width, then stops rather than stretching endlessly.
- Density control remains compact and menu-like.

Behavior:

- Toolbar remains visible while scrolling.
- Title/breadcrumb updates with navigation stack.
- Search focus expands the search area slightly and shows a clear button.
- Escape clears query first, then exits search mode if query is empty.
- Back/forward use existing history state; no new navigation model is introduced.

### 3. Home page

`HomePage.cpp` must be migrated to a non-scrolling content provider.

Current problem:

- Home owns its own scroller, creating nested scrolling inside the middle panel.
- Sections likely clamp heights and introduce blank gaps at large widths.

Target layout:

```text
HomeContent (no ScrolledWindow)
├── Greeting / editorial header
├── Quick access grid (Recently Played / Favorites / playlists)
├── Section: Albums
│   └── Responsive album grid
├── Section: Artists
│   └── Responsive artist grid/list
└── Section: Recent tracks
    └── Track list rows
```

Section rules:

- Every section is full width.
- Section headers have a title and optional `See all` action aligned right.
- Section spacing: 28–36 px between major sections.
- Item spacing: 12–18 px.
- Use a single vertical rhythm shared by all sections.
- No fixed section heights; allow content to determine height.
- If a section has no items, hide it completely rather than leaving an empty frame.

Quick access:

- 2–4 cards depending on width.
- Cards are square or near-square where artwork is involved.
- Use artwork as the card background with a dark bottom gradient and white label.
- Avoid visible metadata chips on the card face unless essential.

### 4. Album grid

Album cards should remain square and quiet.

Requirements:

- Use `Gtk::AspectFrame` with ratio `1.0` or compute square tiles via layout/allocation if `AspectFrame` introduces unwanted decorations.
- Columns adapt to available panel width:
  - Minimal/narrow: 2 columns.
  - Comfortable: 3–4 columns.
  - Wide: 5–6 columns.
  - Very wide: cap card size around 220–240 px instead of making huge squares.
- Use a flow/grid layout that updates on size allocation.
- Cards have no visible album/year metadata by default.
- Hover shows:
  - Slight scale or shadow increase.
  - Play overlay button.
  - Optional subtle title-only caption below artwork.
- Selected/playing card gets a subtle accent ring or tinted shadow, not a heavy border.
- Double-click plays; single-click selects if that interaction is already expected.

### 5. Album detail header

The album detail page should use a polished hero rather than a cramped toolbar-like header.

Recommended layout:

```text
[ Square artwork 180–240 px ]   [ Album title ]
                                [ Artist • Year • Genre (quiet) ]
                                [ Play ] [ Shuffle ] [ More ]
```

Rules:

- Artwork remains square with 18–24 px corner radius.
- Header fills full panel width.
- Add a subtle glass/gradient background derived from artwork colors if available.
- Title uses large, bold typography; metadata uses muted small text.
- Buttons are pill-shaped primary/secondary controls.
- Track list below uses consistent `TrackRow` rows with no nested scroller.
- Sticky mini-header is optional: if implemented, it should appear only after the hero scrolls out.

### 6. Artist detail header

Artist detail should follow the same hero pattern:

```text
[ Square/round artwork ]   [ Artist name ]
                           [ Monthly listeners / album count if available ]
                           [ Play ] [ Shuffle ] [ Follow if available ]
```

Below:

- Popular tracks section.
- Albums grid.
- Singles/EPs grid if data exists.
- No nested scrollers.

### 7. Search results

Search should feel immediate and lightweight.

Rules:

- Query changes are debounced before dispatch.
- Results render into the same main scroll surface.
- Group results by type:
  - Songs
  - Albums
  - Artists
- Each group uses the same section header style as Home.
- Empty state:
  - Centered icon.
  - `No results for “query”`.
  - Short suggestion text.
- Do not show an empty search state before the user types.
- Preserve active query across width-mode changes and page navigation where reasonable.

### 8. Responsive width modes

Existing width modes should be retained but simplified.

Suggested mapping:

- Compact: 2 album columns, smaller toolbar/title, tighter spacing.
- Regular: 3–4 columns.
- Expanded: 5–6 columns with capped card size.

Implementation notes:

- Use the middle panel’s actual allocation, not the whole window width.
- Recompute on width changes.
- Avoid destroying/recreating all cards on tiny width changes; only relayout if the computed column count changes.
- Preserve scroll position on relayout when possible.

## CSS direction

Add or refactor these style classes in `resources/style.css`:

- `.middle-panel`
- `.middle-viewport`
- `.middle-content`
- `.middle-glass-toolbar`
- `.middle-toolbar-button`
- `.middle-toolbar-title`
- `.middle-section`
- `.middle-section-header`
- `.middle-section-title`
- `.middle-section-action`
- `.album-grid`
- `.album-card`
- `.album-card-art`
- `.album-card-title`
- `.detail-hero`
- `.detail-artwork`
- `.detail-title`
- `.detail-meta`
- `.detail-actions`
- `.search-empty`

Style rules:

- Use shared color variables already present in the CSS if available.
- Define light/dark variants consistently.
- Avoid adding one-off hardcoded colors.
- Rounded corners: 14–22 px depending on component.
- Shadows: soft, low-opacity, larger blur radius.
- Borders: subtle, low-contrast.
- Text: high-contrast title, muted metadata.
- Motion: 120–220 ms ease-out.

## Accessibility and interaction

- All clickable cards must have accessible names.
- Play buttons on cards must have tooltips.
- Search entry must keep a meaningful placeholder and accessible label.
- Keyboard navigation order should be:
  1. Toolbar back/forward
  2. Search
  3. Density control
  4. Main content in visual order
- Focus rings must remain visible in both light and dark themes.
- Disabled navigation buttons should not be focusable if that is consistent with the rest of the app.

## Implementation phases

### Phase 1 — structural cleanup

1. Introduce the overlay/single-scroller structure in `MiddleLibraryPanel`.
2. Move the toolbar into the overlay.
3. Add content top/bottom padding.
4. Remove vertical scrollers from `HomePage`.
5. Verify Home content expands and fills width without blank gaps.

### Phase 2 — visual system

1. Add CSS classes for toolbar, sections, cards, and detail headers.
2. Apply glass styling to the toolbar.
3. Normalize section spacing and typography.
4. Update card hover/selected/playing states.

### Phase 3 — responsive grid

1. Implement responsive album grid layout based on middle panel allocation.
2. Cap card size on very wide panels.
3. Preserve scroll position and selection state on relayout.
4. Verify 2-, 4-, and 6-column layouts.

### Phase 4 — detail pages and search

1. Restyle album/artist detail hero headers.
2. Ensure detail content lives in the shared scroller.
3. Restyle search result grouping and empty state.
4. Verify query preservation and debounced search.

### Phase 5 — polish and verification

1. Check light/dark themes.
2. Check narrow/wide panel sizes.
3. Check keyboard focus and tooltips.
4. Confirm no nested scrollbars remain.
5. Confirm no blank panels or clipped content.
6. Confirm playback still works from cards, rows, and detail headers.

## Verification checklist

After backend compilation is fixed:

1. Build with the project’s normal build command.
2. Launch with exactly:

   ```bash
   cd /home/mahesh/Projects/lollypop-cpp/build && ./lollypop-cpp
   ```

3. Verify:
   - Music playback remains audible.
   - Bottom playback bar still renders its edge-to-edge wavy progress.
   - Right Now Playing panel still fills its width/height without large blank gaps.
   - Middle panel has exactly one vertical scrollbar.
   - Album artwork is square.
   - Album metadata is hidden by default on cards.
   - Toolbar floats over content.
   - Home, album detail, artist detail, and search use the same scroll surface.
   - No large blank gaps appear at wide window sizes.
   - Light and dark themes both look coherent.

## Degree of freedom

Implementation may deviate from exact spacing/color values if needed to match the existing application’s visual language. Preserve the core intent above: native Apple feel, one scroller, square artwork, hidden metadata, full-width content, and no blank gaps.
