/* port/vk_rt_experiment/Engine.cpp
 *
 * See Engine.h for the high-level lifecycle and intent. This file
 * implements the Vulkan boot sequence + per-frame loop in the same
 * order init() drives them: window → instance → surface → physical
 * device → logical device → command pool → swapchain → storage image
 * → frame sync.
 *
 * Each step throws Error on failure; init() catches and logs.
 */

#include "Engine.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

namespace tmc_vkrt {

/* Extensions the device MUST advertise, in the order our gating
 * code below queries them. The first four are the ray-tracing core
 * set per VK_KHR_ray_tracing_pipeline's preamble; the swapchain
 * extension is the standard presentation requirement. */
static const std::array<const char*, 6> kRequiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,  /* required by acceleration-structure */
};

#ifdef TMC_VK_RT_VALIDATION
static const std::array<const char*, 1> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    /* Skip verbose chatter; warn/error go to stderr so they show up
     * in CI logs without needing a debugger attached. */
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        std::fprintf(stderr, "[vk validation] %s\n", data->pMessage);
    }
    return VK_FALSE;
}
#endif

Engine::~Engine() {
    shutdown();
}

bool Engine::init(int width, int height, const char* title) {
    try {
        createWindow(width, height, title);
        createInstance();
        createSurface();
        selectPhysicalDevice();
        createDevice();
        createCommandPool();
        createSwapchain();
        createStorageImage();
        createFrameSync();
    } catch (const Error& e) {
        std::fprintf(stderr, "[vkrt] init failed: %s\n", e.what());
        shutdown();
        return false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[vkrt] init failed (std::exception): %s\n", e.what());
        shutdown();
        return false;
    }
    return true;
}

void Engine::createWindow(int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw Error(std::string("SDL_Init: ") + SDL_GetError());
    }
    mWindow = SDL_CreateWindow(title, width, height,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!mWindow) {
        throw Error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    }
}

void Engine::createInstance() {
    /* Gather the SDL3-required instance extensions for the surface,
     * then add VK_EXT_debug_utils when validation is enabled. */
    Uint32 sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (!sdlExts) {
        throw Error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
    }
    std::vector<const char*> exts(sdlExts, sdlExts + sdlExtCount);
#ifdef TMC_VK_RT_VALIDATION
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "TMC-RT";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "tmc_vk_rt_experiment";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{};
    ici.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo        = &appInfo;
    ici.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
#ifdef TMC_VK_RT_VALIDATION
    ici.enabledLayerCount   = static_cast<uint32_t>(kValidationLayers.size());
    ici.ppEnabledLayerNames = kValidationLayers.data();
#else
    ici.enabledLayerCount   = 0;
    ici.ppEnabledLayerNames = nullptr;
#endif

    check(vkCreateInstance(&ici, nullptr, &mInstance), "vkCreateInstance");

#ifdef TMC_VK_RT_VALIDATION
    /* Hook up the debug messenger so the validation layer's messages
     * route through debugCallback. */
    auto pfn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT");
    if (pfn) {
        VkDebugUtilsMessengerCreateInfoEXT dmci{};
        dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dmci.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dmci.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dmci.pfnUserCallback = debugCallback;
        check(pfn(mInstance, &dmci, nullptr, &mDebugMessenger),
              "vkCreateDebugUtilsMessengerEXT");
    }
#endif
}

void Engine::createSurface() {
    if (!SDL_Vulkan_CreateSurface(mWindow, mInstance, nullptr, &mSurface)) {
        throw Error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    }
}

void Engine::selectPhysicalDevice() {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(mInstance, &count, nullptr),
          "vkEnumeratePhysicalDevices (count)");
    if (count == 0) {
        throw Error("no Vulkan-capable physical devices");
    }
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(mInstance, &count, devices.data()),
          "vkEnumeratePhysicalDevices (list)");

    /* Score: prefer discrete GPUs, gate on all required extensions
     * being present AND on a graphics queue family that also presents
     * to our surface. The first device meeting both is taken. */
    for (VkPhysicalDevice dev : devices) {
        /* 1) Extension check */
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

        std::set<std::string> have;
        for (const auto& e : available) have.insert(e.extensionName);

        bool allPresent = true;
        for (const char* req : kRequiredDeviceExtensions) {
            if (!have.count(req)) { allPresent = false; break; }
        }
        if (!allPresent) continue;

        /* 2) Queue family with graphics + present */
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());

        int chosen = -1;
        for (uint32_t i = 0; i < qCount; ++i) {
            if (!(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &present);
            if (present) { chosen = (int)i; break; }
        }
        if (chosen < 0) continue;

        /* 3) RT-features chain — feature-bit check distinct from
         *    extension presence (the device may advertise the
         *    extension but not enable the bit, e.g. in software
         *    fallback paths). */
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeat{};
        rtFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat{};
        asFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeat.pNext = &rtFeat;
        VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeat{};
        bdaFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bdaFeat.pNext = &asFeat;
        VkPhysicalDeviceFeatures2 feat2{};
        feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feat2.pNext = &bdaFeat;
        vkGetPhysicalDeviceFeatures2(dev, &feat2);

        if (!rtFeat.rayTracingPipeline) continue;
        if (!asFeat.accelerationStructure) continue;
        if (!bdaFeat.bufferDeviceAddress) continue;

        /* 4) Pull RT pipeline properties (sbt alignment etc.) */
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
        rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &rtProps;
        vkGetPhysicalDeviceProperties2(dev, &props2);

        mPhysicalDevice = dev;
        mGraphicsFamily = (uint32_t)chosen;
        mRtProperties   = rtProps;
        std::fprintf(stderr, "[vkrt] selected device '%s' (graphics family %u)\n",
                     props2.properties.deviceName, mGraphicsFamily);
        return;
    }
    throw Error("no physical device exposes the full RT extension+feature set");
}

void Engine::createDevice() {
    /* Single graphics+present queue. */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = mGraphicsFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    /* Enable the same feature chain we gated on in selectPhysicalDevice. */
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeat{};
    rtFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtFeat.rayTracingPipeline = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat{};
    asFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeat.accelerationStructure = VK_TRUE;
    asFeat.pNext = &rtFeat;

    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeat{};
    bdaFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaFeat.bufferDeviceAddress = VK_TRUE;
    bdaFeat.pNext = &asFeat;

    VkPhysicalDeviceDescriptorIndexingFeatures diFeat{};
    diFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    diFeat.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    diFeat.runtimeDescriptorArray                    = VK_TRUE;
    diFeat.descriptorBindingPartiallyBound           = VK_TRUE;
    diFeat.descriptorBindingVariableDescriptorCount  = VK_TRUE;
    diFeat.pNext = &bdaFeat;

    VkPhysicalDeviceFeatures2 feat2{};
    feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat2.pNext = &diFeat;
    /* Core 1.0 features. shaderStorageImageWriteWithoutFormat lets
     * the rgen declare its storage image with no format qualifier
     * (`writeonly image2D` instead of `layout(rgba8)`), so the driver
     * matches whatever format the bound view has. Avoids the
     * fixed-qualifier rejection we hit with BGRA8 views. */
    feat2.features.samplerAnisotropy = VK_TRUE;
    feat2.features.shaderStorageImageExtendedFormats = VK_TRUE;
    feat2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &feat2;  /* feature chain hangs here */
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = static_cast<uint32_t>(kRequiredDeviceExtensions.size());
    dci.ppEnabledExtensionNames = kRequiredDeviceExtensions.data();
    /* Device-level layers are deprecated since 1.1 — instance layers cover both. */
    dci.enabledLayerCount       = 0;
    dci.ppEnabledLayerNames     = nullptr;

    check(vkCreateDevice(mPhysicalDevice, &dci, nullptr, &mDevice), "vkCreateDevice");
    vkGetDeviceQueue(mDevice, mGraphicsFamily, 0, &mGraphicsQueue);
}

void Engine::createCommandPool() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = mGraphicsFamily;
    check(vkCreateCommandPool(mDevice, &cpci, nullptr, &mCommandPool), "vkCreateCommandPool");
}

void Engine::createSwapchain() {
    /* Query surface caps to pick a sane image count and clamp the
     * extent. SDL_Vulkan_GetDrawableSize handles HiDPI scaling. */
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    /* Format: prefer B8G8R8A8_UNORM + SRGB_NONLINEAR. Fall back to
     * whatever the first format the device offers is. */
    uint32_t fcount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &fcount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fcount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &fcount, formats.data());

    mSwapchainFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            mSwapchainFormat = f;
            break;
        }
    }

    /* Extent: caps.currentExtent is authoritative when not UINT32_MAX. */
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        mSwapchainExtent = caps.currentExtent;
    } else {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(mWindow, &w, &h);
        mSwapchainExtent.width  = (uint32_t)w;
        mSwapchainExtent.height = (uint32_t)h;
        if (mSwapchainExtent.width  < caps.minImageExtent.width)  mSwapchainExtent.width  = caps.minImageExtent.width;
        if (mSwapchainExtent.height < caps.minImageExtent.height) mSwapchainExtent.height = caps.minImageExtent.height;
        if (mSwapchainExtent.width  > caps.maxImageExtent.width)  mSwapchainExtent.width  = caps.maxImageExtent.width;
        if (mSwapchainExtent.height > caps.maxImageExtent.height) mSwapchainExtent.height = caps.maxImageExtent.height;
    }

    /* Image count: minImageCount + 1 (so we don't stall waiting for
     * the driver), clamped to maxImageCount when present. */
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    /* Present mode: FIFO is always available and matches the engine's
     * vsync-pacing expectation. */
    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = mSurface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = mSwapchainFormat.format;
    sci.imageColorSpace  = mSwapchainFormat.colorSpace;
    sci.imageExtent      = mSwapchainExtent;
    sci.imageArrayLayers = 1;
    /* TRANSFER_DST because we blit our storage image into the
     * swapchain rather than rendering directly to it. */
    sci.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = VK_NULL_HANDLE;

    check(vkCreateSwapchainKHR(mDevice, &sci, nullptr, &mSwapchain), "vkCreateSwapchainKHR");

    /* Swapchain images — we only blit into them (no shader sampling
     * or attachment), so we don't need VkImageViews. They were
     * unused anyway, and validation rejected creating views on
     * images with TRANSFER_DST-only usage. */
    uint32_t actual = 0;
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actual, nullptr);
    mSwapchainImages.resize(actual);
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actual, mSwapchainImages.data());
    mSwapchainViews.clear();
}

void Engine::createStorageImage() {
    /* R8G8B8A8_UNORM exactly matches the rgen's `layout(rgba8)` SPIR-V
     * Format operand — validation requires identical formats for
     * Storage Images (compatible-but-different formats produce
     * undefined values to the entire image, not just the writes).
     * The blit to BGRA8 swapchain does the channel conversion at
     * vkCmdBlitImage time per spec. */
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width  = mSwapchainExtent.width;
    ici.extent.height = mSwapchainExtent.height;
    ici.extent.depth  = 1;
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    /* TRANSFER_DST so vkCmdClearColorImage can clear it to a known
     * colour (used by main.cpp's pre-trace red-clear diagnostic),
     * TRANSFER_SRC for the blit-out to the swapchain, STORAGE so the
     * rgen can write it. */
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                      | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(mDevice, &ici, nullptr, &mStorageImage), "vkCreateImage (storage)");

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(mDevice, mStorageImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX) {
        throw Error("storage image: no device-local memory type");
    }
    check(vkAllocateMemory(mDevice, &mai, nullptr, &mStorageImageMemory),
          "vkAllocateMemory (storage)");
    check(vkBindImageMemory(mDevice, mStorageImage, mStorageImageMemory, 0),
          "vkBindImageMemory (storage)");

    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = mStorageImage;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(mDevice, &vci, nullptr, &mStorageImageView),
          "vkCreateImageView (storage)");

    /* One-shot transition to GENERAL so shaders can imageStore into it. */
    oneShot([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, mStorageImage,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                              0, VK_ACCESS_SHADER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    });

    /* Accumulation image (HDR, rgba32f) — path-tracer running-mean
     * buffer. Same dimensions as the storage image. Stays in GENERAL
     * layout so the shader can imageLoad + imageStore in one pass. */
    VkImageCreateInfo aci{};
    aci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    aci.imageType     = VK_IMAGE_TYPE_2D;
    aci.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    aci.extent.width  = mSwapchainExtent.width;
    aci.extent.height = mSwapchainExtent.height;
    aci.extent.depth  = 1;
    aci.mipLevels     = 1;
    aci.arrayLayers   = 1;
    aci.samples       = VK_SAMPLE_COUNT_1_BIT;
    aci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    aci.usage         = VK_IMAGE_USAGE_STORAGE_BIT;
    aci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    aci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(mDevice, &aci, nullptr, &mAccumImage), "vkCreateImage (accum)");

    VkMemoryRequirements amr{};
    vkGetImageMemoryRequirements(mDevice, mAccumImage, &amr);
    VkMemoryAllocateInfo amai{};
    amai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    amai.allocationSize  = amr.size;
    amai.memoryTypeIndex = findMemoryType(amr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (amai.memoryTypeIndex == UINT32_MAX) throw Error("accum image: no device-local memory type");
    check(vkAllocateMemory(mDevice, &amai, nullptr, &mAccumImageMemory),
          "vkAllocateMemory (accum)");
    check(vkBindImageMemory(mDevice, mAccumImage, mAccumImageMemory, 0),
          "vkBindImageMemory (accum)");

    VkImageViewCreateInfo avci{};
    avci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    avci.image            = mAccumImage;
    avci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    avci.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    avci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    avci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(mDevice, &avci, nullptr, &mAccumImageView),
          "vkCreateImageView (accum)");

    oneShot([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, mAccumImage,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                              0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    });

    /* Slice 7: denoiser guides (normal + world-pos) and a ping-pong
     * scratch image for the multi-pass a-trous filter.  Same format
     * and dimensions as the accum image so they share lifecycle.
     * All start in GENERAL since both rgen-write and compute-read
     * happen via storage-image bindings. */
    auto createGeneralImg = [&](VkFormat format,
                                VkImage* outImg, VkDeviceMemory* outMem, VkImageView* outView,
                                const char* label) {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = format;
        ci.extent.width  = mSwapchainExtent.width;
        ci.extent.height = mSwapchainExtent.height;
        ci.extent.depth  = 1;
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_STORAGE_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::string l = std::string("vkCreateImage (") + label + ")";
        check(vkCreateImage(mDevice, &ci, nullptr, outImg), l.c_str());

        VkMemoryRequirements r{};
        vkGetImageMemoryRequirements(mDevice, *outImg, &r);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = r.size;
        ai.memoryTypeIndex = findMemoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) throw Error(std::string(label) + ": no device-local memory");
        check(vkAllocateMemory(mDevice, &ai, nullptr, outMem), "vkAllocateMemory");
        check(vkBindImageMemory(mDevice, *outImg, *outMem, 0), "vkBindImageMemory");

        VkImageViewCreateInfo vci{};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = *outImg;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = format;
        vci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(mDevice, &vci, nullptr, outView), "vkCreateImageView");

        oneShot([&](VkCommandBuffer cmd) {
            transitionImageLayout(cmd, *outImg,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                  0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                                  | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        });
    };

    createGeneralImg(VK_FORMAT_R32G32B32A32_SFLOAT,
                     &mGNormalImage, &mGNormalImageMemory, &mGNormalImageView, "gNormal");
    createGeneralImg(VK_FORMAT_R32G32B32A32_SFLOAT,
                     &mGHitPosImage, &mGHitPosImageMemory, &mGHitPosImageView, "gHitPos");
    createGeneralImg(VK_FORMAT_R32G32B32A32_SFLOAT,
                     &mDenoiseScratchImage, &mDenoiseScratchImageMemory,
                     &mDenoiseScratchImageView, "denoiseScratch");
}

void Engine::createFrameSync() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;  /* first beginFrame() must not block */

    VkCommandBufferAllocateInfo cai{};
    cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool        = mCommandPool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;

    for (auto& f : mFrames) {
        check(vkCreateSemaphore(mDevice, &si, nullptr, &f.imageAvailable),
              "vkCreateSemaphore (imageAvailable)");
        check(vkCreateSemaphore(mDevice, &si, nullptr, &f.renderFinished),
              "vkCreateSemaphore (renderFinished)");
        check(vkCreateFence(mDevice, &fi, nullptr, &f.inFlight), "vkCreateFence");
        check(vkAllocateCommandBuffers(mDevice, &cai, &f.cmd),
              "vkAllocateCommandBuffers (frame)");
    }
}

bool Engine::pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) return false;
        if (ev.type == SDL_EVENT_KEY_DOWN &&
            ev.key.scancode == SDL_SCANCODE_ESCAPE) {
            return false;
        }
        if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
            ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            mSwapchainOutOfDate = true;
        }
    }
    return true;
}

VkCommandBuffer Engine::beginFrame() {
    if (mSwapchainOutOfDate) {
        recreateSwapchain();
        mSwapchainOutOfDate = false;
    }

    FrameSync& f = mFrames[mCurrentFrame];
    check(vkWaitForFences(mDevice, 1, &f.inFlight, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");

    VkResult ar = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX,
                                        f.imageAvailable, VK_NULL_HANDLE, &mImageIndex);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        mSwapchainOutOfDate = true;
        return VK_NULL_HANDLE;
    } else if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        throw Error("vkAcquireNextImageKHR");
    }

    check(vkResetFences(mDevice, 1, &f.inFlight), "vkResetFences");
    check(vkResetCommandBuffer(f.cmd, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.cmd, &bi), "vkBeginCommandBuffer (frame)");
    return f.cmd;
}

void Engine::endFrame() {
    FrameSync& f = mFrames[mCurrentFrame];

    /* Blit storage image → swapchain. Need layout transitions on both
     * sides; the storage image transitions back to GENERAL when the
     * blit's done so the next frame's rgen can write to it. */
    VkImage swapImage = mSwapchainImages[mImageIndex];

    transitionImageLayout(f.cmd, swapImage,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          0, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    transitionImageLayout(f.cmd, mStorageImage,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {(int32_t)mSwapchainExtent.width, (int32_t)mSwapchainExtent.height, 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {(int32_t)mSwapchainExtent.width, (int32_t)mSwapchainExtent.height, 1};
    vkCmdBlitImage(f.cmd,
                   mStorageImage,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapImage,      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    transitionImageLayout(f.cmd, swapImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    transitionImageLayout(f.cmd, mStorageImage,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    check(vkEndCommandBuffer(f.cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo subInfo{};
    subInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    subInfo.waitSemaphoreCount   = 1;
    subInfo.pWaitSemaphores      = &f.imageAvailable;
    subInfo.pWaitDstStageMask    = &waitStage;
    subInfo.commandBufferCount   = 1;
    subInfo.pCommandBuffers      = &f.cmd;
    subInfo.signalSemaphoreCount = 1;
    subInfo.pSignalSemaphores    = &f.renderFinished;
    check(vkQueueSubmit(mGraphicsQueue, 1, &subInfo, f.inFlight), "vkQueueSubmit");

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &f.renderFinished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &mSwapchain;
    pi.pImageIndices      = &mImageIndex;
    VkResult pr = vkQueuePresentKHR(mGraphicsQueue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        mSwapchainOutOfDate = true;
    } else if (pr != VK_SUCCESS) {
        throw Error("vkQueuePresentKHR");
    }

    mCurrentFrame = (mCurrentFrame + 1) % kFramesInFlight;
}

void Engine::recreateSwapchain() {
    vkDeviceWaitIdle(mDevice);

    for (VkImageView v : mSwapchainViews) {
        if (v) vkDestroyImageView(mDevice, v, nullptr);
    }
    mSwapchainViews.clear();
    if (mSwapchain) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
    if (mStorageImageView) vkDestroyImageView(mDevice, mStorageImageView, nullptr);
    if (mStorageImage)     vkDestroyImage(mDevice, mStorageImage, nullptr);
    if (mStorageImageMemory) vkFreeMemory(mDevice, mStorageImageMemory, nullptr);
    if (mAccumImageView)   vkDestroyImageView(mDevice, mAccumImageView, nullptr);
    if (mAccumImage)       vkDestroyImage(mDevice, mAccumImage, nullptr);
    if (mAccumImageMemory) vkFreeMemory(mDevice, mAccumImageMemory, nullptr);
    if (mGNormalImageView)        vkDestroyImageView(mDevice, mGNormalImageView, nullptr);
    if (mGNormalImage)            vkDestroyImage(mDevice, mGNormalImage, nullptr);
    if (mGNormalImageMemory)      vkFreeMemory(mDevice, mGNormalImageMemory, nullptr);
    if (mGHitPosImageView)        vkDestroyImageView(mDevice, mGHitPosImageView, nullptr);
    if (mGHitPosImage)            vkDestroyImage(mDevice, mGHitPosImage, nullptr);
    if (mGHitPosImageMemory)      vkFreeMemory(mDevice, mGHitPosImageMemory, nullptr);
    if (mDenoiseScratchImageView)   vkDestroyImageView(mDevice, mDenoiseScratchImageView, nullptr);
    if (mDenoiseScratchImage)       vkDestroyImage(mDevice, mDenoiseScratchImage, nullptr);
    if (mDenoiseScratchImageMemory) vkFreeMemory(mDevice, mDenoiseScratchImageMemory, nullptr);
    mStorageImageView = VK_NULL_HANDLE;
    mStorageImage = VK_NULL_HANDLE;
    mStorageImageMemory = VK_NULL_HANDLE;
    mAccumImageView = VK_NULL_HANDLE;
    mAccumImage = VK_NULL_HANDLE;
    mAccumImageMemory = VK_NULL_HANDLE;
    mGNormalImage = VK_NULL_HANDLE;
    mGNormalImageMemory = VK_NULL_HANDLE;
    mGNormalImageView = VK_NULL_HANDLE;
    mGHitPosImage = VK_NULL_HANDLE;
    mGHitPosImageMemory = VK_NULL_HANDLE;
    mGHitPosImageView = VK_NULL_HANDLE;
    mDenoiseScratchImage = VK_NULL_HANDLE;
    mDenoiseScratchImageMemory = VK_NULL_HANDLE;
    mDenoiseScratchImageView = VK_NULL_HANDLE;

    createSwapchain();
    createStorageImage();
}

void Engine::retainBuffer(VkBuffer buf, VkDeviceMemory mem) {
    mRetained.emplace_back(buf, mem);
}

void Engine::shutdown() {
    if (mDevice) vkDeviceWaitIdle(mDevice);

    for (auto& [buf, mem] : mRetained) {
        if (buf) vkDestroyBuffer(mDevice, buf, nullptr);
        if (mem) vkFreeMemory(mDevice, mem, nullptr);
    }
    mRetained.clear();

    for (auto& f : mFrames) {
        if (f.imageAvailable) vkDestroySemaphore(mDevice, f.imageAvailable, nullptr);
        if (f.renderFinished) vkDestroySemaphore(mDevice, f.renderFinished, nullptr);
        if (f.inFlight)       vkDestroyFence(mDevice, f.inFlight, nullptr);
        if (f.cmd && mCommandPool) vkFreeCommandBuffers(mDevice, mCommandPool, 1, &f.cmd);
        f = FrameSync{};
    }
    if (mStorageImageView) vkDestroyImageView(mDevice, mStorageImageView, nullptr);
    if (mStorageImage)     vkDestroyImage(mDevice, mStorageImage, nullptr);
    if (mStorageImageMemory) vkFreeMemory(mDevice, mStorageImageMemory, nullptr);
    if (mAccumImageView)   vkDestroyImageView(mDevice, mAccumImageView, nullptr);
    if (mAccumImage)       vkDestroyImage(mDevice, mAccumImage, nullptr);
    if (mAccumImageMemory) vkFreeMemory(mDevice, mAccumImageMemory, nullptr);
    if (mGNormalImageView)        vkDestroyImageView(mDevice, mGNormalImageView, nullptr);
    if (mGNormalImage)            vkDestroyImage(mDevice, mGNormalImage, nullptr);
    if (mGNormalImageMemory)      vkFreeMemory(mDevice, mGNormalImageMemory, nullptr);
    if (mGHitPosImageView)        vkDestroyImageView(mDevice, mGHitPosImageView, nullptr);
    if (mGHitPosImage)            vkDestroyImage(mDevice, mGHitPosImage, nullptr);
    if (mGHitPosImageMemory)      vkFreeMemory(mDevice, mGHitPosImageMemory, nullptr);
    if (mDenoiseScratchImageView)   vkDestroyImageView(mDevice, mDenoiseScratchImageView, nullptr);
    if (mDenoiseScratchImage)       vkDestroyImage(mDevice, mDenoiseScratchImage, nullptr);
    if (mDenoiseScratchImageMemory) vkFreeMemory(mDevice, mDenoiseScratchImageMemory, nullptr);
    for (VkImageView v : mSwapchainViews) {
        if (v) vkDestroyImageView(mDevice, v, nullptr);
    }
    mSwapchainViews.clear();
    if (mSwapchain)    vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
    if (mCommandPool)  vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    if (mDevice)       vkDestroyDevice(mDevice, nullptr);
    if (mSurface)      vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
#ifdef TMC_VK_RT_VALIDATION
    if (mDebugMessenger) {
        auto pfn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT");
        if (pfn) pfn(mInstance, mDebugMessenger, nullptr);
    }
#endif
    if (mInstance)     vkDestroyInstance(mInstance, nullptr);
    if (mWindow)       SDL_DestroyWindow(mWindow);

    /* Reset everything so a second init() doesn't see stale handles. */
    mWindow = nullptr;
    mInstance = VK_NULL_HANDLE;
    mDebugMessenger = VK_NULL_HANDLE;
    mSurface = VK_NULL_HANDLE;
    mPhysicalDevice = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mGraphicsQueue = VK_NULL_HANDLE;
    mCommandPool = VK_NULL_HANDLE;
    mSwapchain = VK_NULL_HANDLE;
    mStorageImage = VK_NULL_HANDLE;
    mStorageImageMemory = VK_NULL_HANDLE;
    mStorageImageView = VK_NULL_HANDLE;
    mAccumImage = VK_NULL_HANDLE;
    mAccumImageMemory = VK_NULL_HANDLE;
    mAccumImageView = VK_NULL_HANDLE;
    mSwapchainImages.clear();
}

void Engine::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(mDevice, &bci, nullptr, outBuffer), "vkCreateBuffer");

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(mDevice, *outBuffer, &mr);

    /* When the buffer has the SHADER_DEVICE_ADDRESS usage bit (e.g.
     * for use in BLAS geometry refs), the allocation needs to enable
     * the same flag on the AllocateFlagsInfo struct. */
    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext           = (flagsInfo.flags != 0) ? &flagsInfo : nullptr;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, props);
    if (mai.memoryTypeIndex == UINT32_MAX) {
        throw Error("allocateBuffer: no matching memory type");
    }
    check(vkAllocateMemory(mDevice, &mai, nullptr, outMemory), "vkAllocateMemory (buffer)");
    check(vkBindBufferMemory(mDevice, *outBuffer, *outMemory, 0), "vkBindBufferMemory");
}

uint32_t Engine::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

void Engine::transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout from, VkImageLayout to,
                                   VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                   VkPipelineStageFlags srcStage,
                                   VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = from;
    b.newLayout           = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  /* namespace tmc_vkrt */
