/* port/vk_rt_experiment/RenderLayerManager.h
 *
 * Translation layer that converts the GBA port's 2D draw stream
 * (OAM sprites, BG quads, UI cells) into 3D-positioned textured
 * quads for the ray tracer.
 *
 * Each call to drawSprite/drawBgQuad adds 4 vertices + 6 indices to
 * the per-frame batch. At the end of the frame:
 *   - flushToBuffers() uploads the batch into device-local VkBuffers
 *     suitable for use as BLAS geometry (VK_BUFFER_USAGE_VERTEX_BUFFER
 *     | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY
 *     | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS).
 *   - quadCount() reports the number of quads for the BLAS builder.
 *
 * Z assignment per layer follows the prompt spec:
 *   - UI/HUD                  → 0.0
 *   - Sprites/entities (Link) → 0.2
 *   - BG0 / foreground        → 0.4
 *   - BG1 / midground         → 0.6
 *   - BG2 / background        → 0.8
 * Larger Z = farther from the orthographic camera (which looks down
 * the +Z axis). The 0.2 spacing prevents Z-fighting at 32-bit float
 * precision out to comfortable scene sizes.
 *
 * Emissive flag per-quad lets the closest-hit shader treat lava
 * tiles, light orbs, etc. as light sources for the lighting model.
 */
#pragma once

#include "Engine.h"

#include <cstdint>
#include <vector>

namespace tmc_vkrt {

enum class Layer : uint32_t {
    UI         = 0,
    Sprite     = 1,
    BG0        = 2,
    BG1        = 3,
    BG2        = 4,
    Count
};

/* Z value per layer, indexable by Layer cast to uint32_t.
 * Kept as a constexpr array so the compiler folds the table lookup. */
inline constexpr float kLayerZ[(size_t)Layer::Count] = {
    /* UI     */ 0.0f,
    /* Sprite */ 0.2f,
    /* BG0    */ 0.4f,
    /* BG1    */ 0.6f,
    /* BG2    */ 0.8f,
};

/* The per-vertex layout the BLAS builder and the closest-hit shader
 * both reference. POSITION at offset 0 matches the VkAccelerationStructure
 * GeometryTrianglesData expectation of "the first 3 floats of each
 * vertex are the position". UV and the emissive flag follow; the
 * shader picks them up via descriptor-bound storage buffers.
 *
 * Total size: 24 bytes per vertex (6 floats). 4 vertices per quad =
 * 96 bytes, 16-byte aligned by design so the per-quad write stays
 * cache-friendly. */
struct Vertex {
    float position[3];      /* world-space xyz; z = kLayerZ[layer] */
    float uv[2];            /* per-vertex UV into the diffuse atlas */
    float emissive;         /* 0.0 normal, 1.0 emissive (treated as light) */
};
static_assert(sizeof(Vertex) == 24, "Vertex layout drifted — shader binding will misalign");

/* The BLAS builder takes triangles, not quads — each quad expands to
 * two CCW triangles via these six indices. */
inline constexpr uint32_t kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

/* Live data describing the per-frame batch. After flushToBuffers(),
 * the buffer/memory handles become owned by the RenderLayerManager
 * until reset() is called or the manager is destroyed. */
struct GeometryBuffers {
    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer       indexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
    uint32_t       vertexCount  = 0;
    uint32_t       indexCount   = 0;
    /* Device addresses captured at allocation time so the BLAS-build
     * code doesn't need to re-query. */
    VkDeviceAddress vertexAddr  = 0;
    VkDeviceAddress indexAddr   = 0;
};

class RenderLayerManager {
public:
    explicit RenderLayerManager(Engine& eng);
    ~RenderLayerManager();

    RenderLayerManager(const RenderLayerManager&) = delete;
    RenderLayerManager& operator=(const RenderLayerManager&) = delete;

    /* Reset the staging vectors at the start of a new frame. Doesn't
     * destroy the GPU buffers from the previous frame — flushToBuffers
     * frees those before reuploading. */
    void beginFrame();

    /* Add a textured quad at (x, y) — pixel coordinates, top-left
     * origin to match the GBA's screen space — with the given pixel
     * size and UV rect. The Z component comes from the layer table.
     *
     * uvU0..1, uvV0..1 are normalised 0..1 UVs into the diffuse atlas.
     * `emissive` non-zero marks the quad as a light source. */
    void drawSprite(Layer layer, float x, float y, float w, float h,
                    float uvU0, float uvV0, float uvU1, float uvV1,
                    bool emissive = false);

    /* Convenience: draw a full-screen BG quad covering the GBA frame
     * at the layer's Z. The atlas UVs are passed in directly to let
     * the BG-tile assembly fragment-pack into the atlas as it wishes. */
    void drawBgQuad(Layer layer,
                    float uvU0, float uvV0, float uvU1, float uvV1);

    /* Upload the staged vertices/indices to GPU device-local buffers.
     * After return, `buffers()` reports the freshly-allocated handles.
     * Frees the previous frame's buffers. Safe to call when the batch
     * is empty (it'll free old buffers and leave the new ones null). */
    void flushToBuffers();

    /* Read-only accessor for the current GPU buffers (post-flush). */
    const GeometryBuffers& buffers() const { return mBuffers; }

    /* Number of quads currently staged (host-side). Useful for the
     * BLAS-builder to know how many primitives to declare. */
    uint32_t quadCount() const { return (uint32_t)(mVertices.size() / 4); }

    /* For the closest-hit shader: stride between Vertex entries in
     * bytes. Matches sizeof(Vertex). Public so RayTracingPipeline can
     * pass it into the geometry data struct. */
    static constexpr VkDeviceSize vertexStride() { return sizeof(Vertex); }

    /* Slice-11 perf: capacity of the persistent buffers in quads.
     * 256 quads = 1024 verts × 24B = 24 KB vertex, 1.5 KB index, 30 KB
     * total — small enough to keep resident without thinking about
     * memory.  Per-frame batches stay well under this in practice
     * (back composite + ≤128 OAM sprites). */
    static constexpr uint32_t kMaxQuads = 256;

private:
    /* Generate one quad worth of vertices into `mVertices`. */
    void emitQuad(Layer layer, float x, float y, float w, float h,
                  float uvU0, float uvV0, float uvU1, float uvV1, bool emissive);

    /* Free the GPU buffers, zero out GeometryBuffers. */
    void freeBuffers();

    /* Slice 11: lazy allocate the persistent device + staging buffers
     * on the first flush. Sized for kMaxQuads, never reallocated. */
    void ensurePersistentBuffers();

    /* Host-side staging — re-filled every frame. */
    std::vector<Vertex>   mVertices;
    std::vector<uint32_t> mIndices;

    Engine&          mEngine;
    GeometryBuffers  mBuffers{};

    /* Slice-11 persistent host-visible staging.  Kept mapped so the
     * per-frame upload is one memcpy + one vkCmdCopyBuffer instead
     * of alloc/map/copy/unmap/free. */
    VkBuffer        mStagingVertex     = VK_NULL_HANDLE;
    VkDeviceMemory  mStagingVertexMem  = VK_NULL_HANDLE;
    void*           mStagingVertexMap  = nullptr;
    VkBuffer        mStagingIndex      = VK_NULL_HANDLE;
    VkDeviceMemory  mStagingIndexMem   = VK_NULL_HANDLE;
    void*           mStagingIndexMap   = nullptr;
    bool            mPersistentReady   = false;
};

}  /* namespace tmc_vkrt */
