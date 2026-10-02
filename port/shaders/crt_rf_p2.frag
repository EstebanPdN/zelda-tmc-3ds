#version 450
// CRT Warm RF — Stage 5+B pass 2. Same effect as crt_rf.frag but the
// horizontal blur is now an FBO prepass (blur5h.frag → intermediate),
// so this shader only does the mask + tint + scanlines. Proves the
// multi-pass machinery on a real visual output.

layout(set = 2, binding = 0) uniform sampler2D uSource;  /* blurred intermediate */
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    vec3 c = texture(uSource, vTexCoord).rgb;

    /* Aperture-grill stripe. Stripe width = one GBA pixel of width. */
    float stripeW = max(params.uViewportSize.x / 240.0, 1.0);
    int stripe = int(gl_FragCoord.x / stripeW) % 3;
    vec3 gain;
    if      (stripe == 0) gain = vec3(320.0, 184.0, 184.0) / 256.0;
    else if (stripe == 1) gain = vec3(184.0, 320.0, 184.0) / 256.0;
    else                  gain = vec3(184.0, 184.0, 320.0) / 256.0;

    /* Warm tint — R × 1.06, B × 0.90. */
    gain.r *= 271.0 / 256.0;
    gain.b *= 230.0 / 256.0;

    /* Scanlines — every other GBA-pixel row at ×0.80. */
    float rowStride = max(params.uViewportSize.y / 160.0, 1.0);
    int row = int(gl_FragCoord.y / rowStride);
    if ((row & 1) != 0) gain *= 205.0 / 256.0;

    oColor = vec4(clamp(c * gain, 0.0, 1.0), 1.0);
}
