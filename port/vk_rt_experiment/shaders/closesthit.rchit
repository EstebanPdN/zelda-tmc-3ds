/* port/vk_rt_experiment/shaders/closesthit.rchit
 *
 * Closest-hit shader. Runs when a ray hits triangle geometry.
 *
 * Responsibilities:
 *   1. Reconstruct the hit position and per-vertex attributes (UV,
 *      emissive flag) by interpolating with the built-in barycentrics.
 *   2. Sample the diffuse atlas at the interpolated UV.
 *   3. If the surface is emissive, treat it as a light source and
 *      add its colour to the payload directly.
 *   4. Otherwise, cast a shadow ray from the hit toward a virtual
 *      sun position and modulate the diffuse colour by visibility.
 *
 * Lighting model: simplified retro-modern — flat-shaded with soft
 * shadows. No Lambertian falloff because the GBA's pixel art was
 * pre-shaded by the original artists; cosine-falloff would
 * darken everything indiscriminately. We instead emit the diffuse
 * colour scaled by ambient (when shadowed) or 1.0 (when lit).
 */
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

/* Must match raygen.rgen's HitPayload exactly. */
struct HitPayload {
    vec3 colour;
    float distance;
};

/* Shadow-ray payload — distinct from primary so the shadow miss
 * shader at SBT slot 1 can clear `hit = false` without colliding
 * with the primary payload's layout. */
struct ShadowPayload {
    bool hit;
};

layout(location = 0) rayPayloadInEXT HitPayload     payload;
layout(location = 1) rayPayloadEXT   ShadowPayload  shadowPayload;

/* Built-in barycentric attribs for the hit triangle. */
hitAttributeEXT vec2 attribs;

/* Vertex layout MUST match RenderLayerManager::Vertex (24 bytes):
 *   position (3 floats) → 12 bytes
 *   uv       (2 floats) →  8 bytes
 *   emissive (1 float)  →  4 bytes
 * std430 layout because storage-buffer reads default to it. */
struct Vertex {
    float px, py, pz;
    float u, v;
    float emissive;
};

/* Bindings match RayTracingPipeline::createDescriptorLayout. */
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 2, std430) readonly buffer Vertices { Vertex v[]; } verts;
layout(set = 0, binding = 3, std430) readonly buffer Indices  { uint   i[]; } idx;
layout(set = 0, binding = 4) uniform texture2D atlasTextures[];
layout(set = 0, binding = 5) uniform sampler   atlasSampler;

layout(push_constant) uniform PushConstants {
    vec4 params;  /* x=frameIndex, y=time, z=exposure */
} pc;

/* Lighting:
 *   - One static warm "sun" key light far above the playfield.
 *   - Six coloured point lights orbit above the playfield at
 *     different phases + radii so they don't bunch.
 * All shadow rays are soft (N jittered samples per light) so the
 * shadow edges fade rather than crack hard.
 *
 * Light positions derive from pc.params.y (seconds) so they
 * animate frame-by-frame. Z = -1.5..-2.0 puts them in front of
 * the camera (at z=-1.0); rays from playfield hits at z≈0.8 toward
 * the lights travel back through any intervening occluder geometry. */
const float kPi = 3.14159265359;
const vec3  kAmbient = vec3(0.06, 0.07, 0.10);  /* darker = more contrast */

const int  kLightCount    = 6;
const int  kShadowSamples = 1;   /* hard shadows — no per-pixel noise grain */
const float kShadowJitter = 0.0;

const vec3 kLightColours[kLightCount] = vec3[kLightCount](
    vec3(2.50, 0.90, 0.30),   /* warm orange */
    vec3(0.35, 0.90, 2.50),   /* cool blue */
    vec3(0.40, 2.20, 0.75),   /* bright green */
    vec3(2.20, 0.50, 2.00),   /* magenta */
    vec3(2.30, 1.80, 0.50),   /* gold */
    vec3(0.50, 2.20, 2.00)    /* cyan */
);

/* Lights orbit just IN FRONT of the sprite plane (sprites at z=0.2)
 * so the projection from sprite-occluder → BG2-receiver expands the
 * shadow by ~6× the sprite size. Without this the shadows are sprite-
 * sized and barely readable; with it, every on-screen sprite trails a
 * clearly visible coloured penumbra on the background. */
vec3 lightPosition(float t, float phase, float radius) {
    return vec3(120.0 + radius * cos(t + phase),
                80.0  + (radius * 0.6) * sin(t + phase * 1.7),
                -0.40);  /* far enough back to keep shadow projection
                          * modest — shadows are visible but don't
                          * dominate the image as a noisy halo around
                          * every sprite. */
}

const vec3  kSunDir       = normalize(vec3(0.3, -0.5, -0.8));
const vec3  kSunColour    = vec3(0.85, 0.80, 0.70);
const float kSunDistance  = 4.0;     /* shadow trace length for the sun */

/* Cheap deterministic 2D hash → [-1, 1] vec2. Used to jitter shadow
 * sample positions per pixel + per light + per sample. */
vec2 hash2(vec3 seed) {
    float n = dot(seed, vec3(127.1, 311.7, 74.7));
    return vec2(fract(sin(n) * 43758.5453),
                fract(sin(n + 17.0) * 24634.6345)) * 2.0 - 1.0;
}

void main() {
    /* Reconstruct the three vertex indices for the hit triangle. */
    const uint i0 = idx.i[3 * gl_PrimitiveID + 0];
    const uint i1 = idx.i[3 * gl_PrimitiveID + 1];
    const uint i2 = idx.i[3 * gl_PrimitiveID + 2];

    Vertex v0 = verts.v[i0];
    Vertex v1 = verts.v[i1];
    Vertex v2 = verts.v[i2];

    /* attribs = (b1, b2); barycentric b0 = 1 - b1 - b2. */
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    const vec2 uv = bary.x * vec2(v0.u, v0.v)
                  + bary.y * vec2(v1.u, v1.v)
                  + bary.z * vec2(v2.u, v2.v);

    const float emissive = bary.x * v0.emissive
                         + bary.y * v1.emissive
                         + bary.z * v2.emissive;

    const vec3 hitPos = bary.x * vec3(v0.px, v0.py, v0.pz)
                      + bary.y * vec3(v1.px, v1.py, v1.pz)
                      + bary.z * vec3(v2.px, v2.py, v2.pz);

    /* Sample the diffuse atlas. The first (and only, for now)
     * sampled-image array element holds the world's diffuse atlas. */
    const vec4 diffuse = texture(sampler2D(atlasTextures[0], atlasSampler), uv);

    /* Discard fully-transparent texels (e.g. sprite halos) by
     * marking them as miss-equivalent — payload stays zero. The
     * RT pipeline doesn't support a true "transparent hit + continue",
     * but the alpha-cutoff path here is enough for the scaffold;
     * production would use any-hit + ignoreIntersectionEXT. */
    if (diffuse.a < 0.01) {
        payload.colour = vec3(0.0);
        payload.distance = -1.0;
        return;
    }

    /* Emissive surface — radiance equals the diffuse colour, no
     * shadow ray needed. Light orbs / lava tiles get this. */
    if (emissive > 0.5) {
        payload.colour = diffuse.rgb;
        payload.distance = gl_HitTEXT;
        return;
    }

    /* When the ray hit a sprite quad (anything past the first BG2
     * quad's 2 triangles), pretend we're shading the BG plane behind
     * it for shadow-trace purposes. Otherwise the sprite quad
     * appears as a brighter rectangle than the BG around it
     * (sprite-hit shadow rays at z=0.2 don't hit anything in front;
     * BG-hit rays at z=0.8 are occluded by the sprite quads, so the
     * sprite area is lit while surrounding BG is shadowed → visible
     * box). Shifting the shadow origin to z=0.8 unifies the lighting
     * across layers; the sprite still acts as a real shadow caster
     * for *other* BG pixels via the ray it occludes. */
    const bool isSprite = (gl_PrimitiveID >= 2);
    const vec3 shadeOrigin = isSprite ? vec3(hitPos.xy, 0.8) : hitPos;

    /* Sun key light — single shadow trace, no jitter (hard shadow). */
    const float t = pc.params.y;
    vec3 lit = kAmbient;
    {
        vec3 dir = -kSunDir;
        shadowPayload.hit = true;
        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT
                | gl_RayFlagsTerminateOnFirstHitEXT
                | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, 0, 0, 1,
            shadeOrigin + dir * 0.01, 0.0, dir, kSunDistance, 1
        );
        if (!shadowPayload.hit) lit += kSunColour;
    }

    /* Orbiting coloured point lights — disabled for the current
     * "subtle" preset. Setting kLightCount=0 above (or just
     * compile-time guarding this loop) turns the world back to
     * straight diffuse + sun shadow, which is far less busy. The
     * loop is kept structurally so re-enabling is a one-line change
     * when we want the disco vibe back. */
    #if 0
    for (int i = 0; i < kLightCount; ++i) {
        float phase  = (float(i) * 2.0 * kPi) / float(kLightCount);
        float speed  = 0.7 + 0.12 * float(i);   /* per-light orbit rate */
        float radius = 60.0 + 22.0 * sin(t * 0.4 + phase);  /* radius pulses */
        vec3  base   = lightPosition(t * speed, phase, radius);

        vec3  toLight   = base - hitPos;
        float lightDist = length(toLight);
        vec3  centerDir = toLight / lightDist;

        /* Falloff from 2D screen-XY distance — uniform across layers. */
        float dx       = base.x - hitPos.x;
        float dy       = base.y - hitPos.y;
        float dist2D2  = dx * dx + dy * dy;
        float falloff  = 1.0 / (1.0 + 0.0008 * dist2D2);
        if (falloff < 0.02) continue;

        /* Pulse light brightness on a per-light schedule so the
         * scene "breathes" even when occluders aren't moving. */
        float pulse = 0.7 + 0.3 * sin(t * 1.4 + phase * 2.0);

        /* Single hard shadow ray per light. Direction from the
         * shading origin (= BG plane for sprite hits, see comment
         * up top) toward the light's centre. */
        vec3 sToLight = base - shadeOrigin;
        float sDist   = length(sToLight);
        vec3  sDir    = sToLight / sDist;

        shadowPayload.hit = true;
        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT
                | gl_RayFlagsTerminateOnFirstHitEXT
                | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, 0, 0, 1,
            shadeOrigin + sDir * 0.01, 0.0, sDir, sDist, 1
        );
        float visible = shadowPayload.hit ? 0.0 : 1.0;

        lit += kLightColours[i] * falloff * pulse * visible;
    }
    #endif

    /* Brighten ambient since we removed the 6 point lights; otherwise
     * the scene is sun-only and very dim. */
    lit = max(lit, vec3(0.55));

    /* Reinhard tonemap on the accumulated colour — keeps the bright
     * light pile-ups from blowing out, and rolls highlights toward
     * white instead of clipping to neon. */
    vec3 raw = diffuse.rgb * lit;
    vec3 tonemapped = raw / (raw + vec3(1.0));
    payload.colour   = tonemapped;
    payload.distance = gl_HitTEXT;
}
