#include "port_ppu_gpu_3ds.h"

#include "ppu_gpu_3ds_shader_shbin.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPU_GPU3DS_MAX_VERTICES = 32768,
    PPU_GPU3DS_MAX_INDICES = 49152,
    PPU_GPU3DS_MAX_BATCHES = 4096,
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

    if (!C3D_TexInitVRAM(&sOutputTexture, 256, 256, GPU_RGBA5551)) goto fail;
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

bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame) {
    if (!sReady || sDisabled || !frame) return false;
    sCommands.vertexCount = 0;
    sCommands.indexCount = 0;
    sCommands.batchCount = 0;
    sPrepared = true;
    return true;
}

bool PortPpuGpu3DS_DrawPrepared(void) {
    if (!sReady || sDisabled || !sPrepared || !C3D_FrameDrawOn(sOutputTarget)) return false;
    C3D_BindProgram(&sProgram);
    C3D_SetAttrInfo(&sAttributes);
    C3D_SetBufInfo(&sBuffers);
    C3D_TexBind(0, &sAtlas);
    sPrepared = false;
    return true;
}

C3D_Tex* PortPpuGpu3DS_OutputTexture(void) {
    return sReady && !sDisabled ? &sOutputTexture : NULL;
}

void PortPpuGpu3DS_Disable(void) {
    sDisabled = true;
    sPrepared = false;
}
