#version 150

uniform sampler2D u_artwork;
uniform sampler2D u_backdrop;
uniform float u_progress;
uniform vec3 u_accent;

in vec2 v_texcoord;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(u_artwork, 0));
    
    // Artwork area (top 70%)
    if (uv.y < 0.7) {
        vec2 sample_uv = vec2(uv.x, uv.y / 0.7);
        vec4 art = texture(u_artwork, sample_uv);
        fragColor = art;
        return;
    }
    
    // Reflection zone (bottom 30%)
    float reflect_y = (uv.y - 0.7) / 0.3;  // 0 to 1
    vec2 sample_uv = vec2(uv.x, 1.0 - reflect_y * 0.9);
    
    vec4 reflected = texture(u_artwork, sample_uv);
    float fade = 1.0 - smoothstep(0.0, 1.0, reflect_y);
    
    // Glass overlay on reflection
    vec3 glass = vec3(1.0) * 0.1 + vec3(0.03);
    vec3 backdrop_sample = texture(u_backdrop, uv).rgb;
    glass = mix(glass, backdrop_sample, 0.3);
    
    // Accent tint on reflection
    vec3 reflection_color = mix(reflected.rgb, u_accent * 0.08, 0.15);
    
    // Combine
    vec3 final_color = mix(glass, reflection_color * fade, 0.55);
    
    // Progress indicator at bottom
    float progress_alpha = step(0.95, uv.y) * u_progress;
    final_color = mix(final_color, u_accent, progress_alpha * 0.6);
    
    fragColor = vec4(final_color, fade * 0.35 + progress_alpha * 0.4);
}