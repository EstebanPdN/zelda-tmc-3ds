#include "platform_gpu_3ds.h"
#include "top_view_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static C3D_RenderTarget* sTopTarget;
static C3D_RenderTarget* sBottomTarget;
static C3D_Tex sTopTexture;
static C3D_Tex sBottomTexture;
static C3D_Tex sSharpBilinearTexture;
static C3D_RenderTarget* sSharpBilinearTarget;
static Tex3DS_SubTexture sTopSubtexture;
static Tex3DS_SubTexture sSharpBilinearSubtexture;
static Tex3DS_SubTexture sBottomSubtexture;
static uint32_t* sTopUpload;
static uint32_t* sBottomUploads[2];
static void* sC2dFlushBase;
static size_t sC2dFlushSize;
static bool sFrameActive;
static bool sReady;
static bool sOld3DSProfile;
static bool sBottomTargetValid;
static PlatformGpu3DSStats sStats;
static unsigned sTopPresentWidth = 240;
static unsigned sTopPresentHeight = 160;
static unsigned sTopValidSourceWidth = 240;
static unsigned sTopValidSourceHeight = 160;
static Port3DSFullViewMode sTopPresentMode = PORT_3DS_FULL_VIEW_FALLBACK;
static int sTopCropX;
static int sTopCropY;

enum {
    TOP_TEXTURE_WIDTH = 512,
    TOP_TEXTURE_HEIGHT = 256,
    SHARP_BILINEAR_TEXTURE_WIDTH = 1024,
    SHARP_BILINEAR_TEXTURE_HEIGHT = 512,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern bool Port_Config_GetShowFps(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayStyle(void);
extern bool Port_Config_3DSFullViewComboEnabled(void);
extern double Port_PPU_3DS_CurrentFps(void);

static const uint8_t* StatusGlyph(char c) {
    static const uint8_t digits[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },
        { 14, 17, 1, 2, 4, 8, 31 },     { 30, 1, 1, 14, 1, 1, 30 },
        { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 16, 30, 1, 1, 30 },
        { 14, 16, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
        { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 1, 14 },
    };
    static const uint8_t letters[9][7] = {
        { 14, 17, 17, 31, 17, 17, 17 }, /* A */
        { 30, 17, 17, 17, 17, 17, 30 }, /* D */
        { 31, 16, 16, 30, 16, 16, 31 }, /* E */
        { 31, 16, 16, 30, 16, 16, 16 }, /* F */
        { 17, 27, 21, 21, 17, 17, 17 }, /* M */
        { 30, 17, 17, 30, 16, 16, 16 }, /* P */
        { 15, 16, 16, 14, 1, 1, 30 },   /* S */
        { 17, 17, 17, 17, 17, 17, 14 }, /* U */
        { 17, 17, 17, 17, 17, 10, 4 },  /* V */
    };
    static const uint8_t letterIds[26] = {
        0, 255, 255, 1, 2, 3, 255, 255, 255, 255, 255, 255, 4,
        255, 255, 5, 255, 255, 6, 255, 7, 8, 255, 255, 255, 255,
    };
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') {
        uint8_t id = letterIds[c - 'A'];
        if (id != 255) return letters[id];
    }
    return NULL;
}

static void DrawStatusText(float x, float y, float scale, const char* text) {
    const uint32_t color = C2D_Color32(255, 255, 255, 255);
    for (; *text; ++text, x += 6.0f * scale) {
        const uint8_t* glyph = StatusGlyph(*text);
        if (!glyph) continue;
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5;) {
                if ((glyph[row] & (1u << (4 - col))) == 0) {
                    ++col;
                    continue;
                }
                int end = col + 1;
                while (end < 5 && (glyph[row] & (1u << (4 - end))) != 0) ++end;
                C2D_DrawRectSolid(x + col * scale, y + row * scale, 0.8f,
                                  (end - col) * scale, scale, color);
                col = end;
            }
        }
    }
}

static u32 TextureTransfer(void) {
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

static void ConfigureAbgrTextureEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

    env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

/* The first Bilinear pass must preserve the upload texture's ABGR channel
 * order. The existing three-stage conversion is then applied exactly once,
 * when the intermediate texture is drawn to the physical top target. */
static void ConfigureIdentityTextureEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    C3D_TexEnvInit(C3D_GetTexEnv(1));
    C3D_TexEnvInit(C3D_GetTexEnv(2));
}

bool PlatformGpu3DS_Init(bool old3dsProfile) {
    memset(&sStats, 0, sizeof(sStats));
    sOld3DSProfile = old3dsProfile;
    sBottomTargetValid = false;
    sSharpBilinearTarget = NULL;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sTopUpload = (uint32_t*)linearMemAlign(TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t), 0x80);
    sBottomUploads[0] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    sBottomUploads[1] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    if (!sTopUpload || !sBottomUploads[0] || !sBottomUploads[1]) goto fail_linear;
    memset(sTopUpload, 0, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    memset(sBottomUploads[0], 0, 512u * 256u * sizeof(uint32_t));
    memset(sBottomUploads[1], 0, 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sTopUpload, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[0], 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[1], 512u * 256u * sizeof(uint32_t));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) goto fail_linear;
    if (!C2D_Init(128)) {
        C3D_Fini();
        goto fail_linear;
    }
    C2D_Prepare();
    C3D_BufInfo* c2dBuffers = C3D_GetBufInfo();
    if (c2dBuffers && c2dBuffers->bufCount > 0) {
        const u32 heapPhysical = osConvertVirtToPhys((void*)__ctru_linear_heap);
        const u32 vertexPhysical = c2dBuffers->base_paddr + c2dBuffers->buffers[0].offset;
        const uintptr_t heapStart = (uintptr_t)__ctru_linear_heap;
        const uintptr_t heapEnd = heapStart + __ctru_linear_heap_size;
        const uintptr_t vertexAddress = heapStart + (u32)(vertexPhysical - heapPhysical);
        const uintptr_t flushStart = vertexAddress & ~(uintptr_t)0x7Fu;
        uintptr_t flushEnd = flushStart + 64u * 1024u;
        if (flushEnd > heapEnd) flushEnd = heapEnd;
        if (flushStart >= heapStart && flushStart < flushEnd) {
            sC2dFlushBase = (void*)flushStart;
            sC2dFlushSize = flushEnd - flushStart;
        }
    }
    sStats.linearHeapBytes = __ctru_linear_heap_size;
    sStats.c2dFlushBytes = (uint32_t)sC2dFlushSize;
    sStats.c2dFlushAddress = (uintptr_t)sC2dFlushBase;
    sStats.topUploadAddress = (uintptr_t)sTopUpload;
    sStats.bottomUploadAddress[0] = (uintptr_t)sBottomUploads[0];
    sStats.bottomUploadAddress[1] = (uintptr_t)sBottomUploads[1];
    if (!C3D_TexInitVRAM(&sTopTexture, TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT, GPU_RGBA8)) goto fail;
    if (!C3D_TexInitVRAM(&sBottomTexture, 512, 256, GPU_RGBA8)) goto fail_top_texture;
    C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sBottomTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    /* 266x160 Wide is the largest fallback frame, so a 1024x512 container
     * safely holds its exact 532x320 nearest-neighbour 2x image. This target
     * has no depth buffer. Allocation failure is non-fatal: selecting
     * Bilinear then uses the established nearest-neighbour Scaled path. */
    if (C3D_TexInitVRAM(&sSharpBilinearTexture, SHARP_BILINEAR_TEXTURE_WIDTH,
                        SHARP_BILINEAR_TEXTURE_HEIGHT, GPU_RGBA8)) {
        C3D_TexSetFilter(&sSharpBilinearTexture, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&sSharpBilinearTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        sSharpBilinearTarget = C3D_RenderTargetCreateFromTex(
            &sSharpBilinearTexture, GPU_TEXFACE_2D, 0, -1);
        if (!sSharpBilinearTarget) {
            C3D_TexDelete(&sSharpBilinearTexture);
        } else {
            sStats.sharpBilinearAvailable = true;
            sStats.sharpBilinearTargetBytes =
                SHARP_BILINEAR_TEXTURE_WIDTH * SHARP_BILINEAR_TEXTURE_HEIGHT * sizeof(uint32_t);
        }
    }

    sTopTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    sBottomTarget = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (!sTopTarget || !sBottomTarget) goto fail_targets;
    const u32 output = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                       GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                       GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTargetSetOutput(sTopTarget, GFX_TOP, GFX_LEFT, output);
    C3D_RenderTargetSetOutput(sBottomTarget, GFX_BOTTOM, GFX_LEFT, output);
    sReady = true;
    return true;

fail_targets:
    if (sBottomTarget) C3D_RenderTargetDelete(sBottomTarget);
    if (sTopTarget) C3D_RenderTargetDelete(sTopTarget);
    if (sSharpBilinearTarget) {
        C3D_RenderTargetDelete(sSharpBilinearTarget);
        sSharpBilinearTarget = NULL;
    }
    if (sStats.sharpBilinearAvailable) {
        C3D_TexDelete(&sSharpBilinearTexture);
        sStats.sharpBilinearAvailable = false;
    }
    C3D_TexDelete(&sBottomTexture);
fail_top_texture:
    C3D_TexDelete(&sTopTexture);
fail:
    C2D_Fini();
    C3D_Fini();
fail_linear:
    if (sBottomUploads[1]) linearFree(sBottomUploads[1]);
    if (sBottomUploads[0]) linearFree(sBottomUploads[0]);
    if (sTopUpload) linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopUpload = NULL;
    return false;
}

uint32_t* PlatformGpu3DS_TopBuffer(void) { return sTopUpload; }
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index) {
    return index < 2 ? sBottomUploads[index] : NULL;
}

static void DrawTopImage(const uint32_t* pixels, unsigned width, unsigned height,
                         unsigned validSourceWidth, unsigned validSourceHeight,
                         Port3DSFullViewMode requestedMode, int cropX, int cropY) {
    const int style = Port_Config_Get3DSDisplayStyle();
    TopView3DSPlan plan;
    /* `requestedMode` is latched with the IO/OAM generation. A settings
     * change can occur after that generation was produced; do not reinterpret
     * its final experimental frame through the new live combo state. */
    TopView3DS_BuildPlan(sOld3DSProfile,
                         requestedMode != PORT_3DS_FULL_VIEW_FALLBACK,
                         Port_Config_Get3DSAspectRatio(), style, requestedMode,
                         (int)width, (int)height, (int)validSourceWidth,
                         (int)validSourceHeight, cropX, cropY, &plan);
    const Port3DSFullViewPresentation* presentation = &plan.source;
    sTopPresentWidth = (unsigned)presentation->renderWidth;
    sTopPresentHeight = (unsigned)presentation->renderHeight;
    sTopValidSourceWidth = validSourceWidth;
    sTopValidSourceHeight = validSourceHeight;
    sTopPresentMode = plan.mode;
    sTopCropX = presentation->sourceX;
    sTopCropY = presentation->sourceY;

    GSPGPU_FlushDataCache(pixels, TOP_TEXTURE_WIDTH * sTopPresentHeight * sizeof(uint32_t));
    /* Old 3DS only: the CPU renderer publishes 160 rows. Describe that exact
     * source rectangle so the display engine does not read another 96 unused
     * RGBA rows. New 3DS retains the established transfer dimensions. */
    const unsigned sourceHeight = sOld3DSProfile ? 160u : TOP_TEXTURE_HEIGHT;
    C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, sourceHeight),
                            (u32*)sTopTexture.data, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                            TextureTransfer());
    sTopSubtexture = (Tex3DS_SubTexture){
        .width = (u16)presentation->sourceWidth,
        .height = (u16)presentation->sourceHeight,
        .left = (float)presentation->sourceX / TOP_TEXTURE_WIDTH,
        .top = 1.0f - (float)presentation->sourceY / TOP_TEXTURE_HEIGHT,
        .right = (float)(presentation->sourceX + presentation->sourceWidth) / TOP_TEXTURE_WIDTH,
        .bottom = 1.0f - (float)(presentation->sourceY + presentation->sourceHeight) / TOP_TEXTURE_HEIGHT,
    };
    const C2D_Image image = { .tex = &sTopTexture, .subtex = &sTopSubtexture };
    const C2D_DrawParams params = {
        .pos = { .x = (float)plan.drawX, .y = (float)plan.drawY,
                 .w = (float)plan.drawWidth, .h = (float)plan.drawHeight },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    const unsigned intermediateWidth = (unsigned)presentation->sourceWidth * 2u;
    const unsigned intermediateHeight = (unsigned)presentation->sourceHeight * 2u;
    const bool useSharpBilinear = plan.useSharpBilinear && sSharpBilinearTarget &&
                                  intermediateWidth <= SHARP_BILINEAR_TEXTURE_WIDTH &&
                                  intermediateHeight <= SHARP_BILINEAR_TEXTURE_HEIGHT;
    if (useSharpBilinear) {
        const C2D_DrawParams integerParams = {
            .pos = { .x = 0.0f, .y = 0.0f,
                     .w = (float)intermediateWidth, .h = (float)intermediateHeight },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
        C2D_SceneBegin(sSharpBilinearTarget);
        ConfigureIdentityTextureEnv();
        C2D_DrawImage(image, &integerParams, NULL);

        /* Linear filtering can sample one texel beyond a subtexture edge.
         * The valid image starts on the texture's clamped top/left edges;
         * duplicate its final source column, row and corner into a one-texel
         * right/bottom guard instead of allowing stale atlas data to bleed. */
        const Tex3DS_SubTexture rightEdgeSubtexture = {
            .width = 1,
            .height = (u16)presentation->sourceHeight,
            .left = (float)(presentation->sourceX + presentation->sourceWidth - 1) /
                    TOP_TEXTURE_WIDTH,
            .top = 1.0f - (float)presentation->sourceY / TOP_TEXTURE_HEIGHT,
            .right = (float)(presentation->sourceX + presentation->sourceWidth) /
                     TOP_TEXTURE_WIDTH,
            .bottom = 1.0f -
                      (float)(presentation->sourceY + presentation->sourceHeight) /
                          TOP_TEXTURE_HEIGHT,
        };
        const Tex3DS_SubTexture bottomEdgeSubtexture = {
            .width = (u16)presentation->sourceWidth,
            .height = 1,
            .left = (float)presentation->sourceX / TOP_TEXTURE_WIDTH,
            .top = 1.0f -
                   (float)(presentation->sourceY + presentation->sourceHeight - 1) /
                       TOP_TEXTURE_HEIGHT,
            .right = (float)(presentation->sourceX + presentation->sourceWidth) /
                     TOP_TEXTURE_WIDTH,
            .bottom = 1.0f -
                      (float)(presentation->sourceY + presentation->sourceHeight) /
                          TOP_TEXTURE_HEIGHT,
        };
        const Tex3DS_SubTexture cornerSubtexture = {
            .width = 1,
            .height = 1,
            .left = rightEdgeSubtexture.left,
            .top = bottomEdgeSubtexture.top,
            .right = rightEdgeSubtexture.right,
            .bottom = bottomEdgeSubtexture.bottom,
        };
        const C2D_Image rightEdgeImage = { .tex = &sTopTexture, .subtex = &rightEdgeSubtexture };
        const C2D_Image bottomEdgeImage = { .tex = &sTopTexture, .subtex = &bottomEdgeSubtexture };
        const C2D_Image cornerImage = { .tex = &sTopTexture, .subtex = &cornerSubtexture };
        const C2D_DrawParams rightEdgeParams = {
            .pos = { .x = (float)intermediateWidth, .y = 0.0f,
                     .w = 1.0f, .h = (float)intermediateHeight },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        const C2D_DrawParams bottomEdgeParams = {
            .pos = { .x = 0.0f, .y = (float)intermediateHeight,
                     .w = (float)intermediateWidth, .h = 1.0f },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        const C2D_DrawParams cornerParams = {
            .pos = { .x = (float)intermediateWidth, .y = (float)intermediateHeight,
                     .w = 1.0f, .h = 1.0f },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        C2D_DrawImage(rightEdgeImage, &rightEdgeParams, NULL);
        C2D_DrawImage(bottomEdgeImage, &bottomEdgeParams, NULL);
        C2D_DrawImage(cornerImage, &cornerParams, NULL);

        /* Beginning the physical scene flushes the complete nearest pass
         * before this texture is sampled. UVs cover only the valid 2x image,
         * never the unused power-of-two container. */
        sSharpBilinearSubtexture = (Tex3DS_SubTexture){
            .width = (u16)intermediateWidth,
            .height = (u16)intermediateHeight,
            .left = 0.0f,
            .top = 1.0f,
            .right = (float)intermediateWidth / SHARP_BILINEAR_TEXTURE_WIDTH,
            .bottom = 1.0f - (float)intermediateHeight / SHARP_BILINEAR_TEXTURE_HEIGHT,
        };
        const C2D_Image intermediateImage = {
            .tex = &sSharpBilinearTexture, .subtex = &sSharpBilinearSubtexture
        };
        C2D_SceneBegin(sTopTarget);
        C3D_TexSetFilter(&sSharpBilinearTexture, GPU_LINEAR, GPU_LINEAR);
        C2D_DrawImage(intermediateImage, &params, NULL);
        ConfigureAbgrTextureEnv();
        ++sStats.sharpBilinearFrames;
    } else {
        const GPU_TEXTURE_FILTER_PARAM filter = plan.linearFilter ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(&sTopTexture, filter, filter);
        C2D_SceneBegin(sTopTarget);
        C2D_DrawImage(image, &params, NULL);
        ConfigureAbgrTextureEnv();
        if (plan.useSharpBilinear) ++sStats.sharpBilinearFallbacks;
    }
    if (Port_Config_GetShowFps()) {
        char label[20];
        double fps = Port_PPU_3DS_CurrentFps();
        unsigned rounded = fps > 0.0 ? (unsigned)(fps + 0.5) : 0u;
        if (rounded > 999u) rounded = 999u;
        snprintf(label, sizeof(label), "FPS %u", rounded);
        C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, 82.0f, 20.0f, C2D_Color32(0, 0, 0, 210));
        DrawStatusText(10.0f, 219.0f, 2.0f, label);
    }
}

void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width, unsigned height,
                             unsigned validSourceWidth, unsigned validSourceHeight,
                             Port3DSFullViewMode mode, int cropX, int cropY) {
    if (!sReady || !pixels) return;
    if (!C3D_FrameBegin(0)) {
        ++sStats.frameBeginFailures;
        return;
    }
    sFrameActive = true;
    DrawTopImage(pixels, width, height, validSourceWidth, validSourceHeight,
                 mode, cropX, cropY);
    ++sStats.topTransfers;
}

bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return false;
    if (changed) {
        GSPGPU_FlushDataCache(pixels, 512u * 240u * sizeof(uint32_t));
        const unsigned sourceHeight = sOld3DSProfile ? 240u : 256u;
        C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(512, sourceHeight),
                                (u32*)sBottomTexture.data, GX_BUFFER_DIM(512, 256), TextureTransfer());
        ++sStats.bottomTransfers;
    }
    if (!sOld3DSProfile || changed || !sBottomTargetValid) {
        sBottomSubtexture = (Tex3DS_SubTexture){
            .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
            .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
        };
        const C2D_Image image = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
        const C2D_DrawParams params = {
            .pos = { .x = 0.0f, .y = 0.0f, .w = 320.0f, .h = 240.0f },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        C2D_TargetClear(sBottomTarget, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(sBottomTarget);
        C2D_DrawImage(image, &params, NULL);
        ConfigureAbgrTextureEnv();
        sBottomTargetValid = true;
        ++sStats.bottomTargetDraws;
    } else {
        /* The physical bottom image and its hitbox generation are unchanged.
         * Old 3DS can leave that render target displayed instead of clearing,
         * drawing and scheduling an identical output transfer on every top
         * presentation. New 3DS retains the established two-target frame. */
        ++sStats.bottomTargetReuseSkips;
    }
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) {
        GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        sStats.boundedFlushBytes += sC2dFlushSize;
    }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    ++sStats.frames;
    sStats.drawingTime = C3D_GetDrawingTime();
    sStats.processingTime = C3D_GetProcessingTime();
    sFrameActive = false;
    return true;
}

void PlatformGpu3DS_ShowDumpSavedOverlay(void) {
    if (!sReady || !sTopUpload || !sBottomUploads[0] || !C3D_FrameBegin(0)) return;
    sFrameActive = true;
    DrawTopImage(sTopUpload, sTopPresentWidth, sTopPresentHeight,
                 sTopValidSourceWidth, sTopValidSourceHeight,
                 sTopPresentMode, sTopCropX, sTopCropY);
    C2D_DrawRectSolid(132.0f, 12.0f, 0.7f, 136.0f, 24.0f, C2D_Color32(0, 0, 0, 220));
    DrawStatusText(141.0f, 17.0f, 2.0f, "DUMP SAVED");

    sBottomSubtexture = (Tex3DS_SubTexture){
        .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
    };
    const C2D_Image bottomImage = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
    const C2D_DrawParams bottomParams = {
        .pos = { .x = 0.0f, .y = 0.0f, .w = 320.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_SceneBegin(sBottomTarget);
    C2D_DrawImage(bottomImage, &bottomParams, NULL);
    ConfigureAbgrTextureEnv();
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    sFrameActive = false;
    gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    svcSleepThread(600000000LL);
}

void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats) {
    if (stats) *stats = sStats;
}

void PlatformGpu3DS_InvalidateBottomTarget(void) {
    /* HOME and lid sleep may invalidate or rotate the physical framebuffer.
     * Force one opaque redraw after APT resumes before Old 3DS starts reusing
     * the unchanged target again. */
    sBottomTargetValid = false;
    sTopPresentWidth = 240;
    sTopPresentHeight = 160;
    sTopValidSourceWidth = 240;
    sTopValidSourceHeight = 160;
    sTopPresentMode = PORT_3DS_FULL_VIEW_FALLBACK;
    sTopCropX = 0;
    sTopCropY = 0;
}

void PlatformGpu3DS_Shutdown(void) {
    if (!sReady) return;
    if (sFrameActive) {
        C2D_Flush();
        if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    }
    if (!aptShouldClose()) C3D_FrameSync();
    C3D_RenderTargetDelete(sBottomTarget);
    C3D_RenderTargetDelete(sTopTarget);
    if (sSharpBilinearTarget) C3D_RenderTargetDelete(sSharpBilinearTarget);
    if (sStats.sharpBilinearAvailable) C3D_TexDelete(&sSharpBilinearTexture);
    C3D_TexDelete(&sBottomTexture);
    C3D_TexDelete(&sTopTexture);
    C2D_Fini();
    C3D_Fini();
    linearFree(sBottomUploads[1]);
    linearFree(sBottomUploads[0]);
    linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopUpload = NULL;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sFrameActive = false;
    sReady = false;
    sOld3DSProfile = false;
    sBottomTargetValid = false;
    sSharpBilinearTarget = NULL;
}
