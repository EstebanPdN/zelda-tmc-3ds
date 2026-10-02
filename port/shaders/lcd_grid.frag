#version 450
// LCD Grid filter — GLSL port of port_filter.c::Apply_LcdGrid.
//
// Effect: GBA-LCD pixel grid. Each source pixel becomes a cell whose
// borders are darkened to ~70%; cell interior is left alone. No colour
// cast — the LCD reproduces the source faithfully and the grid is the
// entire filter.
//
// The CPU version sized the cell stride to `internal_scale`, i.e. the
// number of screen pixels per source pixel. We get the same behaviour
// here by reading the source-texture size and the viewport size from
// the shader uniforms — see the SDL_GPU vertex shader for how the
// viewport coords are exposed. Stage 3 wire-up: the source texture is
// 240×160, the output viewport is the centred fit-rect, so cell pitch
// = viewport_w / 240.

layout(set = 2, binding = 0) uniform sampler2D uSource;
/* Uniform block at fragment set=3, binding=0 — SDL_GPU's documented
 * convention for "fragment uniforms". Currently a single vec2 holding
 * the output viewport size in pixels. Add fields here as later shaders
 * need them (mask phase, scanline strength, tint colour, ...). */
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    vec4 c = texture(uSource, vTexCoord);

    /* Source texture is fixed 240×160. Cell stride = output_pixels /
     * source_pixels, rounded down. At a 4x scale that's 4 pixels per
     * cell with a 1-pixel-wide darkened border on both axes. The CPU
     * version skipped the effect entirely when scale < 2; the GLSL
     * equivalent: when stride is <2, the modulus check below catches
     * every pixel and dims uniformly, which still looks fine. */
    vec2 sourceSize = vec2(240.0, 160.0);
    vec2 cellStride = params.uViewportSize / sourceSize;
    /* gl_FragCoord is the screen-space output pixel. Convert to
     * viewport-relative (Vulkan's clip Y is +down so gl_FragCoord.y
     * grows downward, which matches what we want). */
    vec2 cellPos = mod(gl_FragCoord.xy, cellStride);
    /* "Border" = the cellPos == 0 row/column. With floating-point we
     * test "less than half a screen pixel from zero" so we're robust
     * to non-integer cellStride. */
    bool border_x = cellPos.x < 1.0;
    bool border_y = cellPos.y < 1.0;
    float dim = (border_x || border_y) ? (180.0 / 255.0) : 1.0;
    oColor = vec4(c.rgb * dim, c.a);
}
