/* port/vk_rt_experiment/shaders/path_trace_miss.rmiss
 *
 * Miss shader for the path tracer. The rgen pre-sets
 *   payload.hitDistance = -1.0
 * before each traceRayEXT call, so when this miss runs we don't
 * need to write anything — the rgen reads hitDistance < 0 as
 * "ray escaped, add sky contribution and terminate the path".
 *
 * Kept as a separate file (not reusing the existing miss.rmiss)
 * because that one targets the old non-PT HitPayload struct; the
 * path tracer's PathPayload layout is different and binding the
 * wrong miss would let the shader write garbage into the payload's
 * memory slot. */
#version 460
#extension GL_EXT_ray_tracing : require

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

void main() {
    /* No-op: rgen already set hitDistance = -1.0 before the trace.
     * The miss running is the signal that we're in a "ray escaped"
     * state; the rgen reads hitDistance afterwards and routes to
     * its sky-sample branch. */
}
