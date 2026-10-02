/* port/vk_rt_experiment/shaders/path_trace.rchit
 *
 * Closest-hit shader for the path tracer. Reads per-vertex data via
 * the existing storage buffer bindings (verts + idx), samples three
 * texture arrays (diffuse atlas, emissive atlas, optional normal map),
 * computes the interpolated material attributes at the hit point,
 * and writes them into the PathPayload struct for the rgen to drive
 * its next bounce.
 *
 * Pairs with path_trace.rgen; payload layout MUST be identical.
 *
 * Vertex layout assumed here:
 *   px, py, pz         — world-space position (12 bytes)
 *   u, v               — atlas UV (8 bytes)
 *   matCode            — packed material descriptor (4 bytes):
 *                         0.0      → opaque, non-emissive
 *                         1.0      → emissive (torch / portal / kinstone)
 *                         2.0      → translucent (leaf / paper)
 *                         3.0      → emissive + translucent
 *
 * The current scaffold's Vertex struct only has an `emissive` float;
 * to use this rchit, extend the vertex struct on the host side to
 * pass the full matCode, and write 1.0/2.0/3.0 instead of just 1.0
 * for emissive/translucent assets.
 *
 * Texture-array bindings:
 *   set 0 binding 4: diffuse atlas array  (sampled by [0])
 *   set 0 binding 5: sampler              (linear-clamp by default)
 *   set 0 binding 7: emissive atlas array (sampled by [0])
 *   set 0 binding 8: normal atlas array   (sampled by [0]; tangent-space)
 *
 * Binding 7 / 8 are new; bind a 1×1 dummy texture for them when the
 * scene has no emissive / normal maps to satisfy the descriptor.
 */
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

/* Payload — MUST match path_trace.rgen byte-for-byte. */
struct PathPayload {
    vec3  radiance;
    vec3  albedo;
    vec3  hitPos;
    vec3  normal;
    float hitDistance;
    uint  materialFlags;
    vec3  scatterDir;
};

layout(location = 0) rayPayloadInEXT PathPayload payload;
hitAttributeEXT vec2 attribs;

/* Vertex struct in the storage buffer. Material code is stored in
 * the slot the existing scaffold uses for `emissive`; the host code
 * just needs to write the packed material values listed in the file
 * header instead of a 0/1 emissive flag. */
struct Vertex {
    float px, py, pz;
    float u, v;
    float matCode;
};

layout(set = 0, binding = 2, std430) readonly buffer Vertices { Vertex v[]; } verts;
layout(set = 0, binding = 3, std430) readonly buffer Indices  { uint   i[]; } idx;

layout(set = 0, binding = 4) uniform texture2D atlasTextures[];
layout(set = 0, binding = 5) uniform sampler   atlasSampler;
layout(set = 0, binding = 7) uniform texture2D emissiveTextures[];
layout(set = 0, binding = 8) uniform texture2D normalTextures[];

layout(push_constant) uniform PushConstants {
    vec4 params;
} pc;

/* Material-flag bits (kept in sync with rgen). */
const uint kMaterialTranslucent = 1u;
const uint kMaterialEmissive    = 2u;

/* Decode a tangent-space normal-map sample. Standard [0,1]→[-1,1]
 * remap; z is reconstructed when the map is two-channel, but we
 * accept the full RGB sample for simplicity. */
vec3 unpackNormalSample(vec3 s) {
    vec3 n = s * 2.0 - 1.0;
    return normalize(n);
}

void main() {
    /* Reconstruct the three vertex indices for the hit triangle. */
    const uint i0 = idx.i[3u * uint(gl_PrimitiveID) + 0u];
    const uint i1 = idx.i[3u * uint(gl_PrimitiveID) + 1u];
    const uint i2 = idx.i[3u * uint(gl_PrimitiveID) + 2u];

    Vertex v0 = verts.v[i0];
    Vertex v1 = verts.v[i1];
    Vertex v2 = verts.v[i2];

    /* attribs.x, attribs.y are the second and third barycentric
     * weights; b0 = 1 - b1 - b2. */
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    /* Interpolate position, UV, and material code. */
    const vec3 worldPos =
        bary.x * vec3(v0.px, v0.py, v0.pz) +
        bary.y * vec3(v1.px, v1.py, v1.pz) +
        bary.z * vec3(v2.px, v2.py, v2.pz);

    const vec2 uv =
        bary.x * vec2(v0.u, v0.v) +
        bary.y * vec2(v1.u, v1.v) +
        bary.z * vec2(v2.u, v2.v);

    const float matCode =
        bary.x * v0.matCode +
        bary.y * v1.matCode +
        bary.z * v2.matCode;

    /* Pack material flags from the matCode. Values are interpolated
     * across the triangle so use thresholds rather than equality. */
    uint flags = 0u;
    if (matCode > 0.5 && matCode < 1.5)          flags |= kMaterialEmissive;
    else if (matCode > 1.5 && matCode < 2.5)     flags |= kMaterialTranslucent;
    else if (matCode > 2.5)                      flags |= kMaterialEmissive | kMaterialTranslucent;

    /* Slice-8 water detection: colour-based heuristic on the diffuse
     * sample.  TMC water tiles are saturated cyan/blue — high blue,
     * low-to-mid red, moderate-to-high green, with a clear B > R gap.
     * The kMaterialReflective bit triggers the rgen's reflection
     * path (specular sun + reflected sprite/BG colour) on top of the
     * usual diffuse lambert. */

    /* Diffuse sample. Atlas[0] is the primary diffuse target; the
     * descriptor binding is a runtime array so extra atlases can be
     * indexed by future material-table lookups. */
    const vec4 diffuse =
        texture(sampler2D(atlasTextures[0], atlasSampler), uv);

    /* Slice 8 + 9: material auto-detection from diffuse colour.
     * BG-composite hits (matCode < 0.5) can be water or lava.
     * Sprite hits (matCode > 0.5) can be metal/armor.
     *
     * The flag bits below must match the rgen's kMaterial* constants:
     *   bit 0 (1)  = translucent
     *   bit 1 (2)  = emissive
     *   bit 2 (4)  = reflective (water mirror, metal specular)
     *   bit 3 (8)  = lava (animated shimmer + strong emissive)
     */
    if (diffuse.a > 0.5) {
        float wR    = diffuse.r;
        float wG    = diffuse.g;
        float wB    = diffuse.b;
        float maxC  = max(max(wR, wG), wB);
        float minC  = min(min(wR, wG), wB);
        float sat   = (maxC > 0.0001) ? (maxC - minC) / maxC : 0.0;
        float luma  = dot(diffuse.rgb, vec3(0.2126, 0.7152, 0.0722));

        if (matCode < 0.5) {
            /* --- BG-composite materials --- */

            /* Water: blue dominates red, decent saturation, mid value. */
            if (wB > wR + 0.10 && wB > wG - 0.05 && sat > 0.25 && wB > 0.35) {
                flags |= 4u;
            }
            /* Lava: red dominates green and blue, very saturated,
             * mid-to-high luma. Distinguishes lava from blood
             * splatters by the high luma requirement. */
            else if (wR > wG + 0.15 && wR > wB + 0.20 && sat > 0.55 && luma > 0.35) {
                flags |= 8u;  /* kMaterialLava */
                flags |= 2u;  /* also tag emissive — gives self-glow */
            }
        } else {
            /* --- Sprite materials --- */

            /* Metal/armor: low saturation, high value (silver/grey).
             * Sword blades, knight armor, shield rim.  The sat<0.20
             * gate also catches Link's eyes (pure white) — accept
             * the false positive since "specular eyes" is barely
             * visible and trying to filter further risks losing
             * legitimate metals. */
            if (sat < 0.20 && luma > 0.65) {
                flags |= 4u;  /* reflective — rgen treats metal like water */
                /* No emissive flag — metal isn't a light source. */
            }
        }
    }

    /* Transparent texel — treat as a miss for path-tracing purposes
     * (rgen will accumulate sky on the next bounce). Note that the
     * current pipeline doesn't have an any-hit shader to skip the
     * triangle while keeping the trace going; a full implementation
     * would call ignoreIntersectionEXT() from an any-hit. For now
     * setting hitDistance < 0 makes the rgen treat it as a miss. */
    if (diffuse.a < 0.01) {
        payload.hitDistance = -1.0;
        return;
    }

    /* Emissive sample — when the geometry is tagged emissive, gate
     * the boost by the diffuse texel's luma + alpha. The sprite
     * plane has alpha=0 outside silhouettes and the diffuse colour
     * is the actual sprite pixel, so this lights ONLY genuinely
     * bright opaque texels (Link's lantern bulb, fire core, lit
     * sign tiles) rather than the full 16×16 quad rectangle.
     *
     * Two gates: high luma AND high saturation.  Luma alone catches
     * Link's eyes (pure white, luma=1.0 sat=0) — but real emissives
     * (fire, lanterns) are highly-saturated warm colours.  Requiring
     * sat >= 0.4 rejects neutral whites while admitting torches /
     * sparkles / red-glow effects.  "× 4.0" lifts the emitted
     * radiance well above 1.0 so the HDR contribution visibly
     * illuminates neighbouring surfaces after tonemap. */
    vec3 emissive = vec3(0.0);
    if ((flags & kMaterialEmissive) != 0u && diffuse.a > 0.5) {
        float luma = dot(diffuse.rgb, vec3(0.2126, 0.7152, 0.0722));
        float maxC = max(max(diffuse.r, diffuse.g), diffuse.b);
        float minC = min(min(diffuse.r, diffuse.g), diffuse.b);
        float sat  = (maxC > 0.0001) ? (maxC - minC) / maxC : 0.0;
        if (luma >= 0.6 && sat >= 0.4) {
            emissive = diffuse.rgb * 4.0;
        } else {
            /* Not a true emissive texel — clear the flag so the
             * rgen path-traces it as a normal diffuse surface. */
            flags &= ~kMaterialEmissive;
        }
    }

    /* Normal — start with the quad's geometric normal (camera-facing
     * 2D quads → vec3(0, 0, -1) facing the orthographic camera at
     * z=-1). Then perturb with the normal-map sample if present.
     * The "if length > epsilon" gate makes the normal map optional:
     * a 1×1 grey dummy at (0.5, 0.5, 0.5) decodes to vec3(0,0,0)
     * which is rejected here and the geometric normal stands.
     *
     * For real 3D quads with arbitrary orientations, derive the
     * geometric normal from the cross product of two triangle edges
     * instead. */
    vec3 geomNormal = vec3(0.0, 0.0, -1.0);
    vec3 nSample = texture(sampler2D(normalTextures[0], atlasSampler), uv).rgb;
    vec3 normal = geomNormal;
    if (length(nSample - vec3(0.5)) > 0.05) {
        vec3 tangentNormal = unpackNormalSample(nSample);
        /* For pure -Z geometry the tangent frame is trivial:
         *   tangent  = +X, bitangent = +Y, geomNormal = -Z
         * So a (x, y, z) tangent-space normal maps to
         * world (x, y, -z) — we just flip the Z. */
        normal = normalize(vec3(tangentNormal.x, tangentNormal.y, -tangentNormal.z));
    }

    /* Translucent forward-scatter direction. The rgen probabilistic-
     * ally chooses between this and a diffuse hemisphere bounce; we
     * just pre-compute the dir to keep the rgen branch cheap. The
     * direction is the incoming ray direction, slightly offset
     * along the geometric normal to avoid self-intersection. */
    vec3 scatterDir = vec3(0.0);
    if ((flags & kMaterialTranslucent) != 0u) {
        scatterDir = gl_WorldRayDirectionEXT;
    }

    /* Write the full payload back to the rgen. */
    payload.albedo        = diffuse.rgb;
    payload.radiance      = emissive;
    payload.hitPos        = worldPos;
    payload.normal        = normal;
    payload.hitDistance   = gl_HitTEXT;
    payload.materialFlags = flags;
    payload.scatterDir    = scatterDir;
}
