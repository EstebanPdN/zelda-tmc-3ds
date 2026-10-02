/* port/vk_rt_experiment/Engine.h
 *
 * Standalone Vulkan 1.3 + KHR ray-tracing engine wrapper for the
 * pseudo-3D-projected GBA frame. NOT linked into tmc_pc — this is an
 * experimental module that owns its own window, instance, and device.
 *
 * Lifecycle:
 *   Engine eng;
 *   if (!eng.init(960, 640, "TMC-RT")) return 1;
 *   while (eng.pumpEvents()) {
 *       eng.beginFrame();
 *       // ... record render commands via RayTracingPipeline ...
 *       eng.endFrame();
 *   }
 *   eng.shutdown();
 *
 * The Engine owns:
 *   - the SDL window + Vulkan surface
 *   - VkInstance + debug messenger (validation when built with TMC_VK_RT_VALIDATION)
 *   - the chosen VkPhysicalDevice (RT-capable, gating on the four extensions)
 *   - the VkDevice and command pool
 *   - per-frame command buffers and synchronisation primitives
 *   - the swapchain and its image views + a sampled-output image the
 *     ray tracer writes to (storage-image usage) and that we then blit
 *     into the swapchain in endFrame
 *
 * Style notes:
 *   - Vulkan struct initialisation is always explicit field-by-field
 *     (no `{}` with implicit zero) so the reader can see exactly which
 *     pNext chains and flags we set.
 *   - Every VkResult is checked. Failures throw vk::Error which the
 *     init() caller turns into a return-false + log.
 *   - The class is non-copyable / non-movable to keep handle ownership
 *     unambiguous.
 */
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace tmc_vkrt {

/* Frames in flight. Two is the standard PC-port balance: 1 frame can
 * be on the GPU while we record the next on the CPU, with predictable
 * pacing under vsync. Three frames adds latency for marginal
 * throughput gain when the GPU isn't already saturated. */
inline constexpr uint32_t kFramesInFlight = 2;

/* All Vulkan-error returns funnel through this exception type. The
 * Engine::init() outer catch logs the message and returns false so
 * callers don't have to wrap every step in try/catch themselves. */
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

/* Helper that turns a VkResult into an Error when it's not VK_SUCCESS.
 * Inline to keep call-site stack traces clean. */
inline void check(VkResult r, const char* where) {
    if (r != VK_SUCCESS) {
        throw Error(std::string(where) + ": VkResult=" + std::to_string((int)r));
    }
}

/* Bundle of per-frame primitives — the Engine maintains kFramesInFlight
 * copies and rotates between them. */
struct FrameSync {
    VkSemaphore imageAvailable = VK_NULL_HANDLE;  /* signaled when the swapchain image is ready */
    VkSemaphore renderFinished = VK_NULL_HANDLE;  /* signaled when the queue completes */
    VkFence     inFlight       = VK_NULL_HANDLE;  /* CPU-side wait so we don't overwrite this frame's resources */
    VkCommandBuffer cmd        = VK_NULL_HANDLE;  /* allocated from the primary pool */
};

class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /* Bring the engine up from nothing. Returns false on any failure
     * (no exceptions leak out). After init() returns true, all of the
     * accessors below are valid. */
    bool init(int width, int height, const char* title);

    /* SDL event pump. Returns false when the user closes the window
     * or presses Escape, signalling the main loop to exit. */
    bool pumpEvents();

    /* beginFrame waits for the previous instance of this frame slot to
     * finish, acquires the next swapchain image, and begins recording
     * into the frame's command buffer. The returned VkCommandBuffer is
     * ready for the caller to record into. Returns VK_NULL_HANDLE if
     * the swapchain is out of date (window resized / minimised); the
     * caller should skip the frame and let the next pumpEvents() drive
     * a recreate. */
    VkCommandBuffer beginFrame();

    /* endFrame records the swapchain blit from the storage image
     * (the ray tracer's output), ends the command buffer, submits to
     * the graphics queue, and presents. Pairs 1:1 with beginFrame. */
    void endFrame();

    /* Caller hands raw uint32_t buffers (e.g. from RayTracingPipeline)
     * that we'll keep alive until shutdown. Useful for SBT allocations
     * the engine outlives but doesn't own conceptually. */
    void retainBuffer(VkBuffer buf, VkDeviceMemory mem);

    /* Tear everything down. Idempotent — safe to call from the
     * destructor even after a failed init(). */
    void shutdown();

    /* Accessors — used by RayTracingPipeline and RenderLayerManager to
     * allocate against the same device + queue. */
    VkInstance       instance()       const { return mInstance; }
    VkPhysicalDevice physicalDevice() const { return mPhysicalDevice; }
    VkDevice         device()         const { return mDevice; }
    uint32_t         graphicsFamily() const { return mGraphicsFamily; }
    VkQueue          graphicsQueue()  const { return mGraphicsQueue; }
    VkCommandPool    commandPool()    const { return mCommandPool; }
    uint32_t         swapchainWidth() const { return mSwapchainExtent.width; }
    uint32_t         swapchainHeight()const { return mSwapchainExtent.height; }
    VkImage          storageImage()   const { return mStorageImage; }
    VkImageView      storageImageView()const{ return mStorageImageView; }
    VkImage          accumImage()     const { return mAccumImage; }
    VkImageView      accumImageView() const { return mAccumImageView; }

    /* Slice-7 denoiser guide buffers — populated by the rgen at the
     * primary hit, read by the compute filter's edge-stopping weights. */
    VkImage     gNormalImage()     const { return mGNormalImage; }
    VkImageView gNormalImageView() const { return mGNormalImageView; }
    VkImage     gHitPosImage()     const { return mGHitPosImage; }
    VkImageView gHitPosImageView() const { return mGHitPosImageView; }
    /* Ping-pong scratch for the multi-pass a-trous filter. */
    VkImage     denoiseScratchImage()     const { return mDenoiseScratchImage; }
    VkImageView denoiseScratchImageView() const { return mDenoiseScratchImageView; }

    uint32_t         currentFrameIndex() const { return mCurrentFrame; }

    /* Returned by enumeratePhysicalDevice; the ray-tracing pipeline
     * needs these properties to compute SBT alignment. */
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties() const {
        return mRtProperties;
    }

    /* Allocate a device-memory-backed buffer with usage flags and
     * memory properties of the caller's choosing. Used by both the
     * acceleration-structure code and the SBT. Returns both handles —
     * caller owns destruction unless they hand them back via
     * retainBuffer(). */
    void allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags props,
                        VkBuffer* outBuffer, VkDeviceMemory* outMemory);

    /* Look up a memory-type index matching the requested filter (e.g.
     * the BLAS scratch buffer needs a type that's allowed by the
     * device + meets HOST_VISIBLE | HOST_COHERENT or DEVICE_LOCAL
     * properties). Returns UINT32_MAX if no match. */
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    /* For one-shot host→device or layout transitions, lambda gets a
     * fresh command buffer it can record into; we submit + wait. */
    template <class Fn>
    void oneShot(Fn&& fn) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = mCommandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(mDevice, &ai, &cmd), "vkAllocateCommandBuffers (oneShot)");

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer (oneShot)");

        fn(cmd);

        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (oneShot)");

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        check(vkQueueSubmit(mGraphicsQueue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit (oneShot)");
        check(vkQueueWaitIdle(mGraphicsQueue), "vkQueueWaitIdle (oneShot)");
        vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmd);
    }

private:
    /* Step-by-step init helpers. Each throws Error on failure; init()
     * catches and returns false. */
    void createWindow(int width, int height, const char* title);
    void createInstance();
    void createSurface();
    void selectPhysicalDevice();
    void createDevice();
    void createCommandPool();
    void createSwapchain();
    void createStorageImage();
    void createFrameSync();
    void recreateSwapchain();

    /* Helpers for image layout transitions used by storageImage
     * setup and the per-frame blit. */
    void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout from, VkImageLayout to,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    /* Owned state */
    SDL_Window*      mWindow = nullptr;
    VkInstance       mInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR     mSurface = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice         mDevice = VK_NULL_HANDLE;
    uint32_t         mGraphicsFamily = 0;
    VkQueue          mGraphicsQueue = VK_NULL_HANDLE;
    VkCommandPool    mCommandPool = VK_NULL_HANDLE;

    VkSwapchainKHR   mSwapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR mSwapchainFormat{};
    VkExtent2D       mSwapchainExtent{};
    std::vector<VkImage>     mSwapchainImages;
    std::vector<VkImageView> mSwapchainViews;

    /* Storage image — the ray tracer writes here, then we blit to the
     * swapchain. RGBA8 to match what closest-hit produces. */
    VkImage          mStorageImage = VK_NULL_HANDLE;
    VkDeviceMemory   mStorageImageMemory = VK_NULL_HANDLE;
    VkImageView      mStorageImageView = VK_NULL_HANDLE;

    /* Accumulation image — HDR running mean for path-tracer
     * progressive sampling. rgba32f so emissive radiance >1 doesn't
     * clip; rebuilt alongside the storage image on swapchain
     * resize. Stays in GENERAL layout — the shader does in-place
     * read/write each frame. */
    VkImage          mAccumImage = VK_NULL_HANDLE;
    VkDeviceMemory   mAccumImageMemory = VK_NULL_HANDLE;
    VkImageView      mAccumImageView = VK_NULL_HANDLE;

    /* Slice-7 denoiser auxiliaries — created alongside the accum image
     * because they share its dimensions and lifecycle.  All RGBA32F so
     * the rgen can store unbounded HDR / world-space without loss. */
    VkImage          mGNormalImage = VK_NULL_HANDLE;
    VkDeviceMemory   mGNormalImageMemory = VK_NULL_HANDLE;
    VkImageView      mGNormalImageView = VK_NULL_HANDLE;
    VkImage          mGHitPosImage = VK_NULL_HANDLE;
    VkDeviceMemory   mGHitPosImageMemory = VK_NULL_HANDLE;
    VkImageView      mGHitPosImageView = VK_NULL_HANDLE;
    VkImage          mDenoiseScratchImage = VK_NULL_HANDLE;
    VkDeviceMemory   mDenoiseScratchImageMemory = VK_NULL_HANDLE;
    VkImageView      mDenoiseScratchImageView = VK_NULL_HANDLE;

    std::array<FrameSync, kFramesInFlight> mFrames{};
    uint32_t         mCurrentFrame = 0;
    uint32_t         mImageIndex = 0;
    bool             mSwapchainOutOfDate = false;

    /* RT properties — needed by SBT alignment math. */
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR mRtProperties{};

    /* Retained allocations — held alive until shutdown(). */
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> mRetained;
};

}  /* namespace tmc_vkrt */
