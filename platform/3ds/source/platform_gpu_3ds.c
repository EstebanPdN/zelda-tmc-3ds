#include "platform_3ds.h"
#include "platform_gpu_3ds.h"
#include "top_view_3ds.h"
#include "port_ppu_gpu_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Which renderer produced the frame on screen, so the overlay can say so
 * without needing a log read over FTP. */
/* The static quad is drawn with the PPU's own shader and attribute info, so it
 * must use the PPU's vertex type -- not a look-alike. A separate struct here
 * silently became wrong the moment the vertex was packed to 16 bytes (UV is
 * int16 scaled by PPU_GPU3DS_UV_SCALE, and the stride shrank), which would have
 * mis-sampled the whole top screen. Aliasing the real type keeps them in step
 * by construction. */
typedef PpuGpu3DSVertex PresentVertex;
static PresentVertex* sPresentQuad;
static bool sPresentQuadValid;
static float sPresentQuadW, sPresentQuadH;
static unsigned sPresentQuadWidth;

/* Declared locally: port_runtime_config.h pulls in port_types.h, whose
 * u32/s32 collide with libctru's. */
bool Port_Config_GpuStaticQuad(void);
bool Port_Config_CompactUpload(void);
bool Port_Config_BottomRgb565(void);

static bool sLastFrameUsedGpu;
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
static PlatformGpu3DSUploadLayout sUploadLayout;
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

_Static_assert(SHARP_BILINEAR_TEXTURE_WIDTH >= 266 * 3 + 1,
               "Ultra Sharp target must hold Wide plus its guard column");
_Static_assert(SHARP_BILINEAR_TEXTURE_HEIGHT >= 160 * 3 + 1,
               "Ultra Sharp target must hold the frame plus its guard row");

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern bool Port_Config_GetShowFps(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayStyle(void);
extern bool Port_Config_3DSFullViewComboEnabled(void);
extern bool Port_Config_GpuFrameSync(void);
extern double Port_PPU_3DS_CurrentFps(void);

static const uint8_t* StatusGlyph(char c) {
    static const uint8_t digits[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },
        { 14, 17, 1, 2, 4, 8, 31 },     { 30, 1, 1, 14, 1, 1, 30 },
        { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 16, 30, 1, 1, 30 },
        { 14, 16, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
        { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 1, 14 },
    };
    static const uint8_t letters[11][7] = {
        { 14, 17, 17, 31, 17, 17, 17 }, /* A */
        { 30, 17, 17, 17, 17, 17, 30 }, /* D */
        { 31, 16, 16, 30, 16, 16, 31 }, /* E */
        { 31, 16, 16, 30, 16, 16, 16 }, /* F */
        { 17, 27, 21, 21, 17, 17, 17 }, /* M */
        { 30, 17, 17, 30, 16, 16, 16 }, /* P */
        { 15, 16, 16, 14, 1, 1, 30 },   /* S */
        { 17, 17, 17, 17, 17, 17, 14 }, /* U */
        { 17, 17, 17, 17, 17, 10, 4 },  /* V */
        { 14, 17, 16, 16, 16, 17, 14 }, /* C */
        { 14, 17, 16, 23, 17, 17, 15 }, /* G */
    };
    static const uint8_t letterIds[26] = {
        /* A    B    C  D  E  F   G */
        0,   255,   9, 1, 2, 3, 10, 255, 255, 255, 255, 255, 4,
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

/* Report Task 3 wants the bottom screen in RGB565. Tried and it does not work
 * here, for a reason worth recording so it is not attempted again.
 *
 * GX converts formats in hardware, so in principle the texture could be RGB565
 * while the painter keeps writing 32-bit -- halving the texture's VRAM and the
 * transfer's write bandwidth, which is GSP work on core 1 where the audio
 * worker lives, at no CPU cost. But the painter writes **ABGR**, and
 * ConfigureAbgrTextureEnv un-swizzles it at sample time by reading *alpha as
 * red*. RGB565 has no alpha, so the conversion discards the channel carrying
 * red and the screen comes out red. Verified on an emulator.
 *
 * Making this work needs the painter to emit true RGBA8 -- a format migration
 * across 9000 lines and 85 signatures -- and even then only the transfer would
 * shrink: at 512 KB in 17.2 ms the painter is compute-bound at ~30 MB/s, so its
 * pixel loops would not speed up. Left switchable and off. */
static bool sBottomIsRgb565;

static u32 BottomTextureTransfer(void) {
    if (!sBottomIsRgb565) return TextureTransfer();
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
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

/* sTopTexture is uploaded as the port's ABGR carrier format: its texture
 * alpha component contains the red colour channel, not transparency.  The
 * Bilinear pre-pass must therefore overwrite the intermediate target instead
 * of applying Citro2D's normal source-alpha blend.  Otherwise black pixels
 * (red == 0) become transparent and leave the previous frame behind, which
 * erases text-box fills, outlines and shadows and creates motion trails.
 *
 * Preserve that carrier alpha in the render target with ONE/ZERO; the final
 * pass then performs the established ABGR conversion and uses normal alpha
 * blending on the physical top target. */
static void ConfigureOpaqueOverwriteBlend(void) {
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

static void ConfigureStandardAlphaBlend(void) {
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

bool PlatformGpu3DS_Init(bool old3dsProfile) {
    memset(&sStats, 0, sizeof(sStats));
    sOld3DSProfile = old3dsProfile;
    /* MUST match the expression in Port_PPU_Init exactly: that one gives the
     * pitch to the painter, this one sizes the allocation and the transfer. */
    sUploadLayout = PlatformGpu3DS_GetUploadLayout(old3dsProfile && Port_Config_CompactUpload());
    const size_t topBytes =
        (size_t)sUploadLayout.topPitch * sUploadLayout.topRows * sizeof(uint32_t);
    const size_t bottomBytes =
        (size_t)sUploadLayout.bottomPitch * sUploadLayout.bottomRows * sizeof(uint32_t);
    sBottomTargetValid = false;
    sSharpBilinearTarget = NULL;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sTopUpload = (uint32_t*)linearMemAlign(topBytes, 0x80);
    /* Four vertices for the optional static-quad presenter (report Task 2). */
    sPresentQuad = (PresentVertex*)linearMemAlign(4 * sizeof(PresentVertex), 0x80);
    sPresentQuadValid = false;
    sBottomUploads[0] = (uint32_t*)linearMemAlign(bottomBytes, 0x80);
    sBottomUploads[1] = (uint32_t*)linearMemAlign(bottomBytes, 0x80);
    if (!sTopUpload || !sBottomUploads[0] || !sBottomUploads[1]) goto fail_linear;
    memset(sTopUpload, 0, topBytes);
    memset(sBottomUploads[0], 0, bottomBytes);
    memset(sBottomUploads[1], 0, bottomBytes);
    GSPGPU_FlushDataCache(sTopUpload, topBytes);
    GSPGPU_FlushDataCache(sBottomUploads[0], bottomBytes);
    GSPGPU_FlushDataCache(sBottomUploads[1], bottomBytes);
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
    sStats.topUploadPitch = sUploadLayout.topPitch;
    sStats.topUploadBytes = (uint32_t)topBytes;
    sStats.bottomUploadPitch = sUploadLayout.bottomPitch;
    sStats.bottomUploadBytes = (uint32_t)bottomBytes;
    sStats.topUploadAddress = (uintptr_t)sTopUpload;
    sStats.bottomUploadAddress[0] = (uintptr_t)sBottomUploads[0];
    sStats.bottomUploadAddress[1] = (uintptr_t)sBottomUploads[1];
    if (!C3D_TexInitVRAM(&sTopTexture, TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT, GPU_RGBA8)) goto fail;
    sBottomIsRgb565 = Port_Config_BottomRgb565();
    if (!C3D_TexInitVRAM(&sBottomTexture, 512, 256,
                         sBottomIsRgb565 ? GPU_RGB565 : GPU_RGBA8))
        goto fail_top_texture;
    C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
    /* The complete 320x240 compositor (map, HUD and menus) shares this
     * texture, so linear filtering here makes bilinear presentation the
     * default consistently instead of special-casing individual panels. */
    C3D_TexSetFilter(&sBottomTexture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    /* 266x160 Wide is the largest fallback frame, so a 1024x512 container
     * safely holds its exact 798x480 nearest-neighbour 3x image plus guard
     * texels. This target has no depth buffer. Allocation failure is
     * non-fatal: Bilinear and Ultra Sharp then use a nearest-neighbour
     * presentation without the intermediate pass. */
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
    if (sPresentQuad) { linearFree(sPresentQuad); sPresentQuad = NULL; }
    sPresentQuadValid = false;
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

    const size_t topFlushBytes =
        (size_t)sUploadLayout.topPitch * sTopPresentHeight * sizeof(uint32_t);
    Platform3DS_CleanDataCache(pixels, topFlushBytes);
    /* Old 3DS only: the CPU renderer publishes 160 rows. Describe that exact
     * source rectangle so the display engine does not read another 96 unused
     * RGBA rows. New 3DS retains the established transfer dimensions. */
    const unsigned sourceHeight = sOld3DSProfile ? sUploadLayout.topRows : TOP_TEXTURE_HEIGHT;
    C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(sUploadLayout.topPitch, sourceHeight),
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
    const unsigned intermediateScale = (unsigned)plan.sharpBilinearScale;
    const bool validSharpBilinearScale = intermediateScale == 2u || intermediateScale == 3u;
    const unsigned intermediateWidth = validSharpBilinearScale
                                           ? (unsigned)presentation->sourceWidth * intermediateScale
                                           : 0u;
    const unsigned intermediateHeight = validSharpBilinearScale
                                            ? (unsigned)presentation->sourceHeight * intermediateScale
                                            : 0u;
    const bool useSharpBilinear = plan.useSharpBilinear && validSharpBilinearScale &&
                                  sSharpBilinearTarget &&
                                  intermediateWidth < SHARP_BILINEAR_TEXTURE_WIDTH &&
                                  intermediateHeight < SHARP_BILINEAR_TEXTURE_HEIGHT;
    if (useSharpBilinear) {
        const C2D_DrawParams integerParams = {
            .pos = { .x = 0.0f, .y = 0.0f,
                     .w = (float)intermediateWidth, .h = (float)intermediateHeight },
            .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
        };
        C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
        C2D_SceneBegin(sSharpBilinearTarget);
        ConfigureIdentityTextureEnv();
        ConfigureOpaqueOverwriteBlend();
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
         * before this texture is sampled. UVs cover only the valid 2x or 3x
         * image, never the unused power-of-two container. */
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
        /* SceneBegin flushes the complete overwrite batch before changing
         * blend state for the physical-target batch. */
        C2D_SceneBegin(sTopTarget);
        ConfigureStandardAlphaBlend();
        C3D_TexSetFilter(&sSharpBilinearTexture, GPU_LINEAR, GPU_LINEAR);
        C2D_DrawImage(intermediateImage, &params, NULL);
        ConfigureAbgrTextureEnv();
        ++sStats.sharpBilinearFrames;
    } else {
        const GPU_TEXTURE_FILTER_PARAM filter = plan.linearFilter ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(&sTopTexture, filter, filter);
        C2D_SceneBegin(sTopTarget);
        ConfigureStandardAlphaBlend();
        C2D_DrawImage(image, &params, NULL);
        ConfigureAbgrTextureEnv();
        if (plan.useSharpBilinear) ++sStats.sharpBilinearFallbacks;
    }
    if (Port_Config_GetShowFps()) {
        char label[28];
        double fps = Port_PPU_3DS_CurrentFps();
        unsigned rounded = fps > 0.0 ? (unsigned)(fps + 0.5) : 0u;
        if (rounded > 999u) rounded = 999u;
        snprintf(label, sizeof(label), "FPS %u %s", rounded,
                 sLastFrameUsedGpu ? "GPU" : "CPU");
        C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, 122.0f, 20.0f, C2D_Color32(0, 0, 0, 210));
        DrawStatusText(10.0f, 219.0f, 2.0f, label);
    }
}

/* The top screen is 400x240 and the game image rarely covers all of it, so the
 * surrounding bars have to be black -- but they only need painting when the
 * layout changes, not every frame. Clearing is a full-screen GPU fill, and
 * presentation is the largest remaining cost on an Old 3DS. Three frames of
 * clearing covers every buffer in the swap chain. */
static struct {
    float x, y, w, h;
    int style;
    unsigned width;
    bool overlay;
} sTopLayout;
static unsigned sTopClearFrames = 3;

void PlatformGpu3DS_InvalidateTopBorder(void) { sTopClearFrames = 3; }

static void TopLayoutChanged(float x, float y, float w, float h, int style,
                             unsigned width, bool overlay) {
    if (sTopLayout.x != x || sTopLayout.y != y || sTopLayout.w != w ||
        sTopLayout.h != h || sTopLayout.style != style ||
        sTopLayout.width != width || sTopLayout.overlay != overlay) {
        sTopLayout.x = x;
        sTopLayout.y = y;
        sTopLayout.w = w;
        sTopLayout.h = h;
        sTopLayout.style = style;
        sTopLayout.width = width;
        sTopLayout.overlay = overlay;
        sTopClearFrames = 3;
    }
}

/* Report Task 2: present the frame with one static quad instead of letting
 * citro2d rebuild and re-upload vertices every frame.
 *
 * The quad lives in linear memory, is flushed once, and is rebuilt only when
 * the layout actually changes. The PPU's own vertex shader is reused with a
 * zero offset, so no new program is needed.
 *
 * Off by default: citro2d still draws the bottom screen, so its per-frame
 * vertex flush stays either way, which caps the saving well below what the
 * report assumes. And C2D_TargetClear -- which this path must keep -- is the
 * render-to-texture barrier whose removal caused the white and black screens.
 * Enable with gpu_static_quad=1 to measure it. */
static void BuildPresentQuad(float drawX, float drawY, float drawW, float drawH,
                             unsigned width, const Tex3DS_SubTexture* sub) {
    if (!sPresentQuad) return;
    /* The top target is 240 wide by 400 tall and the display rotates it, so a
     * screen-space rectangle has to be mapped across swapped axes: the screen's
     * horizontal extent runs along the target's tall axis, and its vertical
     * extent along the narrow one. Using screen axes directly drew the frame
     * rotated a quarter turn. */
    const float x0 = (drawY / 240.0f) * 2.0f - 1.0f;
    const float x1 = ((drawY + drawH) / 240.0f) * 2.0f - 1.0f;
    const float y0 = (drawX / 400.0f) * 2.0f - 1.0f;
    const float y1 = ((drawX + drawW) / 400.0f) * 2.0f - 1.0f;
    const float u0 = sub->left, u1 = sub->right;
    const float v0 = sub->top, v1 = sub->bottom;
    /* Corner order follows the rotation: u advances along the target's y axis
     * (screen horizontal), v along its x axis (screen vertical). */
    /* u runs along the target's tall axis (screen horizontal) and v along its
     * narrow one (screen vertical); both are inverted relative to the naive
     * pairing because the display rotation reverses each. */
    const int16_t pu0 = PpuGpu3DS_PackUV(u0);
    const int16_t pu1 = PpuGpu3DS_PackUV(u1);
    const int16_t pv0 = PpuGpu3DS_PackUV(v0);
    const int16_t pv1 = PpuGpu3DS_PackUV(v1);
    sPresentQuad[0] = (PresentVertex){ x0, y0, 0.0f, pu1, pv1 };
    sPresentQuad[1] = (PresentVertex){ x0, y1, 0.0f, pu0, pv1 };
    sPresentQuad[2] = (PresentVertex){ x1, y0, 0.0f, pu1, pv0 };
    sPresentQuad[3] = (PresentVertex){ x1, y1, 0.0f, pu0, pv0 };
    Platform3DS_CleanDataCache(sPresentQuad, 4 * sizeof(*sPresentQuad));
    sPresentQuadW = drawW;
    sPresentQuadH = drawH;
    sPresentQuadWidth = width;
    sPresentQuadValid = true;
}

static void DrawTopTexture(C3D_Tex* texture, unsigned width, bool configureAbgr) {
    if (!texture) return;
    if (width < 240u) width = 240u;
    if (width > 266u) width = 266u;

    const int style = Port_Config_Get3DSDisplayStyle();
    TopView3DSPlan plan;
    TopView3DS_BuildPlan(true, false, Port_Config_Get3DSAspectRatio(), style,
                         PORT_3DS_FULL_VIEW_FALLBACK, (int)width, 160,
                         (int)width, 160, 0, 0, &plan);
    const Port3DSFullViewPresentation* presentation = &plan.source;
    sTopPresentWidth = (unsigned)presentation->renderWidth;

    sTopSubtexture = (Tex3DS_SubTexture){
        .width = (u16)presentation->sourceWidth,
        .height = (u16)presentation->sourceHeight,
        .left = (float)presentation->sourceX / texture->width,
        .top = 1.0f - (float)presentation->sourceY / texture->height,
        .right = (float)(presentation->sourceX + presentation->sourceWidth) / texture->width,
        .bottom = 1.0f -
                  (float)(presentation->sourceY + presentation->sourceHeight) / texture->height,
    };
    const C2D_Image image = { .tex = texture, .subtex = &sTopSubtexture };
    const C2D_DrawParams params = {
        .pos = { .x = (float)plan.drawX, .y = (float)plan.drawY,
                 .w = (float)plan.drawWidth, .h = (float)plan.drawHeight },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };

    C3D_TexSetFilter(texture, plan.linearFilter ? GPU_LINEAR : GPU_NEAREST,
                     plan.linearFilter ? GPU_LINEAR : GPU_NEAREST);
    C2D_Prepare();
    TopLayoutChanged(params.pos.x, params.pos.y, params.pos.w, params.pos.h,
                     style, width, Port_Config_GetShowFps());

    /* Keep the clear as the render-to-texture submission boundary between
     * the PICA200 PPU target and its physical-screen sampling pass. */
    if (sTopClearFrames != 0) --sTopClearFrames;
    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sTopTarget);
    if (Port_Config_GpuStaticQuad() && sPresentQuad &&
        PortPpuGpu3DS_BindPresentShader()) {
        C2D_Flush();
        if (!sPresentQuadValid || sPresentQuadW != params.pos.w ||
            sPresentQuadH != params.pos.h || sPresentQuadWidth != width) {
            BuildPresentQuad(params.pos.x, params.pos.y, params.pos.w,
                             params.pos.h, width, &sTopSubtexture);
        }
        C3D_BufInfo bufInfo;
        BufInfo_Init(&bufInfo);
        BufInfo_Add(&bufInfo, sPresentQuad, sizeof(PresentVertex), 2, 0x10);
        C3D_SetBufInfo(&bufInfo);
        C3D_TexBind(0, texture);
        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xff, 0);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 4);
        C2D_Prepare();
        C2D_SceneBegin(sTopTarget);
    } else {
        C2D_DrawImage(image, &params, NULL);
    }
    if (configureAbgr) ConfigureAbgrTextureEnv();

    if (Port_Config_GetShowFps()) {
        char label[28];
        double fps = Port_PPU_3DS_CurrentFps();
        unsigned rounded = fps > 0.0 ? (unsigned)(fps + 0.5) : 0u;
        if (rounded > 999u) rounded = 999u;
        snprintf(label, sizeof(label), "FPS %u %s", rounded,
                 sLastFrameUsedGpu ? "GPU" : "CPU");
        C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, 122.0f, 20.0f,
                          C2D_Color32(0, 0, 0, 210));
        DrawStatusText(10.0f, 219.0f, 2.0f, label);
    }
}


void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width, unsigned height,
                             unsigned validSourceWidth, unsigned validSourceHeight,
                             Port3DSFullViewMode mode, int cropX, int cropY) {
    if (!sReady || !pixels) return;
    const u8 frameFlags = (u8)(Port_Config_GpuFrameSync() ? C3D_FRAME_SYNCDRAW : 0);
    if (!C3D_FrameBegin(frameFlags)) {
        ++sStats.frameBeginFailures;
        if (sStats.frameBeginFailures <= 3u ||
            (sStats.frameBeginFailures % 300u) == 0u) {
            char line[112];
            snprintf(line, sizeof(line),
                     "[tmc3ds] GPU busy, frame begin skipped (%llu total)\n",
                     (unsigned long long)sStats.frameBeginFailures);
            Platform3DS_Debug(line);
        }
        return;
    }
    sFrameActive = true;
    DrawTopImage(pixels, width, height, validSourceWidth, validSourceHeight,
                 mode, cropX, cropY);
    ++sStats.topTransfers;
}

bool PlatformGpu3DS_BeginCustomTop(void) {
    if (!sReady) return false;
    if (sFrameActive) return true;
    /* SYNCDRAW waits for the previous frame's drawing to retire. The command
     * buffers the PPU builder writes into are read by the GPU asynchronously,
     * so building the next frame before that wait would overwrite geometry
     * still being drawn -- invisible under an emulator whose GPU completes
     * instantly, a flicker on hardware. */
    /* C3D_FrameBegin waits on the GX queue with no timeout, so a command list
     * the GPU never retires stops the main thread here for good: both screens
     * hold their last contents, audio keeps playing on its own core, and no
     * quick dump can be taken because the dump runs on this thread. That is
     * what a watchdog caught as "stopped at stage 20". C3D_FRAME_NONBLOCK
     * returns false instead of waiting, so a wedged GPU costs a skipped frame
     * and leaves the console responsive and diagnosable. */
    /* NONBLOCK was an emergency measure while an unbounded wait could hang the
     * console. The hang had a cause -- zero-count draws, now never submitted --
     * and the counters show the queue is not wedged: a second begin later in
     * the same frame succeeds every time. What NONBLOCK produced instead was a
     * standoff. The GPU is busy at the top of a frame, so the PICA path is
     * skipped; the software path then spends 512 KB transferring, which keeps
     * the GPU busy into the next frame, so it is skipped again. beginFail rose
     * by exactly one per frame while attempted frames sat frozen at 170.
     * Waiting is the correct behaviour: it is a frame's worth of pacing, not a
     * deadlock, and the watchdog now catches it if that ever stops being true. */
    const u8 frameFlags = (u8)(Port_Config_GpuFrameSync() ? C3D_FRAME_SYNCDRAW : 0);
    if (!C3D_FrameBegin(frameFlags)) {
        ++sStats.frameBeginFailures;
        if (sStats.frameBeginFailures <= 3u ||
            (sStats.frameBeginFailures % 300u) == 0u) {
            char line[112];
            snprintf(line, sizeof(line),
                     "[tmc3ds] GPU busy, frame begin skipped (%llu total)\n",
                     (unsigned long long)sStats.frameBeginFailures);
            Platform3DS_Debug(line);
        }
        return false;
    }
    sFrameActive = true;
    return true;
}

void PlatformGpu3DS_DrawTopTexture(void* texturePointer, unsigned width) {
    C3D_Tex* texture = texturePointer;
    if (!sFrameActive || !texture) return;
    sLastFrameUsedGpu = true;
    DrawTopTexture(texture, width, false);
}

bool PlatformGpu3DS_QueueRgba5551Readback(void* texturePointer, uint16_t* pixels) {
    C3D_Tex* texture = texturePointer;
    if (!sFrameActive || !texture || !texture->data || !pixels ||
        texture->fmt != GPU_RGBA5551)
        return false;
    const size_t bytes = (size_t)texture->width * texture->height * sizeof(*pixels);
    if (R_FAILED(GSPGPU_FlushDataCache(pixels, bytes))) return false;
    /* Retire the queued PPU draws before the transfer reads the target.
     * C3D_FrameSplit takes GX_CMDLIST_* flags; C3D_FRAME_SYNCDRAW belongs to
     * C3D_FrameBegin and would set GX_CMDLIST_UPDATE_GAS_ACC here. */
    C3D_FrameSplit(0);
    /* The top presenter samples visible row 0 at v=1 and its software upload
     * uses the same no-flip transfer. Untiling with no flip is therefore the
     * inverse mapping: linear row y is the visible row y, not raw Morton data. */
    C3D_SyncDisplayTransfer(
        (u32*)texture->data, GX_BUFFER_DIM(texture->width, texture->height),
        (u32*)pixels, GX_BUFFER_DIM(texture->width, texture->height),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
            GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB5A1) |
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB5A1) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    return true;
}


bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return false;
    if (changed) {
        /* NOT a blocking transfer. citro3d source/renderqueue.c:417-430 shows
         * C3D_SyncDisplayTransfer only blocks when called OUTSIDE a frame:
         *
         *   if (inFrame) { C3D_FrameSplit(0); GX_DisplayTransfer(...); }
         *   else        { C3Di_SafeDisplayTransfer(...); gspWaitForPPF(); }
         *
         * This runs inside an active frame, so it queues and returns. The timer
         * below therefore measures a queue append, not DMA -- which is why the
         * emulator read 0.029 ms. That figure was correct, not an artifact.
         *
         * A count correlation (378 of these against ~362 overrunning frames)
         * made this look like the overrun mechanism. It is not. The CPU block is
         * C3D_FrameBegin -> C3Di_WaitAndClearQueue(-1), waiting on the PREVIOUS
         * frame's whole GPU workload. Shrinking this payload still helps, but by
         * reducing GPU work so that next wait is shorter -- not by shortening
         * anything here. */
        const uint64_t transferStart = svcGetSystemTick();
        const size_t bottomFlushBytes =
            (size_t)sUploadLayout.bottomPitch * 240u * sizeof(uint32_t);
        Platform3DS_CleanDataCache(pixels, bottomFlushBytes);
        C3D_SyncDisplayTransfer((u32*)pixels,
                                GX_BUFFER_DIM(sUploadLayout.bottomPitch, sUploadLayout.bottomRows),
                                (u32*)sBottomTexture.data, GX_BUFFER_DIM(512, 256),
                                BottomTextureTransfer());
        const uint64_t transferTicks = svcGetSystemTick() - transferStart;
        sStats.bottomTransferTicks += transferTicks;
        if (transferTicks > sStats.bottomTransferMaxTicks)
            sStats.bottomTransferMaxTicks = transferTicks;
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
        /* One GSP round trip per presented frame: the dump's own counters show
         * 876609536 bytes / 65536 per call = 13375 calls against 13376 frames.
         * At the ~330 us platform_3ds.c:638 measured for this IPC that is
         * ~0.33 ms/frame, spent cleaning 64 KiB that svcStoreProcessDataCache
         * cleans locally in microseconds without waking core 1. */
        Platform3DS_CleanDataCache(sC2dFlushBase, sC2dFlushSize);
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
    if (!sReady || !sTopUpload || !sBottomUploads[0] || !C3D_FrameBegin(C3D_FRAME_NONBLOCK)) return;
    /* This frame paints outside the usual layout. */
    PlatformGpu3DS_InvalidateTopBorder();
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
    /* Bottom-only presentation path: same per-frame GSP round trip as above. */
    if (sC2dFlushBase && sC2dFlushSize) Platform3DS_CleanDataCache(sC2dFlushBase, sC2dFlushSize);
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
    sUploadLayout = (PlatformGpu3DSUploadLayout){ 0 };
}
