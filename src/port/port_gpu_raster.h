#pragma once
/*
 * port_gpu_raster.h — GPU PPU rasterizer host side.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Renders the GBA frame on the GPU (fragment-shader reimplementation of
 * port/ppu/src/mode1.c) into an offscreen R8G8B8A8_UNORM texture. The CPU
 * software rasterizer stays the golden reference and the automatic fallback;
 * see docs/gpu-rasterizer-design.md.
 *
 * Device-agnostic: the caller supplies an SDL_GPUDevice (the game reuses the
 * presentation device; tools/ppu_gpu_parity creates a headless one) and the
 * SPIR-V shader blobs (the game embeds them; the harness loads from disk).
 */

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PortGpuRaster PortGpuRaster;

/* Inputs for one frame. All pointers are borrowed for the duration of the
 * Render call (uploaded synchronously); the caller retains ownership.
 * The per-line arrays are the outputs of the CPU sequential pass
 * (virtuappu_mode1 prepare): io snapshots, per-line DISPCNT, affine refs. */
typedef struct PortGpuRasterFrame {
    int frame_width;        /* visible pixels per line */
    int frame_height;       /* scanlines (160) */
    int frame_pitch;        /* readback row stride in pixels */
    int mode;               /* GBA BG mode from PPUMemory.mode */
    bool affine;            /* mode == 2 */
    uint16_t frame_dispcnt; /* frame-start DISPCNT (forced blank + backdrop) */
    int scale;              /* supersample factor S>=1; target is S*W x S*H (S=1 = native, bit-exact) */

    const uint8_t* vram;              /* MODE1_VRAM_SIZE bytes */
    const uint16_t* bg_palette;       /* 256 entries */
    const uint16_t* obj_palette;      /* 256 entries */
    const uint16_t* oam;              /* 512 halfwords */
    const uint8_t* io_per_line;       /* io_uniform ? 1 row : frame_height rows (1024 B each) */
    bool io_uniform;                  /* true = no per-line HDMA: only row 0 valid, shader reads row 0 */
    const uint16_t* dispcnt_per_line; /* frame_height entries */
    const int32_t* affine_ref_x;      /* frame_height entries, mode2, may be NULL */
    const int32_t* affine_ref_y;      /* frame_height entries, mode2, may be NULL */

    /* Widescreen Option A (mirrors virtuappu_mode1_ws_* globals). All zero /
     * NULL / -1 at native 240; the shader's reveal branches then stay inert. */
    int ws_bg_clip_x;           /* MODE1_GBA_BG_CLIP_X (240) */
    int ws_cols;                /* MODE1_WS_SHADOW_COLS */
    int ws_hud_right_anchor;    /* 0/1 */
    int ws_hud_right_native_x;  /* MODE1_WS_HUD_RIGHT_NATIVE_X (176) */
    int ws_msg_shift;           /* px */
    int ws_msg_x0, ws_msg_x1;   /* native box rect */
    int ws_msg_y0, ws_msg_y1;   /* box line band */
    int ws_shadow_base_tile[4]; /* per-BG; <0 = no shadow for that BG */
    /* Concatenated 4-BG shadow tilemaps: [bg][32 rows][ws_cols] halfwords, or
     * NULL when no BG has a shadow (then ws_shadow_base_tile are all <0). */
    const uint16_t* ws_shadow;
    int ws_shadow_halfwords; /* total halfwords in ws_shadow (4*32*ws_cols) */
} PortGpuRasterFrame;

/* Create a rasterizer on `device` from vertex/fragment shader blobs in
 * `format` (SPIR-V on Vulkan; MSL on Metal), with `entrypoint` ("main" for
 * SPIR-V, "main0" for the SDL_GPU MSL convention). Returns NULL on any failure
 * (unsupported format, shader/pipeline build) — the caller then uses the CPU
 * rasterizer. */
PortGpuRaster* Port_GpuRaster_Create(SDL_GPUDevice* device, SDL_GPUShaderFormat format, const char* entrypoint,
                                     const void* vert, size_t vert_len, const void* frag, size_t frag_len);

void Port_GpuRaster_Destroy(PortGpuRaster* r);

/* Render one frame into the context's offscreen texture. Returns the texture
 * (owned by the context, valid until the next Render/Destroy) or NULL on
 * failure. */
SDL_GPUTexture* Port_GpuRaster_Render(PortGpuRaster* r, const PortGpuRasterFrame* f);

/* Copy the last-rendered texture into `dst` as ABGR8888, honoring `pitch`
 * (pixels per row). Returns false on failure. Used by the parity harness and
 * by CPU consumers that need pixels (screenshot/shm/xBRZ). */
bool Port_GpuRaster_Readback(PortGpuRaster* r, uint32_t* dst, int width, int height, int pitch);

/* Render frame `f` AND read it back into `dst` (ABGR8888, `pitch` px/row) in a
 * SINGLE command buffer with ONE fence wait — the render pass and the download
 * copy pass share one submit, eliminating the second GPU->CPU round-trip that
 * separate Render + Readback calls incur. This is the preferred per-frame entry
 * point; Render + Readback remain for callers that need the texture handle.
 * Returns false on any failure (caller uses the CPU rasterizer). */
bool Port_GpuRaster_RenderReadback(PortGpuRaster* r, const PortGpuRasterFrame* f, uint32_t* dst, int pitch);

/* Deferred (double-buffered) render+readback: submits frame N and returns the
 * PREVIOUS frame's pixels (checked non-blocking via a fence poll), so the CPU
 * NEVER stalls on the GPU — the ~2ms same-frame fence wait disappears. Costs 1
 * frame of display latency. Returns true if `dst` was filled with a (1-frame-
 * stale) frame; false if not yet available (first frame, or GPU still busy) —
 * the caller should keep the prior framebuffer or use the CPU path that frame.
 * `dst`/`pitch` sized for the CURRENT frame; the returned stale frame must have
 * matched dimensions (it does while width is stable). */
bool Port_GpuRaster_RenderReadbackDeferred(PortGpuRaster* r, const PortGpuRasterFrame* f, uint32_t* dst, int pitch);

#ifdef __cplusplus
}
#endif
