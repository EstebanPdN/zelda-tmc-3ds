#version 450
// CRT Warm Composite filter — GLSL port of port_filter.c::Apply_CrtWarmComposite.
//
// Sonkun-style warm composite CRT approximation. Three passes in the
// CPU version, all collapsed into one fragment here:
//   1. 3-tap horizontal blur ([1, 2, 1] / 4)
//   2. Aperture-grill mask — every three columns get R/G/B-dominant
//      gains, sized to the GBA-pixel stride
//   3. Warm tint: R × 1.05, B × 0.92
//
// The blur runs inline by sampling 3 horizontal source taps per
// fragment instead of writing to an intermediate texture. Same per-
// fragment cost as a separate blur pass, no FBO plumbing.

layout(set = 2, binding = 0) uniform sampler2D uSource;
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

vec3 blur3(sampler2D src, vec2 uv) {
    // Tap spacing = 1 source-texel; derive from the live source size (240,
    // the 384 widescreen FB, or an internal-scaled buffer) rather than 1/240.
    vec2 dx = vec2(1.0 / float(textureSize(src, 0).x), 0.0);
    vec3 L = texture(src, uv - dx).rgb;
    vec3 C = texture(src, uv     ).rgb;
    vec3 R = texture(src, uv + dx).rgb;
    return (L + 2.0 * C + R) * 0.25;
}

void main() {
    vec3 c = blur3(uSource, vTexCoord);

    // Aperture-grill stripe — three columns per "phosphor triad",
    // each column boosting one channel and attenuating the others.
    // Stripe width = output_pixels / 240 = one GBA pixel of width.
    float stripeW = max(params.uViewportSize.x / 240.0, 1.0);
    int stripe = int(gl_FragCoord.x / stripeW) % 3;
    vec3 gain;
    if      (stripe == 0) gain = vec3(320.0, 184.0, 184.0) / 256.0;
    else if (stripe == 1) gain = vec3(184.0, 320.0, 184.0) / 256.0;
    else                  gain = vec3(184.0, 184.0, 320.0) / 256.0;

    // Warm tint folded in: R × 1.05 (268/256), B × 0.92 (235/256).
    gain.r *= 268.0 / 256.0;
    gain.b *= 235.0 / 256.0;

    oColor = vec4(clamp(c * gain, 0.0, 1.0), 1.0);
}
