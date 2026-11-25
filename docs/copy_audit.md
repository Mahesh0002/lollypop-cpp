# User-visible copy audit

Companion to `docs/design-rules.yaml` rule `SIM-2` ("Use plain language") and backlog item
P1-4.

## Scope

Audited quoted strings in `frontend/src/**/*.cpp` that can reach the UI: labels, menu items,
tooltips, empty/loading/error copy, and the new queue undo bar. Excluded from the UI copy
audit:

- `std::cout` / `std::cerr` diagnostics and log prefixes.
- Internal identifiers, action names, page IDs, and CSS class names.
- Debug comments and tests.

## Method

1. Searched UI strings for implementation words: `cache`, `provider`, `entity`, `index`,
   `ID`, `uuid`, `backend`, `frontend`, `API`.
2. Reviewed all `gtk_label_new`, `gtk_button_new_with_label`, `set_tooltip_text`, and
   `g_menu_append` call sites for outcome-focused wording and consistent terms.
3. Compared state copy against the component state contracts in
   `docs/APPLE_DESIGN_EXTRACTION_ACTION_PLAN.md` section 4.

## Findings and changes

| Finding | Action | Status |
| --- | --- | --- |
| No user-visible string contains `cache`, `provider`, `index`, `ID`, or UUID jargon; matches were log messages such as "artwork cache unavailable". | No UI change needed; logs are excluded by scope. | Verified by grep |
| Queue empty state said only "Queue is empty" and did not tell the listener what to do next. | Replaced with "Your queue is empty" and "Add tracks from your library to start listening." in `frontend/src/panels/right_now_playing.cpp`. | Implemented |
| Album context menus used "Favorite" while the page and tooltips use "Favourite". | Changed `AlbumCard` and `AppleAlbumTile` menu labels to "Favourite". | Implemented |
| Queue removal previously gave no feedback about what happened or how to reverse it. | Undo bar copy: "Removed <track> from the queue", button "Undo", tooltip "Put <track> back at its original place", failure "Couldn't put the track back." | Implemented |
| "Add to Playlist..." was reviewed as implementation-sounding. | Kept: playlist is a user concept, not backend language. | Verified |
| Existing empty/loading/error copy was reviewed. | Kept: "No results found", "Try a different search term", "No track playing", "No lyrics available", and retry/error titles already describe user outcomes. | Verified |

## Remaining polish (not jargon)

- Favourite menu labels are static. When a track/album is already favourited, a dynamic
  "Remove from favourites" label would be more precise; this needs per-row menu model updates
  and is deferred to a later stage.
- Runtime pass with a real library should confirm that long track titles in undo copy ellipsize
  cleanly and that translated/bidi locales keep the Undo button reachable.

## Acceptance

- User-visible implementation jargon: **0 found**.
- Inconsistent favourite term in visible UI copy: **0 remaining** in audited call sites.
- Undo copy describes the outcome and recovery action, not the backend operation.
