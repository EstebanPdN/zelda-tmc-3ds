/* port/vk_rt_experiment/DenoisePipeline.cpp */
#include "DenoisePipeline.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace tmc_vkrt {

namespace {
class DenoiseError : public std::runtime_error {
public:
    explicit DenoiseError(const std::string& m) : std::runtime_error(m) {}
};
void dCheck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s: VkResult=%d", what, (int)r);
        throw DenoiseError(buf);
    }
}
}  /* anonymous namespace */

DenoisePipeline::DenoisePipeline(Engine& eng) : mEngine(eng) {}

DenoisePipeline::~DenoisePipeline() {
    if (mPipeline)            vkDestroyPipeline(mEngine.device(), mPipeline, nullptr);
    if (mPipelineLayout)      vkDestroyPipelineLayout(mEngine.device(), mPipelineLayout, nullptr);
    if (mDescriptorPool)      vkDestroyDescriptorPool(mEngine.device(), mDescriptorPool, nullptr);
    if (mDescriptorSetLayout) vkDestroyDescriptorSetLayout(mEngine.device(), mDescriptorSetLayout, nullptr);
}

std::vector<char> DenoisePipeline::loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw DenoiseError("denoise: cannot open " + path);
    size_t n = (size_t)f.tellg();
    std::vector<char> buf(n);
    f.seekg(0);
    f.read(buf.data(), n);
    return buf;
}

void DenoisePipeline::createPipeline(const std::string& shaderDir) {
    /* Five bindings, all storage images:
     *   0 srcImage  (read)
     *   1 dstImage  (write)
     *   2 gNormal   (read; primary-hit normal)
     *   3 gHitPos   (read; primary-hit world pos)
     *   4 accumImage (read; original noisy HDR — used as the colour
     *                 reference for edge-stop weights so successive
     *                 passes don't over-blur) */
    VkDescriptorSetLayoutBinding bindings[5]{};
    for (int i = 0; i < 5; ++i) {
        bindings[i].binding         = (uint32_t)i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 5;
    li.pBindings    = bindings;
    dCheck(vkCreateDescriptorSetLayout(mEngine.device(), &li, nullptr, &mDescriptorSetLayout),
          "denoise vkCreateDescriptorSetLayout");

    /* Push constants: stride (uint), exposure (float), bool finalPass (uint). */
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.size       = 16;  /* 4 uints worth */

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &mDescriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    dCheck(vkCreatePipelineLayout(mEngine.device(), &plci, nullptr, &mPipelineLayout),
          "denoise vkCreatePipelineLayout");

    /* Compute pipeline */
    auto spv = loadSpv(shaderDir + "/denoise.comp.spv");
    VkShaderModuleCreateInfo smci{};
    smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spv.size();
    smci.pCode    = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule sm = VK_NULL_HANDLE;
    dCheck(vkCreateShaderModule(mEngine.device(), &smci, nullptr, &sm), "denoise vkCreateShaderModule");

    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = sm;
    cpi.stage.pName  = "main";
    cpi.layout = mPipelineLayout;
    dCheck(vkCreateComputePipelines(mEngine.device(), VK_NULL_HANDLE, 1, &cpi, nullptr, &mPipeline),
          "vkCreateComputePipelines");
    vkDestroyShaderModule(mEngine.device(), sm, nullptr);

    /* Descriptor pool: 5 storage images × 3 sets = 15 */
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps.descriptorCount = 5 * 3;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets       = 3;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &ps;
    dCheck(vkCreateDescriptorPool(mEngine.device(), &dpci, nullptr, &mDescriptorPool),
          "denoise vkCreateDescriptorPool");

    /* Allocate the 3 sets. */
    VkDescriptorSetLayout layouts[3] = { mDescriptorSetLayout, mDescriptorSetLayout, mDescriptorSetLayout };
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = mDescriptorPool;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts        = layouts;
    dCheck(vkAllocateDescriptorSets(mEngine.device(), &ai, mSets), "vkAllocateDescriptorSets");

    /* Wire up the two ping-pong passes:
     *   pass 0: src=accum    dst=scratch  (stride 1, HDR preserved)
     *   pass 1: src=scratch  dst=output   (stride 2, tonemap → LDR)
     *
     * Two passes (vs. SVGF's full 5) keeps intermediates in rgba32f
     * scratch — the LDR output is only written once, on the final
     * pass after Reinhard tonemap.  Otherwise an intermediate write
     * into the rgba8 outputImage would clamp HDR signal to 1.0,
     * causing visible over-brightening on bright surfaces.
     *
     * All passes also bind gNormal, gHitPos, and accumImage (the
     * latter as the colour-reference for edge-stop weights).
     */
    auto wire = [&](int passIdx, VkImageView src, VkImageView dst) {
        VkDescriptorImageInfo info[5]{};
        info[0] = { VK_NULL_HANDLE, src,                          VK_IMAGE_LAYOUT_GENERAL };
        info[1] = { VK_NULL_HANDLE, dst,                          VK_IMAGE_LAYOUT_GENERAL };
        info[2] = { VK_NULL_HANDLE, mEngine.gNormalImageView(),   VK_IMAGE_LAYOUT_GENERAL };
        info[3] = { VK_NULL_HANDLE, mEngine.gHitPosImageView(),   VK_IMAGE_LAYOUT_GENERAL };
        info[4] = { VK_NULL_HANDLE, mEngine.accumImageView(),     VK_IMAGE_LAYOUT_GENERAL };

        VkWriteDescriptorSet writes[5]{};
        for (int b = 0; b < 5; ++b) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = mSets[passIdx];
            writes[b].dstBinding      = (uint32_t)b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[b].pImageInfo      = &info[b];
        }
        vkUpdateDescriptorSets(mEngine.device(), 5, writes, 0, nullptr);
    };
    wire(0, mEngine.accumImageView(),           mEngine.denoiseScratchImageView());
    wire(1, mEngine.denoiseScratchImageView(),  mEngine.storageImageView());
    /* mSets[2] is allocated but unused — kept for future extension. */
}

void DenoisePipeline::dispatch(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);

    const uint32_t gx = (width  + 7) / 8;
    const uint32_t gy = (height + 7) / 8;

    struct PC {
        uint32_t stride;
        float    exposure;
        uint32_t finalPass;
        uint32_t pad;
    };

    /* Two a-trous passes at increasing stride.  Final pass tonemaps
     * and writes to storage image (which Engine::endFrame blits to
     * the swapchain). */
    const uint32_t strides[2]    = { 1u, 2u };
    const uint32_t finalFlags[2] = { 0u, 1u };

    for (int p = 0; p < 2; ++p) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                mPipelineLayout, 0, 1, &mSets[p], 0, nullptr);
        PC pc { strides[p], 0.8f, finalFlags[p], 0u };
        vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, gx, gy, 1);

        /* Memory barrier between passes: write of dst image must
         * complete before next pass reads it. */
        if (p < 1) {
            VkMemoryBarrier mb{};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }
}

}  /* namespace tmc_vkrt */
