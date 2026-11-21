# Frontend gap analysis — current app vs. Aurora Music mock

Reference mock: `/home/mahesh/Downloads/Aurora Music Dark-Themed Player UI.png`
Reference visualizer: `~/.config/quickshell/end4-pC/modules/common/widgets/WaveVisualizer.qml`
Plan: `frontendPlan.md`

I ran `build/lollypop-cpp` and captured the real window. The mock and the running app are not close yet. The causes are a mix of one fatal styling bug, several layout/rendering bugs, and missing components.

---

## 0. The single biggest bug: the stylesheet is discarded at startup

`frontend/src/application.cpp:86` loads `resources/style.css`, then immediately calls
`theme_manager_->set_accent_color(...)` at line 90.

`ThemeManager::set_accent_color()` calls `update_css_accent()`, which calls
`gtk_css_provider_load_from_data()` on the **same provider** at
`frontend/src/ui/theme/ThemeManager.cpp:68`.

`gtk_css_provider_load_from_data()` **replaces the provider's entire contents**.
So the full stylesheet is replaced by this tiny snippet:

```css
:root { --accent-color: ...; --accent-soft: ...; }
```

That is why the running app looks like default Adwaita:

- Greeting is ~13 px instead of 28 px.
- Active sidebar row is gray instead of purple.
- Search box is square, not a 24 px pill.
- Right panel has no card background or rounded corners.
- Bottom bar has no floating card, no purple play button.
- All the custom classes in `style.css` are dead.

**Fix**

1. Use two providers:
   - `base_provider_` → loads `resources/style.css` once.
   - `accent_provider_` → contains only the dynamic `--accent-*` variables.
2. Never call `load_from_data()` on the base provider.
3. Check `parsing-error` on both providers.
4. Prefer a GResource (`/com/.../style.css`) over a relative path so it works from any cwd.
5. If you keep CSS variables, verify they resolve; GTK 4.24 parses them, but `@define-color` is the safer fallback.

Until this is fixed, no amount of CSS editing will change the app.

---

## 1. Current vs. target layout

| Area | Current | Mock |
|---|---|---|
| Window | `GtkApplicationWindow`, decorated, no custom controls | Rounded, borderless-looking, custom top-right controls |
| Sidebar | 220 px, no brand, one flat nav list | ~278 px, brand at top, grouped nav, COLLECTIONS label, More |
| Top bar | Greeting + search inside middle panel only | Search centered over main area, window controls over right panel |
| Right panel | Flush, full-height, 300 px, no card | Rounded card, ~384 px, 25% of content, inset |
| Bottom bar | Grid row, border-top, controls stacked above waveform | Floating 104 px rounded card, one horizontal row |
| Home | Text rows, 56 px thumbnails, no sections | Recently Played cards, Your Library tiles, Recently Added cards |
| Waveform | Dark circle + flat polygon + blue | Rounded purple bars / smooth wave + split playhead |
| Artwork | 24×24 due to a sizing bug, or no art | 348 px in right panel, 78 px in bottom bar |

Target structure:

```text
GtkApplicationWindow
└─ GtkBox vertical
   ├─ GtkBox horizontal (main, expands)
   │  ├─ LeftSidebar (≈278 px)
   │  └─ GtkBox vertical
   │     ├─ TopBar (search + window controls, ≈84 px)
   │     └─ GtkBox horizontal
   │        ├─ MiddleLibrary (expands)
   │        └─ RightNowPlaying card (≈384 px, inset)
   └─ BottomPlayback (104 px, 12 px margins, rounded)
```

The current `GtkGrid` can be made to work, but the top bar must span middle + right, and the sidebar must stop above the bottom bar. A root vertical box is simpler.

---

## 2. P0 blockers (fix first)

1. **ThemeManager wipes `style.css`** — see section 0.
2. **ArtworkView shrinks artwork to 24×24** — `set_corner_radius()` at
   `frontend/src/ui/components/ArtworkView.cpp:57` calls
   `gtk_widget_set_size_request(image, radius * 2, radius * 2)`.
   `RightNowPlaying` calls `set_preferred_size(280,280)` then
   `set_corner_radius(12)`, so the image is reset to 24×24. Bottom bar resets to 12×12.
   The radius CSS string is built and then freed without ever being applied.
3. **Waveform draws a filled circle** — `frontend/src/panels/waveform_view.cpp:181`
   uses `cairo_arc(...)` as the background. That is the dark circle visible in both panels.
4. **Waveform never animates** — `WaveformView::set_playing()` is never called by
   `BottomPlayback` or `RightNowPlaying`. `RightNowPlaying::set_playing()` is empty
   (`right_now_playing.cpp:341`), and `BottomPlayback::set_playing()` only stores a bool
   (`bottom_playback.cpp:271`).
5. **Home page is not implemented** — `HomePage::refresh()` prints a "Recently Played"
   header but never renders the tracks (`HomePage.cpp:67-84`), and uses 56 px rows
   instead of cards (`HomePage.cpp:106-107`).
6. **Sidebar compact mode hides the entire nav** — the `GtkRevealer` wraps the whole
   `nav_list_box_` (`left_sidebar.cpp:107-116`), not just labels.
7. **Bottom bar is structurally wrong** — controls are stacked above the waveform
   (`bottom_playback.cpp:99-188`), no shuffle/repeat controls, vertical volume popover,
   no like/overflow actions.
8. **Right panel is structurally wrong** — outer scroll wraps everything, time label
   and prev/next flank the waveform, modes are icon-only, lyrics are a plain `GtkTextView`.
9. **All playback controls are disabled** — `MainWindow::connect_backend()` calls
   `set_controls_sensitive(false)` on both panels (`main_window.cpp:169-170`).
   The mock shows active purple controls.
10. **Favourite Albums returns every album** — `BackendController::get_favourite_albums()`
    returns `storage_->get_all_albums()` (`backend/src/controller.cpp:740-742`).

---

## 3. File-by-file changes

### `frontend/src/ui/theme/ThemeManager.cpp`

- Split base and accent providers.
- Do not reload the base provider when the accent changes.
- Report CSS parse errors.
- Load from GResource.
- Expose accent RGB so `WaveformView` can use it.

### `frontend/src/application.cpp`

- Use `AdwApplicationWindow` (plan §2.2) or a custom undecorated `GtkWindow` with a
  `GtkWindowHandle`.
- Force dark scheme: `adw_style_manager_set_color_scheme(..., ADW_COLOR_SCHEME_FORCE_DARK)`.
- Set application name/icon ("Aurora Music" vs "Lollypop" is an open decision).
- Apply accent color after loading the base stylesheet.

### `resources/style.css`

This needs a substantial rewrite, not tweaks. Add at least:

- Design tokens for the mock: window `#0b0b10`, sidebar `#101016`, surface `#15151d`,
  elevated `#1b1b25`, accent `#8b5cf6`/`#a78bfa`, text `#f4f4f7`, muted `#8b8b98`,
  border `rgba(255,255,255,0.07)`.
- `.app-window` / rounded window background.
- `.top-bar`, `.search-entry` (48 px pill), `.window-control-button`.
- `.sidebar-brand`, `.sidebar-brand-icon`, `.sidebar-section-label`,
  `.sidebar-separator`, `.sidebar-nav-item.active` purple gradient.
- `.home-content`, `.greeting`, `.section-header`, `.section-title`, `.see-all`.
- `.media-card`, `.media-card-art`, `.media-card-title`, `.media-card-artist`.
- `.library-tile`, `.library-tile-icon`, `.library-tile-count`.
- `.right-panel` (rounded card), `.now-playing-*` updates.
- `.bottom-playback` (floating rounded card, 104 px).
- `.bottom-playback-play-button` purple circle.
- `.mode-button`, `.mode-button:checked` purple.
- `.tab-pill`, `.tab-pill.active` purple.
- `.lyrics-line`, `.lyrics-line.current` purple pill.
- `textview`, `textview text` transparent (the black lyrics rectangle is default
  textview background).
- Remove the global `button` reset if it fights Adwaita, and remove the global
  `* { transition: ... }` rule (expensive and over-broad).

### `frontend/src/main_window.cpp` / `.hpp`

- Replace the 3-column grid with:
  - main horizontal row: sidebar + content column,
  - content column: top bar + body (middle + right),
  - bottom bar row, 104 px, 12 px margins.
- Right panel: target `clamp(0.25 * (W - S), 300, 430)`; current is fixed 300.
  Hook `notify::width`/size-allocate and compute it.
- Bottom bar: 104 px target; current content forces a taller bar.
- Remove the duplicate `set_playback_state_changed_callback` (lines 156 and 162;
  the first is overwritten).
- Pass the full `Track` to panels, not only title/artist/artwork. The right panel
  needs `track.id` to request lyrics.
- Forward `playing` to the waveform views (via panels).
- Remove `set_controls_sensitive(false)` or implement the pass-through option.
- Set minimum size 600×600.
- Add window controls / custom top bar.
- Add keyboard shortcuts (space, arrows) if desired.

### `frontend/src/panels/left_sidebar.cpp` / `.hpp`

- Add brand header: purple rounded icon + "Aurora Music".
- Group navigation:
  - Home, Albums, Artists, Playlists, Genres, Songs
  - separator
  - `COLLECTIONS` label
  - Favourite Albums, Liked Music
  - separator
  - More (with chevron)
- Use `GtkListBox` with `GTK_SELECTION_NONE` and manual `.active` class.
- Fix compact mode: revealer must wrap only labels. Current revealer hides the entire
  nav list (`lines 107-116`).
- Fix use-after-free in `toggle_expanded()`:
  `gtk_button_set_child()` destroys the old child, then `g_object_unref(old_child)`
  is called on a dead pointer (`lines 203-204`).
- Fix icons: use distinct album/artist/playlist/genre/song/heart icons.
- Add tooltips in compact mode.
- Persist expanded state and active page (currently TODO at lines 248-258).
- Sidebar width: mock ~278 px; plan says 220. Pick one token and use it consistently.
- Keep collapse and menu as separate controls (plan §4.5). The mock does not show them;
  put collapse at the bottom or reveal on hover if you want to match the mock.

### `frontend/src/panels/middle_library.cpp` / `.hpp`

- Remove the greeting from the header; move it into HomePage.
- Top bar should be: centered search (max width ~844 px) + window controls on the right.
- Current search has `hexpand = TRUE` (`middle_library.cpp:113`), so it stretches across
  the whole header. Use a fixed max width / center box.
- Remove the outer `GtkScrolledWindow` that wraps the stack (`lines 93-97`). Every page
  already creates its own scrolled window; this creates nested scrolling.
- Do not call `refresh()` on every page switch; refresh only when data changes.
  `set_active_page()` at line 224 currently rebuilds content every time.
- Add a search `changed` handler with debounce + worker thread + generation counter
  (plan §5.12). Search currently does nothing.
- Add page title/header inside the page, not the global top bar.
- Wire `See All` navigation to the relevant page.

### `frontend/src/ui/components/HomePage.cpp` / `.hpp`

Replace the current text-list implementation with the mock's three sections:

1. **Greeting** — "Good Evening, Mahesh 🎵" (name from settings / `g_get_real_name`).
2. **Recently Played** — horizontal row of album cards, "See All →".
   Render `get_recently_played()` tracks; group by album or use album metadata.
3. **Your Library** — five tiles: Albums (245), Artists (128), Playlists (32),
   Genres (12), Songs (1,842). Counts from `ILibraryService`; click navigates.
4. **Recently Added** — horizontal row of cards, "See All →".
5. Optional **Favourite Albums** section (plan §5.2).

Implementation details:

- Add a reusable `SectionHeader` (title + See All).
- Add a reusable `LibraryTile` component.
- Use `AlbumCard` in a compact/row mode, not 56 px rows.
- Use a horizontal `GtkScrolledWindow` or `GtkBox` with overflow for card rows.
- Add proper empty states per section.
- Use `ArtworkView`/cache, not raw `GtkPicture`.
- Remove the duplicate "Listen Now" heading.
- Do not clear/rebuild the whole page on every refresh if avoidable.

### `frontend/src/ui/components/AlbumCard.cpp` / `.hpp`

- Card: ~180 px wide, transparent background, artwork 180×180, radius 12.
- Title/artist: left-aligned below artwork, 14/12 px.
- Play button: purple circle overlaid on artwork center/bottom-right, visible on hover
  **and keyboard focus** (plan §5.3).
- Current play button is appended below the labels (`line 82`), causing layout jumps,
  and it is a `GtkButton` nested inside another `GtkButton` — invalid/awkward.
  Use `GtkOverlay` + a click gesture on the card, with a separate play button.
- Remove fixed 160×220 and centered labels (`lines 27, 53, 63`).
- Add fallback artwork (gradient + music note).
- Wire `on_album_played` to `IPlayerService`; it is currently a TODO in `AlbumsPage.cpp`.
- Add selected/focus styles.

### `frontend/src/ui/components/ArtworkView.cpp` / `.hpp`

- Remove the size request from `set_corner_radius()`; it is the reason art is 24×24.
- Apply rounding with `gtk_widget_set_overflow(widget, GTK_OVERFLOW_HIDDEN)` +
  a CSS class, or a `GtkSnapshot` clip.
- Fallback: music note, not `media-playback-start-symbolic`.
- Size the fallback/container to the requested size, not half.
- Load asynchronously through `ArtworkCache` / `ThumbnailGenerator` (both exist but are
  unused). Do not decode full artwork on the GTK main thread.
- Reuse textures; preserve aspect ratio with `GTK_CONTENT_FIT_COVER`.
- Add a neutral gradient placeholder for missing artwork.

### `frontend/src/panels/right_now_playing.cpp` / `.hpp`

Target structure (top to bottom):

```text
rounded panel
  artwork (square, adaptive)
  title / artist (centered)
  waveform (full width, no prev/next, no time label)
  Shuffle | Repeat All | Repeat One (icon + label)
  Lyrics | Queue segmented pill
  scrollable tab content
```

Changes:

- Wrap the panel in a rounded `.right-panel` card with 16–18 px padding.
- Remove the outer scrolled window (`lines 65-79`); keep artwork/title/waveform/modes
  fixed, scroll only the lyrics/queue content.
- Remove the time label (`lines 129-132`) and prev/next buttons from the waveform row.
- Waveform: full width, 44–56 px high, shared model, accent color.
- Modes: icon **and** label, 3 equal columns, active purple. Current is icon-only
  (`lines 178-205`).
- Enforce mutual exclusion for Repeat All / Repeat One (plan §6.5). Current
  `on_repeat_toggled` and `on_loop_toggled` are independent.
- Avoid feedback loops: `set_shuffle/set_repeat/set_loop` call
  `gtk_toggle_button_set_active()`, which emits `toggled` and sends a backend command
  again. Block signals or guard with a state flag.
- Tabs: segmented pill container, active purple. Current active class uses
  `var(--bg-secondary)`, same as the panel (`style.css`), and the tab buttons are plain.
- Lyrics: replace `GtkTextView` with a `GtkBox`/`GtkListView` of line labels; parse LRC;
  highlight the current line with a purple pill; auto-scroll smoothly (plan §6.7).
  The current text view has a black default background (the black rectangle in the app).
- Make `tab_content_stack_` and the lyrics scroll `vexpand` so the content fills the
  remaining height.
- `set_playing()` is empty (`line 341`); forward to `waveform_view_->set_playing()` and
  update mode/play state.
- `set_panel_height()` is called once with 900 (`main_window.cpp:103`); connect it to
  actual allocation. Artwork should be
  `min(panel_width - 32, 0.38 * available_height)` (plan §6.3).
- Request lyrics when the track changes; `request_lyrics_for_track()` exists but is
  never called.
- Queue: highlight the current track; support reorder/remove when the backend allows.
- Do not disable mode buttons if you want the mock's active appearance.

### `frontend/src/panels/bottom_playback.cpp` / `.hpp`

Target: one horizontal row, 104 px tall, 12 px margins, rounded card.

```text
[art 78] [title/artist] [heart] [...]  [prev] [play 56] [next]  [waveform + times]  [shuffle] [repeat] [repeat-one] | [volume icon + slider] [PiP] [queue]
```

Changes:

- Remove the vertical center box that stacks controls above the waveform
  (`lines 99-188`). Use a horizontal row.
- Left: artwork 78–96 px, title 17 px, artist 13 px, plus like and overflow buttons.
- Transport: prev, purple 56 px play/pause, next; update icon in `set_playing()`.
- Center: waveform expands, 36–44 px high; times below at the ends, not flanking.
- Right: shuffle, repeat all, repeat one, inline horizontal volume slider, PiP, queue.
- Add mode buttons; they are entirely missing today (plan §7.4).
- Replace the vertical volume popover (`lines 201-226`) with an inline horizontal
  `GtkScale`.
- Play button: purple background + white icon; current CSS makes it white.
- Do not disable controls, or the mock's purple active state is impossible.
- `set_playing()` only stores a bool (`lines 271-273`); it never swaps the icon or
  updates the waveform.
- `set_volume()` does not update the volume icon; add mute/unmute.
- Add a seek interaction (click/drag waveform) if using a whole-track waveform.
- Style the bar as a floating card, not a border-top row.

### `frontend/src/panels/waveform_view.cpp` / `.hpp` — the progress bar

This is the part you specifically called out.

Current problems:

1. **Circle background** — `cairo_arc` at `line 181` fills a circle/ellipse.
   Remove it. The waveform background should be transparent or a rounded rect.
2. **Placeholder is a rectangle** — `lines 193-194`. Draw a dim flat wave instead.
3. **Flat polygon, not wavy** — `lines 220-232` connect samples with straight lines.
   The shell `WaveVisualizer.qml` uses a moving average + filled path; with enough
   points it reads as a wave, but it still needs smoothing.
4. **No split color** — the comment claims saturated left / dimmed right, but the code
   fills one color and draws a white playhead. The mock has a clear split at the playhead.
5. **Per-frame normalization** — `max_val` is computed from the current frame
   (`lines 209-214`), so quiet audio always fills the height and the wave jumps.
   Use the model's fixed `maxVisualizerValue` (1000) or an adaptive ceiling with decay.
6. **Blue accent** — `accent_r/g/b` default to `(0.4, 0.7, 1.0)` and
   `set_accent_color()` is never called. The mock is purple.
7. **Widget hides when there is no data** — `set_model()` calls
   `gtk_widget_set_visible(..., model->hasData())` (`lines 71-72`), so the layout
   collapses. Keep the height and draw a placeholder.
8. **No temporal smoothing** — the moving average is spatial only; add attack/decay
   (lerp toward the new frame) to get the shell's fluid motion.
9. **No animation** — `set_playing()` is never called; fix the callers.
10. **Smoothing recomputed every tick** even when the model did not change; use
    `sequenceNumber`.

Recommended renderer (shell-style wavy):

- Normalize with `maxVisualizerValue = 1000`.
- Spatial moving average (`smoothing = 2`) + temporal lerp (`0.25–0.35`).
- Build a path with Catmull-Rom → cubic Bézier (or midpoint quadratic smoothing).
- Fill from the bottom with accent at ~0.15–0.35 alpha.
- Split at `progress_`: saturated accent left, dimmed right.
- Draw a 2 px white playhead only when `progress_ > 0`.
- Optional blur: render to an image surface and box-blur, or use a GSK blur node.
- Draw no background; reserve height even with no data.

If you want the mock's **bars** instead of the shell's filled wave:

- Draw N rounded bars (3 px wide, 3 px gap), height from smoothed samples.
- Use a vertical purple gradient.
- Split color at the playhead.
- White playhead line.
- This is visually closer to the mock, but not what `WaveVisualizer.qml` does.

Pick one style; do not mix the current flat polygon with either.

### `frontend/src/ui/pages/*.cpp` — bugs

- `AlbumsPage.cpp:48`, `ArtistsPage.cpp:47`, `FavouriteAlbumsPage.cpp:48`,
  `PlaylistsPage.cpp:47` call `gtk_box_remove(GTK_BOX(flow_box), child)`.
  That is a type-confusion bug; use `gtk_flow_box_remove()`.
- `SongsPage.cpp` uses a `GtkListBox` with manual headers, not the required
  `GtkColumnView` (plan §5.7). Columns do not align with `TrackRow`.
- `SongsPage` loads every track (`get_sorted_track_ids` + `get_tracks_by_ids`),
  not lazy loading.
- `on_track_played()` in `SongsPage` and `on_album_played()` in `AlbumsPage` are TODOs;
  playback is not wired.
- `TrackRow::set_liked()` has the same use-after-free pattern as the sidebar:
  `gtk_button_set_child()` destroys the old child, then `g_object_unref(old_child)`.
- `TrackRow` gives title/artist/album all `hexpand = TRUE`, so they get equal widths.
  Use a fixed grid/column layout.
- `AlbumDetailPage` / `ArtistDetailPage` use raw `GtkPicture` with no fallback/cache.
- `HomePage` and detail pages decode artwork synchronously.

### `backend/src/controller.cpp`

- `get_favourite_albums()` returns all albums (`lines 740-742`). Filter by
  `album.favorite`.
- The library DB currently has duplicate album rows (e.g. "Homework" by `artist_daftpunk`
  and "Daft Punk"). That is a scanner/data issue, but it makes the home page look wrong.
- Ensure `get_recently_played()` has data; otherwise HomePage should fall back to
  Recently Added with an explicit empty state.

### Functional wiring / state

- Search entry has no `changed` handler.
- `MainWindow` passes only title/artist/artwork on track change; lyrics never load.
- `set_playing` is not propagated to waveforms.
- Shuffle/repeat/volume are not synced from the backend/MPRIS.
- Like buttons mostly log or no-op.
- Queue has no current-track marker, reorder, or remove.
- Artwork cache/thumbnail generator exist but are unused.
- Decide authority model: `docs/AUTHORITY_MODEL.md` recommends read-only controls,
  but the mock shows active controls. If you want the mock, implement Option B
  (pass-through to `BackendController`/MPRIS) and sync state both ways.

---

## 4. Suggested implementation order

1. **Theme + CSS provider fix**; verify the app immediately looks styled.
2. **Window shell**: top bar, sidebar/content split, right panel card, bottom bar card.
3. **Sidebar**: brand, groups, active purple, compact-mode fix, crash fix.
4. **Middle header + Home page**: search bar, greeting, sections, cards, tiles.
5. **Right panel**: fixed top, waveform, mode labels, tabs, lyrics widget.
6. **Bottom bar**: horizontal layout, transport, modes, volume, like/overflow.
7. **Waveform renderer**: remove circle, smooth wave/bars, split color, accent, animation.
8. **ArtworkView**: radius/size fix, fallback, async cache.
9. **Data/functional wiring**: favourites filter, search, playback, lyrics, state sync.
10. **Cleanup**: FlowBox remove API, use-after-free patterns, duplicate callbacks,
    unused mocks.

## 5. Open decisions

- Branding: "Aurora Music" (mock) vs "Lollypop" (plan/window title).
- Sidebar width: 278 px (mock) vs 220 px (plan).
- Waveform: smooth filled wave (your shell) vs rounded bars (mock).
- Controls: read-only (authority doc) vs pass-through (mock's active controls).
- Right-panel playhead: mock omits it; plan says show it.
- User name for the greeting: settings vs `g_get_real_name()`.

## 6. Acceptance checklist

- [ ] `style.css` survives startup; purple accent visible.
- [ ] Window has the mock's top bar and custom controls.
- [ ] Sidebar has brand, grouped nav, purple active row, working compact mode.
- [ ] Home shows greeting, Recently Played cards, Your Library tiles, Recently Added.
- [ ] Album cards use square art, left-aligned text, hover/focus play overlay.
- [ ] Right panel is a rounded card; artwork is large and not 24×24.
- [ ] Right panel waveform is full-width, purple, and animates.
- [ ] Mode buttons show icon + label; Repeat All/One are mutually exclusive.
- [ ] Lyrics tab shows lines with a current-line highlight, not a black text box.
- [ ] Bottom bar is 104 px, one row, purple play button, waveform + times below.
- [ ] Bottom bar has shuffle/repeat/repeat-one, inline volume, like, overflow, PiP, queue.
- [ ] No FlowBox type-confusion warnings/crashes.
- [ ] No use-after-free when changing sidebar icon or like icon.
- [ ] Search works; playback buttons work or are intentionally read-only and styled.
