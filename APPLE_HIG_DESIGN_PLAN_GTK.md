# Apple-Inspired GTK Design Plan for Lollypop-cpp

> **Goal**: Build a GTK 4 + libadwaita music application that *feels* like Apple Music (Tahoe/Liquid Glass era) using **only GTK 4, libadwaita, and custom GLSL shaders**.
>
> **Constraint**: Zero Apple proprietary assets. No SF Pro, SF Symbols, or Apple frameworks. All code is standard GTK 4 + libadwaita + custom GLSL.

---

## 🎯 Design North Star (Apple References as Inspiration)

| Apple Reference | What We Borrow | GTK Implementation |
|-----------------|----------------|-------------------|
| [HIG - Materials](https://developer.apple.com/design/human-interface-guidelines/materials) | Material hierarchy, vibrancy | CSS `backdrop-filter` + semi-transparent layers |
| [Liquid Glass](https://developer.apple.com/documentation/TechnologyOverviews/adopting-liquid-glass) | Glass morphing, refraction | Custom `GskGLShader` + `backdrop-filter` |
| [Apple Music Tahoe](https://hicks.design/journal/apple-music-in-tahoe) | Variable aspect artwork, glass reflections | `ArtworkCard` + `GskGLShader` reflection |
| [SF Symbols](https://developer.apple.com/sf-symbols/) | Semantic icons, variable weight | Adwaita symbolic + custom SVG |
| [Spring animations](https://developer.apple.com/videos/play/wwdc2025/219/) | Damping 0.82, stiffness 180 | `GtkSpringAnimation` (GTK 4.14+) |

---

## 🚫 Hard Constraints (What We CANNOT Use)

| Apple Asset | Legal Status | Our Alternative |
|-------------|--------------|-----------------|
| **SF Pro / SF Pro Rounded** | Proprietary EULA | **Inter** (open source, near-identical) |
| **SF Symbols** | Encrypted `.sfsymbol`, Apple-only | **Adwaita symbolic** + custom SVG |
| **Liquid Glass framework** | Private Apple UIKit | Custom `GskGLShader` + CSS `backdrop-filter` |
| **SF Symbol animations** | Private `SymbolAnimation` | `GskGLShader` morphing SVG paths |
| **System haptics** | Apple hardware only | Not available on Linux |

---

## 📦 Tech Stack (Pure GTK Ecosystem)

| Layer | Technology | Version |
|-------|------------|---------|
| UI Toolkit | **GTK 4** | 4.14+ (required for `GskGLShader`, `backdrop-filter`) |
| Widget Library | **libadwaita** | 1.5+ (`AdwToolbarView`, `AdwClamp`) |
| Shader Pipeline | **GSK / GLSL** | `gsk-glsl-compiler` → `.gskshader` |
| Math | **graphene** | 1.10+ (matrix math for shaders) |
| Font | **Inter** | Shipped with app (open source) |
| Icons | **Adwaita** + custom SVG | Symbolic weight + custom fill |

---

## 🏗 Architecture Overview

```
src/
├── ui/
│   ├── components/
│   │   ├── GlassPanel.hpp/cpp          # Base glass widget (backdrop-filter + shader)
│   │   ├── GlassCard.hpp/cpp           # Album/artist cards (variable aspect)
│   │   ├── GlassToolbar.hpp/cpp        # Native AdwToolbarView + glass
│   │   ├── GlassPopover.hpp/cpp        # Glass popovers (volume, menus)
│   │   ├── ArtworkCard.hpp/cpp         # Variable aspect + glass reflection
│   │   ├── GlassDial.hpp/cpp           # Volume dial (GskGLShader)
│   │   ├── GlassWaveform.hpp/cpp       # Animated waveform (GskGLShader)
│   │   └── StackNavigator.hpp/cpp      # Safe stack transitions
│   ├── shaders/
│   │   ├── glass.frag/vert             # Base glass material
│   │   ├── glass_reflection.frag       # Artwork bottom reflection
│   │   ├── glass_dial.frag/vert        # Volume dial glass
│   │   ├── waveform.frag/vert          # Animated waveform path
│   │   └── morph.frag/vert             # Icon morphing
│   └── animation/
│       ├── SpringAnimation.hpp/cpp     # Spring physics wrapper
│       └── TransitionCoordinator.hpp/cpp # Stack transitions
```

---

## 🎨 Shader Pipeline (GTK-Native)

### Compilation
```bash
# Build step (CMake)
gsk-glsl-compiler \
  --vertex src/ui/shaders/glass.vert \
  --fragment src/ui/shaders/glass.frag \
  --output build/shaders/glass.gskshader
```

### Base Glass Shader (`glass.frag`)
```glsl
// glass.frag - Base Liquid Glass material
uniform sampler2D u_backdrop;
uniform vec3 u_tint;
uniform float u_blur_radius;
uniform float u_time;

void main() {
  vec2 uv = gl_FragCoord.xy / u_resolution;
  
  // Sample backdrop with blur
  vec3 backdrop = texture(u_backdrop, uv).rgb;
  
  // Frosted glass base
  float vignette = smoothstep(0.0, 1.0, length(uv - 0.5) * 1.8);
  vec3 glass = mix(backdrop, vec3(1.0), 0.1) * 0.9 + vec3(0.03);
  glass = mix(glass, backdrop, 0.3) * vignette + glass * (1.0 - vignette);
  
  // Subtle specular
  vec3 view_dir = normalize(vec3(uv - 0.5, 1.0));
  float spec = pow(max(0.0, dot(view_dir, vec3(0.0, 0.0, 1.0))), 40.0);
  
  // Accent tint
  vec3 color = mix(glass, u_tint * 0.15, 0.25);
  
  // Subtle animated shimmer
  float shimmer = sin(uv.x * 15.0 + u_time * 1.5) * 0.015;
  
  gl_FragColor = vec4(color + shimmer + spec * 0.2, 0.88);
}
```

### Artwork Reflection (`glass_reflection.frag`)
```glsl
// glass_reflection.frag - Bottom glass reflection for artwork
uniform sampler2D u_artwork;
uniform sampler2D u_backdrop;
uniform float u_progress;      // Playback progress 0-1
uniform vec3 u_accent;

void main() {
  vec2 uv = gl_FragCoord.xy / u_resolution;
  
  // Artwork area (top 70%)
  if (uv.y < 0.7) {
    vec4 art = texture(u_artwork, vec2(uv.x, uv.y / 0.7));
    gl_FragColor = art;
    return;
  }
  
  // Reflection zone (bottom 30%)
  float reflect_y = (uv.y - 0.7) / 0.3;  // 0 to 1
  vec2 sample_uv = vec2(uv.x, 1.0 - reflect_y * 0.95);
  
  vec4 reflected = texture(u_artwork, sample_uv);
  float fade = 1.0 - smoothstep(0.0, 1.0, reflect_y);
  
  // Glass overlay
  vec3 glass = vec3(1.0) * 0.15 + vec3(0.05);
  
  gl_FragColor = vec4(
    mix(glass, reflected.rgb * fade, 0.6),
    fade * 0.4
  );
}
```

### Volume Dial (`glass_dial.frag`)
```glsl
// glass_dial.frag - Circular volume dial with glass material
uniform float u_value;        // 0-1
uniform vec3 u_accent;
uniform float u_time;

void main() {
  vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution) / min(u_resolution.x, u_resolution.y);
  float r = length(uv);
  float angle = atan(uv.y, uv.x);
  
  // Dial track
  float track = smoothstep(0.75, 0.82, r);
  float fill = smoothstep(0.75, 0.82, r) * step(angle, u_value * 6.283 - 1.57);
  
  // Glass track
  vec3 glass = vec3(1.0) * 0.12 + vec3(0.03);
  vec3 track_color = mix(glass, u_accent * 0.3, track);
  
  // Filled portion
  vec3 fill_color = u_accent;
  
  vec3 color = mix(track_color, fill_color, fill);
  
  // Subtle highlight at fill edge
  float highlight = exp(-abs(r - 0.785) * 200.0) * fill;
  color += u_accent * highlight * 0.5;
  
  float alpha = max(track, fill);
  gl_FragColor = vec4(color, alpha * 0.9);
}
```

---

## 📦 Build System (CMake)

```cmake
# Shader compilation
find_program(GSK_GLSL_COMPILER gsk-glsl-compiler)

function(compile_gsk_shader OUTPUT_NAME VERT FRAG)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader
    COMMAND ${GSK_GLSL_COMPILER}
      --vertex ${VERT}
      --fragment ${FRAG}
      --output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader
    DEPENDS ${VERT} ${FRAG}
    COMMENT "Compiling GSK shader: ${OUTPUT_NAME}"
  )
  set(${OUTPUT_NAME}_SHADER ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME}.gskshader PARENT_SCOPE)
endfunction()

# Compile all shaders
compile_gsk_shader(glass_shader     src/ui/shaders/glass.vert     src/ui/shaders/glass.frag)
compile_gsk_shader(glass_reflection_shader  src/ui/shaders/glass_reflection.vert  src/ui/shaders/glass_reflection.frag)
compile_gsk_shader(glass_dial_shader      src/ui/shaders/glass_dial.vert      src/ui/shaders/glass_dial.frag)
compile_gsk_shader(waveform_shader        src/ui/shaders/waveform.vert        src/ui/shaders/waveform.frag)
compile_gsk_shader(morph_shader           src/ui/shaders/morph.vert           src/ui/shaders/morph.frag)

# Copy shaders to install prefix
install(FILES 
  ${glass_shader} 
  ${glass_reflection_shader} 
  ${glass_dial_shader} 
  ${waveform_shader} 
  ${morph_shader}
  DESTINATION share/lollypop-cpp/shaders
)
```

---

## 📅 Updated 7-Phase Plan (GTK-Native)

### Phase 1: Stability & Foundation (Week 1-2)
| Task | GTK Implementation |
|------|-------------------|
| 1.1 Fix segfaults | Audit all `gtk_box_append`/`gtk_stack_add`; use `safe_gtk.hpp` everywhere |
| 1.2 ArtworkView lifecycle | `std::weak_ptr`, cancel async on destroy, `GObject` finalize |
| 1.3 Safe navigation | `StackNavigator` class managing `GtkStack` transitions |
| 1.4 Async cancellation | `GTask` + `GCancellable` for all artwork/lyrics loads |
| 1.5 AddressSanitizer | `-fsanitize=address` in CMake, stress test |

**Deliverable**: Zero segfaults under ASan + 10k rapid nav events

---

### Phase 2: GTK Glass Materials (Week 2-3)
| Task | GTK Implementation |
|------|-------------------|
| 2.1 CSS glass variables | `--glass-bg`, `--glass-border`, `--glass-blur` in `style.css` |
| 2.2 Backdrop blur | CSS `backdrop-filter: blur(20px)` (GTK 4.14+) |
| 2.3 Base glass shader | `glass.gskshader` loaded via `gsk_shader_new_from_bytes()` |
| 2.4 GlassPanel widget | `GtkWidget` subclass drawing with `GskGLShader` |
| 2.5 Vibrancy labels | CSS `color: rgba(255,255,255,0.9)` on glass panels |

**Deliverable**: All panels (sidebar, toolbar, cards) use glass material

---

### Phase 3: Variable Aspect Artwork Cards (Week 3-4)
| Task | GTK Implementation |
|------|-------------------|
| 3.1 ArtworkCard widget | `GtkBox` subclass preserving aspect ratio (1:1, 4:3, 16:9) |
| 3.2 Glass reflection shader | `glass_reflection.gskshader` on bottom 30% |
| 3.3 Spring hover/press | `GtkSpringAnimation` (damping 0.82, stiffness 180) |
| 3.4 Grid layout | `GtkFlowBox` homogeneous=false, variable row heights |
| 3.5 Hover elevation | Spring scale 1.02 + shadow + glass highlight |

**Deliverable**: Album grid = Apple Music Tahoe (variable rectangles, glass reflection, spring)

---

### Phase 4: Typography (Week 4)
| Task | GTK Implementation |
|------|-------------------|
| 4.1 Font | Bundle **Inter** (variable font) via `g_resource` |
| 4.2 Type scale | CSS custom properties matching Apple scale |
| 4.3 Dynamic type | `GtkSettings::gtk-font-name` + CSS `clamp()` |
| 4.4 Icon mapping | Adwaita symbolic → SF Symbol name map + custom SVG |

```css
/* style.css */
:root {
  --font-family: "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  --font-display: 34px; --font-title1: 28px; --font-title2: 22px;
  --font-title3: 20px; --font-headline: 17px; --font-body: 17px;
  --font-callout: 16px; --font-subhead: 15px; --font-footnote: 13px;
  --font-caption1: 12px; --font-caption2: 11px;
  --font-weight-light: 300; --font-weight-regular: 400;
  --font-weight-medium: 500; --font-weight-semibold: 600; --font-weight-bold: 700;
}
```

---

### Phase 5: Spring Animations & Transitions (Week 5)
| Task | GTK Implementation |
|------|-------------------|
| 5.1 Spring physics | `GtkSpringAnimation` (GTK 4.14+) or tick-callback fallback |
| 5.2 Stack transitions | Zoom/spring push/pop in `StackNavigator` |
| 5.3 Card → NowPlaying morph | Shared element transition (hero animation) |
| 5.4 Volume dial spring | Spring on show/hide, rotation animation |
| 5.5 Respect reduce motion | `GtkSettings::gtk-enable-animations` |

```cpp
// SpringAnimation wrapper
class SpringAnimation {
  static Glib::RefPtr<Gtk::SpringAnimation> create(Gtk::Widget& widget, const char* property) {
    return Gtk::SpringAnimation::create(widget, property);
  }
  void set_params(double damping = 0.82, double stiffness = 180, double mass = 1.0) {
    animation_->set_spring_params(Gtk::SpringParams{damping, stiffness, mass});
  }
};
```

---

### Phase 6: Toolbar & Volume Dial (Week 5-6)
| Task | GTK Implementation |
|------|-------------------|
| 6.1 Native toolbar | `AdwToolbarView` + `AdwHeaderBar` with `GlassPanel` |
| 6.2 Volume dial | `GlassDial` widget: `GtkDrawingArea` + `glass_dial.gskshader` |
| 6.3 Playback controls | Centered: prev / waveform / next (no play/pause) |
| 6.4 Artwork in toolbar | Mini artwork left of waveform |
| 6.5 Waveform in toolbar | `GlassWaveform` spanning center |

**Volume Dial Widget:**
```cpp
class GlassDial : public Gtk::DrawingArea {
  Glib::RefPtr<Gsk::GLShader> shader_;
  double value_ = 0.7;
  
  void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    auto shader = Gsk::GLShader::create_for_bytes(shader_bytes_);
    Gsk::RenderNode* node = gsk_gl_shader_node_new(shader, w, h, 
      { {"u_value", Gsk::Value(value_)}, {"u_accent", Gsk::Value(accent_color)} });
    gsk_renderer_render(renderer, node, cr->cobj());
  }
};
```

---

### Phase 7: Accessibility & Polish (Week 6)
| Task | GTK Implementation |
|------|-------------------|
| 7.1 ATK roles | `gtk_accessible_update_property()` for all custom widgets |
| 7.2 Keyboard nav | `gtk_widget_set_focusable()`, arrow keys, Escape |
| 7.3 High contrast | `@media (prefers-contrast: more)` in CSS |
| 7.4 Reduce motion | `@media (prefers-reduced-motion: reduce)` disables springs |
| 7.5 Localization | `gettext` for all strings |

---

## 📦 Dependencies (CMake)

```cmake
cmake_minimum_required(VERSION 3.20)
project(lollypop-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4>=4.14)
pkg_check_modules(ADWAITA REQUIRED libadwaita-1>=1.5)
pkg_check_modules(GRAPHENE REQUIRED graphene-1.0>=1.10)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-audio-1.0 gstreamer-pbutils-1.0 gstreamer-app-1.0)

# Shader compiler
find_program(GSK_GLSL_COMPILER gsk-glsl-compiler REQUIRED)

# Inter font embedding
file(GLOB INTER_FONTS "resources/fonts/Inter/*.ttf")
add_custom_target(embed_fonts COMMAND glib-compile-resources ...)

add_executable(lollypop-cpp
  src/main.cpp
  src/ui/components/GlassPanel.cpp
  src/ui/components/ArtworkCard.cpp
  src/ui/components/GlassDial.cpp
  src/ui/components/GlassWaveform.cpp
  src/ui/components/GlassToolbar.cpp
  src/ui/components/StackNavigator.cpp
  # ... other sources
)

target_link_libraries(lollypop-cpp PRIVATE
  ${GTK4_LIBRARIES}
  ${ADWAITA_LIBRARIES}
  ${GRAPHENE_LIBRARIES}
  ${GST_LIBRARIES}
  gobject-2.0 gio-2.0 glib-2.0
)

target_include_directories(lollypop-cpp PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/shaders
  include
)
```

---

## ✅ Acceptance Criteria

| Phase | Criteria | Verification |
|-------|----------|--------------|
| 1 | Zero segfaults under ASan + 10k nav events | `./test_stress` |
| 2 | Glass panels render with blur + shader | `gtk-inspector` visual |
| 3 | Variable aspect cards, glass reflection, spring | Visual diff vs Apple Music |
| 4 | Inter font, Apple type scale, Adwaita icons | `gtk-inspector` |
| 5 | 60fps springs, damping 0.82, stiffness 180 | `perf record` + visual |
| 6 | Native toolbar, glass dial, waveform | Visual + interaction |
| 7 | Passes Accerciser; respects reduce motion/contrast | Accessibility Inspector |

---

## 🎯 Definition of Done

> **"Feels like Apple Music on Linux — built with GTK"**

- ✅ Zero crashes under ASan stress test
- ✅ Glass materials via `backdrop-filter` + `GskGLShader`
- ✅ Variable aspect artwork cards with glass reflections
- ✅ Spring animations everywhere (damping 0.82, stiffness 180)
- ✅ Native `AdwToolbarView` + glass volume dial
- ✅ Inter font, dynamic type, Adwaita icons + custom SVG
- ✅ Full accessibility (ATK, keyboard, high contrast, reduce motion)
- ✅ MPRIS + native media keys work flawlessly

---

## 📝 Notes

- **GTK 4.14+ mandatory** for `GskGLShader`, `backdrop-filter`, `GtkSpringAnimation`
- **Inter font** bundled via `glib-compile-resources` (SIL Open Font License)
- **Adwaita icons** provide 95% coverage; custom SVG for gaps
- **No Apple code/assets** — 100% GTK 4 + libadwaita + custom GLSL
- **Performance budget**: 60fps UI, <16ms frame, <100ms cold start

---

*This plan builds a GTK-native app that *feels* like Apple Music, using only open-source GTK ecosystem technologies.*