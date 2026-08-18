#include "port_ppu_gpu_3ds.h"

#include "ppu_gpu_3ds_shader_shbin.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPU_GPU3DS_MAX_VERTICES = 32768,
    PPU_GPU3DS_MAX_INDICES = 49152,
    PPU_GPU3DS_MAX_BATCHES = 4096,
    PPU_GPU3DS_OUTPUT_WIDTH = 512,
    PPU_GPU3DS_OUTPUT_HEIGHT = 256,
};

static PpuGpu3DSCache* sCache;
static PpuGpu3DSVertex* sVertices;
static uint16_t* sIndices;
static PpuGpu3DSBatch* sBatches;
static PpuGpu3DSCommandBuffer sCommands;
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

void PortPpuGpu3DS_Shutdown(void) {
    sPrepared = false;
    sReady = false;

    if (sOutputTarget) C3D_RenderTargetDelete(sOutputTarget);
    if (sOutputTexture.data) C3D_TexDelete(&sOutputTexture);
    if (sProgramInitialized) shaderProgramFree(&sProgram);
    if (sShader) DVLB_Free(sShader);
    if (sAtlas.data) C3D_TexDelete(&sAtlas);
    free(sBatches);
    if (sIndices) linearFree(sIndices);
    if (sVertices) linearFree(sVertices);
    free(sCache);

    sCache = NULL;
    sVertices = NULL;
    sIndices = NULL;
    sBatches = NULL;
    sOutputTarget = NULL;
    sShader = NULL;
    sProgramInitialized = false;
    sDisabled = false;
    sCommands = (PpuGpu3DSCommandBuffer){ 0 };
    sPreparedWidth = 0;
    sPreparedHeight = 0;
    sFrame = 0;
    sAtlas = (C3D_Tex){ 0 };
    sOutputTexture = (C3D_Tex){ 0 };
    sProgram = (shaderProgram_s){ 0 };
    sAttributes = (C3D_AttrInfo){ 0 };
    sBuffers = (C3D_BufInfo){ 0 };
}

bool PortPpuGpu3DS_Init(void) {
    if (sReady) return true;

    sCache = malloc(sizeof(*sCache));
    sVertices = linearMemAlign(PPU_GPU3DS_MAX_VERTICES * sizeof(*sVertices), 0x80);
    sIndices = linearMemAlign(PPU_GPU3DS_MAX_INDICES * sizeof(*sIndices), 0x80);
    sBatches = malloc(PPU_GPU3DS_MAX_BATCHES * sizeof(*sBatches));
    if (!sCache || !sVertices || !sIndices || !sBatches) goto fail;

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

bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame) {
    sPrepared = false;
    if (!sReady || sDisabled || !frame || !frame->memory.bg_palette ||
        !frame->memory.obj_palette)
        return false;

    sCommands.vertexCount = 0;
    sCommands.indexCount = 0;
    sCommands.batchCount = 0;
    PpuGpu3DS_CacheBeginFrame(sCache, frame->memory.bg_palette,
                              frame->memory.obj_palette, ++sFrame);
    if (!PpuGpu3DS_BuildCommands(frame, sCache, (uint16_t*)sAtlas.data,
                                 &sCommands)) {
        return false;
    }

    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        PpuGpu3DSCacheEntry* entry = &sCache->entries[slot];
        if (!entry->dirty) continue;
        uint16_t* pixels = (uint16_t*)sAtlas.data + (size_t)slot * 64u;
        if (R_FAILED(GSPGPU_FlushDataCache(pixels, 64u * sizeof(*pixels)))) {
            sCommands.vertexCount = 0;
            sCommands.indexCount = 0;
            sCommands.batchCount = 0;
            return false;
        }
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
        return false;
    }

    sPreparedWidth = frame->width;
    sPreparedHeight = frame->height;
    sPrepared = true;
    return true;
}

bool PortPpuGpu3DS_DrawPrepared(void) {
    if (!sReady || sDisabled || !sPrepared || sCommands.batchCount == 0)
        return false;
    C3D_RenderTargetClear(sOutputTarget, C3D_CLEAR_ALL,
                          ClearColor(sCommands.batches[0].color), 0);
    if (!C3D_FrameDrawOn(sOutputTarget)) {
        sPrepared = false;
        return false;
    }
    sPrepared = false;
    C3D_SetViewport(0, 0, sPreparedWidth, sPreparedHeight);
    C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, sPreparedWidth, sPreparedHeight);
    C3D_BindProgram(&sProgram);
    C3D_SetAttrInfo(&sAttributes);
    C3D_SetBufInfo(&sBuffers);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

    for (int stage = 0; stage < 6; ++stage) {
        C3D_TexEnvInit(C3D_GetTexEnv(stage));
    }
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexSetFilter(&sAtlas, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0, &sAtlas);
    C3D_AlphaTest(true, GPU_GREATER, 0);

    for (size_t i = 1; i < sCommands.batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &sCommands.batches[i];
        const u32 top =
                sPreparedHeight - (batch->firstLine + batch->lineCount);
        const u32 bottom = sPreparedHeight - batch->firstLine;
        C3D_SetScissor(GPU_SCISSOR_NORMAL, batch->scissorLeft, top,
                       batch->scissorRight, bottom);
        C3D_DrawElements(GPU_TRIANGLES, (int)batch->indexCount,
                         C3D_UNSIGNED_SHORT, sIndices + batch->firstIndex);
    }
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    return true;
}

C3D_Tex* PortPpuGpu3DS_OutputTexture(void) {
    return sReady && !sDisabled ? &sOutputTexture : NULL;
}

void PortPpuGpu3DS_Disable(void) {
    sDisabled = true;
    sPrepared = false;
}
