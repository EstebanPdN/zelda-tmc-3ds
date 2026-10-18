#version 450
// Horizontal 5-tap blur ([1, 2, 3, 2, 1] / 9) — prepass for the CRT
// filters. Stage 5+A: separated from the inline blur in crt_*.frag so
// the multi-pass FBO machinery has a real consumer.
//
// Reads from the source texture (240×160 GBA framebuffer), writes to
// the intermediate texture (also 240×160). The CRT final-pass shaders
// then sample the blurred intermediate instead of the raw source.

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    // Texel size from the actual source dimensions, not a hardcoded 240: the
    // source may be native 240, the 384 widescreen FB, or an internal-scaled buffer.
    vec2 dx = vec2(1.0 / float(textureSize(uSource, 0).x), 0.0);
    vec3 t0 = texture(uSource, vTexCoord - dx - dx).rgb;
    vec3 t1 = texture(uSource, vTexCoord - dx     ).rgb;
    vec3 t2 = texture(uSource, vTexCoord          ).rgb;
    vec3 t3 = texture(uSource, vTexCoord + dx     ).rgb;
    vec3 t4 = texture(uSource, vTexCoord + dx + dx).rgb;
    vec3 c  = (t0 + 2.0 * t1 + 3.0 * t2 + 2.0 * t3 + t4) / 9.0;
    oColor = vec4(c, 1.0);
}
