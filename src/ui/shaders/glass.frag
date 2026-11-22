#version 150

uniform sampler2D u_backdrop;
uniform vec3 u_tint;
uniform float u_blur_radius;
uniform float u_time;
uniform vec2 u_resolution;

in vec2 v_texcoord;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    
    // Sample backdrop with blur approximation
    vec3 backdrop = texture(u_backdrop, uv).rgb;
    
    // Frosted glass base - subtle transparency with backdrop blending
    float vignette = smoothstep(0.0, 1.0, length(uv - 0.5) * 1.8);
    vec3 glass = mix(backdrop, vec3(1.0), 0.08) * 0.92 + vec3(0.02);
    glass = mix(glass, backdrop, 0.25) * vignette + glass * (1.0 - vignette);
    
    // Subtle specular highlight (top-left light source)
    vec3 view_dir = normalize(vec3(uv - 0.5, 1.0));
    vec3 light_dir = normalize(vec3(-0.3, -0.3, 1.0));
    float spec = pow(max(0.0, dot(view_dir, light_dir)), 48.0);
    
    // Accent color tint (very subtle)
    vec3 color = mix(glass, u_tint * 0.12, 0.18);
    
    // Subtle animated shimmer
    float shimmer = sin(uv.x * 12.0 + u_time * 1.2) * 0.008;
    
    // Edge highlight for depth perception
    float edge = smoothstep(0.96, 1.0, length(uv - 0.5) * 2.0);
    
    fragColor = vec4(color + shimmer + spec * 0.15 + edge * 0.05, 0.88);
}