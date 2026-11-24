# Sidebar icon + typography plan

## Outline icon packages

Research date: 2026-11-23. Arch package names are from the local `pacman` database.

| Package | Style | License | Arch availability | Notes |
| --- | --- | --- | --- | --- |
| **Lucide / `lucide-static`** | Outline, 24px grid, 2px stroke | ISC | Not in official repos; use npm `lucide-static` or vendor SVGs | Best overall match for Apple-style outline UI. 1600+ consistent SVG files. Recommended for the next step if we want to bundle our own icons. |
| **Tabler Icons** | Outline, 24px grid, 2px stroke | MIT | Not in official repos; vendor from GitHub | 6545 icons. Very consistent, slightly more "dashboard" than SF Symbols. |
| **Phosphor** | Outline + Thin/Light/Regular/Bold/Fill | MIT | Not in official repos; vendor from GitHub | 1248 icons designed on a 16px grid. Excellent weight control; closest to SF Symbol weight variations. |
| **Material Symbols Outlined** | Variable outline font, weight 100–700, fill axis | Apache 2.0 | `otf-material-icons`, `ttf-material-icons` are classic Material Icons, not the newer variable Symbols family | Powerful but visually more Material/Android than Apple. |
| **Papirus** | General flat icon theme | GPLv3 | `extra/papirus-icon-theme` | Good coverage, but not a pure outline set. Useful for app/file icons, not source-list navigation. |
| **Adwaita symbolic** | Monochrome outline glyphs | LGPL | `extra/adwaita-icon-theme` (installed) | Already available. Used by this build so the sidebar works without adding a dependency. |

**Recommendation:** bundle **Lucide** SVGs (or Phosphor Regular if we want lighter strokes) as a GResource for the final look. For this visual pass I used the installed **Adwaita symbolic** icons because they are outline-style, consistent, and require no package install.

## Font recommendation

**Primary:** Inter Variable (already present in `resources/fonts/Inter/`; Arch package `extra/inter-font` is available).
**Fallback:** Noto Sans, then sans-serif.

Inter is the closest practical Linux match to Apple's SF Pro: neutral, highly legible at small sizes, and its semibold reads cleanly without looking heavy.

### Typography rhythm

| Role | Family | Size | Weight | Tracking | Where |
| --- | --- | ---: | ---: | ---: | --- |
| Display title | Inter | 34px | 600 | -0.5px | Page hero / main header only |
| Page title | Inter | 28px | 600 | -0.3px | Albums, Artists, Playlists headers |
| Section title | Inter | 20px | 600 | -0.1px | "Recently played", card section headers |
| Row title | Inter | 15px | 400 / 600 active | 0 | Sidebar labels, track titles, playlist names |
| Body / subtitle | Inter | 13px | 400 | 0 | Artist names, album metadata, tooltips |
| Caption | Inter | 11px | 400 | 0 | Counts, timestamps, secondary metadata |
| Button label | Inter | 13px | 600 | 0 | Toolbar buttons, sort chip |

Rules:
- Use only **Regular 400** and **Semibold 600** for UI text. Avoid bold/light in navigation.
- Active row is Semibold; inactive row is Regular.
- Numbers in track lists should use tabular figures.
- Keep one size step between a title and its subtitle; never jump more than two steps.

The new stylesheet `resources/apple_ui.css` defines these tokens and applies the sidebar portion.
