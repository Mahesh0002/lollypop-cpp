# Apple HIG to GTK 4.24.x Mapping Guide

## Executive Summary

This document maps Apple Human Interface Guidelines concepts, materials, components, and patterns to their GTK 4.24.x equivalents. Since GTK is a cross-platform toolkit (primarily Linux/Windows) while HIG targets Apple platforms exclusively, mappings are **functional approximations** — not pixel-perfect translations. The goal is achieving native-feeling GTK apps that respect HIG *principles* where applicable.

**Key GTK 4.24.x Features Used:**
- `GtkWidget` + `GtkLayoutManager` (replaces `GtkContainer`)
- `GtkCssProvider` for styling (replaces theme engines)
- `GskRenderer` / `GskRenderNode` for advanced rendering
- `GtkGesture` / `GtkEventController` for input
- `Adw` (libadwaita) for GNOME HIG compliance (optional but recommended)

---

## Part 1: Foundations — Layout, Materials, Typography

### 1.1 Layout System

| HIG Concept | GTK 4.24.x Equivalent | Implementation Notes |
|-------------|----------------------|---------------------|
| **Size Classes** (compact/regular horizontal/vertical) | `GtkWindow` + `GtkWidget::compute-expand` + CSS `@media (max-width: Npx)` | No direct equivalent. Use `GtkWindow::default-width/height`, `GtkWidget::hexpand/vexpand`, and CSS media queries for responsive breakpoints. |
| **Safe Areas / Margins** | `GtkWindow::content-margins` (via `GtkRoot`) + `GtkWidget::margin-*` | `adw_window_set_content()` handles system bars. For custom: `gtk_root_get_display()` → `gdk_display_get_monitor_at_window()` → `gdk_monitor_get_geometry()` |
| **Reading Order (RTL/LTR)** | `GtkWidget::direction` + `gtk_widget_set_direction()` | Automatic via locale. Force with `gtk_widget_set_direction(GTK_TEXT_DIR_RTL)` |
| **Visual Hierarchy (alignment, indentation, grouping)** | `GtkBox` (vertical/horizontal), `GtkGrid`, `GtkCenterBox`, `GtkFrame`, `GtkSeparator` | `GtkCenterBox` = leading/center/trailing toolbar layout. `GtkFrame` + CSS for grouped sections. |
| **Progressive Disclosure** | `GtkExpander`, `GtkRevealer`, `GtkStack` + `GtkStackSwitcher` | `GtkRevealer` for animated show/hide. `GtkStack` for tab-like views. |
| **Scroll Edge Effects** | `GtkScrolledWindow` + CSS `overscroll-behavior` | GTK 4.16+: `gtk_scrolled_window_set_propagate_natural_width/height()`. Custom edge fade: CSS `mask-image: linear-gradient(...)` on content. |
| **Background Extension Effect** | Custom `GskRenderNode` in `GtkWidget::snapshot()` | Advanced. Subclass `GtkWidget`, override `snapshot()`, render background extended + blurred under sidebar/toolbar areas. |

**Recommended Layout Pattern (HIG Toolbar-like):**
```c
GtkWidget *window = gtk_window_new();
GtkWidget *center_box = gtk_center_box_new();
GtkWidget *header = gtk_header_bar_new(); // or custom GtkBox

gtk_center_box_set_start_widget(GTK_CENTER_BOX(center_box), start_box);  // Leading: back, sidebar toggle
gtk_center_box_set_center_widget(GTK_CENTER_BOX(center_box), title_label); // Center: title
gtk_center_box_set_end_widget(GTK_CENTER_BOX(center_box), end_box); // Trailing: actions, search, more

GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
gtk_box_append(GTK_BOX(main_box), header);
gtk_box_append(GTK_BOX(main_box), content_area); // flex/grow
gtk_window_set_child(GTK_WINDOW(window), main_box);
```

---

### 1.2 Materials — Liquid Glass & Standard Materials

| HIG Material | GTK 4.24.x Approach | CSS / Code |
|--------------|---------------------|------------|
| **Liquid Glass (Regular)** | `GtkWidget` + CSS `backdrop-filter: blur(20px) saturate(180%)` + semi-transparent background | `.liquid-glass { background: rgba(255,255,255,0.1); backdrop-filter: blur(20px) saturate(180%); border: 1px solid rgba(255,255,255,0.2); }` |
| **Liquid Glass (Clear)** | Same as above, lower opacity, no saturation boost | `.liquid-glass-clear { background: rgba(255,255,255,0.05); backdrop-filter: blur(30px); }` |
| **Dimming Layer (35% dark)** | Pseudo-element or child widget with `background: rgba(0,0,0,0.35)` | `.liquid-glass-clear::before { content: ""; position: absolute; inset: 0; background: rgba(0,0,0,0.35); }` |
| **Standard Materials (iOS: ultraThin → thick)** | CSS classes with increasing `backdrop-filter` blur + opacity | `.material-ultra-thin { backdrop-filter: blur(5px); background: rgba(255,255,255,0.05); }`<br>`.material-thick { backdrop-filter: blur(40px); background: rgba(255,255,255,0.4); }` |
| **Vibrant Labels/Fills** | CSS `color: <system-color>` with `mix-blend-mode` | `.vibrant-label { color: currentColor; mix-blend-mode: overlay; }` (limited — GTK lacks true vibrancy) |
| **Separator Vibrancy** | `GtkSeparator` + CSS `background: currentColor; opacity: 0.2;` | |

**Critical GTK Limitation:** True "vibrancy" (dynamic color adaptation to background) requires **custom shaders** via `GskGLShader` or `GskRenderNode`. CSS `backdrop-filter` only blurs — doesn't adapt foreground color.

**Workaround for Vibrancy:**
```c
// In widget snapshot(): sample background average luminance, choose light/dark text
static void my_widget_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  // 1. Render background to offscreen texture
  // 2. Sample center pixel luminance (via compute shader or CPU readback)
  // 3. Choose text color: luminance > 0.5 ? black : white
  // 4. Render text with chosen color
}
```

**Recommendation:** For GTK 4.24, use **libadwaita** (`AdwToolbarView`, `AdwHeaderBar`, `AdwClamp`) which implements GNOME's material system (similar principles, different aesthetic). If targeting cross-platform, implement simplified Liquid Glass via CSS `backdrop-filter` (works on Wayland + GTK4 + Mutter/KWin; limited on X11/Windows).

---

### 1.3 Typography

| HIG Spec | GTK 4.24.x Implementation |
|----------|---------------------------|
| **System Font (SF Pro)** | **Not available on Linux.** Use `Adw` fonts (Inter, Cantarell) or bundle **SF Pro** legally (Apple license: Apple platforms only). **Alternative:** `Noto Sans` (Google), `Inter` (Rasmus Andersson) — both metric-compatible-ish. |
| **Variable Fonts** | `PangoFontDescription` + `pango_font_description_set_variations()` | GTK 4 / Pango 1.50+ supports variable fonts. Load `.ttf` with `wght`, `wdth`, `opsz` axes. |
| **Dynamic Optical Sizes (opsz)** | `pango_font_description_set_variations_static(desc, "opsz", size_in_points)` | Automatic in Pango 1.50+ when font has `opsz` axis. |
| **Text Styles (Large Title → Caption)** | Define CSS classes mapping to font-size/weight | See **Typography CSS** below |
| **Dynamic Type (user text scaling)** | `GtkSettings::gtk-font-name` + `gtk-fontconfig-timestamp` | Listen to `GtkSettings::notify::gtk-font-name` → recompute layout. `Adw` handles this via `AdwStyleManager`. |
| **Minimum Sizes Table** | Enforce in CSS: `font-size: max(11px, 1rem);` | Use CSS `clamp(min, preferred, max)` |
| **Tracking Values** | `letter-spacing` in CSS | `.text-body { letter-spacing: -0.011em; }` (convert HIG pts to em) |
| **Leading (Line Height)** | `line-height` in CSS | `.text-body { line-height: 1.4; }` |

**Typography CSS (HIG-inspired, using Inter/Noto):**
```css
/* Base scaling: 1rem = 12pt (macOS default) → adjust for platform */
:root {
  --text-scale: 1; /* User-controlled via GtkSettings */
  --font-sans: 'Inter', 'Noto Sans', sans-serif;
  --font-serif: 'Noto Serif', serif;
  --font-mono: 'JetBrains Mono', 'Noto Sans Mono', monospace;
}

/* Text Styles — approximate HIG hierarchy */
.text-large-title  { font: var(--font-sans); font-size: clamp(28px, 3.5vw, 34px); font-weight: 700; line-height: 1.15; letter-spacing: 0.007em; }
.text-title1       { font: var(--font-sans); font-size: clamp(24px, 3vw, 28px);  font-weight: 600; line-height: 1.2;  letter-spacing: 0.005em; }
.text-title2       { font: var(--font-sans); font-size: clamp(20px, 2.5vw, 22px); font-weight: 600; line-height: 1.25; letter-spacing: 0.003em; }
.text-title3       { font: var(--font-sans); font-size: clamp(18px, 2vw, 20px);  font-weight: 600; line-height: 1.3;  letter-spacing: 0.002em; }
.text-headline     { font: var(--font-sans); font-size: clamp(16px, 1.5vw, 17px); font-weight: 600; line-height: 1.35; letter-spacing: -0.003em; }
.text-body         { font: var(--font-sans); font-size: clamp(13px, 1rem, 17px);  font-weight: 400; line-height: 1.4;  letter-spacing: -0.011em; }
.text-callout      { font: var(--font-sans); font-size: clamp(13px, 1rem, 16px);  font-weight: 400; line-height: 1.4;  letter-spacing: -0.008em; }
.text-subheadline  { font: var(--font-sans); font-size: clamp(12px, 0.9rem, 15px); font-weight: 400; line-height: 1.35; letter-spacing: -0.005em; }
.text-footnote     { font: var(--font-sans); font-size: clamp(11px, 0.8rem, 13px); font-weight: 400; line-height: 1.3;  letter-spacing: -0.002em; }
.text-caption1     { font: var(--font-sans); font-size: clamp(10px, 0.75rem, 12px); font-weight: 400; line-height: 1.25; letter-spacing: 0.001em; }
.text-caption2     { font: var(--font-sans); font-size: clamp(9px, 0.7rem, 11px);  font-weight: 400; line-height: 1.2;  letter-spacing: 0.003em; }

/* Dynamic Type support — responds to GtkSettings font size */
@media (min-resolution: 1.25dppx) {
  :root { --text-scale: 1.25; }
}
.text-body { font-size: calc(clamp(13px, 1rem, 17px) * var(--text-scale)); }
```

**Dynamic Type Handler:**
```c
static void on_font_changed(GtkSettings *settings, GParamSpec *pspec, gpointer app) {
  const char *font_name = gtk_settings_get_font_name(settings);
  // Parse font_name (e.g., "Inter 14") → extract size → update --text-scale CSS variable
  GtkCssProvider *provider = get_app_css_provider();
  char *css = g_strdup_printf(":root { --text-scale: %f; }", extracted_size / 13.0);
  gtk_css_provider_load_from_data(provider, css, -1);
  g_free(css);
}
g_signal_connect(gtk_settings_get_default(), "notify::gtk-font-name", G_CALLBACK(on_font_changed), app);
```

---

## Part 2: Components — Toolbars, Navigation, Controls

*(Continued in next section — this document covers Foundations only to stay within token limits)*

---

## Part 3: SF Symbols → GTK Icons

| SF Symbols Feature | GTK 4.24.x Equivalent |
|--------------------|----------------------|
| **7,000+ symbols, 9 weights, 3 scales** | **No direct equivalent.** Options:<br>1. **Adwaita Icon Theme** (GNOME HIG, ~1,500 icons, 1 weight)<br>2. **Material Symbols** (Google, variable font, 2,500+, 5 weights, 3 optical sizes) — load as `PangoFontDescription` with `wght`/`opsz`<br>3. **Custom SVG icon set** + `GtkImage::set_from_paintable()` |
| **Weight matching with text** | **Material Symbols Variable Font** — set `wght` axis = text weight (400=Regular, 600=Semibold, 700=Bold) |
| **Render modes (Monochrome, Hierarchical, Palette, Multicolor)** | **CSS `color` + `font-variation-settings`** for weight. Palette/Multicolor: use colored SVG icons (`GdkTexture` from `gdk_texture_new_from_file()`) |
| **Animations (Draw, Bounce, Variable)** | `GtkImage` + `GskRenderNode` custom animation OR `AdwAnimation` (libadwaita) for simple transitions. Complex path animation: **custom widget** with `GtkSnapshot` + time-based path interpolation. |
| **Annotation / Draw On/Off** | **Not available.** Requires per-glyph path data + animation timeline. Use **Lottie** (`liblite` / `rlottie`) for After Effects → JSON → GTK playback. |

**Recommended: Material Symbols Variable Font**
```c
// Load Material Symbols Variable Font (.ttf)
PangoFontDescription *desc = pango_font_description_new();
pango_font_description_set_family(desc, "Material Symbols Rounded");
pango_font_description_set_variations_static(desc, "wght", 400, "opsz", 24, "FILL", 0, "GRAD", 0, -1);

// Use in GtkLabel with markup:
gtk_label_set_markup(GTK_LABEL(label), "<span font_desc='Material Symbols Rounded 24' font_variations='wght=400 opsz=24 FILL=0 GRAD=0'>&#xe3af;</span>"); // home icon
```

**Icon Weight Sync with Text:**
```c
void sync_icon_weight_with_label(GtkWidget *icon_label, GtkWidget *text_label) {
  PangoAttrList *attrs = gtk_label_get_attributes(GTK_LABEL(text_label));
  // Parse attrs for weight → apply to icon_label via font_variations
}
```

---

## Part 4: Platform-Specific — macOS Patterns in GTK

| macOS / HIG Pattern | GTK 4.24.x Approach |
|---------------------|---------------------|
| **Menu Bar (global, per-app)** | **Not applicable on Linux/Windows.** Use `GtkMenuBar` inside window (traditional) or `GtkPopoverMenu` from header bar button (modern). For cross-platform: `#ifdef __APPLE__` → native `NSMenu`; else → in-window menu. |
| **Window Toolbar + Title Bar Unity** | `GtkHeaderBar` + `gtk_window_set_titlebar()` — unified chrome. |
| **Sidebar (Source List)** | `GtkSidebar` (GTK 4.10+) or `AdwNavigationView` / `AdwNavigationPage` (libadwaita) |
| **Inspector / Trailing Sidebar** | `GtkPaned` with trailing child, or `AdwToolbarView` + `AdwTabView` |
| **Full Screen Mode** | `gtk_window_fullscreen()` + `GtkHeaderBar` hides automatically (with `gtk_header_bar_set_show_title_buttons()`) |
| **Keyboard Shortcuts (Accelerators)** | `GtkShortcutController` + `GtkShortcut` + `GtkKeyvalTrigger` — global or per-widget |
| **Touch Input (macOS 2026+)** | `GtkGestureClick` / `GtkGestureSwipe` / `GtkGestureZoom` — works on touchscreens. `GtkEventControllerMotion` for hover. |
| **Document Menu (window proxy icon)** | Custom `GtkButton` in header bar leading, showing document icon + popover menu |
| **Dock Menu / App Badge** | **Linux:** `libunity` / `libayatana` for launcher integration. **Windows:** `ITaskbarList3` via `g_win32_*`. Abstract behind platform layer. |

---

## Part 5: Implementation Checklist

### Minimum Viable "HIG-like" GTK 4.24 App
- [ ] **CSS Provider** with Liquid Glass approximations + Typography scale
- [ ] **Header Bar** (`GtkHeaderBar`) with `GtkCenterBox` for leading/center/trailing
- [ ] **Responsive Layout** using `GtkBox`/`GtkGrid` + CSS media queries for size-class-like breakpoints
- [ ] **Dynamic Type** listener on `GtkSettings::gtk-font-name`
- [ ] **Material Symbols Variable Font** for icons weight-matched to text
- [ ] **Backdrop Blur** on header/sidebar via CSS `backdrop-filter` (Wayland only)
- [ ] **Keyboard Shortcuts** via `GtkShortcutController`
- [ ] **Touch Gestures** via `GtkGesture*` controllers
- [ ] **Dark Mode** via `AdwStyleManager` (libadwaita) or `GtkSettings::gtk-application-prefer-dark-theme`

### Recommended Dependencies
```meson
# meson.build
dependency('gtk4', version: '>=4.24.0')
dependency('libadwaita-1', version: '>=1.6.0', required: false) # optional but recommended
dependency('pango', version: '>=1.50.0') # variable font support
dependency('graphene-1.0') # for GskRenderNode math
```

### Platform-Specific Code Organization
```
src/
├── platform/
│   ├── linux.c      # GTK4 + libadwaita
│   ├── macos.m      # Native AppKit (menubar, dock, window proxy)
│   └── windows.c    # Win32 taskbar, DWM blur
├── ui/
│   ├── css/         # HIG-inspired themes
│   ├── widgets/     # Custom widgets (LiquidGlassBox, VibrantLabel, etc.)
│   └── icons/       # Material Symbols font loading
└── app/
    └── shortcuts.c  # GtkShortcutController setup
```

---

## Part 6: Known Gaps (HIG → GTK)

| HIG Feature | GTK Status | Workaround |
|-------------|------------|------------|
| True vibrancy (dynamic text color on blur) | ❌ Missing | Custom shader or CPU luminance sampling |
| Liquid Glass "clear" variant with auto-dimming | ❌ Missing | Manual dimming layer + heuristic |
| SF Symbols multicolor / palette rendering | ❌ Missing | Use colored SVG assets |
| SF Symbols Draw/Bounce/Variable animations | ❌ Missing | Lottie / custom path animation |
| iPhone Duo / folding device layouts | ❌ N/A | `GtkWindow` responsive only |
| VisionOS immersive / spatial layouts | ❌ N/A | Not applicable |
| Menu bar (global, always visible) | ❌ Platform diff | In-window header bar menu |
| Document proxy icon in titlebar | ⚠️ Partial | Custom header bar button |
| System-wide accent color | ⚠️ Partial | `GtkSettings::gtk-theme-name` + CSS vars |
| Handoff / Continuity | ❌ N/A | Not applicable |

---

## Appendix: CSS Variables for HIG-Themed GTK App

```css
:root {
  /* Colors — HIG-inspired semantic palette */
  --hig-label-primary: #1d1d1f;
  --hig-label-secondary: #6e6e73;
  --hig-label-tertiary: #a1a1a6;
  --hig-label-quaternary: #d1d1d6;
  
  --hig-fill-primary: rgba(0,0,0,0.08);
  --hig-fill-secondary: rgba(0,0,0,0.05);
  --hig-fill-tertiary: rgba(0,0,0,0.03);
  
  --hig-separator: rgba(0,0,0,0.15);
  
  --hig-accent: #007aff; /* iOS blue */
  --hig-accent-hover: #0056cc;
  --hig-accent-pressed: #003d99;
  
  --hig-destructive: #ff3b30;
  --hig-destructive-hover: #cc2e26;
  
  /* Materials */
  --hig-glass-regular-bg: rgba(255,255,255,0.72);
  --hig-glass-regular-blur: 20px;
  --hig-glass-regular-border: rgba(255,255,255,0.2);
  
  --hig-glass-clear-bg: rgba(255,255,255,0.1);
  --hig-glass-clear-blur: 30px;
  --hig-glass-clear-dim: rgba(0,0,0,0.35);
  
  /* Spacing (8pt grid) */
  --hig-space-1: 4px;
  --hig-space-2: 8px;
  --hig-space-3: 12px;
  --hig-space-4: 16px;
  --hig-space-5: 20px;
  --hig-space-6: 24px;
  --hig-space-8: 32px;
  --hig-space-10: 40px;
  --hig-space-12: 48px;
  
  /* Corner Radius */
  --hig-radius-small: 6px;
  --hig-radius-medium: 8px;
  --hig-radius-large: 12px;
  --hig-radius-pill: 999px;
}

/* Dark mode overrides */
@media (prefers-color-scheme: dark) {
  :root {
    --hig-label-primary: #f5f5f7;
    --hig-label-secondary: #a1a1a6;
    --hig-label-tertiary: #6e6e73;
    --hig-label-quaternary: #424245;
    
    --hig-fill-primary: rgba(255,255,255,0.12);
    --hig-fill-secondary: rgba(255,255,255,0.08);
    --hig-fill-tertiary: rgba(255,255,255,0.04);
    
    --hig-separator: rgba(255,255,255,0.15);
    
    --hig-glass-regular-bg: rgba(28,28,30,0.72);
    --hig-glass-regular-border: rgba(255,255,255,0.1);
    
    --hig-glass-clear-bg: rgba(28,28,30,0.1);
    --hig-glass-clear-dim: rgba(0,0,0,0.5);
  }
}
```

---

---

## Part 2: Components — Toolbars, Navigation, Controls

### 2.1 Toolbars (Header Bars)

| HIG Toolbar Element | GTK 4.24.x Equivalent | Implementation |
|---------------------|----------------------|----------------|
| **Toolbar / Navigation Bar** | `GtkHeaderBar` (unified titlebar+toolbar) | `gtk_window_set_titlebar(window, header_bar)` |
| **Leading: Back Button** | `GtkButton` with `GtkImage` (arrow-left) in `GtkHeaderBar::pack_start()` | Use `GtkIconTheme` "go-previous" or Material Symbols `&#xe5c4;` |
| **Leading: Sidebar Toggle** | `GtkToggleButton` with "sidebar-show/hide" icon | Connect to `GtkSidebar::set_collapsed()` or `GtkPaned` position |
| **Center: Window Title** | `GtkHeaderBar::set_title_widget()` — `GtkLabel` or `GtkCenterBox` | `gtk_header_bar_set_title_widget(header, gtk_label_new("Title"))` |
| **Center: Document Proxy Menu** | Custom `GtkMenuButton` with document icon + popover | `GtkMenuButton` → `GtkPopoverMenu` from `GMenuModel` |
| **Trailing: Primary Action (Done/Submit)** | `GtkButton` with `.suggested-action` style class | `gtk_widget_add_css_class(button, "suggested-action")` |
| **Trailing: Search Field** | `GtkSearchEntry` in `GtkHeaderBar::pack_end()` | `gtk_header_bar_pack_end(header, search_entry)` |
| **Trailing: More Menu (overflow)** | `GtkMenuButton` with `GtkPopoverMenu` + `GMenuModel` | Auto-populated from `GtkActionMuxer` or manual `GMenu` |
| **Item Grouping (leading/center/trailing)** | `GtkHeaderBar::pack_start/center/end()` | Direct mapping |
| **Customization (user reorder)** | **Not built-in.** Implement via `GtkDragSource`/`GtkDropTarget` on toolbar items | Save/restore order in `GSettings` |
| **Overflow → More Menu** | **Not automatic.** Monitor `GtkWidget::size-allocate` → move items to popover when width < threshold | Custom `GtkWidget` subclass managing child visibility |
| **Translucent Background + Blur** | CSS on `headerbar` + `backdrop-filter` (see Materials) | `.headerbar { backdrop-filter: blur(20px); background: rgba(255,255,255,0.72); }` |

**Complete Header Bar Setup:**
```c
GtkWidget *header = gtk_header_bar_new();
gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);

// Leading
GtkWidget *back_btn = gtk_button_new_from_icon_name("go-previous");
gtk_header_bar_pack_start(GTK_HEADER_BAR(header), back_btn);

GtkWidget *sidebar_toggle = gtk_toggle_button_new();
gtk_button_set_child(GTK_BUTTON(sidebar_toggle), gtk_image_new_from_icon_name("sidebar-show"));
gtk_header_bar_pack_start(GTK_HEADER_BAR(header), sidebar_toggle);

// Center - Title
GtkWidget *title = gtk_label_new("Document Title");
gtk_widget_add_css_class(title, "text-headline");
gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);

// Center - Document Menu (proxy icon)
GtkWidget *doc_menu = gtk_menu_button_new();
gtk_button_set_child(GTK_BUTTON(doc_menu), gtk_image_new_from_icon_name("text-x-generic"));
GMenu *doc_menu_model = g_menu_new();
g_menu_append(doc_menu_model, "Duplicate", "app.duplicate");
g_menu_append(doc_menu_model, "Rename", "app.rename");
g_menu_append(doc_menu_model, "Move To…", "app.move");
g_menu_append(doc_menu_model, "Export…", "app.export");
gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(doc_menu), G_MENU_MODEL(doc_menu_model));
gtk_header_bar_pack_start(GTK_HEADER_BAR(header), doc_menu); // or center via custom widget

// Trailing - Actions
GtkWidget *share_btn = gtk_button_new_from_icon_name("share");
gtk_header_bar_pack_end(GTK_HEADER_BAR(header), share_btn);

GtkWidget *search = gtk_search_entry_new();
gtk_header_bar_pack_end(GTK_HEADER_BAR(header), search);

GtkWidget *done_btn = gtk_button_new_with_label("Done");
gtk_widget_add_css_class(done_btn, "suggested-action");
gtk_header_bar_pack_end(GTK_HEADER_BAR(header), done_btn);

// More Menu (overflow)
GtkWidget *more_btn = gtk_menu_button_new();
gtk_button_set_child(GTK_BUTTON(more_btn), gtk_image_new_from_icon_name("open-menu"));
GMenu *more_menu = g_menu_new();
g_menu_append(more_menu, "Print…", "app.print");
g_menu_append(more_menu, "Find…", "app.find");
gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(more_btn), G_MENU_MODEL(more_menu));
gtk_header_bar_pack_end(GTK_HEADER_BAR(header), more_btn);

gtk_window_set_titlebar(GTK_WINDOW(window), header);
```

---

### 2.2 Navigation Patterns

| HIG Pattern | GTK 4.24.x Equivalent |
|-------------|----------------------|
| **Tab Bar (iOS bottom)** | `GtkStack` + `GtkStackSwitcher` (top) OR `AdwTabView` + `AdwTabBar` (libadwaita, bottom support) |
| **Sidebar (macOS/iPadOS)** | `GtkSidebar` (GTK 4.10+) or `AdwNavigationView` + `AdwNavigationPage` |
| **Split View (Primary/Detail)** | `GtkPaned` (horizontal) or `AdwNavigationView` (adaptive) |
| **Back Navigation** | `GtkStack` page history + `GtkHeaderBar` back button → `gtk_stack_set_visible_child_name()` |
| **Modal Sheets** | `GtkWindow` + `gtk_window_set_transient_for()` + `gtk_window_set_modal()` + `gtk_window_present()` |
| **Popovers (anchored)** | `GtkPopover` / `GtkPopoverMenu` |
| **Drawers (visionOS)** | Custom `GtkWidget` with `GtkRevealer` sliding from edge |

**Adaptive Sidebar → Tab Bar (Size Class Pattern):**
```c
// Monitor window width → switch between Sidebar and Tab Bar
static void on_window_size_changed(GtkWindow *window, GParamSpec *pspec, gpointer data) {
  int width, height;
  gtk_window_get_default_size(window, &width, &height);
  gboolean is_compact = width < 720; // HIG compact threshold
  
  if (is_compact != GTK_IS_STACK_SWITCHER(gtk_widget_get_parent(sidebar))) {
    if (is_compact) {
      // Compact: Show Tab Bar, hide Sidebar
      gtk_widget_hide(sidebar);
      gtk_widget_show(tab_bar);
      // Move navigation to stack switcher
    } else {
      // Regular: Show Sidebar, hide Tab Bar
      gtk_widget_show(sidebar);
      gtk_widget_hide(tab_bar);
    }
  }
}
g_signal_connect(window, "notify::default-width", G_CALLBACK(on_window_size_changed), NULL);
```

---

### 2.3 Core Controls Mapping

| HIG Control | GTK 4.24.x Widget | Style Classes / Notes |
|-------------|-------------------|----------------------|
| **Button (Standard)** | `GtkButton` | `.flat`, `.pill`, `.circular` |
| **Button (Prominent/Done)** | `GtkButton` + `.suggested-action` | Blue accent, bold text |
| **Button (Destructive)** | `GtkButton` + `.destructive-action` | Red accent |
| **Button (Icon Only)** | `GtkButton` + `GtkImage` child | `.flat`, `.circular` for toolbar |
| **Toggle / Switch** | `GtkSwitch` | CSS for custom thumb/track |
| **Checkbox** | `GtkCheckButton` | `.inline` for label-right |
| **Radio Group** | `GtkCheckButton` + same `group` | Or `GtkFlowBox` with single-selection |
| **Slider** | `GtkScale` | `.fine-tune` for precise, `.marks` for discrete |
| **Stepper** | `GtkSpinButton` | Or two `GtkButton` (+/-) + label |
| **Picker (Date/Time)** | `GtkCalendar` popover / `AdwDateTimePicker` (libadwaita) | |
| **Menu / Popup Button** | `GtkMenuButton` + `GtkPopoverMenu` | `GMenuModel` for items |
| **Segmented Control** | `GtkBox` (horizontal) + toggle buttons with `.linked` | `gtk_widget_add_css_class(box, "linked")` |
| **Search Field** | `GtkSearchEntry` | Rounded, cancel button built-in |
| **Text Field** | `GtkEntry` / `GtkTextView` | `.error`, `.warning` for validation |
| **Label** | `GtkLabel` | Markup for styled text |
| **Progress** | `GtkProgressBar` | `.pulse` for indeterminate |
| **Activity Indicator** | `GtkSpinner` | |
| **Separator** | `GtkSeparator` | `.horizontal`, `.vertical` |
| **Disclosure Triangle** | `GtkExpander` or `GtkButton` + arrow icon | |
| **Color Well** | `GtkColorDialogButton` (GTK 4.10+) | |
| **Font Picker** | `GtkFontDialogButton` (GTK 4.10+) | |
| **File Picker** | `GtkFileDialog` (GTK 4.10+) | Replaces `GtkFileChooserDialog` |

**Custom Liquid Glass Button:**
```c
// liquid-glass-button.c
typedef struct { GtkButton parent; } LiquidGlassButton;
G_DEFINE_TYPE(LiquidGlassButton, liquid_glass_button, GTK_TYPE_BUTTON)

static void liquid_glass_button_class_init(LiquidGlassButtonClass *klass) {
  GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);
  wc->snapshot = liquid_glass_button_snapshot;
  gtk_widget_class_set_css_name(wc, "liquid-glass-button");
  gtk_widget_class_set_layout_manager_type(wc, GTK_TYPE_BIN_LAYOUT);
}

static void liquid_glass_button_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  graphene_rect_t bounds;
  gtk_widget_get_allocation(widget, &bounds);
  
  // 1. Render background blur (requires offscreen or shader)
  // Simplified: draw rounded rect with semi-transparent bg + border
  GskRoundedRect rrect = GSK_ROUNDED_RECT_INIT(bounds.origin.x, bounds.origin.y,
                                                bounds.size.width, bounds.size.height,
                                                8, 8, 8, 8);
  gsk_render_node_append_color(snapshot, &rrect, &(GdkRGBA){1,1,1,0.1}, TRUE);
  gsk_render_node_append_border(snapshot, &rrect, 1, &(GdkRGBA){1,1,1,0.2});
  
  // 2. Render child (label/icon) via default
  GTK_WIDGET_CLASS(liquid_glass_button_parent_class)->snapshot(widget, snapshot);
}
```

---

### 2.4 Lists & Collections

| HIG Pattern | GTK 4.24.x Equivalent |
|-------------|----------------------|
| **Table / List (Plain)** | `GtkColumnView` + `GtkListModel` (GTK 4.12+) |
| **Table / List (Inset Grouped)** | `GtkColumnView` + custom row widget with `.card` CSS |
| **Collection / Grid** | `GtkGridView` + `GtkListModel` |
| **Outline / Hierarchical** | `GtkTreeListModel` + `GtkColumnView` with `GtkTreeExpander` |
| **Swipe Actions** | `GtkSwipeTracker` on row widget → reveal action buttons |
| **Pull to Refresh** | `GtkScrolledWindow` + `GtkGestureSwipe` on content → trigger refresh |
| **Section Headers (sticky)** | `GtkColumnView` with `GtkSectionHeader` factory (GTK 4.14+) |
| **Empty State** | `GtkStack` with "empty" page + `GtkLabel` + `GtkImage` + action button |

**ColumnView Setup (Modern List):**
```c
GtkWidget *list = gtk_column_view_new(GTK_SELECTION_MODEL(single_selection));
GtkColumnViewColumn *col = gtk_column_view_column_new("Name", 
  gtk_signal_list_item_factory_new_with_callback(
    (GCallback)setup_name_cell, NULL, NULL));
gtk_column_view_append_column(GTK_COLUMN_VIEW(list), col);

// Factory setup
static void setup_name_cell(GtkSignalListItemFactory *factory, GtkListItem *item) {
  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_list_item_set_child(item, label);
}

static void bind_name_cell(GtkSignalListItemFactory *factory, GtkListItem *item) {
  MyObject *obj = gtk_list_item_get_item(item);
  GtkWidget *label = gtk_list_item_get_child(item);
  gtk_label_set_label(GTK_LABEL(label), my_object_get_name(obj));
}
```

---

### 2.5 Dialogs & Alerts

| HIG Type | GTK 4.24.x Equivalent |
|----------|----------------------|
| **Alert (Modal)** | `GtkAlertDialog` (GTK 4.10+) or `AdwAlertDialog` (libadwaita) |
| **Action Sheet (iOS)** | `GtkPopoverMenu` anchored to button, full-width on mobile |
| **Confirmation Dialog** | `GtkAlertDialog` with "confirm" + "cancel" buttons |
| **File Save/Open** | `GtkFileDialog` (GTK 4.10+) — async, no dialog widget |
| **Color/Font Picker** | `GtkColorDialog` / `GtkFontDialog` (GTK 4.10+) |
| **Custom Sheet** | `GtkWindow` + `gtk_window_set_transient_for()` + `gtk_window_set_modal()` + custom content |

**AlertDialog Example:**
```c
GtkAlertDialog *dialog = gtk_alert_dialog_new("Delete Document?");
gtk_alert_dialog_set_detail(dialog, "This action cannot be undone.");
gtk_alert_dialog_set_buttons(dialog, (const char*[]){"Cancel", "Delete", NULL});
gtk_alert_dialog_set_default_button(dialog, 0); // Cancel
gtk_alert_dialog_set_cancel_button(dialog, 0);
gtk_alert_dialog_set_destructive_button(dialog, 1); // Delete

gtk_alert_dialog_show(dialog, GTK_WINDOW(window), NULL, (GAsyncReadyCallback)on_dialog_response, NULL);

static void on_dialog_response(GObject *source, GAsyncResult *result, gpointer data) {
  int response = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);
  if (response == 1) { /* Delete confirmed */ }
}
```

---

## Part 3: Advanced — Custom Shaders, Animations, Input

### 3.1 True Vibrancy via GskGLShader

```glsl
/* vibrant.frag - Fragment shader for vibrancy */
uniform sampler2D u_background;
uniform vec2 u_resolution;
in vec2 v_tex_coord;
out vec4 fragColor;

void main() {
  vec3 bg = texture(u_background, v_tex_coord).rgb;
  float luminance = dot(bg, vec3(0.2126, 0.7152, 0.0722));
  // Vibrant light: invert + overlay on light backgrounds
  // Vibrant dark: invert + multiply on dark backgrounds
  vec3 vibrant = mix(vec3(1.0) - bg, bg, step(0.5, luminance));
  fragColor = vec4(vibrant, 1.0);
}
```

```c
// Apply shader to label widget
GskGLShader *shader = gsk_gl_shader_new_for_resource("vibrant.frag", "vibrant");
GskRenderNode *node = gsk_gl_shader_render_node_new(shader, &bounds, 
  (GskUniform[]){
    {"u_background", GSK_UNIFORM_TEXTURE, background_texture},
    {"u_resolution", GSK_UNIFORM_FLOAT2, {width, height}},
  }, 2);
gtk_snapshot_append_render_node(snapshot, node);
```

### 3.2 SF Symbols-Like Animations (Draw/Bounce)

**Option A: Lottie (Recommended for complex)**
```c
// rlottie-c API
rlottie_animation *anim = lottie_animation_from_file("bounce.json");
while (frame < total_frames) {
  unsigned char *buf = malloc(width * height * 4);
  lottie_animation_render(anim, frame, buf, width, height, width * 4);
  GdkTexture *tex = gdk_memory_texture_new(width, height, GDK_MEMORY_R8G8B8A8, buf, width*4, g_free, NULL);
  gtk_image_set_from_paintable(GTK_IMAGE(image), GDK_PAINTABLE(tex));
  g_object_unref(tex);
  frame++;
}
```

**Option B: Custom Path Animation (Simple Draw)**
```c
// Animate stroke-dashoffset on SVG path
typedef struct { GtkWidget parent; double progress; } DrawIcon;
G_DEFINE_TYPE(DrawIcon, draw_icon, GTK_TYPE_WIDGET)

static void draw_icon_snapshot(GtkWidget *w, GtkSnapshot *snap) {
  DrawIcon *self = DRAW_ICON(w);
  graphene_rect_t bounds;
  gtk_widget_get_allocation(w, &bounds);
  
  // Render SVG path with stroke-dashoffset = total_length * (1 - progress)
  GskPath *path = load_icon_path("icon.svg");
  double len = gsk_path_get_length(path);
  GskPath *trimmed = gsk_path_trim(path, 0, len * self->progress);
  
  gsk_render_node_append_stroke(snap, trimmed, &(GskStrokeParams){
    .width = 2, .join = GSK_ROUND_JOIN, .cap = GSK_ROUND_CAP
  }, &(GdkRGBA){0,0,0,1});
}
```

### 3.3 Touch & Gesture Handling (macOS 2026+ Touch Support)

```c
// Touch-friendly targets: min 44x44pt (HIG) → 44*scale px
gtk_widget_set_size_request(button, 44 * scale, 44 * scale);

// Gesture controllers
GtkGestureClick *click = gtk_gesture_click_new();
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0); // All buttons
g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), widget);
gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

GtkGestureSwipe *swipe = gtk_gesture_swipe_new();
g_signal_connect(swipe, "swipe", G_CALLBACK(on_swipe), widget);
gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(swipe));

GtkGestureZoom *zoom = gtk_gesture_zoom_new();
g_signal_connect(zoom, "scale-changed", G_CALLBACK(on_zoom), widget);
gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(zoom));

// Hover for pointer (reveal hidden controls)
GtkEventControllerMotion *motion = gtk_event_controller_motion_new();
g_signal_connect(motion, "enter", G_CALLBACK(on_enter), widget);
g_signal_connect(motion, "leave", G_CALLBACK(on_leave), widget);
gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(motion));
```

### 3.4 Keyboard Shortcuts (Accelerators)

```c
GtkShortcutController *shortcuts = gtk_shortcut_controller_new();

// App-wide: Cmd/Ctrl+N = New Window
GtkShortcut *new_win = gtk_shortcut_new(
  gtk_keyval_trigger_new(GDK_KEY_n, GDK_CONTROL_MASK | GDK_SHIFT_MASK),
  gtk_callback_action_new("app.new-window", NULL));
gtk_shortcut_controller_add_shortcut(shortcuts, new_win);

// Widget-scoped: Escape = Close Popover
GtkShortcut *esc = gtk_shortcut_new(
  gtk_keyval_trigger_new(GDK_KEY_Escape, 0),
  gtk_callback_action_new("popover.close", popover));
gtk_widget_add_controller(popover, GTK_EVENT_CONTROLLER(shortcuts));

// Register action
GActionEntry entries[] = {
  {"new-window", on_new_window, NULL, NULL, NULL},
  {"popover.close", on_popover_close, NULL, NULL, NULL},
};
g_action_map_add_action_entries(G_ACTION_MAP(app), entries, 2, NULL);
```

---

## Part 4: Platform Integration Layer

### 4.1 Abstract Platform Interface

```c
// platform.h
typedef struct {
  void (*show_menu_bar)(App *app, GMenuModel *menu);
  void (*set_dock_badge)(App *app, const char *text);
  void (*set_window_proxy_icon)(GtkWindow *win, GdkTexture *icon, GMenuModel *menu);
  void (*enable_fullscreen_spaces)(GtkWindow *win, gboolean enable);
  void (*register_services)(App *app); // Handoff, Continuity, etc.
} PlatformImpl;

PlatformImpl *platform_get_impl(void); // Returns macOS/Linux/Windows impl
```

### 4.2 macOS Implementation (Objective-C++)
```objc
// platform_macos.mm
#import <AppKit/AppKit.h>

void macos_show_menu_bar(App *app, GMenuModel *menu) {
  NSMenu *nsmenu = gmenu_to_nsmenu(menu);
  [NSApp setMainMenu:nsmenu];
}

void macos_set_window_proxy_icon(GtkWindow *win, GdkTexture *icon, GMenuModel *menu) {
  NSWindow *nswin = gtk_native_get_surface(GTK_NATIVE(gtk_widget_get_native(GTK_WIDGET(win))));
  NSImage *nsimg = gdktexture_to_nsimage(icon);
  nswin.representedURL = [NSURL fileURLWithPath:document_path];
  nswin.title = @"Document Title";
  // Proxy icon menu handled by NSWindowDelegate
}
```

---

## Part 5: Migration Checklist (HIG App → GTK 4.24)

| Phase | Tasks | GTK APIs |
|-------|-------|----------|
| **1. Foundation** | CSS theme (colors, spacing, typography), Font loading (variable), Icon font setup | `GtkCssProvider`, `PangoFontDescription`, `GtkIconTheme` |
| **2. Chrome** | HeaderBar, Sidebar/Navigation, Window management | `GtkHeaderBar`, `GtkSidebar`/`AdwNavigationView`, `GtkWindow` |
| **3. Core Controls** | Buttons, inputs, lists, dialogs | `GtkButton`, `GtkEntry`, `GtkColumnView`, `GtkAlertDialog` |
| **4. Materials** | Blur backgrounds, vibrancy approximations | CSS `backdrop-filter`, custom shaders |
| **5. Animation** | Transitions, draw/bounce, state changes | `GtkRevealer`, `AdwAnimation`, Lottie/rlottie |
| **6. Input** | Keyboard shortcuts, touch gestures, hover | `GtkShortcutController`, `GtkGesture*`, `GtkEventControllerMotion` |
| **7. Platform** | Menu bar, dock, proxy icon, fullscreen | Platform abstraction layer |
| **8. Accessibility** | Labels, roles, keyboard nav, screen reader | `GtkAccessible`, `atk` interfaces |
| **9. Testing** | Dynamic Type, RTL, compact/regular, dark mode | `GtkSettings`, CSS media queries, `GtkWidget::direction` |

---

## Appendix: Useful Resources

| Resource | Link |
|----------|------|
| GTK 4 API Reference | https://docs.gtk.org/gtk4/ |
| libadwaita (GNOME HIG widgets) | https://gnome.pages.gitlab.gnome.org/libadwaita/ |
| GTK 4 Migration Guide | https://docs.gtk.org/gtk4/migrating-3-to-4.html |
| CSS Properties (GTK) | https://docs.gtk.org/gtk4/css-properties.html |
| GskRenderNode / Shaders | https://docs.gtk.org/graphene/ |
| Material Symbols Variable Font | https://github.com/material-design/material-symbols |
| rlottie (Lottie C renderer) | https://github.com/Samsung/rlottie |
| Adwaita Icon Theme | https://gitlab.gnome.org/GNOME/adwaita-icon-theme |

---

*End of mapping guide. This covers Foundations, Components, Advanced rendering, Input, and Platform integration. For production use, start with libadwaita for GNOME-targeted apps, or plain GTK4 + custom CSS for cross-platform.*