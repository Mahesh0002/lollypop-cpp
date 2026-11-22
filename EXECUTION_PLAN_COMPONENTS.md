# Component Development Execution Plan

> **Duration**: 3-4 weeks (Phases 2-6)  
> **Goal**: All custom GTK widgets implemented, tested, and integrated  
> **Branch**: `component-dev`

---

## 🎯 Components to Build

| Component | File | Base Class | Shaders Used |
|-----------|------|------------|--------------|
| **GlassPanel** | `GlassPanel.hpp/cpp` | `GtkWidget` | `glass.gskshader` |
| **ArtworkCard** | `ArtworkCard.hpp/cpp` | `GtkBox` | `glass_reflection.gskshader` |
| **GlassDial** | `GlassDial.hpp/cpp` | `GtkDrawingArea` | `glass_dial.gskshader` |
| **GlassWaveform** | `GlassWaveform.hpp/cpp` | `GtkDrawingArea` | `waveform.gskshader` |
| **GlassToolbar** | `GlassToolbar.hpp/cpp` | `AdwToolbarView` | `glass.gskshader` |
| **GlassPopover** | `GlassPopover.hpp/cpp` | `GtkPopover` | `glass.gskshader` |
| **StackNavigator** | `StackNavigator.hpp/cpp` | Plain C++ | N/A |

---

## 📋 Phase 2: GlassPanel (Week 1)

### 2.1 GlassPanel Widget (Days 1-3)

**File**: `frontend/include/ui/components/GlassPanel.hpp`, `frontend/src/ui/components/GlassPanel.cpp`

```cpp
// GlassPanel.hpp
class GlassPanel : public Gtk::Widget {
public:
    GlassPanel();
    
    void set_tint(const Gdk::RGBA& color);
    void set_blur_radius(float radius);
    void set_corner_radius(float radius);
    
protected:
    void on_realize() override;
    void on_unrealize() override;
    void on_map() override;
    void on_unmap() override;
    
    // GTK 4 drawing
    void snapshot(Gtk::Snapshot& snapshot, double width, double height) override;
    
private:
    Glib::RefPtr<Gsk::GLShader> shader_;
    Gdk::RGBA tint_;
    float blur_radius_ = 20.0f;
    float corner_radius_ = 16.0f;
    guint tick_id_ = 0;
    float time_ = 0.0f;
    
    static gboolean on_tick(GtkWidget*, GdkFrameClock*, gpointer);
    void on_tick(GdkFrameClock* clock);
};
```

**Implementation Notes**:
- Use `gsk_gl_shader_node_new()` in `snapshot()`
- Animate `u_time` uniform at 60fps via tick callback
- Bind `u_backdrop` to parent's backdrop texture

---

### 2.2 CSS Integration (Day 4)

**File**: `resources/style.css`

```css
.glass-panel {
    background-color: rgba(255, 255, 255, 0.08);
    border: 1px solid rgba(255, 255, 255, 0.12);
    backdrop-filter: blur(20px);
    border-radius: 16px;
}

.glass-panel:hover {
    background-color: rgba(255, 255, 255, 0.12);
}
```

**Widget CSS Class**: `gtk_widget_add_css_class(widget, "glass-panel")`

---

### 2.3 Integration Testing (Day 5)

Replace in `MainWindow`:
- Sidebar background → `GlassPanel`
- Bottom playback bar → `GlassPanel`
- Right panel → `GlassPanel`

---

## 🎨 Phase 3: ArtworkCard (Week 2)

### 3.1 ArtworkCard Widget (Days 1-3)

**File**: `frontend/include/ui/components/ArtworkCard.hpp`, `frontend/src/ui/components/ArtworkCard.cpp`

```cpp
// ArtworkCard.hpp
class ArtworkCard : public Gtk::Box {
public:
    ArtworkCard();
    
    void set_artwork(const std::string& path);
    void set_title(const std::string& title);
    void set_artist(const std::string& artist);
    void set_aspect_ratio(float ratio);  // 1.0, 4/3, 16/9
    
    // Spring animation
    void set_hovered(bool hovered);
    void set_pressed(bool pressed);
    
protected:
    void on_realize() override;
    void snapshot(Gtk::Snapshot& snapshot, double width, double height) override;
    
private:
    std::unique_ptr<Gtk::DrawingArea> artwork_area_;
    std::unique_ptr<Gtk::Label> title_label_;
    std::unique_ptr<Gtk::Label> artist_label_;
    
    Glib::RefPtr<Gsk::GLShader> reflection_shader_;
    float aspect_ratio_ = 1.0f;
    float hover_scale_ = 1.0f;
    float press_scale_ = 1.0f;
    bool hovered_ = false;
    bool pressed_ = false;
    
    // Spring animations
    Glib::RefPtr<Gtk::SpringAnimation> hover_anim_;
    Glib::RefPtr<Gtk::SpringAnimation> press_anim_;
};
```

**Key Features**:
- Preserves original aspect ratio (measures loaded pixbuf)
- Glass reflection shader on bottom 30%
- Spring animation on hover (scale 1.02) and press (scale 0.98)
- Rounded corners matching panel radius

---

### 3.2 Grid Layout (Days 4-5)

**File**: `frontend/src/ui/pages/AlbumsPage.cpp`, `ArtistsPage.cpp`

```cpp
// Replace GtkGrid with GtkFlowBox
flow_box_ = gtk_flow_box_new();
gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow_box_), FALSE);
gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow_box_), 4);
gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow_box_), 8);
gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow_box_), GTK_SELECTION_NONE);

// Add cards
for (auto& album : albums) {
    auto card = std::make_unique<ArtworkCard>();
    card->set_artwork(album.artwork_path);
    card->set_title(album.title);
    card->set_artist(album.artist);
    card->set_aspect_ratio(calculate_aspect(album.artwork_path));
    gtk_flow_box_append(GTK_FLOW_BOX(flow_box_), card->get_widget());
    cards_.push_back(std::move(card));
}
```

---

## 🎛 Phase 4: GlassDial (Volume) - Week 3

### 4.1 GlassDial Widget (Days 1-3)

**File**: `frontend/include/ui/components/GlassDial.hpp`, `frontend/src/ui/components/GlassDial.cpp`

```cpp
class GlassDial : public Gtk::DrawingArea {
public:
    GlassDial();
    
    void set_value(double value);  // 0-1
    void set_accent(const Gdk::RGBA& color);
    double get_value() const { return value_; }
    
    // Callbacks
    sigc::signal<void(double)> signal_value_changed();
    
protected:
    void snapshot(Gtk::Snapshot& snapshot, double width, double height) override;
    bool on_button_press(GdkEventButton* event) override;
    bool on_button_release(GdkEventButton* event) override;
    bool on_motion_notify(GdkEventMotion* event) override;
    bool on_scroll(GdkEventScroll* event) override;
    
private:
    Glib::RefPtr<Gsk::GLShader> shader_;
    double value_ = 0.7;
    Gdk::RGBA accent_;
    bool dragging_ = false;
    double drag_start_y_ = 0.0;
    double drag_start_value_ = 0.0;
    
    sigc::signal<void(double)> signal_value_changed_;
};
```

**Shader Uniforms**:
```glsl
// glass_dial.frag uniforms
uniform float u_value;        // 0-1
uniform vec3 u_accent;
uniform float u_time;
```

---

### 4.2 Volume Popover Integration (Days 4-5)

**File**: `frontend/src/panels/bottom_playback.cpp`

```cpp
// Replace volume_button_ with GlassDial
volume_dial_ = std::make_unique<GlassDial>();
volume_dial_->set_value(current_volume_);
volume_dial_->set_accent(accent_color_);
volume_dial_->signal_value_changed().connect(
    [this](double v) { on_volume_changed(v); });

// Wrap in GtkMenuButton for popover
volume_button_ = gtk_menu_button_new();
gtk_menu_button_set_child(GTK_MENU_BUTTON(volume_button_), volume_dial_->get_widget());
gtk_widget_add_css_class(volume_button_, "bottom-playback-volume-dial");
```

---

## 🌊 Phase 5: GlassWaveform (Week 4)

### 5.1 GlassWaveform Widget (Days 1-3)

**File**: `frontend/include/ui/components/GlassWaveform.hpp`, `frontend/src/ui/components/GlassWaveform.cpp`

```cpp
class GlassWaveform : public Gtk::DrawingArea {
public:
    GlassWaveform();
    
    void set_samples(const std::vector<float>& samples);
    void set_progress(double progress);  // 0-1
    void set_accent(const Gdk::RGBA& color);
    void set_playing(bool playing);
    
protected:
    void snapshot(Gtk::Snapshot& snapshot, double width, double height) override;
    
private:
    Glib::RefPtr<Gsk::GLShader> shader_;
    std::vector<float> samples_;
    float progress_ = 0.0f;
    Gdk::RGBA accent_;
    bool playing_ = false;
    float animation_phase_ = 0.0f;
    guint tick_id_ = 0;
    
    static gboolean on_tick(GtkWidget*, GdkFrameClock*, gpointer);
    void on_tick(GdkFrameClock* clock);
};
```

**Shader**: `waveform.gskshader` (animated wavy bars)

---

### 5.2 BottomPlayback Integration (Days 4-5)

**File**: `frontend/src/panels/bottom_playback.cpp`

```cpp
// Center section: Prev | Waveform | Next
center_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
gtk_widget_set_hexpand(center_box_, TRUE);

// Prev button
prev_button_ = create_control_button("media-skip-backward-symbolic");

// Waveform - expands to fill
waveform_ = std::make_unique<GlassWaveform>();
waveform_->set_preferred_size(800, 48);
gtk_widget_set_hexpand(waveform_->get_widget(), TRUE);
gtk_box_append(GTK_BOX(center_box_), prev_button_);
gtk_box_append(GTK_BOX(center_box_), waveform_->get_widget());
gtk_box_append(GTK_BOX(center_box_), next_button_);
```

---

## 🧭 Phase 6: StackNavigator & Toolbar (Week 5)

### 6.1 StackNavigator (Days 1-2)

**File**: `frontend/include/ui/components/StackNavigator.hpp`, `frontend/src/ui/components/StackNavigator.cpp`

```cpp
class StackNavigator {
public:
    explicit StackNavigator(GtkStack* stack);
    
    void push(const std::string& page, GtkWidget* widget);
    void pop();
    void replace(const std::string& page, GtkWidget* widget);
    
    // Transition types
    enum class Transition { SlideLeft, SlideRight, Fade, Zoom };
    void set_transition(Transition t);
    
private:
    GtkStack* stack_;
    std::vector<std::string> history_;
    GtkStackTransitionType current_transition_ = GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT;
    std::mutex mutex_;
};
```

**Transitions**:
- `push()`: `GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT`
- `pop()`: `GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT`
- `replace()`: `GTK_STACK_TRANSITION_TYPE_CROSSFADE`

---

### 6.2 GlassToolbar (Days 3-5)

**File**: `frontend/include/ui/components/GlassToolbar.hpp`, `frontend/src/ui/components/GlassToolbar.cpp`

```cpp
class GlassToolbar : public Adw::ToolbarView {
public:
    GlassToolbar();
    
    void set_artwork(const std::string& path);
    void set_title(const std::string& title);
    void set_artist(const std::string& artist);
    void set_progress(double progress);
    
    // Controls
    void set_prev_callback(sigc::slot<void> cb);
    void set_next_callback(sigc::slot<void> cb);
    void set_volume_callback(sigc::slot<void(double)> cb);
    void set_pip_callback(sigc::slot<void> cb);
    
private:
    std::unique_ptr<GlassDial> volume_dial_;
    std::unique_ptr<GlassWaveform> waveform_;
    // ... buttons, labels
};
```

---

## ✅ Definition of Done per Component

| Component | Compiles | Renders | Springs Work | Uniforms Update | Integrated |
|-----------|----------|---------|--------------|-----------------|------------|
| GlassPanel | ✅ | ✅ | N/A | ✅ | ✅ |
| ArtworkCard | ✅ | ✅ | ✅ | ✅ | ✅ |
| GlassDial | ✅ | ✅ | ✅ | ✅ | ✅ |
| GlassWaveform | ✅ | ✅ | ✅ | ✅ | ✅ |
| GlassToolbar | ✅ | ✅ | ✅ | ✅ | ✅ |
| StackNavigator | ✅ | ✅ | ✅ | N/A | ✅ |

---

## 🧪 Testing Checklist

```bash
# Per component
./build/component_test_GlassPanel
./build/component_test_ArtworkCard
./build/component_test_GlassDial
./build/component_test_GlassWaveform

# Visual regression
./scripts/visual_regression.sh

# Integration
./build/lollypop-cpp
# Navigate all pages, test hover/press, volume, seek
```

---

## 📝 Notes

- **GTK 4.14+ required** for `GtkSpringAnimation`, `GskGLShader`
- **Shader loading**: Via `ShaderManager` singleton
- **Animation**: `GtkSpringAnimation` (damping 0.82, stiffness 180)
- **Memory**: Use `Glib::RefPtr` for all GTK objects
- **Thread safety**: UI updates only on main thread

---

**Dependencies**: Phase 1 (Stability) + Shaders (Phase 2) must complete first