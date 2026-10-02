/* port/vk_rt_experiment/RayTracingPipeline.h
 *
 * Owns the ray-tracing-specific Vulkan objects:
 *   - One BLAS per per-frame geometry batch (built from
 *     RenderLayerManager's vertex/index buffers).
 *   - One TLAS holding a single instance pointing at the BLAS, with
 *     an identity transform.
 *   - Pipeline layout + ray-tracing pipeline (rgen + rmiss + rchit
 *     stages, one shader group each).
 *   - Shader binding table (SBT) — three regions (raygen, miss, hit)
 *     each aligned to rt.shaderGroupBaseAlignment.
 *   - Descriptor set bindings:
 *       binding 0: VkAccelerationStructureKHR (TLAS)
 *       binding 1: VkImage   (storage image — RT writes here)
 *       binding 2: VkBuffer  (vertex storage buffer, read-only)
 *       binding 3: VkBuffer  (index storage buffer, read-only)
 *       binding 4: VkImage[] (texture-atlas sampled image — for now
 *                              we bind exactly one; descriptor-indexing
 *                              lets future code expand to N atlases)
 *       binding 5: VkSampler (linear-clamp sampler for the atlas)
 *
 * Lifecycle:
 *   RayTracingPipeline rt(engine, layers);
 *   rt.createPipeline("path/to/shaders/");        // compiles SPIR-V
 *   rt.setAtlas(atlasImageView, sampler);
 *   ... per frame:
 *   rt.rebuildAS(cmd);                            // BLAS + TLAS
 *   rt.dispatchRays(cmd, w, h);                   // vkCmdTraceRaysKHR
 *
 * SPIR-V is loaded from disk at createPipeline() time — the .rgen
 * and .rchit at the bottom of this directory compile to .spv via
 * glslangValidator (the experiment's Makefile snippet handles that).
 */
#pragma once

#include "Engine.h"
#include "RenderLayerManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tmc_vkrt {

class RayTracingPipeline {
public:
    RayTracingPipeline(Engine& eng, RenderLayerManager& layers);
    ~RayTracingPipeline();

    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;

    /* Load SPIR-V from `shaderDir/raygen.rgen.spv` etc., create the
     * descriptor-set layout, pipeline layout, ray-tracing pipeline,
     * and shader binding table.  Throws Error on failure. */
    void createPipeline(const std::string& shaderDir);

    /* Bind the three texture-array entries the path tracer's rchit
     * reads through bindings 4 (diffuse), 7 (emissive), 8 (normal).
     * The sampler at binding 5 is shared. The first call is required
     * before the first dispatchRays(); subsequent calls just update
     * the views (next rebuildAS' descriptor write picks them up).
     * Pass VK_NULL_HANDLE for emissive / normal to use a 1×1 fallback
     * (the path tracer treats absent emissive as zero radiance and
     * absent normal map as the geometric normal). */
    void setAtlas(VkImageView diffuseView, VkSampler atlasSampler,
                  VkImageView emissiveView = VK_NULL_HANDLE,
                  VkImageView normalView   = VK_NULL_HANDLE);

    /* Build BLAS from the layer manager's current vertex/index
     * buffers, then build/refit TLAS over it. Records all commands
     * into `cmd` — caller is responsible for the surrounding
     * vkBeginCommandBuffer/vkEndCommandBuffer + queue submission.
     *
     * `cmd` MUST be outside any render pass when this is called
     * (acceleration-structure build commands cannot nest in render
     * passes). */
    void rebuildAS(VkCommandBuffer cmd);

    /* Issue vkCmdTraceRaysKHR for a `width × height` ray grid. The
     * RT pipeline must have been created and the AS must have been
     * built at least once. `time` is the wall-clock seconds the
     * rgen/rchit use to animate the moving point lights — pass
     * 0 to disable animation. */
    void dispatchRays(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                      uint32_t frameIndex, float time);

    /* GPU side per-frame point lights (extracted from the OAM-only
     * sprite plane by the consumer).  Each entry is 8 floats:
     *   pos.xyz (12B) + radius (4B) + radiance.rgb (12B) + intensity (4B)
     * The shader picks one at random per pixel for direct illumination
     * (importance-sampled). updateLights() memcpys into the persistent
     * device-local storage buffer; pass 0 lights to disable. */
    struct Light {
        float posX, posY, posZ;
        float radius;
        float r, g, b;
        float intensity;
    };
    static constexpr uint32_t kMaxLights = 32;
    void updateLights(VkCommandBuffer cmd, const Light* lights, uint32_t count);

private:
    /* SPIR-V loader. Throws Error if the file is missing or malformed. */
    std::vector<char> loadSpv(const std::string& path);

    /* Single-purpose helpers split out of createPipeline for clarity. */
    void createDescriptorLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void createPipelineLayout();
    void createRayTracingPipeline(const std::string& shaderDir);
    void createShaderBindingTable();

    /* AS-build helpers. */
    void destroyAS();
    void buildBLAS(VkCommandBuffer cmd);
    void buildTLAS(VkCommandBuffer cmd);

    /* Owned state */
    Engine&              mEngine;
    RenderLayerManager&  mLayers;

    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      mDescriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       mDescriptorSet       = VK_NULL_HANDLE;
    VkPipelineLayout      mPipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            mPipeline            = VK_NULL_HANDLE;

    /* SBT — one buffer with three named regions inside. The strided
     * device address regions get computed in createShaderBindingTable
     * and reused by dispatchRays. */
    VkBuffer       mSbtBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mSbtMemory = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR mRgenRegion{};
    VkStridedDeviceAddressRegionKHR mMissRegion{};
    VkStridedDeviceAddressRegionKHR mHitRegion{};
    VkStridedDeviceAddressRegionKHR mCallRegion{};  /* unused, kept for symmetry */

    /* Acceleration structures */
    VkAccelerationStructureKHR mBlas = VK_NULL_HANDLE;
    VkBuffer       mBlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mBlasMemory = VK_NULL_HANDLE;
    VkAccelerationStructureKHR mTlas = VK_NULL_HANDLE;
    VkBuffer       mTlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mTlasMemory = VK_NULL_HANDLE;

    /* Scratch buffer for AS builds — reused across BLAS and TLAS;
     * sized to the max requirement. */
    VkBuffer       mScratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mScratchMemory = VK_NULL_HANDLE;
    VkDeviceSize   mScratchSize   = 0;

    /* TLAS instance buffer — host-visible because vkCmdBuildAccelerationStructures
     * reads it through a device address; we update its contents each
     * frame with the current per-instance transform. */
    VkBuffer       mInstancesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mInstancesMemory = VK_NULL_HANDLE;

    /* Point-light storage buffer (binding 9). Capacity = kMaxLights;
     * the active count is sent in the push constants each dispatch. */
    VkBuffer        mLightsBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory  mLightsMemory        = VK_NULL_HANDLE;
    VkBuffer        mLightsStaging       = VK_NULL_HANDLE;
    VkDeviceMemory  mLightsStagingMem    = VK_NULL_HANDLE;
    void*           mLightsStagingMap    = nullptr;
    uint32_t        mLightCount          = 0;
    void createLightsBuffer();

    /* Atlas bindings (set externally via setAtlas) */
    VkImageView mAtlasView      = VK_NULL_HANDLE;
    VkSampler   mAtlasSampler   = VK_NULL_HANDLE;
    VkImageView mEmissiveView   = VK_NULL_HANDLE;
    VkImageView mNormalView     = VK_NULL_HANDLE;
    bool        mAtlasBound     = false;

    /* Dynamic-loader pointers — these RT entry points aren't in the
     * core dispatch table because they come from KHR extensions. We
     * fetch them via vkGetDeviceProcAddr at pipeline-create time. */
    PFN_vkCreateAccelerationStructureKHR              pfnCreateAS              = nullptr;
    PFN_vkDestroyAccelerationStructureKHR             pfnDestroyAS             = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR       pfnGetBuildSizes         = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR           pfnCmdBuildAS            = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR    pfnGetASDeviceAddress    = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR                pfnCreateRTPipelines     = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR          pfnGetShaderGroupHandles = nullptr;
    PFN_vkCmdTraceRaysKHR                             pfnCmdTraceRays          = nullptr;
};

}  /* namespace tmc_vkrt */
