#version 150

uniform float u_value;
uniform vec3 u_accent;
uniform float u_time;
uniform vec2 u_resolution;

in vec2 v_texcoord;
out vec4 fragColor;

void main() {
    vec2 center = u_resolution * 0.5;
    vec2 uv = (gl_FragCoord.xy - center) / min(u_resolution.x, u_resolution.y);
    float r = length(uv);
    float angle = atan(uv.y, uv.x);  // -PI to PI
    
    // Dial geometry: outer ring at 0.75-0.82, inner at 0.5
    float outer_radius = 0.82;
    float inner_radius = 0.75;
    float track_width = outer_radius - inner_radius;
    
    // Distance from track centerline
    float track_center = (outer_radius + inner_radius) * 0.5;
    float dist_from_track = abs(r - track_center);
    
    // Track base (frosted glass ring)
    float track_mask = 1.0 - smoothstep(0.0, track_width * 0.5, dist_from_track);
    
    // Filled portion (from -PI/2 to value*2PI - PI/2)
    float fill_end = u_value * 6.283185 - 1.5708;
    float fill_mask = step(angle, fill_end);
    
    // Glass track color
    vec3 glass = vec3(1.0) * 0.1 + vec3(0.02);
    vec3 track_color = mix(glass, u_accent * 0.25, track_mask);
    
    // Filled portion - accent color with glass overlay
    vec3 fill_color = u_accent;
    vec3 filled_glass = mix(glass, fill_color * 0.3, 0.6);
    
    // Combine track and fill
    vec3 color = mix(track_color, filled_glass, fill_mask * track_mask);
    
    // Highlight at fill edge
    float edge_dist = abs(angle - fill_end);
    float edge_highlight = exp(-edge_dist * 80.0) * fill_mask * track_mask;
    color += u_accent * edge_highlight * 0.4;
    
    // Subtle rotation animation on idle
    float idle_shimmer = sin(angle * 3.0 + u_time * 2.0) * 0.005 * (1.0 - u_value);
    
    // Inner glow when active
    float inner_glow = smoothstep(0.5, inner_radius, r) * (1.0 - smoothstep(inner_radius, 0.55, r));
    inner_glow *= u_value * 0.15;
    
    float alpha = max(track_mask, fill_mask * track_mask);
    fragColor = vec4(color + idle_shimmer + vec3(inner_glow), alpha * 0.92);
}