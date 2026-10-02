/* port/vk_rt_experiment/DenoisePipeline.h
 *
 * Slice-7 denoiser: a compute-shader edge-stopping a-trous wavelet
 * filter that runs after the RT dispatch and produces the final LDR
 * output image.
 *
 * Inputs (all VK_FORMAT_R32G32B32A32_SFLOAT, swapchain-sized):
 *   - accumImage    HDR running mean from the rgen (temporal accum)
 *   - gNormal       per-pixel primary-hit normal (world space)
 *   - gHitPos       per-pixel primary-hit world position
 *   - scratchImage  ping-pong target between passes
 *
 * Output:
 *   - storageImage  LDR RGBA8, blitted to swapchain by Engine::endFrame
 *
 * Three passes with strides 1, 2, 4 implement the a-trous wavelet.
 * Ping-pong cycle (per dispatch):
 *   pass 0: accum    -> scratch     (stride 1, no tonemap)
 *   pass 1: scratch  -> accum-copy? no -> scratch alternates with
 *           accum.  We can NOT write back into accum without
 *           destroying the temporal history, so instead we treat
 *           accum as read-only and ping-pong between scratch +
 *           outputImage.  The final pass tonemaps into outputImage.
 *
 * Effective ping-pong using one scratch + outputImage:
 *   pass 0: accum    -> outputImage  (stride 1, HDR)  <- not tonemap yet
 *   pass 1: output   -> scratch      (stride 2, HDR)
 *   pass 2: scratch  -> output       (stride 4, HDR + Reinhard tonemap)
 *
 * The push constant carries: stride, exposure, bool isFinalPass.
 */
#pragma once

#include "Engine.h"
#include <string>

namespace tmc_vkrt {

class DenoisePipeline {
public:
    explicit DenoisePipeline(Engine& eng);
    ~DenoisePipeline();

    DenoisePipeline(const DenoisePipeline&) = delete;
    DenoisePipeline& operator=(const DenoisePipeline&) = delete;

    /* Load the compute SPIR-V from `shaderDir/denoise.comp.spv` and
     * create the descriptor layout, pipeline, and three descriptor
     * sets (one per pass with different src/dst image bindings). */
    void createPipeline(const std::string& shaderDir);

    /* Record the three a-trous passes into cmd.  Caller is responsible
     * for ensuring accum/gNormal/gHitPos have been written and barrier-
     * coordinated before this call.  Output ends up in the engine's
     * storage image, ready for the swapchain blit. */
    void dispatch(VkCommandBuffer cmd, uint32_t width, uint32_t height);

private:
    std::vector<char> loadSpv(const std::string& path);

    Engine&               mEngine;
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      mPipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            mPipeline            = VK_NULL_HANDLE;
    VkDescriptorPool      mDescriptorPool      = VK_NULL_HANDLE;
    /* Three sets — one per pass.  Each binds different src/dst pairs
     * so we don't have to rebind / vkUpdateDescriptorSets between
     * dispatches inside the same cmd. */
    VkDescriptorSet       mSets[3]{};
};

}  /* namespace tmc_vkrt */
