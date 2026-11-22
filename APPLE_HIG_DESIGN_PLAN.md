# Apple HIG & Liquid Glass Design Plan for Lollypop-cpp

> **Goal**: Transform Lollypop-cpp into a native-feeling, Apple-quality music application following Human Interface Guidelines, Liquid Glass design system, and Apple Music design patterns.

---

## 📚 Reference Analysis

### Core HIG Principles (References 1, 2)
| Principle | Application to Lollypop |
|-----------|------------------------|
| **Deference** | Content > chrome; artwork, waveform, lyrics take center stage |
| **Clarity** | Sharp typography, generous whitespace, clear visual hierarchy |
| **Depth** | Layered materials (sidebar, middle, right, bottom) with proper elevation |
| **Consistency** | System patterns: sidebar navigation, toolbar, sheet presentations |

### macOS Specifics (Reference 2)
| Pattern | Lollypop Implementation |
|---------|------------------------|
| **Source list** | Left sidebar (compact 42px / expanded 220px) ✅ |
| **Split view** | Three-pane: sidebar \| library \| now-playing ✅ |
| **Toolbar** | Bottom playback bar as persistent toolbar ✅ |
| **Sheets/popovers** | Volume dial popover, PiP window, lyrics |
| **Window behavior** | Single window, resizable, state persistence |

### Icons & Symbols (References 3, 5, 12)
| Requirement | Current | Target |
|-------------|---------|--------|
| **SF Symbols** | GTK icon theme (Adwaita) | Map to SF Symbol equivalents |
| **Variable weight** | Fixed weight | Use `symbolic` + weight variants |
| **Semantic meaning** | Generic icons | Precise semantic mapping |
| **Animation** | Static | SF Symbol spring/bounce animations |

### Materials & Liquid Glass (References 7, 8, 11)
| Material | Usage in Lollypop |
|----------|-------------------|
| **Ultra-thin** | Sidebar background, toolbar |
| **Thin** | Popovers, sheets |
| **Regular** | Cards, album artwork backgrounds |
| **Thick** | Modal sheets, PiP window |
| **Liquid Glass** | Volume dial, waveform, artwork reflections |

---

## 🔍 Current State Audit

### ✅ Already Aligned
- Source list sidebar with compact/expanded states
- Three-pane split view layout
- Persistent bottom toolbar (playback bar)
- Artwork-first visual hierarchy
- Accent color from artwork (ThemeManager)
- MPRIS integration (native media key support)

### ❌ Critical Gaps

| Area | Issue | HIG Violation |
|------|-------|---------------|
| **Album artwork** | Fixed rounded-square cards | Variable aspect ratio, Liquid Glass reflection |
| **Card transitions** | Instant, no animation | Spring/bounce, Liquid Glass morphing |
| **Typography** | Basic CSS, no dynamic type | SF Pro, dynamic type scaling |
| **Materials** | Flat CSS colors | Liquid Glass, vibrancy, blur |
| **Volume dial** | GTK scale in popover | Liquid Glass MaterialDial with haptics |
| **Waveform** | Static bars | Liquid Glass animated path |
| **Transitions** | None | Spring animations, Liquid Glass morph |
| **Toolbar** | Custom CSS | Native toolbar with Liquid Glass |
| **Segues** | Instant stack switch | Zoom/spring sheet presentations |
| **Focus ring** | Basic CSS | Liquid Glass focus halo |

### 🐛 Segmentation Fault Sources
| Location | Likely Cause | Fix Priority |
|----------|--------------|--------------|
| Album/Artist grid | Double-free in ArtworkView | 🔴 Critical |
| Section navigation | Stack widget double-parenting | 🔴 Critical |
| Artwork loading | Async callback after widget destroy | 🔴 Critical |
| Section switch | Stack child not unparented | 🟠 High |

---

## 🎯 Phased Implementation Plan

### Phase 1: Stability & Foundation (Week 1-2)
**Goal**: Eliminate segfaults, establish safe patterns

| Task | Details | Files |
|------|---------|-------|
| 1.1 Fix double-parenting | Audit all `gtk_box_append`/`gtk_stack_add`; use `safe_gtk.hpp` everywhere | `middle_library.cpp`, `albums_page.cpp`, `artists_page.cpp` |
| 1.2 ArtworkView lifecycle | Weak references, cancel async on destroy, `GObject` finalize | `ArtworkView.hpp/cpp` |
| 1.3 Safe navigation | `StackNavigator` class managing stack transitions | New: `StackNavigator.hpp/cpp` |
| 1.4 Async cancellation | `GTask` + `GCancellable` for all artwork/lyrics loads | `ArtworkManager`, `LyricsManager` |
| 1.5 AddressSanitizer build | Add `-fsanitize=address` to CMake, run tests | `CMakeLists.txt` |

**Deliverable**: Zero segfaults under ASan + stress testing (rapid nav, scroll, resize)

---

### Phase 2: Liquid Glass Materials (Week 2-3)
**Goal**: Implement Apple's material system

| Task | Details | Files |
|------|---------|-------|
| 2.1 Material CSS variables | `--material-ultra-thin`, `--material-thin`, `--material-regular`, `--material-thick`, `--liquid-glass` | `style.css`, `ThemeManager` |
| 2.2 Backdrop blur | `gtk_widget_set_backdrop_blur()` (GTK 4.14+) or CSS `backdrop-filter` | `style.css` |
| 2.3 Liquid Glass shader | Custom GSK shader for volume dial, waveform, artwork reflection | New: `liquid_glass_shader.cpp` |
| 2.4 Material widgets | `MaterialCard`, `MaterialToolbar`, `MaterialPopover` base classes | New: `MaterialWidget.hpp/cpp` |
| 2.5 Vibrancy labels | `GtkLabel` with `opacity` + backdrop blur for vibrancy effect | `style.css` |

**Deliverable**: All surfaces use Liquid Glass materials; volume dial has glass shader

---

### Phase 3: Album Artwork System (Week 3-4)
**Goal**: Variable aspect ratio cards with Liquid Glass reflections

| Task | Details | Files |
|------|---------|-------|
| 3.1 Variable aspect cards | `ArtworkCard` widget: preserves original aspect ratio (1:1, 4:3, 16:9) | New: `ArtworkCard.hpp/cpp` |
| 3.2 Liquid Glass reflection | Bottom fade + glass reflection using shader | `ArtworkCard`, shader |
| 3.3 Spring animations | `gtk_widget_animate()` with spring physics (damping=0.8, stiffness=180) | `ArtworkCard`, `AlbumsPage` |
| 3.4 Grid layout | `GtkFlowBox` with variable row heights, homogeneous=false | `AlbumsPage`, `ArtistsPage` |
| 3.5 Hover/press states | Scale 1.02 + shadow elevation + glass highlight | `ArtworkCard`, CSS |

**Deliverable**: Album grid matches Apple Music Tahoe — variable rectangles, glass reflections, spring transitions

---

### Phase 4: Typography & Dynamic Type (Week 4)
**Goal**: SF Pro / system font with dynamic scaling

| Task | Details | Files |
|------|---------|-------|
| 4.1 Font stack | `--font-family: -apple-system, BlinkMacSystemFont, "SF Pro", system-ui` | `style.css` |
| 4.2 Type scale | Display (34), Title 1 (28), Title 2 (22), Title 3 (20), Headline (17), Body (17), Callout (16), Subhead (15), Footnote (13), Caption 1 (12), Caption 2 (11) | `style.css` |
| 4.3 Dynamic type | `gtk_widget_set_font_scale()` + CSS `font-size: clamp()` | `ThemeManager`, `style.css` |
| 4.4 Font weights | Light (300), Regular (400), Medium (500), Semibold (600), Bold (700) | `style.css` |
| 4.5 SF Symbols mapping | Map GTK icons → SF Symbol names + weight variants | New: `SymbolMapper.hpp` |

**Deliverable**: Full Apple type scale, respects system text size

---

### Phase 5: Transitions & Motion (Week 5)
**Goal**: Spring animations, Liquid Glass morphing

| Task | Details | Files |
|------|---------|-------|
| 5.1 Spring physics | Damping=0.82, Stiffness=180, Mass=1 (Apple standard) | `AnimationUtils.hpp/cpp` |
| 5.2 Navigation transitions | Zoom/spring for stack push/pop; cross-fade for same-level | `StackNavigator` |
| 5.3 Card morph | Artwork card → Now Playing hero (shared element transition) | `ArtworkCard`, `RightNowPlaying` |
| 5.4 Volume dial spring | Spring on show/hide, rotation with haptic feedback | `VolumeDial` |
| 5.5 Waveform spring | Liquid Glass wave amplitude spring | `WaveformView` |
| 5.5 Respect reduced motion | `gtk_enable_animations` setting | `AnimationUtils` |

**Deliverable**: All transitions feel like Apple Music — springy, responsive, fluid

---

### Phase 6: Toolbar & Volume Dial (Week 5-6)
**Goal**: Native toolbar + Liquid Glass volume dial

| Task | Details | Files |
|------|---------|-------|
| 6.1 Native toolbar | `AdwToolbarView` + `AdwHeaderBar` with Liquid Glass material | `MainWindow` |
| 6.2 Volume dial | `LiquidGlassDial` widget: circular, haptic, glass shader | New: `LiquidGlassDial.hpp/cpp` |
| 6.3 Playback controls | Centered in toolbar: prev / waveform / next | `BottomPlayback` → toolbar |
| 6.4 Haptic feedback | `gdk_display_notify_startup_complete()` + `gdk_seat_get_tools()` | `LiquidGlassDial` |
| 6.5 Artwork in toolbar | Mini artwork left of waveform | Toolbar layout |

**Deliverable**: Native macOS toolbar with Liquid Glass volume dial

---

### Phase 7: Accessibility & Polish (Week 6)
**Goal**: Full HIG compliance

| Task | Details | Files |
|------|---------|-------|
| 7.1 VoiceOver/ATK | `gtk_accessible_*` roles, labels, values | All widgets |
| 7.2 Keyboard nav | Tab order, arrow keys, Escape to dismiss | All dialogs |
| 7.3 High contrast | `prefers-contrast` media query | `style.css` |
| 7.4 Reduced motion | `prefers-reduced-motion` | `AnimationUtils` |
| 7.5 Localization | `gettext` for all strings | `CMakeLists.txt`, `.po` files |

---

## 📐 Technical Architecture

### New Core Classes
```
src/
├── ui/
│   ├── components/
│   │   ├── MaterialWidget.hpp/cpp        # Base Liquid Glass widget
│   │   ├── MaterialCard.hpp/cpp          # Album/artist cards
│   │   ├── MaterialToolbar.hpp/cpp       # Native toolbar
│   │   ├── MaterialPopover.hpp/cpp       # Glass popovers
│   │   ├── ArtworkCard.hpp/cpp           # Variable aspect artwork
│   │   ├── LiquidGlassDial.hpp/cpp       # Volume dial with shader
│   │   ├── LiquidGlassWaveform.hpp/cpp   # Animated waveform
│   │   └── StackNavigator.hpp/cpp        # Safe navigation
│   ├── shaders/
│   │   ├── liquid_glass.frag/vert        # Liquid Glass fragment/vertex
│   │   └── glass_reflection.frag         # Artwork reflection
│   └── animation/
│       ├── SpringAnimation.hpp/cpp       # Spring physics
│       └── TransitionCoordinator.hpp/cpp # Navigation transitions
```

### Shader Pipeline (Liquid Glass)
```glsl
// liquid_glass.frag
uniform float u_time;
uniform vec3 u_accent;
uniform float u_blur_radius;
uniform sampler2D u_backdrop;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    
    // Frosted glass base
    vec3 backdrop = texture(u_backdrop, uv).rgb;
    float blur = smoothstep(0.0, 1.0, length(uv - 0.5) * 2.0);
    vec3 glass = mix(backdrop, vec3(1.0), 0.15) * 0.9 + vec3(0.05);
    
    // Specular highlight
    float highlight = pow(max(0.0, dot(normalize(vec3(uv-0.5, 0.5)), vec3(0.0, 0.0, 1.0))), 32.0);
    
    // Accent tint
    vec3 color = mix(glass, u_accent * 0.3, 0.2);
    
    // Subtle animation
    float shimmer = sin(uv.x * 20.0 + u_time * 2.0) * 0.02;
    
    gl_FragColor = vec4(color + shimmer + highlight * 0.3, 0.85);
}
```

---

## 📦 Dependencies & Build

### New Dependencies
| Library | Version | Purpose |
|---------|---------|---------|
| **GSK 4.14+** | Runtime | `gsk_shader`, `gsk_gl_shader` |
| **graphene** | 1.10+ | Matrix math for shaders |
| **libadwaita** | 1.5+ | `AdwToolbarView`, `AdwClamp` |
| **harfbuzz** | 8.0+ | Text shaping for dynamic type |

### CMake Updates
```cmake
find_package(GTK 4.14 REQUIRED)
find_package(Adwaita 1.5 REQUIRED)
find_package(Graphene 1.10 REQUIRED)

# Shader compilation
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/liquid_glass.gskshader
    COMMAND gsk-glsl-compiler
        --vertex ${CMAKE_SOURCE_DIR}/src/ui/shaders/liquid_glass.vert
        --fragment ${CMAKE_SOURCE_DIR}/src/ui/shaders/liquid_glass.frag
        --output ${CMAKE_CURRENT_BINARY_DIR}/shaders/liquid_glass.gskshader
    DEPENDS ${CMAKE_SOURCE_DIR}/src/ui/shaders/liquid_glass.vert
            ${CMAKE_SOURCE_DIR}/src/ui/shaders/liquid_glass.frag
)
```

---

## ✅ Acceptance Criteria per Phase

| Phase | Criteria | Verification |
|-------|----------|--------------|
| 1 | Zero segfaults under ASan + 10k nav events | `./test_stress` |
| 2 | All surfaces show Liquid Glass materials | Visual diff vs Apple Music |
| 3 | Album grid: variable aspect, glass reflection, spring | Visual + `gtk-inspector` |
| 4 | Type scale matches Apple spec; respects system size | `gtk-inspector` + Accessibility Inspector |
| 5 | All transitions 60fps, spring physics | `perf record` + visual |
| 6 | Native toolbar; volume dial glass shader + haptics | `gtk-inspector` + haptic test |
| 7 | Passes Accessibility Inspector; respects reduce motion/contrast | `Accerciser` |

---

## 📅 Timeline Summary

| Week | Phase | Focus |
|------|-------|-------|
| 1-2 | **1** | Stability & Foundation |
| 2-3 | **2** | Liquid Glass Materials |
| 3-4 | **3** | Album Artwork System |
| 4 | **4** | Typography & Dynamic Type |
| 5 | **5, 6** | Transitions, Toolbar, Volume Dial |
| 6 | **7** | Accessibility & Polish |

---

## 🎯 Definition of Done

> **"Lollypop-cpp feels like a native Apple Music app on macOS"**

- ✅ Zero crashes under stress
- ✅ Liquid Glass materials throughout
- ✅ Variable aspect artwork cards with glass reflections
- ✅ Spring animations everywhere (damping 0.82, stiffness 180)
- ✅ Native toolbar + Liquid Glass volume dial with haptics
- ✅ SF Pro typography, dynamic type, SF Symbols
- ✅ Full accessibility (VoiceOver, keyboard, high contrast, reduce motion)
- ✅ MPRIS + native media keys work flawlessly
- ✅ Feels indistinguishable from Apple Music in Tahoe

---

## 📝 Notes

- **GTK 4.14+ required** for `GskGLShader`, `GskShader`, backdrop blur
- **Liquid Glass shaders** compile to `.gskshader` binary format
- **Haptic feedback** via `GdkSeat` + `GdkDeviceTool` on supported hardware
- **SF Symbols** — map to Adwaita icon names + weight; fallback to custom SVG
- **Performance budget**: 60fps UI, <16ms frame, <100ms cold start

---

*This plan synthesizes Apple HIG, Liquid Glass, WWDC25 sessions, and Apple Music Tahoe design patterns into an actionable roadmap for Lollypop-cpp.*