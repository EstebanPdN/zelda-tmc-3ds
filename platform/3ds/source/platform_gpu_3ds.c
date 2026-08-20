#include "platform_3ds.h"
#include "platform_gpu_3ds.h"
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
bool Port_Config_BottomRgb565(void);

static bool sLastFrameUsedGpu;
static C3D_RenderTarget* sTopTarget;
static C3D_RenderTarget* sBottomTarget;
static C3D_Tex sTopTexture;
static C3D_Tex sBottomTexture;
static Tex3DS_SubTexture sTopSubtexture;
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

enum {
    TOP_TEXTURE_WIDTH = 512,
    TOP_TEXTURE_HEIGHT = 256,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern bool Port_Config_GetShowFps(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayStyle(void);
extern bool Port_Config_GpuFrameSync(void);
extern double Port_PPU_3DS_CurrentFps(void);

enum {
    TOP_ASPECT_WIDE = 0,
    TOP_ASPECT_ORIGINAL,
    TOP_ASPECT_STRETCH,
};

enum {
    TOP_DISPLAY_PIXEL_PERFECT = 0,
    TOP_DISPLAY_SCALED,
    TOP_DISPLAY_BLUR,
};

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

bool PlatformGpu3DS_Init(bool old3dsProfile) {
    memset(&sStats, 0, sizeof(sStats));
    sOld3DSProfile = old3dsProfile;
    sUploadLayout = PlatformGpu3DS_GetUploadLayout(old3dsProfile);
    const size_t topBytes =
        (size_t)sUploadLayout.topPitch * sUploadLayout.topRows * sizeof(uint32_t);
    const size_t bottomBytes =
        (size_t)sUploadLayout.bottomPitch * sUploadLayout.bottomRows * sizeof(uint32_t);
    sBottomTargetValid = false;
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
    C3D_TexSetFilter(&sBottomTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

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
    if (width < 240u) width = 240u;
    if (width > 266u) width = 266u;
    sTopPresentWidth = width;

    sTopSubtexture = (Tex3DS_SubTexture){
        .width = (u16)width, .height = 160, .left = 0.0f, .top = 1.0f,
        .right = (float)width / texture->width,
        .bottom = 1.0f - 160.0f / texture->height,
    };
    const C2D_Image image = { .tex = texture, .subtex = &sTopSubtexture };
    const int style = Port_Config_Get3DSDisplayStyle();
    C3D_TexSetFilter(texture, style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST,
                     style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST);

    float drawW;
    float drawH;
    if (style == TOP_DISPLAY_PIXEL_PERFECT) {
        drawW = (float)width;
        drawH = 160.0f;
    } else {
        drawH = 240.0f;
        switch (Port_Config_Get3DSAspectRatio()) {
            case TOP_ASPECT_STRETCH: drawW = 400.0f; break;
            case TOP_ASPECT_ORIGINAL: drawW = 360.0f; break;
            case TOP_ASPECT_WIDE:
            default: drawW = width >= 266u ? 400.0f : 360.0f; break;
        }
    }
    const C2D_DrawParams params = {
        .pos = { .x = (400.0f - drawW) * 0.5f, .y = (240.0f - drawH) * 0.5f,
                 .w = drawW, .h = drawH },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_Prepare();
    TopLayoutChanged(params.pos.x, params.pos.y, drawW, drawH, style, width,
                     Port_Config_GetShowFps());
    /* Unconditional, and it must stay that way. C2D_TargetClear flushes
     * citro2d's vertex buffer and then issues C3D_FrameSplit, which is the only
     * command-list submission boundary between the PPU rendering into
     * sOutputTexture and the C2D_DrawImage below sampling it. Making this
     * conditional to save a clear removed that boundary, and the sampler read
     * whatever the colour/texture cache still held: a uniformly black or white
     * top screen on hardware, and a black bottom screen once the Old 3DS
     * bottom-target reuse removed the other split. A bare C3D_FrameSplit is not
     * a substitute -- without the vertex-buffer flush it submits mid-batch and
     * corrupts both screens. Emulators complete GPU work instantly and keep no
     * such cache, so none of this is visible there. */
    if (sTopClearFrames != 0) --sTopClearFrames;
    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sTopTarget);
    if (Port_Config_GpuStaticQuad() && sPresentQuad &&
        PortPpuGpu3DS_BindPresentShader()) {
        /* citro2d has vertices buffered for the clear above; they must reach
         * the GPU before the shader changes underneath them. */
        C2D_Flush();
        if (!sPresentQuadValid || sPresentQuadW != drawW ||
            sPresentQuadH != drawH || sPresentQuadWidth != width)
            BuildPresentQuad(params.pos.x, params.pos.y, drawW, drawH, width,
                             &sTopSubtexture);
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
        /* Hand the pipeline back: the bottom screen is still drawn by C2D. */
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
        C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, 122.0f, 20.0f, C2D_Color32(0, 0, 0, 210));
        DrawStatusText(10.0f, 219.0f, 2.0f, label);
    }
}

static void DrawTopImage(const uint32_t* pixels, unsigned width) {
    const size_t topFlushBytes =
        (size_t)sUploadLayout.topPitch * 160u * sizeof(uint32_t);
    GSPGPU_FlushDataCache(pixels, topFlushBytes);
    C3D_SyncDisplayTransfer((u32*)pixels,
                            GX_BUFFER_DIM(sUploadLayout.topPitch, sUploadLayout.topRows),
                            (u32*)sTopTexture.data,
                            GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                            TextureTransfer());
    DrawTopTexture(&sTopTexture, width, true);
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

void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width) {
    if (!pixels || !PlatformGpu3DS_BeginCustomTop()) return;
    sLastFrameUsedGpu = false;
    DrawTopImage(pixels, width);
    ++sStats.topTransfers;
}

bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return false;
    if (changed) {
        const size_t bottomFlushBytes =
            (size_t)sUploadLayout.bottomPitch * 240u * sizeof(uint32_t);
        GSPGPU_FlushDataCache(pixels, bottomFlushBytes);
        C3D_SyncDisplayTransfer((u32*)pixels,
                                GX_BUFFER_DIM(sUploadLayout.bottomPitch, sUploadLayout.bottomRows),
                                (u32*)sBottomTexture.data, GX_BUFFER_DIM(512, 256),
                                BottomTextureTransfer());
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
    if (!sReady || !sTopUpload || !sBottomUploads[0] || !C3D_FrameBegin(C3D_FRAME_NONBLOCK)) return;
    /* This frame paints outside the usual layout. */
    PlatformGpu3DS_InvalidateTopBorder();
    sFrameActive = true;
    DrawTopImage(sTopUpload, sTopPresentWidth);
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
    sUploadLayout = (PlatformGpu3DSUploadLayout){ 0 };
}
