/* port/vk_rt_experiment/main.cpp
 *
 * Driver for the standalone RT scaffold. Boots the Engine, builds
 * a minimal test scene (a procedural diffuse atlas + a handful of
 * quads at different Z layers), and runs the per-frame loop:
 *     beginFrame → rebuildAS → dispatchRays → endFrame
 *
 * This is not a port of TMC — it's a "does the pipeline produce
 * pixels" smoke test for the scaffold. Successful run shows a
 * 4-layer scene with the BG2 (back) blue, BG1 green, BG0 yellow,
 * Sprite red, UI white-bordered. With the closest-hit shader's
 * shadow trace, the foreground quads cast shadows onto the
 * background where the sun ray gets occluded.
 *
 * Exit ESC or close the window.
 */

#include "Engine.h"
#include "FrameSource.h"
#include "RenderLayerManager.h"
#include "RayTracingPipeline.h"
#include "DenoisePipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace tmc_vkrt;

/* Read all bytes from a file. Returns empty vector on failure (caller
 * should check). Used by the real-tile atlas builder below. */
static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto end = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data((size_t)end);
    if (!f.read((char*)data.data(), data.size())) data.clear();
    return data;
}

/* Decode a 4-bits-per-pixel GBA tilesheet into row-major RGBA.
 *
 * GBA tile layout:
 *   - Each 8×8 tile is 32 bytes (4bpp).
 *   - Within a tile, pixels are stored row-by-row, top-to-bottom.
 *   - Each byte holds 2 pixels: low nibble = LEFT, high nibble = RIGHT.
 *   - The sheet's tiles are arranged in a `tileCols`-wide grid,
 *     left-to-right, top-to-bottom.
 *
 * `palette` is 16 RGBA8 colours; index 0 conventionally = transparent.
 * Output buffer is (tileCols*8) × (tileRows*8) × 4 bytes.
 *
 * Returns the decoded RGBA bytes. */
static std::vector<uint8_t> decode4bppTilesheet(const std::vector<uint8_t>& tileBytes,
                                                int tileCols, int tileRows,
                                                const uint32_t palette[16]) {
    const int W = tileCols * 8;
    const int H = tileRows * 8;
    std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
    const size_t bytesPerTile = 32;
    if (tileBytes.size() < bytesPerTile * (size_t)tileCols * tileRows) {
        return rgba;  /* zero-filled — caller sees empty/black on missing data */
    }
    for (int ty = 0; ty < tileRows; ++ty) {
        for (int tx = 0; tx < tileCols; ++tx) {
            const uint8_t* src = &tileBytes[(ty * tileCols + tx) * bytesPerTile];
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; px += 2) {
                    uint8_t b = src[py * 4 + (px / 2)];
                    uint8_t lo = b & 0x0F;
                    uint8_t hi = (b >> 4) & 0x0F;
                    auto put = [&](int dx, int dy, uint8_t idx) {
                        if (idx == 0) return;  /* transparent */
                        uint32_t col = palette[idx];
                        uint8_t* dst = &rgba[((dy * W) + dx) * 4];
                        dst[0] = (col >>  0) & 0xFF;  /* R */
                        dst[1] = (col >>  8) & 0xFF;  /* G */
                        dst[2] = (col >> 16) & 0xFF;  /* B */
                        dst[3] = (col >> 24) & 0xFF;  /* A */
                    };
                    put(tx * 8 + px,     ty * 8 + py, lo);
                    put(tx * 8 + px + 1, ty * 8 + py, hi);
                }
            }
        }
    }
    return rgba;
}

/* Real palette extracted from mods/buttons-gba/assets-src/face buttons.png
 * via `convert ... -colors 16 -unique-colors`. That PNG is the editable
 * source for the buttons-gba mod's 4bpp .bin output (the .bin file was
 * generated from it at build time, so they share index→colour mapping).
 * Only 7 distinct colours used (1 transparent + 6 greys); slots 7..15
 * stay magenta as a "this was never used by this sheet" sentinel so any
 * tile data sampling those indices visibly stands out as broken. */
static constexpr uint32_t kTestPalette[16] = {
    0x00000000u,  /* 0: transparent (GBA convention) */
    0xFF000000u,  /* 1: black (outline) */
    0xFF686050u,  /* 2: dark grey-blue */
    0xFF908870u,  /* 3: mid-dark */
    0xFFC0B0A0u,  /* 4: mid */
    0xFFE8D8D0u,  /* 5: light */
    0xFFF8F8F8u,  /* 6: near-white */
    0xFFFF00FFu,  /* 7..15: sentinel magenta */
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
    0xFFFF00FFu,
};

/* Atlas layout (256×256):
 *
 *      0─────────┬─────64─────128────192────256
 *      │ GBA     │ Sprite  │ BG0      │ BG2 │
 *      │ tile-   │ (red)   │ (yellow) │     │
 *      │ sheet   ├─────────┼──────────┤(blue)
 *      │ 64×56   │ BG1     │ UI box   │     │
 *      │ from .  │ (green) │ (white,  │     │
 *      │ /mods/. │         │ trans-   │     │
 *      │ /buttons│         │ parent   │     │
 *  64 ─┤         │         │ inside)  │     │
 *      ├─────────┘         │          │     │
 *      │ (unused, black)   │          │     │
 *      │                   │          │     │
 *      │                   │          │     │
 *      ├───────────────────┴──────────┴─────┤
 *  256 ─┘
 *
 * Cell UVs (used by main loop's drawSprite calls):
 *
 *   GBA_TILES   : (0.0, 0.0)   → (64/256, 56/256) = (0.25, 0.219)
 *   Sprite      : (0.25, 0.0)  → (0.50, 0.25)
 *   BG0         : (0.50, 0.0)  → (0.75, 0.25)
 *   BG2         : (0.75, 0.0)  → (1.00, 1.00)
 *   BG1         : (0.25, 0.25) → (0.50, 0.50)
 *   UI          : (0.50, 0.25) → (0.75, 0.50)  (hollow box)
 */
[[maybe_unused]] static std::vector<uint8_t> buildAtlas(uint32_t& outW, uint32_t& outH) {
    const uint32_t W = 256;
    const uint32_t H = 256;
    outW = W; outH = H;
    std::vector<uint8_t> px(W * H * 4u, 0);

    /* Block-fill a solid colour into a rect of the atlas. */
    auto fillRect = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                uint8_t* p = &px[(y * W + x) * 4u];
                p[0] = r; p[1] = g; p[2] = b; p[3] = a;
            }
        }
    };

    /* Sprite cell (red), BG0 (yellow), BG2 (blue), BG1 (green), UI (white). */
    fillRect( 64, 0, 128,  64, 230,  60,  50, 255);  /* Sprite */
    fillRect(128, 0, 192,  64, 235, 200,  60, 255);  /* BG0 */
    fillRect(192, 0, 256, 256,  60, 110, 200, 255);  /* BG2 (full-height) */
    fillRect( 64, 64, 128, 128,  80, 180,  90, 255); /* BG1 */
    fillRect(128, 64, 192, 128, 240, 240, 240, 255); /* UI box outer */
    /* UI box: hollow interior. */
    fillRect(134, 70, 186, 122, 0, 0, 0, 0);

    /* Multi-tilesheet atlas packer. Each tilesheet is decoded with
     * the shared kTestPalette and blitted into a free region of the
     * atlas, recorded in the order their UVs are looked up below. */
    struct SheetSpec {
        const char* path;
        int         tileCols;
        int         tileRows;
        uint32_t    atlasX, atlasY;  /* top-left in the 256×256 atlas */
        const char* label;
    };
    /* Tilesheet 1: face buttons (8 tiles × 7 tiles = 64×56). Lands
     *   at atlas (0, 0).
     * Tilesheet 2: shoulder buttons (7 × 2 tiles = 56×16). Lands at
     *   atlas (0, 56) just below the first. */
    const SheetSpec sheets[] = {
        { "../../mods/buttons-gba/gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin",
          8, 7, 0, 0,  "face" },
        { "../../mods/buttons-gba/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin",
          7, 2, 0, 56, "shoulder" },
    };

    for (const SheetSpec& s : sheets) {
        auto bytes = readFile(s.path);
        if (bytes.empty()) {
            std::fprintf(stderr, "[atlas] missing %s — slot stays black\n", s.path);
            continue;
        }
        const int sheetW = s.tileCols * 8;
        const int sheetH = s.tileRows * 8;
        auto rgba = decode4bppTilesheet(bytes, s.tileCols, s.tileRows, kTestPalette);
        for (int y = 0; y < sheetH; ++y) {
            std::memcpy(&px[((s.atlasY + y) * W + s.atlasX) * 4u],
                        &rgba[(y * sheetW) * 4u],
                        (size_t)sheetW * 4u);
        }
        std::fprintf(stderr,
            "[atlas] decoded %s tilesheet (%zu bytes → %dx%d, placed at atlas %u,%u)\n",
            s.label, bytes.size(), sheetW, sheetH, s.atlasX, s.atlasY);
    }

    return px;
}

/* Atlas image + view + memory + sampler. Created once with a fixed
 * size matching the GBA framebuffer (240×160); the contents are
 * overwritten each frame via the persistent staging buffer below.
 *
 * Per-frame upload path:
 *   1. memcpy framebuffer bytes into mapped staging.
 *   2. Record into the per-frame cmd buffer:
 *        - barrier atlas: SHADER_READ_ONLY → TRANSFER_DST
 *        - vkCmdCopyBufferToImage staging → atlas
 *        - barrier atlas: TRANSFER_DST → SHADER_READ_ONLY
 *   3. Then issue rebuildAS + dispatchRays, which samples atlas. */
struct AtlasGpu {
    VkImage        image = VK_NULL_HANDLE;
    VkImageView    view  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;

    /* Persistent staging — mapped at create time, kept mapped. */
    VkBuffer       stagingBuf  = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem  = VK_NULL_HANDLE;
    void*          stagingMap  = nullptr;
    uint32_t       w = 0, h = 0;
};

/* Create an empty (UNDEFINED → SHADER_READ_ONLY) atlas of the given
 * dimensions plus its persistent staging buffer. The image's
 * contents start unspecified — first call to refreshAtlas() writes
 * the first real pixels. */
static AtlasGpu createAtlas(Engine& eng, uint32_t w, uint32_t h) {
    AtlasGpu out;
    out.w = w;
    out.h = h;

    /* Image */
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(eng.device(), &ici, nullptr, &out.image), "atlas vkCreateImage");

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(eng.device(), out.image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = eng.findMemoryType(mr.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(eng.device(), &mai, nullptr, &out.memory), "atlas vkAllocateMemory");
    check(vkBindImageMemory(eng.device(), out.image, out.memory, 0), "atlas vkBindImageMemory");

    /* Persistent staging buffer — host-visible, kept mapped so each
     * frame's memcpy doesn't pay the Map/Unmap cost. */
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
    eng.allocateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &out.stagingBuf, &out.stagingMem);
    check(vkMapMemory(eng.device(), out.stagingMem, 0, bytes, 0, &out.stagingMap),
          "atlas staging vkMapMemory");

    /* Transition UNDEFINED → SHADER_READ_ONLY so the first frame's
     * descriptor binding is valid even before refreshAtlas runs.
     * (Contents undefined until the first refresh — that's fine, the
     * rgen+rchit only sample after the first frame's upload.) */
    eng.oneShot([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = out.image;
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask       = 0;
        b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, nullptr, 0, nullptr, 1, &b);
    });

    /* View */
    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = out.image;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(eng.device(), &vci, nullptr, &out.view), "atlas vkCreateImageView");

    /* Linear sampler with clamp-to-edge. */
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_NEAREST;  /* pixel-art: nearest stays crisp */
    sci.minFilter    = VK_FILTER_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    check(vkCreateSampler(eng.device(), &sci, nullptr, &out.sampler), "atlas vkCreateSampler");
    return out;
}

/* Per-frame atlas refresh — supports up to 4 source planes packed
 * vertically into the atlas image (composite + BG1 + BG2 + sprite).
 * Each plane is `srcW × srcH` RGBA8; the atlas's height must be at
 * least `numPlanes * srcH`. Pass nullptr for missing planes to leave
 * them unchanged from the previous frame. */
static void refreshAtlas(Engine& eng, VkCommandBuffer cmd,
                         AtlasGpu& atlas,
                         const uint8_t* composite,
                         const uint8_t* bg1,
                         const uint8_t* bg2,
                         const uint8_t* sprite,
                         uint32_t srcW, uint32_t srcH) {
    (void)eng;
    if (!composite && !bg1 && !bg2 && !sprite) return;
    if (srcW == 0 || srcH == 0) return;

    /* Stage the up-to-four planes in the persistent staging buffer.
     * Layout matches the atlas image: composite/BG1/BG2/sprite top-to-
     * bottom. Skipped planes leave the staging bytes unchanged
     * (harmless — only the regions we copy below get pushed). */
    const size_t planeBytes = (size_t)srcW * srcH * 4;
    uint8_t* dst = (uint8_t*)atlas.stagingMap;
    if (composite) std::memcpy(dst + 0 * planeBytes, composite, planeBytes);
    if (bg1)       std::memcpy(dst + 1 * planeBytes, bg1,       planeBytes);
    if (bg2)       std::memcpy(dst + 2 * planeBytes, bg2,       planeBytes);
    if (sprite)    std::memcpy(dst + 3 * planeBytes, sprite,    planeBytes);

    /* SHADER_READ_ONLY → TRANSFER_DST */
    VkImageMemoryBarrier toDst{};
    toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image               = atlas.image;
    toDst.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    toDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    /* One vkCmdCopyBufferToImage per plane present. Each copy
     * specifies its destination y-offset to land in the right
     * region of the atlas. */
    VkBufferImageCopy regions[4]{};
    uint32_t regionCount = 0;
    auto addRegion = [&](uint32_t planeIdx) {
        VkBufferImageCopy& r = regions[regionCount++];
        r.bufferOffset      = planeIdx * planeBytes;
        r.bufferRowLength   = 0;
        r.bufferImageHeight = 0;
        r.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageOffset       = {0, (int32_t)(planeIdx * srcH), 0};
        r.imageExtent       = {srcW, srcH, 1};
    };
    if (composite) addRegion(0);
    if (bg1)       addRegion(1);
    if (bg2)       addRegion(2);
    if (sprite)    addRegion(3);
    if (regionCount > 0) {
        vkCmdCopyBufferToImage(cmd, atlas.stagingBuf, atlas.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               regionCount, regions);
    }

    /* TRANSFER_DST → SHADER_READ_ONLY (sync with subsequent trace) */
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0, 0, nullptr, 0, nullptr, 1, &toRead);
}

/* Legacy one-shot uploader retained for reference / future use; not
 * called by main() anymore now that the atlas is refreshed per
 * frame from the FrameSource. Kept compilable so the file builds
 * even if no caller uses it; the linker drops it when unused. */
[[maybe_unused]] static AtlasGpu uploadAtlas(Engine& eng, const std::vector<uint8_t>& px,
                                             uint32_t w, uint32_t h) {
    AtlasGpu out;

    /* Image */
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(eng.device(), &ici, nullptr, &out.image), "atlas vkCreateImage");

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(eng.device(), out.image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = eng.findMemoryType(mr.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(eng.device(), &mai, nullptr, &out.memory), "atlas vkAllocateMemory");
    check(vkBindImageMemory(eng.device(), out.image, out.memory, 0), "atlas vkBindImageMemory");

    /* Staging buffer */
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    eng.allocateBuffer(px.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &stagingBuf, &stagingMem);
    void* p = nullptr;
    check(vkMapMemory(eng.device(), stagingMem, 0, px.size(), 0, &p),
          "atlas staging vkMapMemory");
    std::memcpy(p, px.data(), px.size());
    vkUnmapMemory(eng.device(), stagingMem);

    /* Transition → copy → transition to SHADER_READ_ONLY_OPTIMAL */
    eng.oneShot([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b1{};
        b1.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b1.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.image               = out.image;
        b1.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b1.srcAccessMask       = 0;
        b1.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b1);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuf, out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, nullptr, 0, nullptr, 1, &b2);
    });

    vkDestroyBuffer(eng.device(), stagingBuf, nullptr);
    vkFreeMemory(eng.device(), stagingMem, nullptr);

    /* View */
    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = out.image;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(eng.device(), &vci, nullptr, &out.view), "atlas vkCreateImageView");

    /* Linear sampler with clamp-to-edge (matches the rchit's
     * normalised-UV expectation). */
    VkSamplerCreateInfo sci{};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_LINEAR;
    sci.minFilter     = VK_FILTER_LINEAR;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    check(vkCreateSampler(eng.device(), &sci, nullptr, &out.sampler), "atlas vkCreateSampler");

    return out;
}

/* Cell UV ranges into the atlas built by buildAtlas above. */
struct CellUV { float u0, v0, u1, v1; };
static constexpr CellUV kCellFace   = {0.000f, 0.000f, 64.0f/256, 56.0f/256}; /* face buttons */
static constexpr CellUV kCellShould = {0.000f, 56.0f/256, 56.0f/256, 72.0f/256}; /* shoulder buttons */
static constexpr CellUV kCellSprite = {0.250f, 0.000f, 0.500f,    0.250f};   /* red */
static constexpr CellUV kCellBG0    = {0.500f, 0.000f, 0.750f,    0.250f};   /* yellow */
static constexpr CellUV kCellBG2    = {0.750f, 0.000f, 1.000f,    1.000f};   /* blue */
static constexpr CellUV kCellBG1    = {0.250f, 0.250f, 0.500f,    0.500f};   /* green */
static constexpr CellUV kCellUI     = {0.500f, 0.250f, 0.750f,    0.500f};   /* white */

int main() {
    Engine engine;
    if (!engine.init(960, 640, "tmc-vkrt-experiment")) {
        std::fprintf(stderr, "[main] Engine.init failed\n");
        return 1;
    }
    std::fprintf(stderr, "[main] engine up; swapchain %ux%u\n",
                 engine.swapchainWidth(), engine.swapchainHeight());

    RenderLayerManager layers(engine);
    RayTracingPipeline rt(engine, layers);
    DenoisePipeline    denoise(engine);

    /* Compiled-shader directory: ./shaders relative to the binary's
     * cwd. The build script writes *.spv there. */
    std::fprintf(stderr, "[main] step: createPipeline\n");
    rt.createPipeline("./shaders");
    denoise.createPipeline("./shaders");
    std::fprintf(stderr, "[main] step: pipeline ok\n");

    /* Atlas: 240 × (4 × 160) = 240×640, four stacked planes:
     *   y =   0..160 : composite framebuffer (engine's final output)
     *   y = 160..320 : BG1 layer (mid-world; transparent where holes)
     *   y = 320..480 : BG2 layer (back-world)
     *   y = 480..640 : sprite plane (OAM-only, alpha=0 outside silhouettes)
     * Each plane is refreshed independently per frame from the shm
     * region's four blocks. The sprite plane lets the any-hit shader
     * pass rays through transparent sprite-quad pixels and lets the
     * rchit luma-test only emissive-tag real silhouette texels. */
    std::fprintf(stderr, "[main] step: createAtlas\n");
    AtlasGpu atlas = createAtlas(engine,
                                 FrameSource::kFrameW,
                                 FrameSource::kFrameH * 4u);
    rt.setAtlas(atlas.view, atlas.sampler);
    std::fprintf(stderr, "[main] step: atlas ok (%ux%u)\n", atlas.w, atlas.h);

    /* Two frame sources, in priority order:
     *   1. ShmFrameSource — live mmap of /dev/shm/tmc_framebuffer.
     *      Works when tmc_pc was started with
     *      TMC_PUBLISH_FRAMEBUFFER=1 in its environment. This is
     *      the real integration: the running game's output is
     *      ray-traced in real time.
     *   2. PNG FrameSource — cycles tmc-*.png screenshots as a
     *      fallback when no live producer is available. Useful for
     *      iterating on the RT side without running the full game. */
    ShmFrameSource liveSrc;
    FrameSource    pngSrc;
    bool useLive = liveSrc.open();
    if (!useLive) {
        std::fprintf(stderr, "[main] no live shm producer — falling back to PNG screenshots\n");
        const std::vector<std::string> candidateDirs = {
            ".", "../..", "../../..",
        };
        for (const auto& d : candidateDirs) {
            if (pngSrc.loadDirectory(d)) break;
        }
        if (pngSrc.frameCount() == 0) {
            std::fprintf(stderr,
                "[main] no tmc-*.png frames either — atlas stays at its\n"
                "        initial contents. Start tmc_pc with\n"
                "        TMC_PUBLISH_FRAMEBUFFER=1 ./tmc_pc, or drop a few\n"
                "        240x160 PNGs into the binary's cwd.\n");
        }
    }

    (void)kCellFace;   (void)kCellShould; (void)kCellSprite;
    (void)kCellBG0;    (void)kCellBG1;    (void)kCellBG2;    (void)kCellUI;

    /* Frame loop */
    bool firstFrame = true;
    uint32_t frameIdx = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (engine.pumpEvents()) {
        const float time =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();

        /* Geometry for the trace. World composite back layer + per-OAM
         * sprite quads, sampling the same 240×640 atlas at different
         * vertical regions:
         *
         *   atlas y region   meaning              quad at layer Z
         *   ─────────────────────────────────────────────────────
         *      0/4..1/4      composite (PPU)      back layer (z=0.8)
         *      1/4..2/4      BG1 plane (mid)      [unused — composite already has it]
         *      2/4..3/4      BG2 plane (back)     [unused — composite already has it]
         *      3/4..4/4      sprite plane (OAM-only) sprite quads (z=0.2)
         *
         * Sprite quads now sample the SPRITE plane instead of the
         * composite — its alpha is 0 outside silhouettes, so the
         * any-hit shader skips transparent texels (rays pass through
         * the empty 16×16 quad corners) and the rchit luma-test marks
         * only opaque bright pixels as emissive. This kills the
         * bright-box-around-Link artefact from the previous slice. */
        const float kPlaneU0 = 0.0f, kPlaneU1 = 1.0f;
        const float kCompV1  = 1.0f / 4.0f;
        const float kBg1V0   = 1.0f / 4.0f, kBg1V1 = 2.0f / 4.0f;
        const float kBg2V0   = 2.0f / 4.0f, kBg2V1 = 3.0f / 4.0f;
        const float kSprV0   = 3.0f / 4.0f, kSprV1 = 1.0f;

        layers.beginFrame();
        /* Single back layer: the full composite (every BG layer the
         * engine drew, including BG0 HUD + light rays + alpha
         * overlays). Putting BG1 as a separate quad on top would
         * occlude the composite where BG1 is opaque — which in
         * TMC's foreground layer means almost everywhere — hiding
         * the HUD and BG0 effects. The composite already has BG1
         * content baked into it at the correct positions, so a
         * single back layer is sufficient. Sprites at z=0.2 still
         * cast RT shadows onto this back composite, which is the
         * primary RTX effect for this scene. */
        layers.drawSprite(Layer::BG2, 0.0f, 0.0f, 240.0f, 160.0f,
                          kPlaneU0, 0.0f, kPlaneU1, kCompV1);

        ParsedOam parsed;
        const uint8_t* spritePlane = nullptr;
        if (useLive) {
            parsed.parse(liveSrc.currentOam(), liveSrc.oamCount());
            spritePlane = liveSrc.currentSpritePlane();
        }
        /* Emissive auto-tag: every visible sprite quad is a
         * candidate; the shader (path_trace.rchit) gates per-texel
         * by alpha > 0.5 AND luma ≥ kEmissiveLuma.  Doing the gate
         * shader-side avoids the host-side "center pixel" sampling
         * flipping between frames as the sprite animation cycles
         * (which would flash Link's head emissive on/off as he
         * walked).  The shader sees a stable per-texel signal. */
        auto isSpriteEmissive = [&](const OamSprite& /*s*/) -> bool {
            return true;
        };
        (void)spritePlane;
        for (const OamSprite& s : parsed.visibleSprites()) {
            /* Sprite quad now samples the SPRITE plane (OAM-only
             * render, alpha=0 outside silhouettes). The quad's
             * outer pixels are transparent, so the any-hit shader
             * skips them — rays pass through the empty quad
             * corners onto the back composite. This also means
             * shadows cast by the sprite occupy exactly the
             * silhouette, not the full bounding-box rectangle. */
            const float u0 = (float)s.x / 240.0f;
            const float u1 = (float)(s.x + s.w) / 240.0f;
            const float vy0 = (float)s.y         / 160.0f;
            const float vy1 = (float)(s.y + s.h) / 160.0f;
            const float v0 = kSprV0 + vy0 * (kSprV1 - kSprV0);
            const float v1 = kSprV0 + vy1 * (kSprV1 - kSprV0);
            const bool emit = isSpriteEmissive(s);
            layers.drawSprite(Layer::Sprite,
                              (float)s.x, (float)s.y,
                              (float)s.w, (float)s.h,
                              u0, v0, u1, v1,
                              emit);
        }
        layers.flushToBuffers();

        VkCommandBuffer cmd = engine.beginFrame();
        if (cmd == VK_NULL_HANDLE) continue;  /* swapchain out of date */

        /* Pull all four planes from the live shm (or the PNG ring's
         * single composite for the offline fallback). The refresh
         * function uploads each present plane into its region of the
         * atlas; the BG / sprite planes only show up when the
         * producer is v3+ / v4+ respectively. */
        const uint8_t* composite     = nullptr;
        const uint8_t* bg1Plane      = nullptr;
        const uint8_t* bg2Plane      = nullptr;
        const uint8_t* spritePlaneFB = nullptr;
        if (useLive) {
            composite     = liveSrc.currentFrame();
            bg1Plane      = liveSrc.currentBgPlane(0);
            bg2Plane      = liveSrc.currentBgPlane(1);
            spritePlaneFB = liveSrc.currentSpritePlane();
        } else {
            composite = pngSrc.advance(10);
        }
        if (composite || bg1Plane || bg2Plane || spritePlaneFB) {
            refreshAtlas(engine, cmd, atlas,
                         composite, bg1Plane, bg2Plane, spritePlaneFB,
                         FrameSource::kFrameW, FrameSource::kFrameH);
            if (firstFrame) std::fprintf(stderr, "[main] first frame uploaded, tracing\n");
        }

        /* --- Slice 6: extract point lights from the OAM-only sprite
         * plane.  Scan for saturated bright pixels (the same gate the
         * rchit uses for per-texel emissive), cluster nearby ones, and
         * publish up to kMaxLights point lights for the rgen's direct
         * illumination pass. */
        RayTracingPipeline::Light lights[RayTracingPipeline::kMaxLights];
        uint32_t lightCount = 0;
        if (spritePlaneFB) {
            const int kStride = 4;  /* sub-sample to keep CPU cost down */
            for (int py = 0; py < 160; py += kStride) {
                for (int px = 0; px < 240; px += kStride) {
                    const uint8_t* p = &spritePlaneFB[(py * 240 + px) * 4];
                    if (p[3] < 128) continue;  /* outside silhouette */
                    float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
                    float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    if (luma < 0.6f) continue;
                    float maxC = std::max({r, g, b});
                    float minC = std::min({r, g, b});
                    float sat  = (maxC > 0.0001f) ? (maxC - minC) / maxC : 0.0f;
                    if (sat < 0.4f) continue;

                    /* Cluster: merge into an existing light within
                     * mergeRadius pixels.  Otherwise emit a new one. */
                    const float mergeRadius = 12.0f;
                    bool merged = false;
                    for (uint32_t i = 0; i < lightCount; ++i) {
                        float dx = lights[i].posX - float(px);
                        float dy = lights[i].posY - float(py);
                        if (dx*dx + dy*dy < mergeRadius*mergeRadius) {
                            /* Accumulate position + radiance (averaged on flush). */
                            lights[i].posX = (lights[i].posX + float(px)) * 0.5f;
                            lights[i].posY = (lights[i].posY + float(py)) * 0.5f;
                            lights[i].r    = std::max(lights[i].r, r);
                            lights[i].g    = std::max(lights[i].g, g);
                            lights[i].b    = std::max(lights[i].b, b);
                            merged = true;
                            break;
                        }
                    }
                    if (!merged && lightCount < RayTracingPipeline::kMaxLights) {
                        RayTracingPipeline::Light& L = lights[lightCount++];
                        L.posX      = float(px);
                        L.posY      = float(py);
                        L.posZ      = 0.15f;       /* slightly in front of sprite z=0.2 */
                        L.radius    = 24.0f;       /* falloff radius in world units */
                        L.r         = r;
                        L.g         = g;
                        L.b         = b;
                        L.intensity = 3.0f;        /* HDR scale; tonemap brings down */
                    }
                }
            }
        }
        rt.updateLights(cmd, lights, lightCount);

        if (layers.quadCount() > 0) {
            rt.rebuildAS(cmd);
            rt.dispatchRays(cmd,
                            engine.swapchainWidth(), engine.swapchainHeight(),
                            frameIdx, time);

            /* Barrier: rgen-writes to accumImage + gNormal + gHitPos
             * must complete before the denoise compute shader reads
             * them.  storageImage is the eventual write target — both
             * shader types touch it via storage access. */
            VkMemoryBarrier toCompute{};
            toCompute.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            toCompute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            toCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &toCompute, 0, nullptr, 0, nullptr);

            denoise.dispatch(cmd, engine.swapchainWidth(), engine.swapchainHeight());
        }

        engine.endFrame();
        ++frameIdx;
        firstFrame = false;
    }

    /* Tidy up GPU resources we created here. The Engine + RT + layer
     * destructors free everything they own. */
    vkDeviceWaitIdle(engine.device());
    if (atlas.sampler)    vkDestroySampler(engine.device(), atlas.sampler, nullptr);
    if (atlas.view)       vkDestroyImageView(engine.device(), atlas.view, nullptr);
    if (atlas.image)      vkDestroyImage(engine.device(), atlas.image, nullptr);
    if (atlas.memory)     vkFreeMemory(engine.device(), atlas.memory, nullptr);
    if (atlas.stagingBuf) vkDestroyBuffer(engine.device(), atlas.stagingBuf, nullptr);
    if (atlas.stagingMem) vkFreeMemory(engine.device(), atlas.stagingMem, nullptr);

    return 0;
}
