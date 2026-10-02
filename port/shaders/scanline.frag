#version 450
// Scanlines filter — GLSL port of port_filter.c::Apply_Scanlines.
//
// Every other "logical row" (sized to one GBA pixel of height on the
// output) is dimmed by ~25%. No colour cast — pairs cleanly with a
// CRT mask shader later, stands alone for a softer CRT-ish look.

layout(set = 2, binding = 0) uniform sampler2D uSource;
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    vec4 c = texture(uSource, vTexCoord);
    /* 240×160 source → cell stride = viewport / 160 per row. Every
     * second cell (= every other GBA pixel row) gets dimmed. */
    float rowStride = params.uViewportSize.y / 160.0;
    int row = int(gl_FragCoord.y / max(rowStride, 1.0));
    float dim = ((row & 1) != 0) ? (192.0 / 255.0) : 1.0;
    oColor = vec4(c.rgb * dim, c.a);
}
