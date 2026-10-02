/* port/vk_rt_experiment/shaders/shadow.rmiss
 *
 * Shadow-ray miss shader. Bound to SBT miss slot 1 (the primary
 * miss is at slot 0). Closest-hit's shadow trace passes
 * missIndex = 1, so this runs when the ray to the sun doesn't hit
 * any geometry — meaning the sun has line-of-sight to the hit
 * point and the surface is lit.
 *
 * The payload struct is `ShadowPayload`; matching `rayPayloadInEXT`
 * declaration must use location = 1 (matching closesthit.rchit's
 * trace call). */
#version 460
#extension GL_EXT_ray_tracing : require

struct ShadowPayload {
    bool hit;
};

layout(location = 1) rayPayloadInEXT ShadowPayload payload;

void main() {
    /* Reaching this shader means no occlusion on the way to the sun. */
    payload.hit = false;
}
