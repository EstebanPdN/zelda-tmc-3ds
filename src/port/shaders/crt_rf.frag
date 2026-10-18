#version 450
// CRT Warm RF filter — GLSL port of port_filter.c::Apply_CrtWarmRf.
//
// Sonkun-style warm RF CRT — like Composite but signal-degraded:
//   1. 5-tap horizontal smear ([1, 2, 3, 2, 1] / 9) — heavier than
//      Composite's 3-tap blur, simulating RF cable bandwidth loss
//   2. Aperture-grill mask (same as Composite)
//   3. Warm tint: R × 1.06, B × 0.90 (slightly more saturated)
//   4. Scanlines: every other GBA pixel row attenuated to ×0.80
//
// Inline implementation — sample the 5 taps directly instead of a
// separate blur pass.

layout(set = 2, binding = 0) uniform sampler2D uSource;
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

vec3 blur5(sampler2D src, vec2 uv) {
    vec2 dx = vec2(1.0 / float(textureSize(src, 0).x), 0.0);
    vec3 t0 = texture(src, uv - dx - dx).rgb;
    vec3 t1 = texture(src, uv - dx     ).rgb;
    vec3 t2 = texture(src, uv          ).rgb;
    vec3 t3 = texture(src, uv + dx     ).rgb;
    vec3 t4 = texture(src, uv + dx + dx).rgb;
    return (t0 + 2.0 * t1 + 3.0 * t2 + 2.0 * t3 + t4) / 9.0;
}

void main() {
    vec3 c = blur5(uSource, vTexCoord);

    // Aperture-grill stripe (same as Composite).
    float stripeW = max(params.uViewportSize.x / 240.0, 1.0);
    int stripe = int(gl_FragCoord.x / stripeW) % 3;
    vec3 gain;
    if      (stripe == 0) gain = vec3(320.0, 184.0, 184.0) / 256.0;
    else if (stripe == 1) gain = vec3(184.0, 320.0, 184.0) / 256.0;
    else                  gain = vec3(184.0, 184.0, 320.0) / 256.0;

    // Warm tint — slightly stronger than Composite (R × 1.06, B × 0.90).
    gain.r *= 271.0 / 256.0;
    gain.b *= 230.0 / 256.0;

    // Scanlines — dim every other GBA-pixel-of-height row to ×0.80.
    float rowStride = max(params.uViewportSize.y / 160.0, 1.0);
    int row = int(gl_FragCoord.y / rowStride);
    if ((row & 1) != 0) gain *= 205.0 / 256.0;

    oColor = vec4(clamp(c * gain, 0.0, 1.0), 1.0);
}
