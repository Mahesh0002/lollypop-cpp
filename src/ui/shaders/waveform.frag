#version 150

uniform sampler2D u_samples;
uniform float u_progress;
uniform vec3 u_accent;
uniform float u_time;
uniform vec2 u_resolution;

in vec2 v_texcoord;
out vec4 fragColor;

const int MAX_BARS = 128;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    int bar_count = int(u_resolution.x / 3.0);
    bar_count = clamp(bar_count, 16, MAX_BARS);
    
    float bar_width = 1.0 / float(bar_count);
    float bar_spacing = bar_width * 0.3;
    float bar_draw_width = bar_width - bar_spacing;
    
    float bar_index = floor(uv.x / bar_width);
    float bar_center = (bar_index + 0.5) * bar_width;
    float dist_from_center = abs(uv.x - bar_center);
    
    // Only draw within bar width
    if (dist_from_center > bar_draw_width * 0.5) {
        discard;
    }
    
    // Sample audio data from texture (1D texture of samples)
    float sample_idx = float(bar_index) / float(bar_count);
    float sample = texture(u_samples, vec2(sample_idx, 0.5)).r;
    
    // Waveform height based on sample + progress + animation
    float base_height = sample * 0.8;
    float progress_wave = sin(bar_index * 0.4 + u_time * 2.5) * 0.15;
    float progress_factor = 1.0 - abs(u_progress - sample_idx) * 2.0;
    progress_factor = max(0.0, progress_factor);
    
    float wave_height = base_height + progress_wave * progress_factor;
    
    // Vertical position - centered, growing up and down
    float center_y = 0.5;
    float top = center_y - wave_height * 0.5;
    float bottom = center_y + wave_height * 0.5;
    
    // Check if we're in the bar vertical range
    if (uv.y < top || uv.y > bottom) {
        discard;
    }
    
    // Vertical gradient
    float v_pos = (uv.y - top) / (bottom - top);
    float v_gradient = 1.0 - abs(v_pos - 0.5) * 2.0;
    v_gradient = smoothstep(0.0, 1.0, v_gradient);
    
    // Color: accent with glass overlay
    vec3 glass = vec3(1.0) * 0.08 + vec3(0.02);
    vec3 bar_color = mix(glass, u_accent, 0.7);
    
    // Progress indicator color
    vec3 progress_color = u_accent;
    
    // Blend based on vertical position and progress
    float progress_blend = step(center_y, uv.y) * u_progress;
    vec3 final_color = mix(bar_color, progress_color, progress_blend * 0.5);
    
    // Glass highlight at top
    float top_highlight = exp(-(uv.y - top) * 40.0);
    
    // Smooth edges
    float edge_fade = 1.0 - smoothstep(0.0, bar_draw_width * 0.5, dist_from_center);
    
    fragColor = vec4(final_color + vec3(top_highlight * 0.3), v_gradient * edge_fade * 0.85);
}