#version 450
// Handheld Grid filter — GLSL port of port_filter.c::Apply_HandheldGrid.
//
// LCD grid with a heavier cell-border dim (~60%) AND a cool/green-
// shifted tint matching the unlit GBA / Game Boy Pocket palette:
//   R × 0.94, G × 1.03, B × 0.91
// The tint runs first, then the grid darkens the borders on top.

layout(set = 2, binding = 0) uniform sampler2D uSource;
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    vec4 c = texture(uSource, vTexCoord);
    /* Cool/green tint folded in first. The 8.8-fixed constants from
     * the CPU version (240/264/232) become plain floats here. */
    vec3 tinted = c.rgb * vec3(240.0 / 256.0, 264.0 / 256.0, 232.0 / 256.0);

    /* Grid cells sized to the source-to-viewport ratio, same as
     * lcd_grid.frag. Border test uses the same <1.0 screen-pixel
     * threshold. */
    vec2 sourceSize = vec2(240.0, 160.0);
    vec2 cellStride = params.uViewportSize / sourceSize;
    vec2 cellPos = mod(gl_FragCoord.xy, cellStride);
    bool border = (cellPos.x < 1.0) || (cellPos.y < 1.0);
    /* Heavier dim than LCD Grid (0.60 vs 0.70) makes the cells read
     * as deliberate hardware mimicry rather than just a subtle grid. */
    float dim = border ? (154.0 / 255.0) : 1.0;
    oColor = vec4(clamp(tinted * dim, 0.0, 1.0), c.a);
}
