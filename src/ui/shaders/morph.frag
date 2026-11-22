#version 150

uniform sampler2D u_from;
uniform sampler2D u_to;
uniform float u_progress;
uniform vec3 u_accent;

in vec2 v_texcoord;
out vec4 fragColor;

void main() {
    // Morph between two icon textures using signed distance field approach
    // For simplicity, we cross-fade with a directional wipe
    
    vec2 uv = v_texcoord;
    
    // Wipe direction: left to right
    float wipe = smoothstep(u_progress - 0.1, u_progress + 0.1, uv.x);
    
    vec4 from_color = texture(u_from, uv);
    vec4 to_color = texture(u_to, uv);
    
    // Cross-fade with glass overlay
    vec4 mixed = mix(from_color, to_color, wipe);
    
    // Add glass accent tint during transition
    float transition_intensity = 1.0 - abs(u_progress - 0.5) * 2.0;
    transition_intensity = smoothstep(0.0, 1.0, transition_intensity);
    
    vec3 glass = vec3(1.0) * 0.1 + vec3(0.02);
    vec3 accent_tint = mix(glass, u_accent * 0.15, transition_intensity * 0.3);
    
    vec3 final_color = mixed.rgb + accent_tint * mixed.a;
    
    // Subtle shimmer at wipe edge
    float edge_shimmer = exp(-abs(uv.x - u_progress) * 30.0) * transition_intensity;
    final_color += u_accent * edge_shimmer * 0.2;
    
    fragColor = vec4(final_color, mixed.a);
}