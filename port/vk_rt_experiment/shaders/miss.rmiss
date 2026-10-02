/* port/vk_rt_experiment/shaders/miss.rmiss
 *
 * Primary-ray miss. Paints the sky colour into the payload. A miss
 * shader can only declare a SINGLE rayPayloadInEXT — it must match
 * the layout(location) used by the invoking traceRayEXT call. For
 * shadow rays the right approach is a separate miss shader compiled
 * into its own SBT slot, not a dual-payload single shader (which is
 * invalid SPIR-V).
 *
 * The scaffold currently registers only this primary miss; the
 * rchit's shadow trace is disabled until the second miss is added.
 */
#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 colour;
    float distance;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    /* Light steel-blue sky — visible whenever a primary ray escapes
     * the scene. Useful as a sanity check: a fully blue screen with
     * no foreground means the rays trace but no geometry hits;
     * fully black means rgen isn't producing valid rays at all. */
    payload.colour = vec3(0.42, 0.55, 0.72);
    payload.distance = -1.0;
}
