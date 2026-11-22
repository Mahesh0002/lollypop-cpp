# Shader Development Execution Plan

> **Duration**: 1-2 weeks (parallel with Phase 2)  
> **Goal**: 5 production-ready `.gskshader` files compiled and integrated  
> **Branch**: `shader-dev`

---

## 🎯 Shaders to Build

| Shader | File | Purpose | Uniforms |
|--------|------|---------|----------|
| **Base Glass** | `glass.frag` | Frosted glass panels | `u_backdrop`, `u_tint`, `u_blur`, `u_time` |
| **Artwork Reflection** | `glass_reflection.frag` | Artwork bottom fade | `u_artwork`, `u_backdrop`, `u_progress`, `u_accent` |
| **Volume Dial** | `glass_dial.frag` | Circular glass dial | `u_value`, `u_accent`, `u_time` |
| **Waveform** | `waveform.frag` | Animated wavy bars | `u_samples`, `u_progress`, `u_accent`, `u_time` |
| **Icon Morph** | `morph.frag` | Icon state transitions | `u_from`, `u_to`, `u_progress` |

---

## 📋 Task Breakdown

### 2.1 Shader Source Creation (Days 1-3)

**Directory**: `src/ui/shaders/`

| File | Lines | Complexity |
|------|-------|------------|
| `glass.vert` | ~20 | Simple pass-through |
| `glass.frag` | ~60 | Medium |
| `glass_reflection.vert` | ~20 | Simple |
| `glass_reflection.frag` | ~50 | Medium |
| `glass_dial.vert` | ~20 | Simple |
| `glass_dial.frag` | ~60 | Medium-High |
| `waveform.vert` | ~20 | Simple |
| `waveform.frag` | ~80 | High |
| `morph.vert` | ~20 | Simple |
| `morph.frag` | ~70 | High |

**Vertex Shader Template** (all share similar):
```glsl
// glass.vert
#version 150
in vec2 position;
in vec2 texcoord;
out vec2 v_texcoord;
void main() {
    v_texcoord = texcoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
```

---

### 2.2 Compilation Pipeline (Days 3-4)

**CMake Integration**:
```cmake
# Find compiler
find_program(GSK_GLSL_COMPILER gsk-glsl-compiler REQUIRED)

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

# Compile all
compile_gsk_shader(glass_shader      src/ui/shaders/glass.vert      src/ui/shaders/glass.frag)
compile_gsk_shader(glass_reflection_shader  src/ui/shaders/glass_reflection.vert  src/ui/shaders/glass_reflection.frag)
compile_gsk_shader(glass_dial_shader      src/ui/shaders/glass_dial.vert      src/ui/shaders/glass_dial.frag)
compile_gsk_shader(waveform_shader        src/ui/shaders/waveform.vert        src/ui/shaders/waveform.frag)
compile_gsk_shader(morph_shader           src/ui/shaders/morph.vert           src/ui/shaders/morph.frag)

# Install
install(FILES 
    ${glass_shader} ${glass_reflection_shader} ${glass_dial_shader} 
    ${waveform_shader} ${morph_shader}
    DESTINATION share/lollypop-cpp/shaders
)
```

---

### 2.3 Shader Loading in C++ (Days 4-5)

**ShaderManager.hpp**:
```cpp
class ShaderManager {
public:
    static ShaderManager& instance() {
        static ShaderManager instance;
        return instance;
    }
    
    bool load_all(const std::string& shader_dir);
    Glib::RefPtr<Gsk::GLShader> get(const std::string& name) const;
    
private:
    std::unordered_map<std::string, Glib::RefPtr<Gsk::GLShader>> shaders_;
};
```

**Usage in Widgets**:
```cpp
// In GlassPanel::render()
auto shader = ShaderManager::instance().get("glass");
Gsk::RenderNode* node = gsk_gl_shader_node_new(
    shader->gobj(), width, height,
    {
        {"u_backdrop", Gsk::Value(backdrop_texture)},
        {"u_tint", Gsk::Value(accent_color)},
        {"u_blur", Gsk::Value(20.0f)},
        {"u_time", Gsk::Value(animation_time_)}
    });
gsk_renderer_render(renderer, node, cr->cobj());
```

---

### 2.4 Uniform Binding Helpers (Day 5)

**ShaderUniforms.hpp**:
```cpp
struct GlassUniforms {
    Glib::RefPtr<Gdk::Texture> backdrop;
    Gdk::RGBA tint;
    float blur_radius = 20.0f;
    float time = 0.0f;
    
    std::vector<Gsk::UniformValue> to_uniforms() const {
        return {
            {"u_backdrop", Gsk::Value(backdrop)},
            {"u_tint", Gsk::Value(graphene_vec3_init(...))},
            {"u_blur", Gsk::Value(blur_radius)},
            {"u_time", Gsk::Value(time)}
        };
    }
};

struct WaveformUniforms {
    std::vector<float> samples;
    float progress = 0.0f;
    Gdk::RGBA accent;
    float time = 0.0f;
    // ...
};
```

---

## ✅ Definition of Done

| Shader | Compiles | Loads in GTK | Renders Correctly | Uniforms Work |
|--------|----------|--------------|-------------------|---------------|
| glass | ✅ | ✅ | ✅ | ✅ |
| glass_reflection | ✅ | ✅ | ✅ | ✅ |
| glass_dial | ✅ | ✅ | ✅ | ✅ |
| waveform | ✅ | ✅ | ✅ | ✅ |
| morph | ✅ | ✅ | ✅ | ✅ |

---

## 🧪 Testing

```bash
# 1. Compile shaders
cmake --build build --target shaders

# 2. Verify .gskshader files exist
ls build/shaders/*.gskshader

# 3. Run shader test app
./build/shader_test_app

# 4. Visual verification in main app
./build/lollypop-cpp
```

---

## 📝 Notes

- **GTK 4.14+ required** for `GskGLShader`
- **Uniform naming**: `u_` prefix for all uniforms
- **Texture binding**: Use `Gsk::Value(Glib::RefPtr<Gdk::Texture>)`
- **Time uniform**: Update per-frame in tick callback
- **Error handling**: Check `gsk_gl_shader_compile()` return value

---

**Parallel with**: Phase 2 (Glass Materials) - shaders needed for GlassPanel widget