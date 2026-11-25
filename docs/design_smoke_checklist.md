# Design smoke checklist

This checklist closes the runtime half of the P2 verification. The CTest cases
`layout_stress_tests`, `design_consistency_tests`, and `rtl_smoke_tests` are
headless; the items below need a desktop session and a human judgment call.

## Automated preflight

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Both grep scripts can also be run directly:

```bash
scripts/check_design_consistency.sh
scripts/rtl_smoke_check.sh
```

Record the build commit and GTK version with each pass:

```text
Date:
Build/commit:
GTK version:
Session locale/direction:
Result: PASS / FAIL
Notes:
```

## 1. Text scale: 100%, 150%, 200%

`ThemeManager` derives `--font-scale` from the GTK font size present at startup.
Start at the 100% size (for example `Inter 11`), then change the GTK font name
while the app is running to 150% (`Inter 16.5`) and 200% (`Inter 22`). Restore
the original font name afterwards.

For each scale:

- [ ] Sidebar labels remain readable and do not overlap neighbours.
- [ ] Album tiles and track rows wrap/ellipsize instead of clipping.
- [ ] Queue titles and Undo message stay inside the 340px right panel.
- [ ] PiP title/subtitle and time labels remain visible.
- [ ] Progress rows keep controls and time labels clear of each other.
- [ ] No horizontal scrollbar appears in the main window or right panel.

## 2. RTL smoke pass

Launch with an RTL locale (for example `LANG=fa_IR.UTF-8` where that locale is
installed) or switch the session language to Arabic/Hebrew and restart.

- [ ] Sidebar separator sits on the right; right-now-playing separator sits on the left.
- [ ] Home subtitle and bottom playback modes use right-side spacing.
- [ ] Album tile metadata and favourite control read right-to-left.
- [ ] Track rows, queue rows, and Undo bar align from the right.
- [ ] PiP title/subtitle, transport buttons, and progress/time rows mirror correctly.
- [ ] No leftover LTR-only `margin-left` or `border-right` edges are visible.

## 3. Focus and keyboard

- [ ] Tab reaches sidebar rows, album tiles, artist cards, playlist cards, favourite buttons, track rows, transport controls, and the queue Undo button.
- [ ] Each focused control shows the single accent focus ring.
- [ ] Enter/Space activates the focused tile, favourite, row, or button.
- [ ] Pointer hover and keyboard focus reveal the same favourite control.
- [ ] No tooltip appears without an equivalent accessible name.

## 4. Reduced motion

Enable the desktop "reduce animation" setting and restart the app.

- [ ] Button and tile background transitions collapse to no perceptible motion.
- [ ] No press ripple, bounce, or transform animation plays.
- [ ] State changes (hover, pressed, selected, focused) remain visible instantly.

## 5. Long names, missing artwork, and 1000-track queue

- [ ] Generate the stress data with `scripts/stress_test.sh` or use the harness fixtures in `tests/test_layout_stress.cpp`.
- [ ] 240-character track/artist/album names ellipsize without widening the right panel.
- [ ] Missing artwork shows the fallback tile/row artwork without blank gaps.
- [ ] Artist, playlist, and right-pane artwork tiles are width-driven squares at every panel width.
- [ ] Artwork fills each square tile edge to edge; cover art crops rather than stretches.
- [ ] A 1000-track queue remains scrollable and responsive; row actions still work.
- [ ] Removing a queue row shows Undo; Undo restores the exact original index.
- [ ] Stale Undo hides itself when the queue changes; failure disables Undo with clear copy.

## Sign-off

P2 is complete when every automated check passes and the desktop sections are
marked PASS. Any FAIL should include a screenshot plus the scale/locale used so
the corresponding token or layout rule can be adjusted.
