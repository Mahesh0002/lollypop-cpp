# Theme and colour plan

> Scope: colour, surfaces, materials, accent, and light/dark schemes for the GTK4 frontend.
> Philosophy source: `docs/apple_design_philosophy_deep_dive.md`, `docs/design-rules.yaml`.
> Wiring reference: `frontendPlan.md` §12, `resources/style.css`, `include/ui/theme/design_tokens.hpp`.
> Status: implemented (P0–P3 token work). Verification: `scripts/check_theme_contrast.py`; manual visual sign-off pending.

---

## 1. Design thesis

Apple's colour system is **functional, not decorative**. Colour answers three questions:

1. **Where am I?** Surface elevation separates panel from content from overlay.
2. **What can I act on?** One accent marks interactive and selected state.
3. **What is the content?** Artwork supplies the emotional colour; chrome stays quiet.

Applied to Lollypop:

> Chrome is achromatic. Artwork is chromatic. Accent is the only synthetic colour,
> and it must appear everywhere state changes.

This rule decides every token below. It is also why the current UI reads gray: the
accent exists but does not participate in state, and the surfaces are too close
together to show depth.

### Non-goals

- No rainbow per-page identity colours.
- No imitation of iOS system chrome (traffic lights, SF Symbols, etc.).
- No `backdrop-filter` (unsupported in GTK CSS; already noted in `style.css:13`).
- No gradients as decoration. Gradients express depth or artwork tint only.
- No global GTK theme edits (`frontendPlan.md` §12.3).

---

## 2. Current state audit (verified)

| # | Finding | Evidence |
|---|---|---|
| A1 | Accent is half-wired: only `--accent-color` and `--accent-soft` regenerate at runtime. | `design_tokens.hpp:90-91`; stale `--accent-hover/-active/-shadow/-bg/-text-on-accent` in `style.css:39-45` |
| A2 | Filled accent text is hardcoded white and fails AA on light accents (2.15–2.72:1). | `design_tokens.hpp:66` |
| A3 | Tertiary text is 4.1:1 on the sidebar and is used for 11–12px captions. | `style.css:34`, `.bottom-playback-artist` |
| A4 | Light mode is a stub: one media block covers `.middle-glass-toolbar` only. | `style.css:480-491` |
| A5 | `--destructive-bg` is used but never defined; PiP close hover does nothing. | `style.css:1518`; no definition in `:root` |
| A6 | Canvas widgets hardcode cyan/lavender/near-black instead of accent tokens. | `waveform_widget.cpp:93,112`; `progress_wave.cpp:257`; `waveform_view.cpp:184` |
| A7 | ~35 distinct raw white alphas and ~14 black alphas live outside `:root`; no opacity ramp. | `style.css` audit |
| A8 | Surface ladder has only 3 usable levels; sidebar, right panel, and bottom bar share `--bg-secondary`. | `style.css:24-29` |
| A9 | No status colours; error state is gray. Destructive action is unstyled. | `style.css:920-945`, `2161` |
| A10 | Eight accent presets exist but are never referenced by UI. | `AccentColorManager.cpp:8-16` |
| A11 | Grid clips instead of reflowing at ~1025px content width. | runtime screenshot, 1225px window |

---

## 3. Principles → colour rules

| HIG principle | Rule in this codebase |
|---|---|
| **Clarity** | Text tokens are chosen by contrast budget, never by eye. Body ≥ 4.5:1, large ≥ 3:1. |
| **Deference** | Chrome uses no hue. Accent appears only on state (hover, active, selected, playing, focus). |
| **Depth** | Five surface levels plus elevation shadows. Panels are differentiated by background, not by borders alone. |
| **Familiarity** | Accent presets match Apple system colours so the picker reads as "system accents", not brand swatches. |
| **Consistency** | One accent seed produces every derived token in C++; the stylesheet never hardcodes a second purple. |
| **Craft** | Every translucent overlay comes from a named opacity ramp, so themes retune together. |
| **Responsibility** | Light mode, high contrast, and reduced motion are first-class, not media-query afterthoughts. |
| **Delight** | Artwork tint gives the interface life without adding chrome colour. |

---

## 4. Token architecture

Three layers, one direction of dependency:

```
primitive ramp  →  semantic role  →  component
   --n-950          --surface-panel    .right-now-playing
   --violet-400     --accent-color     .track-row.active
```

`ThemeManager` owns layer 1+2 and emits them from `design_tokens::build_css()`.
The stylesheet keeps dark fallbacks in `:root` for tooling and first frame only.
Components must never reference a primitive directly.

### 4.1 Surface ladder (dark, recommended "Aurora")

| Token | Value | Used by |
|---|---|---|
| `--surface-app` | `#0B0B10` | window, middle library |
| `--surface-sidebar` | `#111118` | sidebar, bottom playback |
| `--surface-panel` | `#16161F` | right now-playing, popovers |
| `--surface-elevated` | `#1C1C27` | cards that need to detach, dialogs |
| `--surface-raised` | `#232331` | menus, tooltips, nested controls |
| `--surface-hover` | `rgba(255,255,255,.06)` | row hover |
| `--surface-active` | `rgba(255,255,255,.10)` | pressed / dragged |
| `--surface-selected` | `rgba(139,108,255,.12)` | selected rows (accent-derived) |

Neutrals are violet-tinted (~6% chroma), not pure gray. This is the single
biggest "feels designed" change and costs nothing in contrast.

### 4.2 Text ladder

| Token | Dark | Light | Minimum contrast |
|---|---|---|---|
| `--text-primary` | `#F5F5F7` | `#1D1D1F` | 12:1 |
| `--text-secondary` | `#B8B6C6` | `#55555D` | 7:1 |
| `--text-tertiary` | `#8E8B9E` | `#77777F` | 4.5:1 |
| `--text-quaternary` | `#6A6778` | `#A0A0A8` | non-text only |

`--text-tertiary` changes from `#7A7A8A` to `#8E8B9E` to clear AA at 11–12px.
`--text-disabled` stays below 4.5:1 by definition and may not carry content.

### 4.3 Borders, opacity, glass

| Token group | Dark | Light |
|---|---|---|
| `--border-subtle` | `#2A2A38` | `#E2E2E8` |
| `--border-strong` | `#3A3A4C` | `#C7C7D1` |
| `--overlay-hairline` | `rgba(255,255,255,.10)` | `rgba(0,0,0,.08)` |
| `--glass-thin-bg` | `rgba(255,255,255,.06)` | `rgba(255,255,255,.72)` |
| `--glass-regular-bg` | `rgba(255,255,255,.08)` | `rgba(255,255,255,.82)` |
| `--glass-thick-bg` | `rgba(255,255,255,.12)` | `rgba(255,255,255,.92)` |

Opacity gets its own ramp so the 35 ad-hoc alphas collapse:

`--opacity-2/.4/.6/.8/1.0/1.2` → `.02 .04 .06 .08 .10 .12` (white or black per scheme).

---

## 5. Accent system

### 5.1 One seed, nine derived tokens

`AccentColorManager` supplies `(h, s, l)`. `design_tokens` derives:

| Token | Derivation | Purpose |
|---|---|---|
| `--accent-color` | seed | icons, text, indicators |
| `--accent-hover` | `l + 8%` | hovered accent control |
| `--accent-active` | `l − 10%` | pressed accent control |
| `--accent-soft` | seed @ 14% | selected row / active tab fill |
| `--accent-softer` | seed @ 7% | large tinted regions |
| `--accent-border` | seed @ 35% | outlined selected state |
| `--accent-shadow` | seed @ 28% | glow under primary control |
| `--accent-shadow-strong` | seed @ 42% | focus/pressed glow |
| `--accent-on` | black or white, whichever has higher contrast | text on filled accent |

Rule: recite the ratio at generation time. If `contrast(accent, on) < 4.5`, choose
the other ink. Never hardcode `#ffffff` (`design_tokens.hpp:66` is the current bug).

Default brand accent stays **violet `#8B6CFF`** (evolution of today's `#A78BFA`).
Presets realign to Apple system colours so they read as system accents:

| Name | Dark seed | Light seed | On-accent |
|---|---|---|---|
| Violet (default) | `#8B6CFF` | `#6D4AE0` | `#0B0B10` |
| Blue | `#0A84FF` | `#0066CC` | `#0B0B10` |
| Teal | `#64D2FF` | `#0A84B8` | `#04262B` |
| Green | `#32D74B` | `#1E8A32` | `#04140A` |
| Yellow | `#FFD60A` | `#B58900` | `#1A1400` |
| Orange | `#FF9F0A` | `#C26A00` | `#1A0D00` |
| Red | `#FF453A` | `#D70015` | `#FFFFFF` |
| Pink | `#FF375F` | `#D81E4A` | `#FFFFFF` |
| Indigo | `#5E5CE6` | `#4B49C6` | `#FFFFFF` |

Light mode uses the darker seed so accent text clears 4.5:1 on white.

### 5.2 Where accent must appear

Accent is a state signal, not wallpaper. Minimum set:

- focus ring (`--border-focus`)
- playing track title/indicator and equalizer bars
- active tab pill and toggled mode buttons
- progress fill, waveform played region, volume highlight
- favourite heart when active
- primary buttons (filled) and their hover/pressed states
- loading spinner
- selection drag target

It must **not** appear in: sidebar idle rows, body text, static icons, panel tint.

### 5.3 Status colours

| Token | Dark | Light |
|---|---|---|
| `--success` | `#32D74B` | `#1E8A32` |
| `--warning` | `#FFD60A` | `#B58900` |
| `--error` | `#FF453A` | `#D70015` |
| `--info` | `#64D2FF` | `#0A84B8` |

Each gets a `-soft` 14% variant for banners. `--destructive-bg` is defined here
and wired at `style.css:1518`; the hardcoded `color: white` becomes `--accent-on`
or `--text-primary` as appropriate.

---

## 6. Light scheme

Driven by `AdwStyleManager:dark` (or `gtk-interface-color-scheme`), not by a
partial media query. `ThemeManager` rebuilds the single runtime provider with
the light token block, so no selector can be left behind.

| Token | Value |
|---|---|
| `--surface-app` | `#F5F5F7` |
| `--surface-sidebar` | `#EFEFF2` |
| `--surface-panel` | `#FFFFFF` |
| `--surface-elevated` | `#FFFFFF` + `--border-subtle` |
| `--surface-raised` | `#F7F7FA` |
| `--surface-hover` | `rgba(0,0,0,.05)` |
| `--surface-active` | `rgba(0,0,0,.09)` |
| `--shadow-strong` | `rgba(0,0,0,.16)` |
| `--shadow-subtle` | `rgba(0,0,0,.08)` |

Artwork scrims invert: light mode uses white scrims with dark text; dark mode uses
black scrims with white text. Expose `--scrim-bg`, `--scrim-text`, `--scrim-border`
instead of the five hardcoded `rgba(0,0,0,…)` values in `apple_ui.css`.

The single existing `@media (prefers-color-scheme: light)` block (`style.css:480-491`)
is deleted once the token switch ships, otherwise a white toolbar can land on a
dark app.

---

## 7. Deference: artwork-derived tint

This is the "colour palette" users perceive, and it comes from content, not chrome.

1. `ThumbnailGenerator` computes the dominant colour once per artwork.
2. Cache it next to the existing thumbnail (`ArtworkCache`).
3. Publish it as `--tint-color`, `--tint-strong` (35%), `--tint-soft` (10%).
4. Apply in three places only:
   - `.right-now-playing` → top radial gradient
   - `.bottom-playback` → 1px top hairline + 6% wash
   - `.detail-hero` → replace the black overlay gradient
5. Clamp tint luminance so text contrast never drops below AA. If the artwork is
   near-black or near-white, fall back to `--surface-panel` and no tint.

No blur is required; large translucent gradients read as vibrancy in GTK.

---

## 8. Component contract

| Selector | Surface | Notes |
|---|---|---|
| `window`, `.middle-library` | `--surface-app` | reference floor |
| `.apple-sidebar`, `.bottom-playback` | `--surface-sidebar` | one step up |
| `.right-now-playing`, popovers | `--surface-panel` | two steps up |
| dialogs, menus, tooltips | `--surface-raised` | shadow + hairline |
| album/artist tiles | transparent | artwork carries colour |
| track rows | transparent → `--surface-hover` / `--surface-selected` | |
| text inputs | `--surface-panel` + `--border-subtle` | focus uses `--accent-border` |
| filled primary button | `--accent-color` + `--accent-on` | hover/active derived |
| destructive control | `--error` + `--error-soft` hover | replaces undefined token |

Canvas widgets (`WaveformView`, `WaveformWidget`, `ProgressWave`) must accept the
ramp through `set_accent_color()` and a new `set_scheme(bool dark)`; no literal
`rgb()` values outside a debug fallback.

---

## 9. Adjacent UI defect (not palette, but blocks "best")

The album grid clips its third column at ~1025px content width. Replace the fixed
column count with a width-driven flow box (or compute columns from
`floor(width / 220)`), so 1225px shows 4 columns and 800px shows 2. Same pass
reviews the Home page's empty top gutter and the missing page title.

---

## 10. Phases

### P0 — Token foundation (0.5 day)

- Split `:root` into primitive → semantic layers (`style.css`).
- Apply surface/text/border/opacity values from §4; fix `--text-tertiary`.
- Define `--destructive-bg`; remove `--accent-active`, `--accent-shadow-strong`,
  and any other dead token, or start using them.
- Extend `scripts/check_design_consistency.sh` with a contrast check.

**Exit:** zero raw hex outside token blocks; all text tokens ≥ 4.5:1 on their
intended surface; app visually unchanged except slightly cleaner grays.

### P1 — Accent engine (1 day)

- `design_tokens::build_css()` emits the full ramp + `--accent-on` from the seed.
- Wire `AccentColorManager` into a palette popover (8 swatches + hex row).
- Persist via GSettings; apply at startup before first frame.
- Feed `WaveformView`, `ProgressWave`, `WaveformWidget` from the ramp.
- If libadwaita exposes accent API, mirror the value; otherwise document the
  divergence and keep custom widgets authoritative.

**Exit:** every preset produces correct hover/active/soft/on-accent; no purple
leaks when Emerald is selected.

### P2 — Depth and content colour (1.5 days)

- Adopt the five-level surface ladder across panels and overlays.
- Replace ad-hoc alphas with the opacity ramp.
- Add status colours and destructive styling.
- Artwork tint (§7) with luminance clamp and fallback.
- Light scheme token block + `AdwStyleManager` switch; delete the partial
  media query.
- Invert scrims and glass tokens for light mode.

**Exit:** both schemes render without a hardcoded colour; screenshots at 100%
and 150% scale.

### P3 — Verification and polish (0.5 day)

- Extend `docs/design_smoke_checklist.md`: dark, light, each accent, high
  contrast, reduced motion.
- Add `scripts/check_theme_contrast.py` to CI.
- Responsive grid reflow (§9).
- Update `docs/design-rules.yaml` `acceptance_metrics`.

---

## 11. Acceptance metrics

Add to `design-rules.yaml`:

```yaml
raw_hex_outside_token_definitions: 0
raw_rgba_outside_token_definitions: 0
contrast_failures_body_text: 0
hardcoded_accent_in_canvas_widgets: 0
accent_tokens_emitted: 9   # color, hover, active, soft, softer, border, shadow, shadow-strong, on
schemes_verified: 2        # dark, light
undefined_css_variables: 0
```

Existing metrics from `design-rules.yaml` stay in force.

---

## 12. Risks and open questions

| Risk | Mitigation |
|---|---|
| GTK CSS lacks `color-mix`/relative colours on older 4.x | Generate all derived values in C++ as `rgba()`. |
| libadwaita accent disagrees with custom accent | Set both from one source; if unavailable, keep custom widgets authoritative and document. |
| Artwork tint reduces text contrast | Luminance clamp + AA assertion in CI. |
| Light mode surfaces look washed out | Use Apple's `#F5F5F7` floor, white only for elevated panels, borders to separate. |
| Scope creep into layout | Grid reflow is one task in P3, not part of the token work. |

Open decisions:

1. Default accent: keep violet brand, or ship Apple Blue as default?
2. Should the app follow the system accent by default, with manual override?
3. Is reduced transparency (solid panels) required, or is reduced motion enough for v1?

---

## 13. Implementation notes

- Default accent stays violet `#8B6CFF`; the palette follows the system
  light/dark scheme and can be overridden from the sidebar Appearance row.
- Accent persistence uses an XDG key file
  (`$XDG_CONFIG_HOME/lollypop-cpp/theme.ini`) because the project ships no
  GSettings schema. Moving to GSettings later only changes
  `Application::apply_accent_seed()`.
- Four seeds were darkened to keep accent text at 4.5:1: light Green
  `#1C7F2E`, Teal `#0979AA`, Yellow `#8C6900`, Orange `#B05F00`; dark Indigo
  is `#6E6CFF`. The original Apple values missed AA on one scheme each.
- Artwork tint is extracted from a 24×24 decode of the current cover, cached by
  path, then luminance/saturation-clamped before it becomes `--tint-*`.
- libadwaita 1.x on this baseline has no stable accent API. The runtime
  `GtkCssProvider` remains authoritative, and Cairo widgets read the same CSS
  tokens.
- High-contrast glass overrides are restated in the runtime provider because
  it sits above the base stylesheet in priority.
