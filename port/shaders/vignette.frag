#version 450
// Vignette filter — GLSL port of port_filter.c::Apply_Vignette.
//
// Radial fall-off from screen centre. Inner 60% of the image is
// untouched; outside that band, brightness drops linearly to 55% at
// the corners. No colour cast, no per-pixel pattern.

layout(set = 2, binding = 0) uniform sampler2D uSource;
layout(set = 3, binding = 0) uniform FragParams {
    vec2 uViewportSize;
} params;

layout(location = 0) in  vec2 vTexCoord;
layout(location = 0) out vec4 oColor;

void main() {
    vec4 c = texture(uSource, vTexCoord);

    /* Distance from centre, normalised to corner-radius units. The
     * gl_FragCoord-based math from the CPU version translates directly
     * — we use the viewport size to find the centre and the max-corner
     * distance for normalisation. */
    vec2 centre = params.uViewportSize * 0.5;
    vec2 delta  = gl_FragCoord.xy - centre;
    /* Avoid sqrt() — work in squared-distance, matching the CPU version. */
    float d2     = dot(delta, delta);
    float max_d2 = dot(centre, centre);                 /* corner² */
    float inner  = max_d2 * 0.36;                       /* 60% of corner */
    float span   = max_d2 - inner;

    float gain = 1.0;
    if (d2 > inner) {
        float t = clamp((d2 - inner) / span, 0.0, 1.0);
        const float cornerGain = 141.0 / 256.0;          /* 0.55× */
        gain = mix(1.0, cornerGain, t);
    }
    oColor = vec4(c.rgb * gain, c.a);
}
