# Apple design research: extracted, usable layer

> This document converts `docs/apple_design_philosophy_deep_dive.md` into working material for this repository.  
> Companion machine-readable rule set: `docs/design-rules.yaml`.  
> **Implementation status:** P0-1 through P0-4, P1-1 through P1-4, and P2-1 through P2-4 were implemented on 2026-11-24; see section 6. Runtime GUI smoke verification is recorded in `docs/design_smoke_checklist.md`.

---

## 1. What we actually extracted

The research report is large, but its useful output is not prose. It is a set of **rules, tokens, contracts, gaps, and tasks**.

| Source material | Extracted data | Local use |
| --- | --- | --- |
| HIG design principles | 8 principles, 24 sub-rules | Design rulebook, review checklist, agent rules |
| HIG foundations | 12 system dimensions | Token inventory and audit categories |
| WWDC transcripts | Rationale behind principles | Decision criteria, not just visual taste |
| Legacy HIG triad | Clarity / Deference / Depth | Fast shorthand for layout and hierarchy reviews |
| Design Awards | 6 evaluation categories | Quality bar and example library |
| Liquid Glass study | Material rules + failure modes | Do/don't guidance for depth and motion |
| Critique section | 7 anti-patterns | Explicit "do not copy" list |
| Repo scan | 12 concrete observations / candidates | Prioritized backlog with file targets |

The extraction can be consumed at three levels:

1. **Human** — this document.
2. **Machine** — `docs/design-rules.yaml`.
3. **Code** — the token/contract/backlog sections below.

---

## 2. The extracted rulebook

### 2.1 Purpose → product rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Create value | Each panel must have one obvious job. | Can the panel be described in one sentence? |
| Keep focused | Do not expose backend concepts as UI without need. | Are library/index/cache details hidden by default? |
| Find a distinct solution | Improve music-finding and listening, not generic dashboard density. | Does this screen help the listener, or just look busy? |

### 2.2 Agency → interaction rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Stay out of the way | Do not trap users in flows or modes. | Escape/back closes or returns. |
| Freedom to explore | Search, browse, queue, and PiP available without prerequisites. | Can a user reach tracks from at least two paths? |
| Recover from mistakes | Reversible queue/library actions. | Can remove/reorder/clear be undone or confirmed? |

### 2.3 Responsibility → trust rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Transparency | Explain artwork/network fetch and external links. | Are non-local actions discoverable before they happen? |
| Data safety | Local library data handled minimally. | No new data collection without a stated reason. |
| Graceful denial | Missing artwork/network must degrade cleanly. | Fallback art and empty/error states remain useful. |

### 2.4 Familiarity → consistency rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Known concepts | Use GTK/Adwaita patterns before custom ones. | Standard buttons/menus/dialogs keep standard behaviour. |
| Consistency | Same action, icon, label, and placement everywhere. | "Like", "play", "more", and "back" do not drift. |
| Feedback | Every action has a visible state. | Hover, pressed, focus, selected, active, loading states exist. |

### 2.5 Flexibility → adaptation rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Design for everyone | Accessible names, focus, contrast, scalable text. | Every focusable custom widget has an accessible label. |
| Preserve context | Consistent panel positions and stable layout. | No layout jumps when art/title loads. |
| Multiple inputs | Keyboard + pointer + touch where relevant. | Custom widgets have key controllers or default button behaviour. |
| Platform intention | Native GTK on Linux, not fake macOS/iOS chrome. | No traffic lights, no iOS tab bars, no copied SF glyph grammar. |

### 2.6 Simplicity → clarity rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Necessary only | Remove redundant controls and duplicated labels. | One primary action per view. |
| Concise | Plain words in tooltips, empty states, errors. | No jargon like "entity", "provider", "cache miss" in UI copy. |
| Hierarchy | One dominant element; metadata recedes. | Album art/title > artist > secondary metadata. |

### 2.7 Craft → quality rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Quality sets tone | Pixel alignment, typography rhythm, motion timing. | No clipped labels, no raw hex outside tokens. |
| Experiment and iterate | Prototype panels with real library data. | Test with long titles, missing art, many tracks. |
| Maintain craft | Keep up with GTK/libadwaita and fix lifecycle issues. | No GTK criticals/assertions in smoke flow. |

### 2.8 Delight → emotion rules

| Rule | Local translation | Check |
| --- | --- | --- |
| Right emotion | Calm, focused, music-first; not playful for its own sake. | Does motion/emotion fit listening? |
| Defining moments | Artwork reveal, PiP transition, playback feedback. | Delight supports the task. |
| Not decoration | No gratuitous blur/animation. | Reduced motion and low-contrast modes still work. |

---

## 3. Extracted design-system tokens

The repository already has a token system, but it is currently **split across two stylesheets**. This is the single most useful extraction target.

### 3.1 Observed token sources

| Source | What it defines | Observation |
| --- | --- | --- |
| `resources/style.css:24-95` | Colours, typography scale, radii, glass materials, focus colour | Mature base token set with `--font-scale`. |
| `resources/apple_ui.css:11-26` | Apple typography sizes and hover/active row colours | Parallel type scale without `--font-scale`. |
| `frontend/src/ui/theme/ThemeManager.cpp:172` | Injects `--font-scale` at runtime | Only the `--font-*` scale responds to it. |
| `resources/style.css:237-242` | `prefers-reduced-motion` support | Already present; new motion tokens should inherit it. |

### 3.2 Consolidation target

Recommended canonical token families:

```css
/* Typography — single source, scale-aware */
--font-display
--font-title1
--font-title2
--font-title3
--font-body
--font-subhead
--font-footnote
--font-caption1
--font-caption2

/* Spacing — to be added */
--space-1: 4px;
--space-2: 8px;
--space-3: 12px;
--space-4: 16px;
--space-6: 24px;
--space-8: 32px;

/* Radius — already mostly present */
--radius-xs: 6px;
--radius-sm: 8px;
--radius-md: 12px;
--radius-lg: 16px;

/* Motion — to be added */
--motion-fast: 120ms;
--motion-base: 160ms;
--motion-slow: 240ms;
--ease-out: cubic-bezier(0.2, 0.8, 0.2, 1);

/* Elevation — semantic, not decorative */
--elevation-1: 0 2px 8px var(--shadow-subtle);
--elevation-2: 0 8px 32px var(--shadow-strong);
```

### 3.3 Concrete inconsistencies to resolve

| Issue | Evidence | Why it matters | Proposed fix |
| --- | --- | --- | --- |
| Two type scales | `--apple-size-*` vs `--font-*` | Dynamic Type only affects one. | Make `apple_ui.css` consume `--font-*`. |
| Two accent fallbacks | `#fa243c` in `apple_ui.css`, `#a78bfa` in `style.css` | Inconsistent favourite/active colour if variables are unavailable. | Use one `var(--accent-color)` fallback from base tokens. |
| Two focus-ring treatments | 1px white outline vs 2px `--border-focus` | Inconsistent keyboard affordance. | One canonical focus token/outline. |
| Global animation kill | `apple_ui.css:28-32` disables all button transitions | Restraint is good, but kills meaningful feedback. | Scope to ripple/press animation; use motion tokens. |
| Hardcoded px in Apple layer | e.g. `13px`, `11px`, `12px` in album metadata | Breaks scale consistency. | Replace with `--font-*` tokens. |

---

## 4. Extracted component contracts

A component contract is the smallest reusable unit from the research. Each custom widget should define these states explicitly:

`idle · hover · focus-visible · pressed · selected/active · disabled · loading · empty · error`

### 4.1 `AlbumCard`

| Aspect | Contract |
| --- | --- |
| Purpose | Represent one album as a compact, selectable object. |
| Primary content | Artwork, album title, artist. |
| Hierarchy | Artwork dominates; title > artist. |
| Interaction | Activate/Enter opens; click opens; context menu available. |
| Keyboard | Already focusable and has key controller (`AlbumCard.cpp:48,93`). |
| Accessibility gap | No shared accessible-label helper observed; add one. |
| Visual states | Add `focus-visible` parity with hover/active. |
| Do not | Add shadows/frames that make art look bordered. |

### 4.2 `AppleAlbumTile`

| Aspect | Contract |
| --- | --- |
| Purpose | Rich album tile for grid views. |
| Primary content | Artwork + scrim metadata + favourite action. |
| Hierarchy | Art dominates; scrim supports legibility. |
| Interaction | Click/Enter opens; favourite is independent. |
| Critical gap | Favourite button is `opacity: 0` unless hovered/favorited (`apple_ui.css:288-303`). |
| Required fix | Show on `:focus-visible`, and ensure accessible name like "Add <album> to favourites". |
| Do not | Increase scrim darkness globally; deference means artwork stays primary. |

### 4.3 `TrackRow`

| Aspect | Contract |
| --- | --- |
| Purpose | Dense, scannable track list item. |
| Primary content | Track number/artwork, title, artist, duration. |
| Secondary | Like, equalizer indicator, playing state. |
| Interaction | Activate plays; like toggles; right-click opens context menu. |
| Accessibility | Tooltip exists on like button, but accessible naming is not centralized. |
| Craft note | Live-row registry cleanup is already implemented; preserve it. |
| Do not | Introduce layout width shifts when like/equalizer appears. |

### 4.4 Sidebar rows

| Aspect | Contract |
| --- | --- |
| Purpose | Structural navigation, not content. |
| Visual role | Defer to library content. |
| States | Hover, active, focus, compact mode. |
| Input | Click and keyboard activation already present in multiple sidebars. |
| Consistency gap | `apple_sidebar` and `left_sidebar` use different CSS classes and focus treatments. |
| Do not | Make sidebar visually louder than album art or now-playing artwork. |

### 4.5 PiP window

| Aspect | Contract |
| --- | --- |
| Purpose | Persistent, glanceable playback state. |
| Size | Default 400×200, resizable (`pip_window.cpp:254-256`). |
| Hierarchy | Artwork > title > transport > progress > secondary controls. |
| Depth | Floating layer must read as above the main window, not as a second app. |
| Input | Drag via toplevel move API; shortcuts/controllers present. |
| Craft | Undecorated + resizable needs clear close affordance and focus behaviour. |
| Do not | Grow into a second full player; that violates purpose and deference. |

### 4.6 State components

`EmptyState`, `LoadingState`, `ErrorState` should share one copy contract:

| State | Required elements |
| --- | --- |
| Empty | Plain explanation + primary next action. |
| Loading | Short label + known/unknown progress semantics. |
| Error | What failed, why in plain language, retry/dismiss action. |

Files: `EmptyState.cpp`, `LoadingState.cpp`, `ErrorState.cpp`.

---

## 5. Gap register from the repo scan

These are **candidates**, not proven bugs. They are derived from direct file inspection on this pass.

| ID | Area | Observation | Severity | Action |
| --- | --- | --- | --- | --- |
| G01 | Tokens | Duplicate type scales. | High | Consolidate on `--font-*`. |
| G02 | Tokens | `--font-scale` only applies to base scale. | High | Route Apple layer through scaled tokens. |
| G03 | Accessibility | Accessible-label helper exists only in `right_now_playing.cpp:74`. | High | Create shared helper and apply broadly. |
| G04 | Accessibility | Favourite button hidden unless hovered/favorited. | High | Add focus-visible reveal. |
| G05 | Focus | Different focus ring styles across layers. | Medium | Canonical focus token. |
| G06 | Motion | Global `button { transition:none; animation:none; }`. | Medium | Scope it; preserve meaningful feedback. |
| G07 | Agency | No obvious undo/forgiveness pattern observed for queue mutations. | Medium | Add undo or confirmation for destructive actions. |
| G08 | Craft | Many hardcoded px values in Apple layer. | Medium | Replace with tokens. |
| G09 | PiP | Undecorated resizable window needs strong focus/close contract. | Medium | Document and verify. |
| G10 | States | Empty/error/loading components exist but usage consistency unknown. | Low | Audit all pages. |
| G11 | Copy | Tooltips exist, but plain-language copy audit not performed. | Low | Review all user-visible strings. |
| G12 | Verification | No design-specific smoke checklist in repo. | Medium | Add checklist to CI/docs. |

---

## 6. Prioritized extraction backlog

### P0 — highest leverage, small scope

#### P0-1: Unify typography tokens — IMPLEMENTED

**Files:** `resources/apple_ui.css`, `resources/style.css`, `frontend/src/ui/theme/ThemeManager.cpp`

- Replace `--apple-size-*` usages with `--font-*`.
- Keep `--font-scale` as the only scaling mechanism.
- Remove duplicate type declarations from the Apple layer.
- Verify with: `grep -R "font-size: [0-9]" resources/apple_ui.css`.

**Definition of done:** no parallel Apple type scale; text scales with the theme setting.

**Implementation note:** added canonical `--font-family`, routed the Apple layer and global base font through `--font-*`, and removed the parallel Apple type variables.

#### P0-2: Shared accessible-name helper — IMPLEMENTED

**Files:** `frontend/include/util/accessibility.hpp`; then custom widgets.

- Move the pattern from `right_now_playing.cpp:74` into shared utility.
- Apply to `AlbumCard`, `AppleAlbumTile`, `TrackRow` like button, custom sidebar rows, PiP buttons.
- Pair visible tooltips with accessible names that include object context.

**Definition of done:** every custom focusable widget has both tooltip and accessible name.

**Implementation note:** helper is header-only; applied to AlbumCard, AppleAlbumTile, TrackRow, PiP controls, and RightNowPlaying transport controls. Sidebar rows already had inline accessible labels.

#### P0-3: Keyboard-visible album favourite — IMPLEMENTED

**Files:** `resources/apple_ui.css`, `frontend/src/ui/components/AppleAlbumTile.cpp`

- Add `.apple-album-tile:focus-within .apple-album-favorite` and `.apple-album-favorite:focus-visible` reveal rules.
- Give the favourite button an album-specific accessible label.

**Definition of done:** Tab to favourite shows the control; Enter/Space toggles it.

**Implementation note:** favourite label now includes the album title and current action, e.g. `Add <album> to favourites`.

#### P0-4: Canonical focus ring — IMPLEMENTED

**Files:** `resources/style.css`, `resources/apple_ui.css`

- Define one focus outline token and use it everywhere.
- Ensure `:focus-visible` beats `:hover` styling.

**Definition of done:** no different focus treatment between Apple and base layers.

**Implementation note:** Apple-layer focus selectors now use `2px solid var(--border-focus)`; the old `outline: none` and 1px white ring were removed.

### P1 — quality and confidence

#### P1-1: State coverage matrix — IMPLEMENTED

**Files:** `docs/component_state_matrix.md`, `resources/style.css`.

- Add a table of states for each component in section 4.
- Implement missing `disabled`, `focus-visible`, and `error` states.

**Definition of done:** all documented components have documented states and runtime checks are listed.

**Implementation note:** the matrix covers `AlbumCard`, `AppleAlbumTile`, `TrackRow`, sidebar rows, PiP, state components, `QueueList`, and `QueueUndoBar`. It also covers `ArtistsPage`, `ArtistItem`, and `ArtistDetailPage`: artist cards have canonical focus rings and accessible names, and artist pages expose empty, unavailable, missing-artist, and no-tracks states. The remaining browse and detail pages now use the shared `PageStateHost`, so Songs, Genres, Liked Music, Playlists, Favourites, and Album Detail expose the same empty/unavailable contract. `PlaylistItem` gained an accessible name and a square centered placeholder; `ArtworkView` preserves caller alignment so card and right-pane cover art stay square. Generic `button:disabled` and canonical focus styles cover the shared cases; queue empty copy now states the next action.

#### P1-2: Forgiveness for queue actions — IMPLEMENTED

**Files:** `frontend/src/panels/right_now_playing.cpp`, `frontend/include/panels/right_now_playing.hpp`, `backend/include/queue_manager.hpp`, `backend/src/queue_manager.cpp`, `backend/include/controller.hpp`, `backend/src/controller.cpp`, `include/services/IPlayerService.hpp`.

- Identify destructive actions: clear queue, remove track, reorder.
- Add undo or confirmation consistent with Apple's Agency principle.

**Definition of done:** visible destructive queue actions are undoable or confirmed.

**Implementation note:** removing a queue track now shows an inline Undo bar under the Queue tab. `QueueManager::insert()` and `BackendController::insert_into_queue()` restore the track at its original index. Queue row actions are deferred by one main-loop turn so the clicked row is not destroyed while its callback rebuilds the list. Clear queue and reorder have backend APIs but no user-visible controls yet.

#### P1-3: Motion policy — IMPLEMENTED

**Files:** `resources/style.css`, `resources/apple_ui.css`.

- Add `--motion-*` tokens.
- Narrow global `button` animation disable.
- Ensure all new transitions respect `prefers-reduced-motion`.

**Definition of done:** motion tokens exist and reduced-motion is inherited.

**Implementation note:** added `--motion-fast`, `--motion-base`, `--motion-slow`, and `--ease-out`. The Apple layer no longer kills all button transitions; it keeps `animation: none` while allowing short state-feedback transitions. Both `prefers-reduced-motion` and `.reduce-motion` collapse transitions and animations to 1 ms.

#### P1-4: Copy audit — IMPLEMENTED

**Files:** `docs/copy_audit.md`, `frontend/src/panels/right_now_playing.cpp`, `frontend/src/ui/components/AlbumCard.cpp`, `frontend/src/ui/components/AppleAlbumTile.cpp`.

- Replace implementation language with task language.
- Tooltips should describe outcome, not mechanism.

**Definition of done:** no user-visible implementation jargon remains.

**Implementation note:** no implementation jargon was found in UI strings. Queue empty copy became "Your queue is empty" plus an actionable hint, Undo copy describes outcome and recovery, and album menu labels now use "Favourite" consistently with the page title and tooltips.

### P2 — longer-term leverage

#### P2-1: Runtime token generation — IMPLEMENTED

**Files:** `include/ui/theme/design_tokens.hpp`, `frontend/src/ui/theme/ThemeManager.cpp`, `resources/style.css`, `scripts/check_design_consistency.sh`.

- Generate CSS token block from `ThemeManager` so C++ and CSS cannot drift.
- Keep stylesheet `:root` values as fallbacks that mirror the generated block.

**Definition of done:** `ThemeManager` emits the canonical CSS token block.

**Implementation note:** `design_tokens::build_css()` is the single C++ source for font, motion, ease, focus, accent, and on-accent text tokens. `ThemeManager` loads that block through the high-priority provider. `check_design_consistency.sh` compares every canonical token with `style.css:root` and fails on drift.

#### P2-2: Layout stress harness — IMPLEMENTED

**Files:** `tests/test_layout_stress.cpp`, `tests/CMakeLists.txt`, `docs/design_smoke_checklist.md`.

- Screenshot/test at 100%, 150%, 200% text scale.
- Long album/artist names, missing artwork, 1000-track queue.

**Definition of done:** 100, 150, and 200 percent text scale are verified.

**Implementation note:** the headless CTest harness builds 1000 long-named tracks with deliberately missing artwork and checks generated token CSS at 1.0, 1.5, and 2.0. Desktop screenshot checks for those scales are recorded in the smoke checklist.

#### P2-3: RTL smoke pass — IMPLEMENTED

**Files:** `resources/style.css`, `resources/apple_ui.css`, `scripts/rtl_smoke_check.sh`.

- Verify sidebar, tiles, track rows, PiP labels, and progress rows.

**Definition of done:** sidebar, tiles, rows, PiP, and progress rows are verified.

**Implementation note:** GTK CSS does not expose logical `margin-inline` or `border-inline` properties. Physical separators and margins are now scoped to `:dir(ltr)` and mirrored in `:dir(rtl)`; `rtl_smoke_check.sh` fails on unscoped physical edges and asserts RTL coverage for the listed surfaces.

#### P2-4: Design smoke checklist in CI/docs — IMPLEMENTED

**Files:** `docs/design_smoke_checklist.md`, `scripts/check_design_consistency.sh`, `scripts/rtl_smoke_check.sh`, `tests/CMakeLists.txt`.

- Grep for raw hex outside token files.
- Grep for duplicate font variables.
- Manual checklist for focus/keyboard/reduced-motion.

**Definition of done:** token, focus, and accessibility greps are documented or automated.

**Implementation note:** both scripts run as CTest cases. They enforce canonical token parity, no literal typography or hardcoded motion outside token blocks, no raw hex outside `:root`, canonical focus styling, shared accessible-name usage, and RTL containment. The remaining desktop-only checks live in `docs/design_smoke_checklist.md`.

---

## 7. Acceptance metrics

A practical definition of "we used the research" is that these numbers move:

| Metric | Target |
| --- | --- |
| Duplicate font-size token families | 0 |
| Raw hex colours outside token definitions | 0 |
| Custom focusable widgets without accessible names | 0 |
| Custom focusable widgets without focus-visible styling | 0 |
| GTK criticals/assertions in design smoke flow | 0 |
| User-visible strings containing implementation jargon | 0 |
| Components with documented state matrix | 100% |
| Hover-only controls | 0 |

---

## 8. Anti-patterns to stop before adding polish

Taken directly from the research critique section:

1. **Minimal but obscure** — hiding functionality to look clean.
2. **Glass but illegible** — blur/translucency that reduces contrast.
3. **Consistent but soulless** — system grammar with no character.
4. **Delightful but obstructive** — motion or decoration that interrupts listening.
5. **Accessible only after shipping** — accessibility as a final pass.
6. **Polished screenshot, broken lifecycle** — visual quality without runtime correctness.
7. **Apple-shaped UI on Linux** — copying macOS/iOS surface instead of translating principles.

---

## 9. Reading order for implementation work

When picking up a task from this extraction, read in this order:

1. `docs/apple_design_philosophy_deep_dive.md` — principle/rationale.
2. `docs/design-rules.yaml` — machine-readable rule and checks.
3. This document — local contract and file target.
4. Relevant component source and CSS.
5. Verify against section 7 metrics.

---

## 10. Final extraction statement

The valuable output of the Apple research is not "make it look like Apple". It is this:

> One token system, one focus system, one accessibility contract, one component state model, one set of interaction rules, and an explicit backlog that converts taste into verifiable work.

That is the chunk of data we can actually use.
