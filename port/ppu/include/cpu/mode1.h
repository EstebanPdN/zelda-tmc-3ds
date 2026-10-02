/*
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Software GBA PPU, vendored as first-party port source. Derived from
 * VirtuaPPU by Mathéo Vignaud (https://github.com/MatheoVignaud/VirtuaPPU,
 * commit 5cf5e99) and incorporating this project's 15 accuracy/portability
 * patches (formerly port/patches/viruappu-*.patch; preserved in git history).
 * Maintained here directly — not kept in sync with upstream.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../ppu_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*virtuappu_mode1_pre_line_fn)(int line);
extern virtuappu_mode1_pre_line_fn virtuappu_mode1_pre_line_callback;

/* PORT_WIDESCREEN_SPIKE: MODE1_GBA_WIDTH made overridable via
 * -DMODE1_GBA_WIDTH=N so the PC port can render wider frames. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif
/* OAM and BG clip extents (widescreen Phase 2 — Option A).
 *
 * MODE1_GBA_BG_CLIP_X (240) is the split point: BG columns < 240 read
 * the engine's VRAM screenblock as usual; columns >= 240 read the
 * port-side shadow tilemap (virtuappu_mode1_ws_shadow[], populated from
 * gMapData*Special) — the 32-tile screenblock only spans 256 px of world
 * and wraps past that, so the reveal columns can't come from VRAM. When
 * no shadow is registered (native 240, or non-gameplay screens) the
 * composite force-blacks past 240.
 *
 * MODE1_GBA_VIEWPORT_X is the OAM clip; it tracks MODE1_GBA_WIDTH so
 * sprites render across the full widescreen viewport. Engine-parked
 * off-screen sprites are kept out by the DISPLAY_WIDTH-relative culling
 * (CheckOnScreen / per-entity bounds), not by clipping here. */
#define MODE1_GBA_VIEWPORT_X MODE1_GBA_WIDTH
#define MODE1_GBA_BG_CLIP_X 240

/* PPU/engine boundary for true widescreen: the renderer here is already
 * width-agnostic — set MODE1_GBA_WIDTH > 240 and a frame_width via
 * set_frame_geometry and it composites a wider viewport (verified
 * byte-identical at 240). What it CANNOT invent is world that the engine never
 * streamed: revealed columns reference BG char/screen data the per-area tileset
 * managers only load for the 240-wide GBA view, so beyond the shadow-tilemap
 * reveal the data simply isn't in VRAM. Expanding "more world" on the sides is
 * therefore an ENGINE char-VRAM-streaming change (hyruleTownTileSetManager et
 * al.), not a renderer change. Keep width/pitch first-class here; do the world
 * extension in the engine. */
enum {
    MODE1_GBA_HEIGHT = 160,
    MODE1_GBA_BG_COUNT = 4,
    MODE1_GBA_OAM_COUNT = 128,
    MODE1_IO_MEM_SIZE = 0x400,
    MODE1_VRAM_SIZE = 0x18000,
    MODE1_PALETTE_COLORS = 256,
    MODE1_OAM_HALFWORDS = 512
};

/* Widescreen Option A — port-side shadow tilemap for the reveal region
 * (display cols >= MODE1_GBA_BG_CLIP_X on 32-tile BGs). Populated by
 * port/port_linked_stubs.c::Port_Widescreen_UpdateShadows; a NULL entry
 * means "no shadow" => render_text_bg_line clips at 240 and the composite
 * force-blacks past it (native-240 / non-gameplay behaviour). COLS scales
 * with the configured width (reveal tiles = (W-240)/8, plus scroll/wrap
 * headroom); ROWS=32 mirrors the engine's mod-32 vertical row rolling. */
#define MODE1_WS_SHADOW_ROWS 32
#define MODE1_WS_SHADOW_COLS (((MODE1_GBA_WIDTH - 240) / 8) + 4)
extern uint16_t* virtuappu_mode1_ws_shadow[MODE1_GBA_BG_COUNT];
extern int virtuappu_mode1_ws_shadow_base_tile[MODE1_GBA_BG_COUNT];

/* Runtime WIP widescreen HUD anchor. BG0 stays 32 tiles wide, but gameplay
 * HUD uses both left-anchored widgets (hearts/charge) and right-anchored
 * widgets (rupees/keys). When enabled by the port, render BG0 cols
 * MODE1_WS_HUD_RIGHT_NATIVE_X..239 at the far right of the wide viewport
 * instead of in the middle of the revealed world. */
#define MODE1_WS_HUD_RIGHT_NATIVE_X 176
extern int virtuappu_mode1_ws_hud_right_anchor;

/* Runtime WIP widescreen message-box centering. The engine composes the
 * textbox on BG0 for a 240-px canvas (default box: tiles 1..27). On a wide
 * frame that leaves the box hugging the left edge, and snapping the whole
 * viewport back to 240 for every dialogue is worse (see
 * docs/widescreen-phase2-design.md). Instead the port publishes the live
 * box rect (native BG0 pixel coords) + a shift; render_text_bg_line then
 * draws those BG0 pixels shifted right by `shift` px ((W-240)/2 = centered)
 * and suppresses them at their native columns. Zero shift = off. */
extern int virtuappu_mode1_ws_msg_shift; /* px to move the box right   */
extern int virtuappu_mode1_ws_msg_x0;    /* native box rect, inclusive */
extern int virtuappu_mode1_ws_msg_x1;    /* native box rect, exclusive */
extern int virtuappu_mode1_ws_msg_y0;    /* first box line             */
extern int virtuappu_mode1_ws_msg_y1;    /* one past last box line     */

enum {
    MODE1_IO_DISPCNT = 0x00,
    MODE1_IO_BG0CNT = 0x08,
    MODE1_IO_BG1CNT = 0x0A,
    MODE1_IO_BG2CNT = 0x0C,
    MODE1_IO_BG3CNT = 0x0E,
    MODE1_IO_BG0HOFS = 0x10,
    MODE1_IO_BG0VOFS = 0x12,
    MODE1_IO_BG1HOFS = 0x14,
    MODE1_IO_BG1VOFS = 0x16,
    MODE1_IO_BG2HOFS = 0x18,
    MODE1_IO_BG2VOFS = 0x1A,
    MODE1_IO_BG3HOFS = 0x1C,
    MODE1_IO_BG3VOFS = 0x1E,
    MODE1_IO_WIN0H = 0x40,
    MODE1_IO_WIN1H = 0x42,
    MODE1_IO_WIN0V = 0x44,
    MODE1_IO_WIN1V = 0x46,
    MODE1_IO_WININ = 0x48,
    MODE1_IO_WINOUT = 0x4A,
    MODE1_IO_MOSAIC = 0x4C,
    MODE1_IO_BLDCNT = 0x50,
    MODE1_IO_BLDALPHA = 0x52,
    MODE1_IO_BLDY = 0x54
};

enum {
    MODE1_DISP_OBJ_1D = 0x0040,
    MODE1_DISP_FORCED_BLANK = 0x0080,
    MODE1_DISP_BG0_ON = 0x0100,
    MODE1_DISP_BG1_ON = 0x0200,
    MODE1_DISP_BG2_ON = 0x0400,
    MODE1_DISP_BG3_ON = 0x0800,
    MODE1_DISP_OBJ_ON = 0x1000,
    MODE1_DISP_WIN0_ON = 0x2000,
    MODE1_DISP_WIN1_ON = 0x4000,
    MODE1_DISP_OBJWIN_ON = 0x8000
};

typedef struct VirtuaPPUMode1GbaMemory {
    uint8_t* io_mem;
    uint8_t* vram;
    uint16_t* bg_palette;
    uint16_t* obj_palette;
    uint16_t* oam_mem;
} VirtuaPPUMode1GbaMemory;

typedef struct VirtuaPPUMode13DSStats {
    uint64_t frames;
    uint64_t mainLastTicks;
    uint64_t mainMaxTicks;
    uint64_t workerLastTicks[2];
    uint64_t workerMaxTicks[2];
    uint32_t mainLastLines;
    uint32_t workerLastLines[2];
    uint32_t workerCount;
} VirtuaPPUMode13DSStats;

void virtuappu_mode1_bind_gba_memory(const VirtuaPPUMode1GbaMemory* memory);
void virtuappu_mode1_get_bound_gba_memory(VirtuaPPUMode1GbaMemory* memory);
void virtuappu_mode1_set_frame_geometry(const PPUMemory* ppu);
void virtuappu_mode1_set_output_buffer(uint32_t* pixels, int pitch);
int virtuappu_mode1_frame_width(void);
int virtuappu_mode1_frame_pitch(void);
uint16_t virtuappu_mode1_io_read16(uint16_t offset);
uint32_t virtuappu_mode1_io_read32(uint16_t offset);
uint32_t virtuappu_mode1_rgb555_to_abgr8888(uint16_t color);
void virtuappu_mode1_set_color_correction(bool enabled);
void virtuappu_mode1_render_text_bg_line(int bg_index, int line, uint32_t* line_buffer, uint8_t* priority_buffer);
void virtuappu_mode1_render_obj_line(int line, bool obj_1d, uint32_t* line_buffer, uint8_t* priority_buffer);
void virtuappu_mode1_composite_line(int line, uint32_t bg_layers[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
                                    uint8_t bg_priority[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
                                    uint32_t obj_layer[MODE1_GBA_WIDTH], uint8_t obj_priority[MODE1_GBA_WIDTH],
                                    uint16_t dispcnt);
void virtuappu_mode1_render_frame(const PPUMemory* ppu);
void virtuappu_mode1_get_3ds_stats(VirtuaPPUMode13DSStats* stats);
void virtuappu_mode1_shutdown_workers(void);
#ifdef VIRTUAPPU_TESTING
/* Test-only oracle switch: the parity fuzzer renders each state once through
 * the optimized native paths and once through the generic reference path. */
void virtuappu_mode1_set_native_fast_paths_enabled(bool enabled);
#endif

/* GPU-raster prepare pass: run ONLY the sequential portion of render_frame —
 * the per-line HDMA callback + IO snapshot, per-line DISPCNT, and the affine
 * per-line reference precompute — into caller-owned buffers, WITHOUT the CPU
 * parallel render. The GPU rasterizer (port/port_gpu_raster.*) consumes these
 * to render the frame on the GPU. Buffers must hold MODE1_GBA_HEIGHT entries:
 *   io_per_line      : MODE1_GBA_HEIGHT * MODE1_IO_MEM_SIZE bytes
 *   dispcnt_per_line : MODE1_GBA_HEIGHT uint16_t
 *   aff_ref_x/y      : MODE1_GBA_HEIGHT int32_t (filled only when affine)
 * `out_frame_dispcnt` receives the frame-start DISPCNT. Returns nonzero if
 * forced-blank is set (the GPU shader handles it, but the host may skip). */
int virtuappu_mode1_prepare_frame(const PPUMemory* ppu, uint8_t* io_per_line, uint16_t* dispcnt_per_line,
                                  int32_t* aff_ref_x, int32_t* aff_ref_y, uint16_t* out_frame_dispcnt);

/* Host-set write strobes for the affine BG2X/BG2Y reference latch. On GBA,
 * ANY write to BG2X/BG2Y reloads the internal reference — including a write
 * of the SAME value (constant-value HBlank DMA pins the layer to one line's
 * reference on hardware). The precompute below can only see per-line VALUES,
 * so an idempotent write is invisible to it; the host (port_ppu.cpp) sets
 * these when an active HBlank-DMA channel targets BG2X / BG2Y this frame. */
extern bool virtuappu_mode1_bg2x_hdma_strobe;
extern bool virtuappu_mode1_bg2y_hdma_strobe;

/* Precompute the per-line affine BG2 internal reference point for one frame
 * (the #132 hardware latch). Pure function over per-line, post-HBlank-DMA
 * inputs, so the result can be consumed by the parallel render pass:
 *   - reload the internal reference from BG2X/BG2Y whenever a line's I/O value
 *     differs from the previous line's (a CPU/DMA write, e.g. the Deepwood
 *     barrel's per-scanline HBlank DMA), or unconditionally when the
 *     corresponding reload_*_every_line strobe says a write EVENT happens
 *     each line (idempotent HDMA);
 *   - otherwise advance it by dmx(pb)/dmy(pd) each scanline.
 * init_ref_{x,y} is the frame-start (pre-callback) reference; line_ref_{x,y}
 * are the post-callback references per line; out_ref_{x,y} receive the value to
 * render each line with. Exposed for unit testing (tools/ppu_affine_test.c). */
void virtuappu_mode1_affine_precompute(int height, int32_t init_ref_x, int32_t init_ref_y, const int32_t* line_ref_x,
                                       const int32_t* line_ref_y, const int16_t* line_pb, const int16_t* line_pd,
                                       bool reload_x_every_line, bool reload_y_every_line, int32_t* out_ref_x,
                                       int32_t* out_ref_y);

/* Sub-pixel re-render of OAM affine sprites into a (240*scale x 160*scale)
 * buffer. Called by the PC port at internal-render-scale > 1 after the
 * standard frame has been S*S nearest-replicated into `dst`. */
void virtuappu_mode1_render_affine_obj_overlay(uint32_t* dst, int dst_w, int dst_h, int scale);

#ifdef __cplusplus
}
#endif
