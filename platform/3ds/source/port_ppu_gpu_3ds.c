#include "platform_3ds.h"
#include "port_ppu_gpu_3ds.h"

#include "platform_gpu_3ds.h"

#include "ppu_gpu_3ds_shader_shbin.h"

extern bool Port_Config_GpuViewportOffset(void);
extern int Port_Config_GpuScissorMode(void);
extern bool Port_Config_GpuStencil(void);
void Platform3DS_Debug(const char* message);

#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum {
    /* Half the vertex space is reserved for the per-layer map slices, so the
     * per-band fallback needs headroom of its own; 16-bit indices cap this at
     * 65536 vertices. 1.2 MB of linear memory against ~12 MB free. */
    /* Heavy frames wanted 49500 vertices against a 49148 capacity and fell back
     * to a full software render, which costs about 15 ms -- far more than the
     * memory. Indices are 16-bit, so 65536 vertices is the hard ceiling and the
     * reset quad still lands at 65532..65535. 65536 quads' worth of indices is
     * 98304. The extra ~440 KB comes from a linear heap with megabytes free. */
    PPU_GPU3DS_MAX_VERTICES = 65536,
    PPU_GPU3DS_MAX_INDICES = 98304,
    /* The tail of each buffer belongs to the backend, not the builder: four
     * vertices and six indices for a full-screen quad that resets the stencil.
     * See ClearStencilPlane. */
    PPU_GPU3DS_CLEAR_FIRST_VERTEX = PPU_GPU3DS_MAX_VERTICES - 4,
    PPU_GPU3DS_CLEAR_FIRST_INDEX = PPU_GPU3DS_MAX_INDICES - 6,
    PPU_GPU3DS_BUILD_VERTICES = PPU_GPU3DS_CLEAR_FIRST_VERTEX,
    PPU_GPU3DS_BUILD_INDICES = PPU_GPU3DS_CLEAR_FIRST_INDEX,
    PPU_GPU3DS_MAX_BATCHES = 4096,
    PPU_GPU3DS_OUTPUT_WIDTH = 512,
    PPU_GPU3DS_OUTPUT_HEIGHT = 256,
    PPU_GPU3DS_REGION_SHIFT = 14,
    /* Coalescing budget for atlas uploads: a GSP flush call costs far more
     * than flushing the slots sitting in a gap. */
    PPU_GPU3DS_ATLAS_FLUSH_RANGES = 2,
    PPU_GPU3DS_ATLAS_FLUSH_GAP = 64,
    /* A scene that falls back for this many frames will not produce a verdict
     * worth waiting for. */
    PPU_GPU3DS_PARITY_MAX_DEFERRALS = 240,
    PPU_GPU3DS_ALPHA_COMPLEMENT = 1u << 13u,
};

static PpuGpu3DSCache* sCache;
static uint32_t sDirtyBitmap[PPU_GPU3DS_SLOT_COUNT / 32];
static PpuGpu3DSVertex* sVertices;
static uint16_t* sIndices;
static PpuGpu3DSBatch* sBatches;
static PpuGpu3DSCommandBuffer sCommands;
static uint16_t* sParityReference;
static uint16_t* sParityReadback;
static C3D_Tex sAtlas;
static C3D_Tex sOutputTexture;
static C3D_RenderTarget* sOutputTarget;
static DVLB_s* sShader;
static shaderProgram_s sProgram;
static int sOffsetUniform = -1;
static float sLastOffsetX, sLastOffsetY;
static C3D_AttrInfo sAttributes;
static C3D_BufInfo sBuffers;
static bool sProgramInitialized;
static bool sReady;
static bool sDisabled;
static bool sPrepared;
static unsigned sPreparedWidth;
static unsigned sPreparedHeight;
static uint32_t sFrame;
static bool sParityRequested;
static unsigned sParityDeferrals;
static bool sParityReferenceReady;
static bool sParitySubmitted;
static bool sParityFinishedThisFrame;
static unsigned sParityWidth;
static unsigned sParityHeight;
static PortPpuGpu3DSStats sStats;

void PortPpuGpu3DS_Shutdown(void) {
    sPrepared = false;
    sReady = false;

    if (sOutputTarget) C3D_RenderTargetDelete(sOutputTarget);
    if (sOutputTexture.data) C3D_TexDelete(&sOutputTexture);
    if (sProgramInitialized) shaderProgramFree(&sProgram);
    if (sShader) DVLB_Free(sShader);
    if (sAtlas.data) C3D_TexDelete(&sAtlas);
    free(sParityReference);
    if (sParityReadback) linearFree(sParityReadback);
    free(sBatches);
    if (sIndices) linearFree(sIndices);
    if (sVertices) linearFree(sVertices);
    free(sCache);

    sCache = NULL;
    sVertices = NULL;
    sIndices = NULL;
    sBatches = NULL;
    sParityReference = NULL;
    sParityReadback = NULL;
    sOutputTarget = NULL;
    sShader = NULL;
    sProgramInitialized = false;
    sDisabled = false;
    sParityRequested = false;
    sParityReferenceReady = false;
    sParitySubmitted = false;
    sParityFinishedThisFrame = false;
    sCommands = (PpuGpu3DSCommandBuffer){ 0 };
    sPreparedWidth = 0;
    sPreparedHeight = 0;
    sFrame = 0;
    sParityWidth = 0;
    sParityHeight = 0;
    sStats.initialized = false;
    sStats.enabled = false;
    sStats.disabled = false;
    sAtlas = (C3D_Tex){ 0 };
    sOutputTexture = (C3D_Tex){ 0 };
    sProgram = (shaderProgram_s){ 0 };
    sAttributes = (C3D_AttrInfo){ 0 };
    sBuffers = (C3D_BufInfo){ 0 };
}

bool PortPpuGpu3DS_Init(void) {
    if (sReady) return true;

    memset(&sStats, 0, sizeof(sStats));
    sCache = malloc(sizeof(*sCache));
    sVertices = linearMemAlign(PPU_GPU3DS_MAX_VERTICES * sizeof(*sVertices), 0x80);
    sIndices = linearMemAlign(PPU_GPU3DS_MAX_INDICES * sizeof(*sIndices), 0x80);
    sBatches = malloc(PPU_GPU3DS_MAX_BATCHES * sizeof(*sBatches));
    sParityReference = malloc(PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
                              sizeof(*sParityReference));
    sParityReadback = linearMemAlign(PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
                                         sizeof(*sParityReadback),
                                     0x80);
    if (!sCache || !sVertices || !sIndices || !sBatches || !sParityReference ||
        !sParityReadback)
        goto fail;
    memset(sParityReference, 0,
           PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
               sizeof(*sParityReference));
    memset(sParityReadback, 0,
           PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
               sizeof(*sParityReadback));
    Platform3DS_CleanDataCache(
        sParityReadback, PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
                             sizeof(*sParityReadback));

    PpuGpu3DS_CacheInit(sCache);
    /* Indices never change, so fill them once and flush them once. */
    PpuGpu3DS_FillStaticIndices(sIndices, PPU_GPU3DS_MAX_INDICES);
    Platform3DS_CleanDataCache(sIndices, PPU_GPU3DS_MAX_INDICES * sizeof(*sIndices));

    /* The builder stops short of the reset quad at the end of both buffers. */
    {
        PpuGpu3DSVertex* quad = sVertices + PPU_GPU3DS_CLEAR_FIRST_VERTEX;
        static const float kX[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
        static const float kY[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        static const uint8_t kOrder[6] = { 0, 1, 2, 2, 1, 3 };
        for (unsigned corner = 0; corner < 4u; ++corner) {
            quad[corner].x = kX[corner];
            quad[corner].y = kY[corner];
            quad[corner].z = 0.0f;
            quad[corner].u = 0;
            quad[corner].v = 0;
        }
        (void)kOrder;  /* the static index pattern already covers this quad */
        Platform3DS_CleanDataCache(quad, 4u * sizeof(*quad));
    }
    PpuGpu3DS_CommandInit(&sCommands, sVertices, PPU_GPU3DS_BUILD_VERTICES, sIndices,
                          PPU_GPU3DS_BUILD_INDICES, sBatches, PPU_GPU3DS_MAX_BATCHES);

    if (!C3D_TexInit(&sAtlas, PPU_GPU3DS_ATLAS_SIDE, PPU_GPU3DS_ATLAS_SIDE,
                     GPU_RGBA5551))
        goto fail;
    memset(sAtlas.data, 0, sAtlas.size);
    C3D_TexFlush(&sAtlas);
    C3D_TexSetFilter(&sAtlas, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    if (!C3D_TexInitVRAM(&sOutputTexture, PPU_GPU3DS_OUTPUT_WIDTH,
                         PPU_GPU3DS_OUTPUT_HEIGHT, GPU_RGBA5551))
        goto fail;
    C3D_TexSetFilter(&sOutputTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sOutputTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sOutputTarget = C3D_RenderTargetCreateFromTex(&sOutputTexture, GPU_TEXFACE_2D, 0,
                                                  GPU_RB_DEPTH24_STENCIL8);
    if (!sOutputTarget) goto fail;
    /* Windows and blending are built entirely on the stencil, so without one
     * every gated batch resolves to whatever the bits happen to hold: the
     * screen comes out uniformly clear-coloured or uniformly blended rather
     * than merely imperfect. citro3d hands back a target regardless and only
     * clears depthBuf/depthMask, so check them and leave the frame to the
     * software rasterizer rather than draw something meaningless. */
    {
        const C3D_FrameBuf* fb = &sOutputTarget->frameBuf;
        char line[160];
        snprintf(line, sizeof(line),
                 "[tmc3ds] gpu depth buffer %p fmt=%d mask=%d vram free %zu\n",
                 fb->depthBuf, (int)fb->depthFmt, (int)fb->depthMask,
                 (size_t)vramSpaceFree());
        Platform3DS_Debug(line);
        if (!fb->depthBuf || fb->depthFmt != GPU_RB_DEPTH24_STENCIL8 ||
            (fb->depthMask & 0x1) == 0) {
            Platform3DS_Debug("[tmc3ds] no stencil attachment; PICA200 disabled\n");
            goto fail;
        }
    }

    sShader = DVLB_ParseFile((u32*)ppu_gpu_3ds_shader_shbin,
                             ppu_gpu_3ds_shader_shbin_size);
    if (!sShader || sShader->numDVLE == 0) goto fail;
    if (R_FAILED(shaderProgramInit(&sProgram))) goto fail;
    sProgramInitialized = true;
    if (R_FAILED(shaderProgramSetVsh(&sProgram, &sShader->DVLE[0]))) goto fail;
    sOffsetUniform = shaderInstanceGetUniformLocation(sProgram.vertexShader,
                                                      "uOffset");
    if (sOffsetUniform < 0) goto fail;

    AttrInfo_Init(&sAttributes);
    if (AttrInfo_AddLoader(&sAttributes, 0, GPU_FLOAT, 3) < 0 ||
        AttrInfo_AddLoader(&sAttributes, 1, GPU_SHORT, 2) < 0)
        goto fail;
    BufInfo_Init(&sBuffers);
    if (BufInfo_Add(&sBuffers, sVertices, sizeof(*sVertices), 2, 0x10) < 0) goto fail;

    sDisabled = false;
    sPrepared = false;
    sReady = true;
    sStats.initialized = true;
    sStats.enabled = true;
    return true;

fail:
    PortPpuGpu3DS_Shutdown();
    return false;
}

static u32 ClearColor(uint16_t gbaColor) {
    /* The output target is GPU_RGBA5551, so C3D_FrameBufClear fills 16 bits at
     * a time and only the low half of this value ever reaches memory. Returning
     * RGBA8888 meant a black backdrop cleared to 0x00ff -- red 0, green 0,
     * blue 31, alpha 1, i.e. saturated blue. That is the blue behind the intro
     * cutscene text, and it is why the parity surface read gpu=00ff where the
     * software renderer had 0001. Platform-identical, so it is wrong under an
     * emulator too. */
    const u32 packed = PpuGpu3DS_PackRgba5551(gbaColor, true);
    return (packed << 16u) | packed;
}

static bool FinishPreflight(bool ready, uint64_t startTick) {
    sStats.preflightTicks += svcGetSystemTick() - startTick;
    if (sCache) {
        sStats.tileHits = sCache->hits;
        sStats.tileDecodes = sCache->decodes;
    }
    if (!ready) ++sStats.fallbackFrames;
    return ready;
}

bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame) {
    const uint64_t startTick = svcGetSystemTick();
    ++sStats.attemptedFrames;
    sPrepared = false;
    if (!sReady || sDisabled || !frame || !frame->memory.bg_palette ||
        !frame->memory.obj_palette)
        return FinishPreflight(false, startTick);

    sCommands.vertexCount = 0;
    sCommands.indexCount = 0;
    sCommands.batchCount = 0;
    const uint64_t buildStartTick = svcGetSystemTick();
    Platform3DS_SetStage(22);
    PpuGpu3DS_CacheBeginFrame(sCache, frame->memory.bg_palette,
                              frame->memory.obj_palette, ++sFrame);
    if (!PpuGpu3DS_BuildCommands(frame, sCache, (uint16_t*)sAtlas.data,
                                 &sCommands)) {
        sStats.buildTicks += svcGetSystemTick() - buildStartTick;
        if (sCommands.failReason < PPU_GPU3DS_BUILD_REASON_COUNT)
            ++sStats.buildFailures[sCommands.failReason];
        if (sCommands.bandCount > sStats.maxBands)
            sStats.maxBands = sCommands.bandCount;
        if (sCommands.requiredVertices > sStats.maxRequiredVertices)
            sStats.maxRequiredVertices = sCommands.requiredVertices;
        return FinishPreflight(false, startTick);
    }
    sStats.lastBuildTicks = svcGetSystemTick() - buildStartTick;
    sStats.buildTicks += sStats.lastBuildTicks;
    sStats.lastBands = sCommands.bandCount;
    sStats.lastVertices = (uint32_t)sCommands.vertexCount;
    sStats.lastMapLayerMask = sCommands.mapLayerMask;
    for (unsigned reason = 0; reason < PPU_GPU3DS_MAP_REJECT_COUNT; ++reason)
        sStats.mapRejects[reason] += sCommands.mapReject[reason];
    for (unsigned reason = 0; reason < PPU_GPU3DS_MAP_REBUILD_COUNT; ++reason)
        sStats.mapRebuildReason[reason] += sCommands.mapRebuild[reason];
    for (unsigned bg = 0; bg < 4u; ++bg) {
        if ((sCommands.mapLayerMask & (1u << bg)) != 0) ++sStats.mapLayers;
    }
    /* mapDirtyMask is the subset that had to re-emit quads. Against mapLayers
     * this gives the reuse hit rate, which is what decides whether the
     * per-frame signature walk is earning its cost. */
    for (unsigned bg = 0; bg < 4u; ++bg) {
        if ((sCommands.mapDirtyMask & (1u << bg)) != 0) ++sStats.mapRebuilds;
    }
    if (sCommands.mapLargestQuads > sStats.mapLargestQuads)
        sStats.mapLargestQuads = sCommands.mapLargestQuads;
    if (sCommands.bandCount > sStats.maxBands)
        sStats.maxBands = sCommands.bandCount;
    if (sCommands.vertexCount > sStats.maxVertices)
        sStats.maxVertices = (uint32_t)sCommands.vertexCount;
    if (sCommands.batchCount > sStats.maxBatches)
        sStats.maxBatches = (uint32_t)sCommands.batchCount;

    const uint64_t flushStartTick = svcGetSystemTick();
    /* Only the slots decoded this frame need uploading, but each
     * GSPGPU_FlushDataCache is a GSP round trip measured at ~330 us on an Old
     * 3DS -- a hundred times the cost of the few kilobytes a call covers. So
     * the dirty slots are coalesced into a handful of ranges and the extra
     * bytes are paid gladly to avoid the calls. */
    if (sCache->dirtyCount != 0) {
        unsigned firstSlot = PPU_GPU3DS_SLOT_COUNT, lastSlot = 0;
        memset(sDirtyBitmap, 0, sizeof(sDirtyBitmap));
        for (unsigned index = 0; index < sCache->dirtyCount; ++index) {
            const unsigned slot = sCache->dirtySlots[index];
            sDirtyBitmap[slot / 32u] |= 1u << (slot % 32u);
            if (slot < firstSlot) firstSlot = slot;
            if (slot > lastSlot) lastSlot = slot;
        }

        /* Walk the bitmap into runs, merging any gap smaller than the bytes a
         * separate call would cost. */
        unsigned rangeFirst[PPU_GPU3DS_ATLAS_FLUSH_RANGES];
        unsigned rangeLast[PPU_GPU3DS_ATLAS_FLUSH_RANGES];
        unsigned rangeCount = 0;
        for (unsigned slot = firstSlot; slot <= lastSlot; ++slot) {
            if ((sDirtyBitmap[slot / 32u] & (1u << (slot % 32u))) == 0) continue;
            if (rangeCount != 0 &&
                slot - rangeLast[rangeCount - 1] <= PPU_GPU3DS_ATLAS_FLUSH_GAP) {
                rangeLast[rangeCount - 1] = slot;
                continue;
            }
            if (rangeCount == PPU_GPU3DS_ATLAS_FLUSH_RANGES) {
                /* Out of ranges: one span covers everything left. */
                rangeLast[rangeCount - 1] = lastSlot;
                break;
            }
            rangeFirst[rangeCount] = slot;
            rangeLast[rangeCount] = slot;
            ++rangeCount;
        }

        bool flushed = true;
        for (unsigned range = 0; range < rangeCount && flushed; ++range) {
            const size_t bytes =
                    ((size_t)rangeLast[range] - rangeFirst[range] + 1u) * 64u *
                    sizeof(uint16_t);
            uint16_t* pixels =
                    (uint16_t*)sAtlas.data + (size_t)rangeFirst[range] * 64u;
            flushed = Platform3DS_CleanDataCache(pixels, bytes);
            if (flushed) sStats.atlasFlushBytes += bytes;
        }
        sStats.atlasFlushCalls += rangeCount;
        if (!flushed) {
            sCommands.vertexCount = 0;
            sCommands.indexCount = 0;
            sCommands.batchCount = 0;
            return FinishPreflight(false, startTick);
        }
        PpuGpu3DS_CacheClearDirty(sCache);
    Platform3DS_SetStage(23);
    }
    /* Same economics as the atlas: one call spanning the rebuilt slices and
     * the per-band geometry beats a call per region. A slice that was kept
     * from last frame is only re-flushed when it sits between two that were
     * not, which costs bytes rather than calls. */
    Platform3DS_SetStage(24);
    size_t geometryFirstVertex = sCommands.dynamicFirstVertex;
    size_t geometryFirstIndex = sCommands.dynamicFirstIndex;
    for (unsigned bg = 0; bg < 4u; ++bg) {
        if ((sCommands.mapDirtyMask & (1u << bg)) == 0) continue;
        const size_t sliceVertex = (size_t)bg * PPU_GPU3DS_MAP_SLICE_VERTICES;
        if (sliceVertex < geometryFirstVertex) {
            geometryFirstVertex = sliceVertex;
            geometryFirstIndex = (size_t)bg * PPU_GPU3DS_MAP_SLICE_INDICES;
        }
    }
    (void)geometryFirstIndex;  /* indices are static; only vertices stream */
    if (sCommands.vertexCount > geometryFirstVertex) {
        if (!Platform3DS_CleanDataCache(
                    sVertices + geometryFirstVertex,
                    (sCommands.vertexCount - geometryFirstVertex) *
                            sizeof(*sVertices))) {
            sCommands.vertexCount = 0;
            sCommands.indexCount = 0;
            sCommands.batchCount = 0;
            return FinishPreflight(false, startTick);
        }
    }

    Platform3DS_SetStage(25);
    sStats.flushTicks += svcGetSystemTick() - flushStartTick;
    sPreparedWidth = frame->width;
    sPreparedHeight = frame->height;
    sPrepared = true;
    return FinishPreflight(true, startTick);
}

/* Set for the frame when any batch writes the object-window stencil bit.
 * Regions OUTSIDE and OBJWIN only mean something relative to that bit, so
 * without a writer the test can only reject work that should have drawn --
 * and both halves of an alpha pair carry it, so a stale bit drops the pixel
 * entirely and leaves the clear colour behind. */
static bool sHasObjWindow;

static unsigned BatchRegion(const PpuGpu3DSBatch* batch) {
    const unsigned region = batch->color >> PPU_GPU3DS_REGION_SHIFT;
    /* Collapse both object-window regions onto "no window" when nothing
     * writes the bit they test. */
    if (!sHasObjWindow && region < 2u) return 2u;
    return region;
}

static u32 Coefficient8(unsigned coefficient) {
    return coefficient == 16u ? 0xffu : coefficient * 255u / 16u;
}

/* Reprogramming the combiners and the blender writes a good many registers
 * into the command list, and consecutive batches usually want exactly the same
 * configuration, so each is applied only when it actually changes. The keys
 * cover every field the two functions read. */
/* Resolved once per frame from the config: the offset that places the frame
 * where the presenter samples it. */
static u32 sViewportOffset;

static uint32_t sTevKey = 0xffffffffu;
static uint32_t sBlendKey = 0xffffffffu;
/* The scissor is reprogrammed per batch, and a frame can carry 391 of them
 * while consecutive batches usually cover the same rectangle -- a band's
 * layers, or an alpha pair drawn twice over identical geometry. Each call
 * writes registers into the command list, so skipping the unchanged ones makes
 * the list shorter, which is GSP work on core 1 as well as CPU here. Same
 * treatment the TEV and blender already get. */
static uint32_t sScissorKeyLow = 0xffffffffu;
static uint32_t sScissorKeyHigh = 0xffffffffu;

static void SetScissorCached(u32 left, u32 bottom, u32 right, u32 top) {
    const uint32_t low = ((uint32_t)left << 16u) | (uint32_t)(bottom & 0xffffu);
    const uint32_t high = ((uint32_t)right << 16u) | (uint32_t)(top & 0xffffu);
    if (low == sScissorKeyLow && high == sScissorKeyHigh) return;
    sScissorKeyLow = low;
    sScissorKeyHigh = high;
    C3D_SetScissor(GPU_SCISSOR_NORMAL, left, bottom, right, top);
}

static uint32_t TevKey(const PpuGpu3DSBatch* batch) {
    return (uint32_t)batch->effect | ((uint32_t)batch->evy << 8u);
}

static uint32_t BlendKey(const PpuGpu3DSBatch* batch) {
    return (uint32_t)batch->effect | ((uint32_t)batch->eva << 8u) |
           ((uint32_t)batch->evb << 16u);
}

static void ResetTev(const PpuGpu3DSBatch* batch) {
    const uint32_t key = TevKey(batch);
    if (key == sTevKey) return;
    sTevKey = key;
    for (int stage = 0; stage < 3; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_TexEnv* texture = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(texture, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE0,
                  GPU_TEXTURE0);
    C3D_TexEnvFunc(texture, C3D_Both, GPU_REPLACE);
    if (batch->effect != PPU_GPU3DS_EFFECT_BRIGHTEN &&
        batch->effect != PPU_GPU3DS_EFFECT_DARKEN)
        return;

    C3D_TexEnv* effect = C3D_GetTexEnv(1);
    const u32 constant =
            (batch->effect == PPU_GPU3DS_EFFECT_BRIGHTEN ? 0x00ffffffu : 0) |
            (Coefficient8(batch->evy) << 24u);
    C3D_TexEnvColor(effect, constant);
    C3D_TexEnvSrc(effect, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS,
                  GPU_CONSTANT);
    C3D_TexEnvOpRgb(effect, GPU_TEVOP_RGB_SRC_COLOR,
                    GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(effect, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(effect, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS,
                  GPU_PREVIOUS);
    C3D_TexEnvFunc(effect, C3D_Alpha, GPU_REPLACE);
}

static void ResetBlend(const PpuGpu3DSBatch* batch) {
    const uint32_t key = BlendKey(batch);
    if (key == sBlendKey) return;
    sBlendKey = key;
    if (batch->effect == PPU_GPU3DS_EFFECT_ALPHA) {
        const u32 eva = Coefficient8(batch->eva);
        const u32 evb = Coefficient8(batch->evb);
        C3D_BlendingColor(eva | (eva << 8u) | (eva << 16u) | (evb << 24u));
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_COLOR,
                       GPU_CONSTANT_ALPHA, GPU_ONE, GPU_ZERO);
    } else {
        C3D_BlendingColor(0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO,
                       GPU_ONE, GPU_ZERO);
    }
}

static void SetWindowStencil(const PpuGpu3DSBatch* batch) {
    if (!Port_Config_GpuStencil()) {
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
        return;
    }
    const unsigned region = BatchRegion(batch);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    if (region == 0u)
        C3D_StencilTest(true, GPU_EQUAL, 0, 0x04, 0);
    else if (region == 1u)
        C3D_StencilTest(true, GPU_EQUAL, 0x04, 0x04, 0);
    else
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
}

/* Every path that relies on the stencil keeps the depth test enabled with
 * GPU_ALWAYS rather than disabling it. The two are equivalent as tests, but on
 * PICA200 the stencil belongs to the depth-stencil stage, and a console draws
 * nothing for the stencil-gated batches while an emulator draws them -- which
 * is exactly the band that came back as the clear colour. */
static void SetColorStencil(const PpuGpu3DSBatch* batch) {
    if (!Port_Config_GpuStencil()) {
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
        return;
    }
    const unsigned region = BatchRegion(batch);
    const bool alphaBranch =
            batch->effect == PPU_GPU3DS_EFFECT_ALPHA ||
            (batch->color & PPU_GPU3DS_ALPHA_COMPLEMENT) != 0;
    const bool oldTarget =
            batch->effect == PPU_GPU3DS_EFFECT_ALPHA;
    unsigned mask = region < 2u ? 0x04u : 0;
    unsigned ref = region == 1u ? 0x04u : 0;
    if (alphaBranch) {
        mask |= 0x08u;
        if (oldTarget) ref |= 0x08u;
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                      oldTarget == (batch->target2 != 0)
                              ? GPU_STENCIL_KEEP
                              : GPU_STENCIL_INVERT);
    } else {
        if (batch->target2) ref |= 0x08u;
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                      GPU_STENCIL_REPLACE);
    }
    C3D_StencilTest(true, mask ? GPU_EQUAL : GPU_ALWAYS, ref, mask, 0x08);
}

/* The vertex shader adds this to every position; only map-space background
 * batches use a non-zero value. */
static void SetBatchOffset(float x, float y) {
    if (sOffsetUniform < 0 || (x == sLastOffsetX && y == sLastOffsetY)) return;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, sOffsetUniform, x, y, 0.0f, 0.0f);
    sLastOffsetX = x;
    sLastOffsetY = y;
}

/* Counted so the log can show the stall condition was actually present and is
 * now being skipped, rather than leaving that as an inference. */
static unsigned long long sEmptyDrawsSkipped;

unsigned long long PortPpuGpu3DS_EmptyDrawsSkipped(void) {
    return sEmptyDrawsSkipped;
}

static void DrawBatch(const PpuGpu3DSBatch* batch) {
    /* A zero-count draw never signals completion on PICA200: the GX queue's
     * interrupt count never reaches the number of entries queued, and the next
     * C3D_FrameBegin waits on it forever. Frames that froze the console carried
     * 4 to 16 of these; frames that rendered carried none. */
    if (batch->indexCount == 0) {
        ++sEmptyDrawsSkipped;
        return;
    }
    SetBatchOffset(batch->offsetX, batch->offsetY);
    const u32 yOffset = sViewportOffset;
    u32 top, bottom;
    switch (Port_Config_GpuScissorMode()) {
        case 0:
            /* Horizontal clipping only; a band's own geometry already covers
             * the right scanlines apart from tile-row overhang. */
            top = 0;
            bottom = PPU_GPU3DS_OUTPUT_HEIGHT;
            break;
        case 2:
            top = yOffset + batch->firstLine;
            bottom = yOffset + batch->firstLine + batch->lineCount;
            break;
        default: {
            /* Unsigned: a band overhanging the bottom of the frame wrapped this
             * to ~4e9, and citro3d packs the scissor as top | (bottom << 16),
             * so the box became garbage silently. */
            const unsigned end = (unsigned)batch->firstLine + batch->lineCount;
            const unsigned clampedEnd =
                    end > sPreparedHeight ? sPreparedHeight : end;
            top = yOffset + sPreparedHeight - clampedEnd;
            bottom = yOffset + sPreparedHeight - batch->firstLine;
            break;
        }
    }
    SetScissorCached(batch->scissorLeft, top, batch->scissorRight, bottom);
    C3D_DrawElements(GPU_TRIANGLES, (int)batch->indexCount,
                     C3D_UNSIGNED_SHORT, sIndices + batch->firstIndex);
}

static bool FinishDraw(bool rendered, uint64_t startTick) {
    sStats.drawTicks += svcGetSystemTick() - startTick;
    if (rendered) {
        ++sStats.renderedFrames;
        sStats.vertices += sCommands.vertexCount;
        sStats.batches += sCommands.batchCount;
    }
    return rendered;
}

/* One-shot dump of the batch list for the first multi-band frame. The fault
 * being chased is a whole band not appearing, so what matters is whether its
 * batches exist and with what state -- which no host replay can answer, since
 * the host command stream is already known to be correct. */
static bool sBatchesLogged;

static void LogBatchList(void) {
    /* Log the frame parity is about to compare -- that is the frame that comes
     * out wrong, and the title screen that logs first is not it. */
    if (sBatchesLogged || !sParityReferenceReady) return;
    sBatchesLogged = true;
    char line[224];
    snprintf(line, sizeof(line), "[tmc3ds] batches=%u viewport=%lu\n",
             (unsigned)sCommands.batchCount, (unsigned long)sViewportOffset);
    Platform3DS_Debug(line);
    for (size_t i = 0; i < sCommands.batchCount && i < 20u; ++i) {
        const PpuGpu3DSBatch* b = &sCommands.batches[i];
        snprintf(line, sizeof(line),
                 "[tmc3ds]  b%u L%u p%u line=%u+%u idx=%u+%u sc=%u..%u eff=%u "
                 "t2=%u win=%02x col=%04x\n",
                 (unsigned)i, b->layer, b->priority, b->firstLine, b->lineCount,
                 b->firstIndex, b->indexCount, b->scissorLeft, b->scissorRight,
                 b->effect, b->target2, b->windowControl, b->color);
        Platform3DS_Debug(line);
    }
}

static void ClearStencilPlane(void) {
    SetBatchOffset(0.0f, 0.0f);
    const u32 yOffset = sViewportOffset;
    SetScissorCached(0, yOffset, sPreparedWidth, yOffset + sPreparedHeight);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    /* Writemask 0 keeps this to the stencil: no colour, no depth. */
    C3D_DepthTest(true, GPU_ALWAYS, (GPU_WRITEMASK)0);
    C3D_StencilTest(true, GPU_ALWAYS, 0, 0xff, 0xff);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
    C3D_DrawElements(GPU_TRIANGLES, 6, C3D_UNSIGNED_SHORT,
                     sIndices + PPU_GPU3DS_CLEAR_FIRST_INDEX);
}

bool PortPpuGpu3DS_DrawPrepared(void) {
    const uint64_t startTick = svcGetSystemTick();
    /* citro2d programs the scissor for its own draws between our frames, so
     * the cache cannot survive across a frame boundary. */
    sScissorKeyLow = 0xffffffffu;
    sScissorKeyHigh = 0xffffffffu;
    if (!sReady || sDisabled || !sPrepared || sCommands.batchCount == 0)
        return FinishDraw(false, startTick);
    sViewportOffset = Port_Config_GpuViewportOffset()
                              ? PPU_GPU3DS_OUTPUT_HEIGHT - sPreparedHeight
                              : 0u;
    sHasObjWindow = false;
    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        if (sCommands.batches[i].objWindow) { sHasObjWindow = true; break; }
    }
    C3D_RenderTargetClear(sOutputTarget, C3D_CLEAR_ALL,
                          ClearColor(sCommands.batches[0].color), 0);
    if (!C3D_FrameDrawOn(sOutputTarget)) {
        sPrepared = false;
        return FinishDraw(false, startTick);
    }
    sPrepared = false;
    /* Scanline 0 has to land in target row 0, where the presenter samples it,
     * so the viewport sits at the top of the 512x256 surface; the scissor is
     * in the same space and must be rebased with it. */
    const u32 yOffset = sViewportOffset;
    C3D_SetViewport(0, yOffset, sPreparedWidth, sPreparedHeight);
    SetScissorCached(0, yOffset, sPreparedWidth, yOffset + sPreparedHeight);
    /* Depth is emitted as -(128 - index)/129, so state the mapping instead of
     * inheriting whatever the Citro2D presenter last configured. */
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_BindProgram(&sProgram);
    C3D_SetAttrInfo(&sAttributes);
    C3D_SetBufInfo(&sBuffers);
    C3D_CullFace(GPU_CULL_NONE);
    /* Nothing about the previous frame's state survives here. */
    sLastOffsetX = 1.0f;
    sLastOffsetY = 1.0f;
    SetBatchOffset(0.0f, 0.0f);
    sTevKey = 0xffffffffu;
    sBlendKey = 0xffffffffu;
    for (int stage = 0; stage < 6; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_TexSetFilter(&sAtlas, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0, &sAtlas);
    LogBatchList();

    /* Windows and blending read the stencil as though it starts at zero every
     * frame. citro3d's target clear is the only thing that guarantees that,
     * and it skips the depth/stencil fill entirely when the attachment is
     * missing while still clearing colour -- which reads on screen as a frame
     * that is uniformly clear-coloured (every gated batch rejected) or
     * uniformly blended (only the alpha half passing). Resetting the plane
     * with a draw costs one quad and depends on nothing. */
    ClearStencilPlane();

    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &sCommands.batches[i];
        if (!batch->objWindow) continue;
        ResetTev(batch);
        ResetBlend(batch);
        C3D_AlphaTest(true, GPU_GREATER, 0);
        C3D_DepthTest(true, GPU_ALWAYS, (GPU_WRITEMASK)0);
        C3D_StencilTest(true, GPU_ALWAYS, 0x04, 0xff, 0x04);
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                      GPU_STENCIL_REPLACE);
        DrawBatch(batch);
    }

    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &sCommands.batches[i];
        if (batch->layer != PPU_GPU3DS_OBJ || batch->objWindow) continue;
        ResetTev(batch);
        ResetBlend(batch);
        C3D_AlphaTest(true, GPU_GREATER, 0);
        SetWindowStencil(batch);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_DEPTH);
        DrawBatch(batch);
    }

    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &sCommands.batches[i];
        if (batch->objWindow) continue;
        if (batch->layer == PPU_GPU3DS_BACKDROP) {
            ResetTev(batch);
            ResetBlend(batch);
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
            C3D_DepthTest(true, GPU_ALWAYS, (GPU_WRITEMASK)0);
            C3D_StencilTest(true, GPU_ALWAYS, 0x08, 0, 0x08);
            C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                          GPU_STENCIL_REPLACE);
            DrawBatch(batch);
            continue;
        }
        ResetTev(batch);
        ResetBlend(batch);
        C3D_AlphaTest(true, GPU_GREATER, 0);
        SetColorStencil(batch);
        if (batch->layer == PPU_GPU3DS_OBJ)
            C3D_DepthTest(true, GPU_EQUAL, GPU_WRITE_COLOR);
        else
            C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_COLOR);
        DrawBatch(batch);
    }

    for (int stage = 0; stage < 3; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_BlendingColor(0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO,
                   GPU_ONE, GPU_ZERO);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
    return FinishDraw(true, startTick);
}

/* The presenter reuses this shader for its static quad, so its vertices must be
 * PpuGpu3DSVertex itself (platform_gpu_3ds.c aliases PresentVertex to it) --
 * the attribute info bound here describes the packed 16-byte layout, including
 * the int16 UV the shader rescales by 1/PPU_GPU3DS_UV_SCALE. */
bool PortPpuGpu3DS_BindPresentShader(void) {
    if (!sReady) return false;
    C3D_BindProgram(&sProgram);
    C3D_SetAttrInfo(&sAttributes);
    SetBatchOffset(0.0f, 0.0f);
    return true;
}

void* PortPpuGpu3DS_OutputTexture(void) {
    return sReady && !sDisabled ? &sOutputTexture : NULL;
}

void PortPpuGpu3DS_Disable(void) {
    /* Retiring the GPU path is the loudest event this renderer has: everything
     * afterwards runs on the software rasterizer, and the reason is otherwise
     * only visible in a dump taken before the evidence is overwritten. */
    if (!sDisabled) {
        char line[192];
        snprintf(line, sizeof(line),
                 "[tmc3ds] PICA200 retired: parity %llu/%llu differ=%llu "
                 "structural=%llu first=%lu,%lu offset=%lu\n",
                 (unsigned long long)sStats.parityChecks,
                 (unsigned long long)sStats.parityFailures,
                 (unsigned long long)sStats.differingPixels,
                 (unsigned long long)sStats.structuralPixels,
                 (unsigned long)sStats.firstDiffX,
                 (unsigned long)sStats.firstDiffY,
                 (unsigned long)sViewportOffset);
        Platform3DS_Debug(line);
    }
    sDisabled = true;
    sPrepared = false;
    sParityRequested = false;
    sParityReferenceReady = false;
    sParitySubmitted = false;
    sStats.enabled = false;
    sStats.disabled = true;
}

bool PortPpuGpu3DS_IsDisabled(void) {
    return sDisabled;
}

void PortPpuGpu3DS_RecordDisabledFrame(void) {
    if (sReady && sDisabled) ++sStats.disabledFrames;
}

void PortPpuGpu3DS_GetStats(PortPpuGpu3DSStats* stats) {
    if (!stats) return;
    sStats.initialized = sReady;
    sStats.enabled = sReady && !sDisabled;
    sStats.disabled = sDisabled;
    *stats = sStats;
}

/* ---------------------------------------------------------------------------
 * Last-frame capture
 *
 * A fault that corrects itself the moment a dump is taken cannot be captured
 * by the parity path: that path re-renders in software and rewinds HDMA before
 * anything is written, so it records the healthy frame. This reads back the
 * output texture at the very start of a frame, while it still holds what the
 * screen is actually showing, and touches no game state at all.
 * ------------------------------------------------------------------------ */
static bool sCaptureRequested;
static bool sCaptureSubmitted;

void PortPpuGpu3DS_RequestFrameCapture(void) {
    if (sReady && !sDisabled && !sCaptureSubmitted) sCaptureRequested = true;
}

bool PortPpuGpu3DS_FrameCaptureRequested(void) {
    return sCaptureRequested;
}

bool PortPpuGpu3DS_QueueFrameCapture(void) {
    if (!sCaptureRequested || !sParityReadback) return false;
    sCaptureRequested = false;
    if (!PlatformGpu3DS_QueueRgba5551Readback(&sOutputTexture, sParityReadback))
        return false;
    sCaptureSubmitted = true;
    return true;
}

void PortPpuGpu3DS_WriteFrameCapture(const char* directory) {
    if (!sCaptureSubmitted || !directory || directory[0] == 0) return;
    sCaptureSubmitted = false;
    if (R_FAILED(GSPGPU_InvalidateDataCache(
                sParityReadback, PPU_GPU3DS_OUTPUT_WIDTH *
                                         PPU_GPU3DS_OUTPUT_HEIGHT *
                                         sizeof(*sParityReadback))))
        return;
    char path[224];
    snprintf(path, sizeof(path), "%s/gpu-frame.bin", directory);
    FILE* file = fopen(path, "wb");
    if (!file) return;
    fwrite(sParityReadback, sizeof(*sParityReadback),
           PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT, file);
    fclose(file);
}

void PortPpuGpu3DS_RequestParityCheck(void) {
    if (sReady && !sDisabled && !sParityRequested && !sParitySubmitted) {
        sParityRequested = true;
        sParityReferenceReady = false;
        sParityDeferrals = 0;
    }
}

bool PortPpuGpu3DS_ParityRequested(void) {
    return sParityRequested;
}

void PortPpuGpu3DS_CaptureParityReference(const uint32_t* pixels, unsigned pitch,
                                          unsigned width, unsigned height) {
    if (!sParityRequested || !pixels || pitch < width ||
        width > PPU_GPU3DS_OUTPUT_WIDTH || height > PPU_GPU3DS_OUTPUT_HEIGHT) {
        sParityRequested = false;
        sParityReferenceReady = false;
        return;
    }
    for (unsigned y = 0; y < height; ++y) {
        const uint32_t* source = pixels + (size_t)y * pitch;
        uint16_t* reference = sParityReference + (size_t)y * PPU_GPU3DS_OUTPUT_WIDTH;
        for (unsigned x = 0; x < width; ++x)
            reference[x] = PpuGpu3DS_PackAbgr8888(source[x]);
    }
    sParityWidth = width;
    sParityHeight = height;
    sParityReferenceReady = true;
}

bool PortPpuGpu3DS_QueueParityCopy(void) {
    if (!sParityRequested || !sParityReferenceReady ||
        !PlatformGpu3DS_QueueRgba5551Readback(&sOutputTexture, sParityReadback))
        return false;
    sParityRequested = false;
    sParityReferenceReady = false;
    sParitySubmitted = true;
    return true;
}

void PortPpuGpu3DS_DeferParityCheck(void) {
    if (!sParityRequested) return;
    if (++sParityDeferrals > PPU_GPU3DS_PARITY_MAX_DEFERRALS) {
        PortPpuGpu3DS_CancelParityCheck();
        return;
    }
    /* Keep the request alive but drop the captured reference: the next frame
     * the GPU can build gets a fresh one taken from its own state. */
    sParityReferenceReady = false;
}

void PortPpuGpu3DS_CancelParityCheck(void) {
    sParityRequested = false;
    sParityReferenceReady = false;
    sParityDeferrals = 0;
}

bool PortPpuGpu3DS_ParityFinishedThisFrame(void) {
    return sParityFinishedThisFrame;
}

/* A 5-bit channel pair that differs by one step is the documented floor of
 * PICA200 blending: the GBA folds `(a*eva + b*evb) >> 4` with four fractional
 * bits, and an 8-bit blender constant cannot carry the last one. Anything
 * larger is a real renderer defect. */
static bool ChannelsWithinOneStep(uint16_t reference, uint16_t readback) {
    static const unsigned shifts[3] = { 11u, 6u, 1u };
    for (unsigned channel = 0; channel < 3; ++channel) {
        const int left = (int)((reference >> shifts[channel]) & 0x1fu);
        const int right = (int)((readback >> shifts[channel]) & 0x1fu);
        const int delta = left > right ? left - right : right - left;
        if (delta > 1) return false;
    }
    return (reference & 1u) == (readback & 1u);
}

static void WriteParitySurfaces(uint64_t differing) {
    const char* directory = Port_PPU_3DS_LastDumpDirectory();
    if (!directory || directory[0] == 0) return;
    static const struct {
        const char* name;
        const uint16_t* pixels;
    } surfaces[2] = { { "parity-cpu.bin", NULL }, { "parity-gpu.bin", NULL } };
    const uint16_t* sources[2] = { sParityReference, sParityReadback };
    char path[224];
    for (unsigned index = 0; index < 2; ++index) {
        snprintf(path, sizeof(path), "%s/%s", directory, surfaces[index].name);
        FILE* file = fopen(path, "wb");
        if (!file) continue;
        fwrite(sources[index], sizeof(uint16_t),
               PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT, file);
        fclose(file);
    }

    snprintf(path, sizeof(path), "%s/parity-diff.txt", directory);
    FILE* report = fopen(path, "w");
    if (!report) return;
    fprintf(report, "differing %llu of %u visible pixels (%ux%u)\n",
            (unsigned long long)differing, sParityWidth * sParityHeight,
            sParityWidth, sParityHeight);
    unsigned listed = 0;
    for (unsigned y = 0; y < sParityHeight && listed < 256u; ++y) {
        const uint16_t* reference =
                sParityReference + (size_t)y * PPU_GPU3DS_OUTPUT_WIDTH;
        const uint16_t* readback =
                sParityReadback + (size_t)y * PPU_GPU3DS_OUTPUT_WIDTH;
        for (unsigned x = 0; x < sParityWidth && listed < 256u; ++x) {
            if (reference[x] == readback[x]) continue;
            fprintf(report, "%u,%u cpu=%04x gpu=%04x %s\n", x, y, reference[x],
                    readback[x],
                    ChannelsWithinOneStep(reference[x], readback[x])
                            ? "one-step"
                            : "STRUCTURAL");
            ++listed;
        }
    }
    fclose(report);
}

void PortPpuGpu3DS_FinishParityCheck(void) {
    sParityFinishedThisFrame = false;
    if (!sParitySubmitted) return;
    /* FrameBegin waits for the prior Citro3D queue, including the queued
     * readback, and leaves this frame active for the selected render path. */
    if (!PlatformGpu3DS_BeginCustomTop()) {
        /* Busy, not broken: leave the comparison armed for a later frame rather
         * than retiring the renderer over a skipped frame begin. */
        return;
    }
    sParitySubmitted = false;
    sParityFinishedThisFrame = true;
    ++sStats.parityChecks;
    if (R_FAILED(GSPGPU_InvalidateDataCache(
            sParityReadback, PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
                                 sizeof(*sParityReadback)))) {
        ++sStats.parityFailures;
        PortPpuGpu3DS_Disable();
        return;
    }

    uint64_t differing = 0;
    uint64_t structural = 0;
    uint32_t firstX = 0;
    uint32_t firstY = 0;
    /* The viewport places scanline y in target row y and the untiled readback
     * keeps that row order, so both buffers index identically. */
    for (unsigned y = 0; y < sParityHeight; ++y) {
        const uint16_t* reference =
            sParityReference + (size_t)y * PPU_GPU3DS_OUTPUT_WIDTH;
        const uint16_t* readback =
            sParityReadback + (size_t)y * PPU_GPU3DS_OUTPUT_WIDTH;
        for (unsigned x = 0; x < sParityWidth; ++x) {
            if (reference[x] == readback[x]) continue;
            if (differing == 0) {
                firstX = x;
                firstY = y;
            }
            ++differing;
            if (!ChannelsWithinOneStep(reference[x], readback[x])) ++structural;
        }
    }

    if (differing == 0) return;
    sStats.differingPixels += differing;
    sStats.structuralPixels += structural;
    sStats.firstDiffX = firstX;
    sStats.firstDiffY = firstY;
    WriteParitySurfaces(differing);
    if (structural == 0) return;
    /* Only a difference the blender cannot explain retires the GPU path. */
    ++sStats.parityFailures;
    PortPpuGpu3DS_Disable();
}
