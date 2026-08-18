#include "port_ppu_gpu_3ds.h"

#include "platform_gpu_3ds.h"

#include "ppu_gpu_3ds_shader_shbin.h"

#include <3ds.h>
#include <citro3d.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPU_GPU3DS_MAX_VERTICES = 32768,
    PPU_GPU3DS_MAX_INDICES = 49152,
    PPU_GPU3DS_MAX_BATCHES = 4096,
    PPU_GPU3DS_OUTPUT_WIDTH = 512,
    PPU_GPU3DS_OUTPUT_HEIGHT = 256,
    PPU_GPU3DS_REGION_SHIFT = 14,
    PPU_GPU3DS_ALPHA_COMPLEMENT = 1u << 13u,
};

static PpuGpu3DSCache* sCache;
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
    GSPGPU_FlushDataCache(
        sParityReadback, PPU_GPU3DS_OUTPUT_WIDTH * PPU_GPU3DS_OUTPUT_HEIGHT *
                             sizeof(*sParityReadback));

    PpuGpu3DS_CacheInit(sCache);
    PpuGpu3DS_CommandInit(&sCommands, sVertices, PPU_GPU3DS_MAX_VERTICES, sIndices,
                          PPU_GPU3DS_MAX_INDICES, sBatches, PPU_GPU3DS_MAX_BATCHES);

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

    sShader = DVLB_ParseFile((u32*)ppu_gpu_3ds_shader_shbin,
                             ppu_gpu_3ds_shader_shbin_size);
    if (!sShader || sShader->numDVLE == 0) goto fail;
    if (R_FAILED(shaderProgramInit(&sProgram))) goto fail;
    sProgramInitialized = true;
    if (R_FAILED(shaderProgramSetVsh(&sProgram, &sShader->DVLE[0]))) goto fail;

    AttrInfo_Init(&sAttributes);
    if (AttrInfo_AddLoader(&sAttributes, 0, GPU_FLOAT, 4) < 0 ||
        AttrInfo_AddLoader(&sAttributes, 1, GPU_FLOAT, 2) < 0)
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
    const u32 red5 = gbaColor & 0x1fu;
    const u32 green5 = (gbaColor >> 5u) & 0x1fu;
    const u32 blue5 = (gbaColor >> 10u) & 0x1fu;
    const u32 red8 = (red5 << 3u) | (red5 >> 2u);
    const u32 green8 = (green5 << 3u) | (green5 >> 2u);
    const u32 blue8 = (blue5 << 3u) | (blue5 >> 2u);
    return (red8 << 24u) | (green8 << 16u) | (blue8 << 8u) | 0xffu;
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
    PpuGpu3DS_CacheBeginFrame(sCache, frame->memory.bg_palette,
                              frame->memory.obj_palette, ++sFrame);
    if (!PpuGpu3DS_BuildCommands(frame, sCache, (uint16_t*)sAtlas.data,
                                 &sCommands))
        return FinishPreflight(false, startTick);

    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        PpuGpu3DSCacheEntry* entry = &sCache->entries[slot];
        if (!entry->dirty) continue;
        uint16_t* pixels = (uint16_t*)sAtlas.data + (size_t)slot * 64u;
        if (R_FAILED(GSPGPU_FlushDataCache(pixels, 64u * sizeof(*pixels)))) {
            sCommands.vertexCount = 0;
            sCommands.indexCount = 0;
            sCommands.batchCount = 0;
            return FinishPreflight(false, startTick);
        }
        sStats.atlasFlushBytes += 64u * sizeof(*pixels);
        entry->dirty = false;
    }
    if ((sCommands.vertexCount != 0 &&
         R_FAILED(GSPGPU_FlushDataCache(
                 sVertices, sCommands.vertexCount * sizeof(*sVertices)))) ||
        (sCommands.indexCount != 0 &&
         R_FAILED(GSPGPU_FlushDataCache(
                 sIndices, sCommands.indexCount * sizeof(*sIndices))))) {
        sCommands.vertexCount = 0;
        sCommands.indexCount = 0;
        sCommands.batchCount = 0;
        return FinishPreflight(false, startTick);
    }

    sPreparedWidth = frame->width;
    sPreparedHeight = frame->height;
    sPrepared = true;
    return FinishPreflight(true, startTick);
}

static unsigned BatchRegion(const PpuGpu3DSBatch* batch) {
    return batch->color >> PPU_GPU3DS_REGION_SHIFT;
}

static u32 Coefficient8(unsigned coefficient) {
    return coefficient == 16u ? 0xffu : coefficient * 255u / 16u;
}

static void ResetTev(const PpuGpu3DSBatch* batch) {
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
    if (batch->effect == PPU_GPU3DS_EFFECT_ALPHA) {
        const u32 eva = Coefficient8(batch->eva);
        C3D_BlendingColor(eva | (eva << 8u) | (eva << 16u) |
                          (Coefficient8(batch->evb) << 24u));
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_COLOR,
                       GPU_CONSTANT_ALPHA, GPU_ONE, GPU_ZERO);
    } else {
        C3D_BlendingColor(0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO,
                       GPU_ONE, GPU_ZERO);
    }
}

static void SetWindowStencil(const PpuGpu3DSBatch* batch) {
    const unsigned region = BatchRegion(batch);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    if (region == 0u)
        C3D_StencilTest(true, GPU_EQUAL, 0, 0x04, 0);
    else if (region == 1u)
        C3D_StencilTest(true, GPU_EQUAL, 0x04, 0x04, 0);
    else
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
}

static void SetColorStencil(const PpuGpu3DSBatch* batch) {
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

static void DrawBatch(const PpuGpu3DSBatch* batch) {
    const u32 top = sPreparedHeight - (batch->firstLine + batch->lineCount);
    const u32 bottom = sPreparedHeight - batch->firstLine;
    C3D_SetScissor(GPU_SCISSOR_NORMAL, batch->scissorLeft, top,
                   batch->scissorRight, bottom);
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

bool PortPpuGpu3DS_DrawPrepared(void) {
    const uint64_t startTick = svcGetSystemTick();
    if (!sReady || sDisabled || !sPrepared || sCommands.batchCount == 0)
        return FinishDraw(false, startTick);
    C3D_RenderTargetClear(sOutputTarget, C3D_CLEAR_ALL,
                          ClearColor(sCommands.batches[0].color), 0);
    if (!C3D_FrameDrawOn(sOutputTarget)) {
        sPrepared = false;
        return FinishDraw(false, startTick);
    }
    sPrepared = false;
    C3D_SetViewport(0, 0, sPreparedWidth, sPreparedHeight);
    C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, sPreparedWidth, sPreparedHeight);
    C3D_BindProgram(&sProgram);
    C3D_SetAttrInfo(&sAttributes);
    C3D_SetBufInfo(&sBuffers);
    C3D_CullFace(GPU_CULL_NONE);
    for (int stage = 0; stage < 6; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_TexSetFilter(&sAtlas, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0, &sAtlas);

    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &sCommands.batches[i];
        if (!batch->objWindow) continue;
        ResetTev(batch);
        ResetBlend(batch);
        C3D_AlphaTest(true, GPU_GREATER, 0);
        C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)0);
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
            C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)0);
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
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
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

void* PortPpuGpu3DS_OutputTexture(void) {
    return sReady && !sDisabled ? &sOutputTexture : NULL;
}

void PortPpuGpu3DS_Disable(void) {
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

void PortPpuGpu3DS_RequestParityCheck(void) {
    if (sReady && !sDisabled && !sParityRequested && !sParitySubmitted) {
        sParityRequested = true;
        sParityReferenceReady = false;
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

void PortPpuGpu3DS_CancelParityCheck(void) {
    sParityRequested = false;
    sParityReferenceReady = false;
}

bool PortPpuGpu3DS_ParityFinishedThisFrame(void) {
    return sParityFinishedThisFrame;
}

void PortPpuGpu3DS_FinishParityCheck(void) {
    sParityFinishedThisFrame = false;
    if (!sParitySubmitted) return;
    /* FrameBegin waits for the prior Citro3D queue, including the queued
     * readback, and leaves this frame active for the selected render path. */
    if (!PlatformGpu3DS_BeginCustomTop()) {
        ++sStats.parityChecks;
        ++sStats.parityFailures;
        PortPpuGpu3DS_Disable();
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
    uint32_t firstX = 0;
    uint32_t firstY = 0;
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
        }
    }

    if (differing == 0) return;
    ++sStats.parityFailures;
    sStats.differingPixels += differing;
    sStats.firstDiffX = firstX;
    sStats.firstDiffY = firstY;
    PortPpuGpu3DS_Disable();
}
