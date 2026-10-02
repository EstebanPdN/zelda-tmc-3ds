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

#include "cpu/mode1.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TMC_3DS
#include <3ds.h>
#endif

#include "virtuappu.h"

virtuappu_mode1_pre_line_fn virtuappu_mode1_pre_line_callback = NULL;

typedef struct Mode1TilemapEntry {
    uint16_t raw;
} Mode1TilemapEntry;

/* Widescreen Option A — port-side shadow tilemap pointers (declared in
 * include/cpu/mode1.h, populated by the PC port). NULL => no widescreen
 * reveal for that BG; render_text_bg_line then clips at MODE1_GBA_BG_CLIP_X
 * and the composite force-blacks past it. */
uint16_t* virtuappu_mode1_ws_shadow[MODE1_GBA_BG_COUNT] = { NULL, NULL, NULL, NULL };
int virtuappu_mode1_ws_shadow_base_tile[MODE1_GBA_BG_COUNT] = { 0, 0, 0, 0 };
int virtuappu_mode1_ws_hud_right_anchor = 0;
/* Widescreen message-box centering (see mode1.h). All zero = inactive. */
int virtuappu_mode1_ws_msg_shift = 0;
int virtuappu_mode1_ws_msg_x0 = 0;
int virtuappu_mode1_ws_msg_x1 = 0;
int virtuappu_mode1_ws_msg_y0 = 0;
int virtuappu_mode1_ws_msg_y1 = 0;

typedef struct Mode1OAMAttr {
    uint16_t attr0;
    uint16_t attr1;
    uint16_t attr2;
} Mode1OAMAttr;

typedef enum Mode1BlendEffect {
    MODE1_BLEND_NONE = 0,
    MODE1_BLEND_ALPHA = 1,
    MODE1_BLEND_BRIGHTEN = 2,
    MODE1_BLEND_DARKEN = 3
} Mode1BlendEffect;

/* ---------------------------------------------------------------------------
 * PPU module state & threading contract (single reference).
 *
 * Bound GBA memory:
 *   mode1_default_*        backing buffers used until the engine binds real
 *                          memory, or when a NULL pointer is bound (see
 *                          virtuappu_mode1_bind_gba_memory — it validates each
 *                          pointer and falls back to these defaults).
 *   mode1_memory           the live binding (engine arrays or the defaults).
 *   mode1_frame_width/_pitch  render geometry (set per frame by
 *                          virtuappu_mode1_set_frame_geometry).
 *
 * Per-frame render threading (virtuappu_mode1_render_frame is OpenMP-parallel
 * over scanlines):
 *   TLS (VPPU_TLS, per render thread): io_thread_override, obj_semitrans,
 *       obj_window. Each thread owns a whole scanline; these must NOT be shared.
 *       IO regs are read via virtuappu_mode1_io_read16/32, which honour the
 *       per-thread override snapshot — never read mode1_memory.io_mem directly
 *       in render code or you get the wrong scanline's registers.
 *   SHARED, read-only during render (set once per frame by the port before the
 *       render): obj_clip_* (swamp sink), ws_shadow* (widescreen Option A),
 *       and the precomputed affine reference arrays.
 *
 * OOB safety: bound pointers are trusted for size; every VRAM/OAM/palette
 * access is range-clamped against the MODE1_*_SIZE constants at use sites, so
 * degraded/short room data can't read out of bounds.
 * ------------------------------------------------------------------------- */
static uint8_t mode1_default_io_mem[MODE1_IO_MEM_SIZE];
static uint8_t mode1_default_vram[MODE1_VRAM_SIZE];
static uint16_t mode1_default_bg_palette[MODE1_PALETTE_COLORS];
static uint16_t mode1_default_obj_palette[MODE1_PALETTE_COLORS];
static uint16_t mode1_default_oam_mem[MODE1_OAM_HALFWORDS];

static VirtuaPPUMode1GbaMemory mode1_memory = { mode1_default_io_mem, mode1_default_vram, mode1_default_bg_palette,
                                                mode1_default_obj_palette, mode1_default_oam_mem };

static int mode1_frame_width = MODE1_GBA_WIDTH;
static int mode1_frame_pitch = MODE1_GBA_WIDTH;
static uint32_t* mode1_output_buffer;
static int mode1_output_pitch;

void virtuappu_mode1_set_frame_geometry(const PPUMemory* ppu) {
    int width = MODE1_GBA_WIDTH;
    int pitch = MODE1_GBA_WIDTH;

    if (ppu != NULL && ppu->frame_width != 0u) {
        width = (int)ppu->frame_width;
    }
    if (width < 1) {
        width = 1;
    } else if (width > MODE1_GBA_WIDTH) {
        width = MODE1_GBA_WIDTH;
    }

    if (ppu != NULL && ppu->frame_pitch != 0u) {
        pitch = (int)ppu->frame_pitch;
    }
    if (pitch < width) {
        pitch = width;
    } else if (pitch > VIRTUAPPU_MAX_FRAME_WIDTH) {
        pitch = VIRTUAPPU_MAX_FRAME_WIDTH;
    }

    mode1_frame_width = width;
    mode1_frame_pitch = pitch;
}

void virtuappu_mode1_set_output_buffer(uint32_t* pixels, int pitch) {
    if (pixels != NULL && pitch >= MODE1_GBA_WIDTH) {
        mode1_output_buffer = pixels;
        mode1_output_pitch = pitch;
    } else {
        mode1_output_buffer = NULL;
        mode1_output_pitch = 0;
    }
}

static uint32_t* mode1_output_row(int line) {
    if (mode1_output_buffer != NULL) {
        return mode1_output_buffer + (size_t)line * (size_t)mode1_output_pitch;
    }
    return &virtuappu_frame_buffer[(size_t)line * (size_t)mode1_frame_pitch];
}

int virtuappu_mode1_frame_width(void) {
    return mode1_frame_width;
}

int virtuappu_mode1_frame_pitch(void) {
    return mode1_frame_pitch;
}

static const uint8_t mode1_obj_widths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };

static const uint8_t mode1_obj_heights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static uint16_t mode1_tile_index(Mode1TilemapEntry entry) {
    return entry.raw & 0x03FFu;
}

static bool mode1_tile_hflip(Mode1TilemapEntry entry) {
    return ((entry.raw >> 10u) & 1u) != 0u;
}

static bool mode1_tile_vflip(Mode1TilemapEntry entry) {
    return ((entry.raw >> 11u) & 1u) != 0u;
}

static uint8_t mode1_tile_palette(Mode1TilemapEntry entry) {
    return (uint8_t)((entry.raw >> 12u) & 0x0Fu);
}

static bool mode1_oam_affine(Mode1OAMAttr attr) {
    return ((attr.attr0 >> 8u) & 1u) != 0u;
}

static bool mode1_oam_double_size(Mode1OAMAttr attr) {
    return mode1_oam_affine(attr) && (((attr.attr0 >> 9u) & 1u) != 0u);
}

static bool mode1_oam_hidden(Mode1OAMAttr attr) {
    return !mode1_oam_affine(attr) && (((attr.attr0 >> 9u) & 1u) != 0u);
}

static bool mode1_oam_bpp8(Mode1OAMAttr attr) {
    return ((attr.attr0 >> 13u) & 1u) != 0u;
}

static int mode1_oam_y(Mode1OAMAttr attr) {
    return attr.attr0 & 0xFF;
}

static uint8_t mode1_oam_shape(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr0 >> 14u) & 3u);
}

static int mode1_oam_x(Mode1OAMAttr attr) {
    return attr.attr1 & 0x1FF;
}

static bool mode1_oam_hflip(Mode1OAMAttr attr) {
    return !mode1_oam_affine(attr) && (((attr.attr1 >> 12u) & 1u) != 0u);
}

static bool mode1_oam_vflip(Mode1OAMAttr attr) {
    return !mode1_oam_affine(attr) && (((attr.attr1 >> 13u) & 1u) != 0u);
}

static uint8_t mode1_oam_affine_index(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr1 >> 9u) & 0x1Fu);
}

static uint8_t mode1_oam_size(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr1 >> 14u) & 3u);
}

static uint16_t mode1_oam_tile_index(Mode1OAMAttr attr) {
    return attr.attr2 & 0x03FFu;
}

static uint8_t mode1_oam_priority(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr2 >> 10u) & 3u);
}

static uint8_t mode1_oam_palette(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr2 >> 12u) & 0x0Fu);
}

/* OBJ mode field (attr0 bits 10-11): 0 = normal, 1 = semi-transparent
 * (forced alpha-blend 1st target), 2 = OBJ window, 3 = prohibited. */
static uint8_t mode1_oam_mode(Mode1OAMAttr attr) {
    return (uint8_t)((attr.attr0 >> 10u) & 3u);
}

/* OBJ mosaic enable (attr0 bit 12). Block size comes from MOSAIC (0x4C)
 * bits 8-11 (h-1) / 12-15 (v-1). */
static bool mode1_oam_mosaic(Mode1OAMAttr attr) {
    return ((attr.attr0 >> 12u) & 1u) != 0u;
}

static bool mode1_is_first_target(uint16_t bldcnt, int layer_id) {
    return ((bldcnt >> layer_id) & 1u) != 0u;
}

static bool mode1_is_second_target(uint16_t bldcnt, int layer_id) {
    return ((bldcnt >> (layer_id + 8)) & 1u) != 0u;
}

static uint32_t mode1_alpha_blend(uint32_t top_abgr, uint32_t bottom_abgr, int eva, int evb) {
    /* VIRTUAPPU_BLEND_5BIT: GBA alpha blend is defined on 5-bit channels
     * (GBATEK: I = MIN(31, I1*eva + I2*evb), eva/evb already clamped 0..16 by
     * the caller). The framebuffer is 8-bit (palette value <<3), so recover the
     * 5-bit intensity (>>3), blend with a truncating >>4 exactly as the
     * hardware does, clamp to 31, then re-expand (<<3). This reproduces the
     * GBA's quantised blend banding instead of a smoother 8-bit-domain blend. */
    int top_r = (int)((top_abgr >> 0u) & 0xFFu) >> 3;
    int top_g = (int)((top_abgr >> 8u) & 0xFFu) >> 3;
    int top_b = (int)((top_abgr >> 16u) & 0xFFu) >> 3;
    int bottom_r = (int)((bottom_abgr >> 0u) & 0xFFu) >> 3;
    int bottom_g = (int)((bottom_abgr >> 8u) & 0xFFu) >> 3;
    int bottom_b = (int)((bottom_abgr >> 16u) & 0xFFu) >> 3;
    int out_r = (top_r * eva + bottom_r * evb) >> 4;
    int out_g = (top_g * eva + bottom_g * evb) >> 4;
    int out_b = (top_b * eva + bottom_b * evb) >> 4;

    if (out_r > 31) {
        out_r = 31;
    }
    if (out_g > 31) {
        out_g = 31;
    }
    if (out_b > 31) {
        out_b = 31;
    }

    return 0xFF000000u | ((uint32_t)(out_b << 3) << 16u) | ((uint32_t)(out_g << 3) << 8u) | (uint32_t)(out_r << 3);
}

static uint32_t mode1_brighten(uint32_t abgr, int evy) {
    /* GBA brightness-increase on 5-bit channels (GBATEK):
     * I = I + (31-I)*evy/16, truncating; evy already clamped 0..16. */
    int r = (int)((abgr >> 0u) & 0xFFu) >> 3;
    int g = (int)((abgr >> 8u) & 0xFFu) >> 3;
    int b = (int)((abgr >> 16u) & 0xFFu) >> 3;

    r = r + (((31 - r) * evy) >> 4);
    g = g + (((31 - g) * evy) >> 4);
    b = b + (((31 - b) * evy) >> 4);

    if (r > 31) {
        r = 31;
    }
    if (g > 31) {
        g = 31;
    }
    if (b > 31) {
        b = 31;
    }

    return 0xFF000000u | ((uint32_t)(b << 3) << 16u) | ((uint32_t)(g << 3) << 8u) | (uint32_t)(r << 3);
}

static uint32_t mode1_darken(uint32_t abgr, int evy) {
    /* GBA brightness-decrease on 5-bit channels (GBATEK):
     * I = I - I*evy/16, truncating; evy already clamped 0..16. */
    int r = (int)((abgr >> 0u) & 0xFFu) >> 3;
    int g = (int)((abgr >> 8u) & 0xFFu) >> 3;
    int b = (int)((abgr >> 16u) & 0xFFu) >> 3;

    r -= ((r * evy) >> 4);
    g -= ((g * evy) >> 4);
    b -= ((b * evy) >> 4);

    if (r < 0) {
        r = 0;
    }
    if (g < 0) {
        g = 0;
    }
    if (b < 0) {
        b = 0;
    }

    return 0xFF000000u | ((uint32_t)(b << 3) << 16u) | ((uint32_t)(g << 3) << 8u) | (uint32_t)(r << 3);
}

void virtuappu_mode1_bind_gba_memory(const VirtuaPPUMode1GbaMemory* memory) {
    /* Validate at bind: any missing pointer falls back to a (zeroed) default
     * buffer so render never dereferences NULL. A fallback means the engine's
     * memory wasn't wired up — surface it once instead of silently rendering a
     * blank/garbage frame. (Per-access range clamps remain the OOB guard for
     * the bound buffers themselves; see the module-state contract above.) */
    const bool fell_back = memory == NULL || memory->io_mem == NULL || memory->vram == NULL ||
                           memory->bg_palette == NULL || memory->obj_palette == NULL || memory->oam_mem == NULL;

    mode1_memory.io_mem = (memory != NULL && memory->io_mem != NULL) ? memory->io_mem : mode1_default_io_mem;
    mode1_memory.vram = (memory != NULL && memory->vram != NULL) ? memory->vram : mode1_default_vram;
    mode1_memory.bg_palette =
        (memory != NULL && memory->bg_palette != NULL) ? memory->bg_palette : mode1_default_bg_palette;
    mode1_memory.obj_palette =
        (memory != NULL && memory->obj_palette != NULL) ? memory->obj_palette : mode1_default_obj_palette;
    mode1_memory.oam_mem = (memory != NULL && memory->oam_mem != NULL) ? memory->oam_mem : mode1_default_oam_mem;

    if (fell_back) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[ppu] WARNING: bind_gba_memory received a NULL pointer; using zeroed "
                            "default buffer(s). The PPU will render blank until real GBA memory is "
                            "bound.\n");
        }
    }
}

void virtuappu_mode1_get_bound_gba_memory(VirtuaPPUMode1GbaMemory* memory) {
    if (memory == NULL) {
        return;
    }

    *memory = mode1_memory;
}

/* PORT_PARALLEL_RENDER: when the per-line IO snapshot pass populates
 * a thread-local override (set by the parallel render loop in
 * virtuappu_mode1_render_frame), read from the snapshot instead of the
 * global io_mem. Lets per-scanline rendering be parallelized over OpenMP
 * threads while still seeing the correct HDMA-mutated register state
 * (BG SCROLL, BLDY, etc.) for that scanline. NULL on the main / setup
 * thread keeps the existing single-threaded behavior intact. */
#ifdef TMC_N64
/* N64: single-threaded (no OpenMP) and bare-metal (no TLS). __thread compiles to
 * RDHWR $29, which the VR4300 (MIPS III) doesn't implement -> Reserved Instruction.
 * Single-threaded rendering makes plain file-scope storage correct here. */
#define VPPU_TLS
#else
#define VPPU_TLS __thread
#endif
VPPU_TLS const uint8_t* virtuappu_mode1_io_thread_override = NULL;

/* Per-scanline scratch (one slot per screen column) marking which OBJ pixels
 * came from a semi-transparent OBJ (attr0 mode 1). Written by render_obj_line
 * for the winning OBJ pixel, read by composite_line to force alpha blending —
 * GBA-accurate (a mode-1 OBJ is an unconditional blend 1st target). __thread so
 * each OpenMP render thread, which processes a whole scanline at a time, has
 * its own copy; render_obj_line clears it at the top of every line. */
VPPU_TLS uint8_t virtuappu_mode1_obj_semitrans[MODE1_GBA_WIDTH];

/* Per-scanline OBJ-window mask (attr0 mode 2). A mode-2 OBJ is INVISIBLE on
 * GBA — it doesn't draw colour; its opaque pixels just carve the OBJ window,
 * inside which WINOUT's high byte (WINOBJ) selects which layers show. Written
 * by render_obj_line, read by composite_line. __thread + cleared per line, as
 * with obj_semitrans. */
VPPU_TLS uint8_t virtuappu_mode1_obj_window[MODE1_GBA_WIDTH];

/* Per-pixel vertical OBJ clip (PC port "swamp sink"): the port marks which OAM
 * entries to clip (one byte per OAM index) and a waterline scanline; a marked
 * entry's pixels at line >= waterline are dropped, leaving everything else
 * untouched. SHARED (not VPPU_TLS): set once per frame by the port before the
 * render and read-only during the OpenMP-parallel scanline render. */
uint8_t virtuappu_mode1_obj_clip_mark[MODE1_GBA_OAM_COUNT];
int virtuappu_mode1_obj_clip_y;
int virtuappu_mode1_obj_clip_enable;

uint16_t virtuappu_mode1_io_read16(uint16_t offset) {
    const uint8_t* src = virtuappu_mode1_io_thread_override ? virtuappu_mode1_io_thread_override : mode1_memory.io_mem;
#ifdef TMC_N64
    return *(const uint16_t*)(src + offset); /* native: engine stores IO regs in host order (BE on N64) */
#else
    return (uint16_t)src[offset] | ((uint16_t)src[offset + 1u] << 8u);
#endif
}

uint32_t virtuappu_mode1_io_read32(uint16_t offset) {
    return (uint32_t)virtuappu_mode1_io_read16(offset) |
           ((uint32_t)virtuappu_mode1_io_read16((uint16_t)(offset + 2u)) << 16u);
}

/* Per-frame ABGR8888 palette lookup tables. The GBA palette is only 256 BG +
 * 256 OBJ entries and is frame-constant (the fade/brightness engine rewrites
 * palette RAM once per frame BEFORE render; the HDMA pre-line callback mutates
 * only IO registers, never palette RAM). So converting all 512 entries once per
 * frame and indexing them per pixel replaces the per-pixel rgb555->ABGR math
 * (5-6 shifts/masks/ORs) with a single load — a big win on the in-order A53,
 * with ZERO added branches. Built by PublishPaletteLuts before the parallel
 * scanline render; SHARED read-only during it (same contract as ws_shadow).
 * Byte-exact: LUT[i] == rgb555_to_abgr(palette[i]) by construction. */
static uint32_t mode1_bg_abgr_lut[MODE1_PALETTE_COLORS];
static uint32_t mode1_obj_abgr_lut[MODE1_PALETTE_COLORS];
static bool mode1_color_correction;
static const uint8_t mode1_gba_lcd_lut[32] = {
    0, 0, 0, 0, 1, 2, 4, 8, 13, 19, 25, 32, 39, 46, 54, 63,
    71, 80, 90, 100, 110, 120, 131, 142, 154, 165, 178, 190, 203, 216, 229, 243,
};
#ifdef TMC_3DS
static uint8_t mode1_obj_line_indices[MODE1_GBA_HEIGHT][MODE1_GBA_OAM_COUNT];
static uint8_t mode1_obj_line_counts[MODE1_GBA_HEIGHT];
#endif

uint32_t virtuappu_mode1_rgb555_to_abgr8888(uint16_t color) {
    uint8_t r5 = (uint8_t)(color & 0x1Fu);
    uint8_t g5 = (uint8_t)((color >> 5u) & 0x1Fu);
    uint8_t b5 = (uint8_t)((color >> 10u) & 0x1Fu);
    uint8_t r = mode1_color_correction ? mode1_gba_lcd_lut[r5] : (uint8_t)(r5 << 3u);
    uint8_t g = mode1_color_correction ? mode1_gba_lcd_lut[g5] : (uint8_t)(g5 << 3u);
    uint8_t b = mode1_color_correction ? mode1_gba_lcd_lut[b5] : (uint8_t)(b5 << 3u);

    return 0xFF000000u | ((uint32_t)b << 16u) | ((uint32_t)g << 8u) | (uint32_t)r;
}

void virtuappu_mode1_set_color_correction(bool enabled) {
    mode1_color_correction = enabled;
}

/* Convert the current BG + OBJ palette RAM into the ABGR8888 LUTs. Call once
 * per frame after the palette is final (post pre-line callback) and before the
 * parallel scanline render. */
static void virtuappu_mode1_publish_palette_luts(void) {
    int i;
    for (i = 0; i < MODE1_PALETTE_COLORS; ++i) {
        mode1_bg_abgr_lut[i] = virtuappu_mode1_rgb555_to_abgr8888(mode1_memory.bg_palette[i]);
        mode1_obj_abgr_lut[i] = virtuappu_mode1_rgb555_to_abgr8888(mode1_memory.obj_palette[i]);
    }
}

#ifdef TMC_3DS
static void virtuappu_mode1_publish_obj_line_lists(void) {
    memset(mode1_obj_line_counts, 0, sizeof(mode1_obj_line_counts));
    for (int i = MODE1_GBA_OAM_COUNT - 1; i >= 0; --i) {
        Mode1OAMAttr attr = {
            mode1_memory.oam_mem[i * 4],
            mode1_memory.oam_mem[i * 4 + 1],
            mode1_memory.oam_mem[i * 4 + 2],
        };
        if (mode1_oam_hidden(attr)) continue;
        const uint8_t shape = mode1_oam_shape(attr);
        if (shape >= 3) continue;
        const uint8_t size = mode1_oam_size(attr);
        int height = mode1_obj_heights[shape][size];
        if (mode1_oam_affine(attr) && mode1_oam_double_size(attr)) height *= 2;
        int first = mode1_oam_y(attr);
        if (first >= MODE1_GBA_HEIGHT) first -= 256;
        int last = first + height;
        if (first < 0) first = 0;
        if (last > MODE1_GBA_HEIGHT) last = MODE1_GBA_HEIGHT;
        for (int line = first; line < last; ++line) {
            const uint8_t count = mode1_obj_line_counts[line];
            mode1_obj_line_indices[line][count] = (uint8_t)i;
            mode1_obj_line_counts[line] = (uint8_t)(count + 1u);
        }
    }
}
#endif

void virtuappu_mode1_render_text_bg_line(int bg_index, int line, uint32_t* line_buffer, uint8_t* priority_buffer) {
    uint16_t bgcnt = virtuappu_mode1_io_read16((uint16_t)(MODE1_IO_BG0CNT + bg_index * 2));
    uint8_t priority = (uint8_t)(bgcnt & 3u);
    uint32_t char_base = (uint32_t)((bgcnt >> 2u) & 3u) * 0x4000u;
    bool mosaic_on = ((bgcnt >> 6u) & 1u) != 0u;
    bool bpp8 = ((bgcnt >> 7u) & 1u) != 0u;
    uint32_t screen_base = (uint32_t)((bgcnt >> 8u) & 0x1Fu) * 0x800u;
    uint16_t size_flag = (uint16_t)((bgcnt >> 14u) & 3u);
    int map_width_tiles = (size_flag & 1u) ? 64 : 32;
    int map_height_tiles = (size_flag & 2u) ? 64 : 32;
    int scroll_x = virtuappu_mode1_io_read16((uint16_t)(MODE1_IO_BG0HOFS + bg_index * 4)) & 0x1FF;
    int scroll_y = virtuappu_mode1_io_read16((uint16_t)(MODE1_IO_BG0VOFS + bg_index * 4)) & 0x1FF;
    uint16_t mosaic_reg = virtuappu_mode1_io_read16(MODE1_IO_MOSAIC);
    /* BG mosaic honored when BGCNT bit 6 is set (GBA-accurate; gate removed). */
    int mosaic_h = mosaic_on ? (int)((mosaic_reg & 0x0Fu) + 1u) : 1;
    int mosaic_v = mosaic_on ? (int)(((mosaic_reg >> 4u) & 0x0Fu) + 1u) : 1;
    /* N64 perf: map_{width,height}_tiles*8 is always 256 or 512 (pow2) and the
     * operands are non-negative, so `% (w)` == `& (w-1)` — avoids the R4300's
     * ~37-cyc idiv. The mosaic `/` is guarded to the (rare) mosaic-on case.
     * Identity-preserving on every host. */
    int eff_line = (mosaic_v == 1) ? line : (line / mosaic_v) * mosaic_v;
    int src_y = (eff_line + scroll_y) & (map_height_tiles * 8 - 1);
    int tile_row = src_y / 8;
    int pixel_y = src_y % 8;
    int x;
    const int frame_width = mode1_frame_width;
    /* Widescreen Option A: 32-tile BGs have valid VRAM tile data only
     * within the native 240 px. Cull the line to the current visible frame
     * width, but keep the fixed framebuffer pitch separate for presentation. */
    int render_max_x = (map_width_tiles >= 64)
                           ? frame_width
                           : ((virtuappu_mode1_ws_shadow[bg_index] != NULL) ? frame_width : MODE1_GBA_BG_CLIP_X);
    if (render_max_x > frame_width)
        render_max_x = frame_width;
    const bool ws_shadow_active = (map_width_tiles < 64) && (virtuappu_mode1_ws_shadow[bg_index] != NULL);
    const int ws_shadow_base = virtuappu_mode1_ws_shadow_base_tile[bg_index];
    uint16_t* const ws_shadow = virtuappu_mode1_ws_shadow[bg_index];
    const bool ws_hud_right_anchor =
        (bg_index == 0) && (virtuappu_mode1_ws_hud_right_anchor != 0) && (frame_width > MODE1_GBA_BG_CLIP_X);
    const int ws_hud_right_dst_x = frame_width - (MODE1_GBA_BG_CLIP_X - MODE1_WS_HUD_RIGHT_NATIVE_X);
    /* Message-box centering: on BG0 lines inside the published box band,
     * draw the box's native columns shifted right by ws_msg_shift and skip
     * them at their native position (see mode1.h). */
    const int ws_msg_shift = virtuappu_mode1_ws_msg_shift;
    const bool ws_msg_line = (bg_index == 0) && (ws_msg_shift != 0) && (frame_width > MODE1_GBA_BG_CLIP_X) &&
                             (line >= virtuappu_mode1_ws_msg_y0) && (line < virtuappu_mode1_ws_msg_y1);
    const int ws_msg_x0 = virtuappu_mode1_ws_msg_x0;
    const int ws_msg_x1 = virtuappu_mode1_ws_msg_x1;

    if (ws_hud_right_anchor || ws_msg_line) {
        render_max_x = frame_width;
    }
    /* N64 perf: the tilemap screen-entry read + its address math are constant
     * across a tile's 8 px; cache them and recompute only when the tile cell
     * (or the shadow/native source) changes. Byte-identical: the cached value
     * equals the per-pixel recompute. Line-constants hoisted out of the x-loop. */
    const int screen_block_y = tile_row / 32;
    const int local_row = tile_row % 32;
    const int blocks_per_row = map_width_tiles / 32;
    int bg_cache_key = -1;
    Mode1TilemapEntry bg_tile_entry;
    bg_tile_entry.raw = 0u;
    /* Per-tile derived values, recomputed only on a cache refill (tile change).
     * Constant across a tile's 8 px, so hoisting them out of the per-pixel body
     * removes ~5 ALU ops/pixel with zero added branches (A53-friendly). */
    bool bg_hflip = false;
    int bg_tpy = 0;
    uint32_t bg_row_base = 0u;
    size_t bg_pal_bank = 0u;

    /* The per-pixel body is identical for the fast (native / no-remap) and the
     * widescreen-remap paths; keep it in one macro so the two loops can never
     * drift. `_sx` is the tilemap SAMPLE column; the loop var `x` is always the
     * destination column (and drives the x>=240 shadow/sentinel guards). */
#define MODE1_BG_PIXEL(_sx)                                                                                            \
    do {                                                                                                               \
        int eff_x = (mosaic_h == 1) ? (_sx) : ((_sx) / mosaic_h) * mosaic_h;                                           \
        int src_x = (eff_x + scroll_x) & (map_width_tiles * 8 - 1);                                                    \
        int tile_col = src_x / 8;                                                                                      \
        int pixel_x = src_x % 8;                                                                                       \
        int cache_use_shadow = (ws_shadow_active && x >= MODE1_GBA_BG_CLIP_X) ? 1 : 0;                                 \
        int cache_key = (tile_col << 1) | cache_use_shadow;                                                            \
        if (cache_key != bg_cache_key) {                                                                               \
            bg_cache_key = cache_key;                                                                                  \
            if (cache_use_shadow) {                                                                                    \
                int shadow_idx = (tile_col - ws_shadow_base + 32) % 32;                                                \
                bg_tile_entry.raw = (shadow_idx < MODE1_WS_SHADOW_COLS)                                                \
                                        ? ws_shadow[(size_t)local_row * MODE1_WS_SHADOW_COLS + shadow_idx]             \
                                        : (uint16_t)0u;                                                                \
            } else {                                                                                                   \
                int screen_block_x = tile_col / 32;                                                                    \
                int screen_block_index = screen_block_x + screen_block_y * blocks_per_row;                             \
                int local_col = tile_col % 32;                                                                         \
                uint32_t map_addr =                                                                                    \
                    screen_base + (uint32_t)screen_block_index * 0x800u + (uint32_t)(local_row * 32 + local_col) * 2u; \
                bg_tile_entry.raw =                                                                                    \
                    (uint16_t)mode1_memory.vram[map_addr] | ((uint16_t)mode1_memory.vram[map_addr + 1u] << 8u);        \
            }                                                                                                          \
            bg_hflip = mode1_tile_hflip(bg_tile_entry);                                                                \
            bg_tpy = mode1_tile_vflip(bg_tile_entry) ? (7 - pixel_y) : pixel_y;                                        \
            if (bpp8) {                                                                                                \
                bg_row_base = char_base + (uint32_t)mode1_tile_index(bg_tile_entry) * 64u + (uint32_t)bg_tpy * 8u;     \
            } else {                                                                                                   \
                bg_row_base = char_base + (uint32_t)mode1_tile_index(bg_tile_entry) * 32u + (uint32_t)bg_tpy * 4u;     \
            }                                                                                                          \
            bg_pal_bank = (size_t)mode1_tile_palette(bg_tile_entry) * 16u;                                             \
        }                                                                                                              \
        int tile_pixel_x = bg_hflip ? (7 - pixel_x) : pixel_x;                                                         \
        uint8_t color_index;                                                                                           \
        if (bpp8) {                                                                                                    \
            uint32_t addr = bg_row_base + (uint32_t)tile_pixel_x;                                                      \
            color_index = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;                                     \
        } else {                                                                                                       \
            uint32_t addr = bg_row_base + (uint32_t)(tile_pixel_x / 2);                                                \
            uint8_t packed = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;                                  \
            color_index = (tile_pixel_x & 1) ? (packed >> 4u) : (packed & 0x0Fu);                                      \
        }                                                                                                              \
        if (color_index != 0u) {                                                                                       \
            size_t pal_idx = bpp8 ? (size_t)color_index : (bg_pal_bank + color_index);                                 \
            if (!(x >= MODE1_GBA_BG_CLIP_X && (mode1_memory.bg_palette[pal_idx] & 0x7FFFu) == 0x7C1Fu)) {              \
                line_buffer[x] = mode1_bg_abgr_lut[pal_idx];                                                           \
                if (priority_buffer != NULL) {                                                                         \
                    priority_buffer[x] = priority;                                                                     \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

    if (!ws_msg_line && !ws_hud_right_anchor) {
        /* Fast path: no widescreen column remap, so sample_x == x. Hoists the
         * per-pixel remap dispatch (its two flags are per-line invariants) out
         * of the hot loop entirely — A53 win, zero added per-pixel branch. */
        for (x = 0; x < render_max_x; ++x) {
            MODE1_BG_PIXEL(x);
        }
    } else {
        for (x = 0; x < render_max_x; ++x) {
            int sample_x = x;
            if (ws_msg_line && x >= ws_msg_x0 + ws_msg_shift && x < ws_msg_x1 + ws_msg_shift) {
                /* Inside the shifted box: sample the box's native columns. */
                sample_x = x - ws_msg_shift;
            } else if (ws_msg_line && x >= ws_msg_x0 && x < ws_msg_x1) {
                /* The box's native columns are suppressed (moved right). */
                continue;
            } else if (ws_hud_right_anchor && !ws_msg_line) {
                /* Anchor is suspended on box lines: a top-anchored box overlaps
                 * the rupee rows, and remapping cols 176..239 there would draw a
                 * second copy of the box fragment at the far right. */
                if (x >= ws_hud_right_dst_x) {
                    sample_x = x - (frame_width - MODE1_GBA_BG_CLIP_X);
                } else if (x >= MODE1_WS_HUD_RIGHT_NATIVE_X) {
                    continue;
                }
            } else if (ws_msg_line && x >= MODE1_GBA_BG_CLIP_X) {
                /* Box lines extend past 240 only for the shifted box copy;
                 * everything else on the line keeps native clipping. */
                continue;
            }
            MODE1_BG_PIXEL(sample_x);
        }
    }
#undef MODE1_BG_PIXEL
}

void virtuappu_mode1_render_obj_line(int line, bool obj_1d, uint32_t* line_buffer, uint8_t* priority_buffer) {
    const uint32_t obj_tile_base = 0x10000u;
    /* OBJ mosaic block dimensions for this line (MOSAIC is in the per-line IO
     * snapshot). GBATEK: block = (Mh+1) x (Mv+1); a mosaic sprite samples the
     * texel of the block's top-left SCREEN position. */
    const uint16_t mosaic_reg = virtuappu_mode1_io_read16(0x4Cu);
    const int mosaic_h = (int)((mosaic_reg >> 8u) & 0x0Fu) + 1;
    const int mosaic_v = (int)((mosaic_reg >> 12u) & 0x0Fu) + 1;
    int i;

    const int obj_frame_width = mode1_frame_width;
    memset(virtuappu_mode1_obj_semitrans, 0, (size_t)obj_frame_width);
    memset(virtuappu_mode1_obj_window, 0, (size_t)obj_frame_width);

#ifdef TMC_3DS
    const int line_object_count = mode1_obj_line_counts[line];
    for (int line_object = 0; line_object < line_object_count; ++line_object) {
        i = mode1_obj_line_indices[line][line_object];
#else
    for (i = MODE1_GBA_OAM_COUNT - 1; i >= 0; --i) {
#endif
        Mode1OAMAttr attr;
        uint8_t shape;
        uint8_t size;
        int obj_width;
        int obj_height;
        bool is_affine;
        int bounds_width;
        int bounds_height;
        int obj_y;
        int obj_x;
        bool bpp8;
        uint8_t priority;
        uint16_t base_tile;
        int tiles_w;
        int16_t pa = 0x100;
        int16_t pb = 0;
        int16_t pc = 0;
        int16_t pd = 0x100;
        int half_width;
        int half_height;
        int sprite_half_width;
        int sprite_half_height;
        int input_rel_y;
        int sx;
        int sx_start;
        int sx_end;
        int viewport_width;
        bool mosaic_x_on;
        int eff_line;

        attr.attr0 = mode1_memory.oam_mem[i * 4];
        attr.attr1 = mode1_memory.oam_mem[i * 4 + 1];
        attr.attr2 = mode1_memory.oam_mem[i * 4 + 2];

        if (mode1_oam_hidden(attr)) {
            continue;
        }

        shape = mode1_oam_shape(attr);
        size = mode1_oam_size(attr);
        if (shape >= 3) {
            /* OAM shape 3 is prohibited on GBA hardware and absent from the
             * mode1_obj_widths/heights[3][4] tables; skip rather than index out
             * of bounds (UBSan: degraded room data can feed a shape-3 entry). */
            continue;
        }
        obj_width = mode1_obj_widths[shape][size];
        obj_height = mode1_obj_heights[shape][size];
        is_affine = mode1_oam_affine(attr);
        bounds_width = obj_width;
        bounds_height = obj_height;

        if (is_affine && mode1_oam_double_size(attr)) {
            bounds_width *= 2;
            bounds_height *= 2;
        }

        obj_y = mode1_oam_y(attr);
        if (obj_y >= MODE1_GBA_HEIGHT) {
            obj_y -= 256;
        }
        if (line < obj_y || line >= obj_y + bounds_height) {
            continue;
        }

        obj_x = mode1_oam_x(attr);
        viewport_width = obj_frame_width;
        if (viewport_width > MODE1_GBA_VIEWPORT_X) {
            viewport_width = MODE1_GBA_VIEWPORT_X;
        }
        if (obj_x >= obj_frame_width) {
            obj_x -= 512;
        }
        sx_start = 0;
        sx_end = bounds_width;
        if (obj_x + bounds_width <= 0 || obj_x >= viewport_width) {
            continue;
        }
        if (obj_x < 0) {
            sx_start = -obj_x;
        }
        if (obj_x + sx_end > viewport_width) {
            sx_end = viewport_width - obj_x;
        }

        bpp8 = mode1_oam_bpp8(attr);
        priority = mode1_oam_priority(attr);
        base_tile = mode1_oam_tile_index(attr);
        tiles_w = obj_width / 8;

        if (is_affine) {
            int affine_group = mode1_oam_affine_index(attr);
            pa = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 3];
            pb = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 7];
            pc = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 11];
            pd = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 15];
        }

        half_width = bounds_width / 2;
        half_height = bounds_height / 2;
        sprite_half_width = obj_width / 2;
        sprite_half_height = obj_height / 2;

        /* OBJ mosaic (attr0 bit 12): the pixel shown at screen (x, line)
         * samples the sprite as if at the mosaic block's top-left screen
         * position (x - x%Mh, line - line%Mv). Vertical part is constant for
         * the whole scanline; a snapped line above the sprite's top samples
         * nothing (transparent). */
        {
            bool mosaic_on = mode1_oam_mosaic(attr) && (mosaic_h > 1 || mosaic_v > 1);
            eff_line = line;
            if (mosaic_on) {
                eff_line = line - (line % mosaic_v);
                if (eff_line < obj_y) {
                    continue;
                }
            }
            input_rel_y = eff_line - obj_y - half_height;
            mosaic_x_on = mosaic_on && mosaic_h > 1;
        }

        /* Horizontal frustum culling is done once per sprite. Partially visible
         * sprites enter the loop at the first on-screen column, so the hot
         * pixel path has no screen_x bounds branch. */
        for (sx = sx_start; sx < sx_end; ++sx) {
            int screen_x = obj_x + sx;
            int eff_sx = sx;
            int tex_x;
            int tex_y;
            int tile_row;
            int pixel_y;
            int tile_col;
            int pixel_x;
            uint16_t tile_index;
            uint8_t color_index;

            if (mosaic_x_on) {
                /* Sample at the mosaic block's left SCREEN column; a snapped
                 * column left of the sprite's edge samples nothing. */
                int snapped_x = screen_x - (screen_x % mosaic_h);
                eff_sx = snapped_x - obj_x;
                if (eff_sx < 0) {
                    continue;
                }
            }

            if (is_affine) {
                int input_rel_x = eff_sx - half_width;
                tex_x = ((pa * input_rel_x + pb * input_rel_y) >> 8) + sprite_half_width;
                tex_y = ((pc * input_rel_x + pd * input_rel_y) >> 8) + sprite_half_height;
                if (tex_x < 0 || tex_x >= obj_width || tex_y < 0 || tex_y >= obj_height) {
                    continue;
                }
            } else {
                int draw_x = mode1_oam_hflip(attr) ? (obj_width - 1 - eff_sx) : eff_sx;
                int draw_y = eff_line - obj_y;
                if (mode1_oam_vflip(attr)) {
                    draw_y = obj_height - 1 - draw_y;
                }
                tex_x = draw_x;
                tex_y = draw_y;
            }

            tile_row = tex_y / 8;
            pixel_y = tex_y % 8;
            tile_col = tex_x / 8;
            pixel_x = tex_x % 8;

            if (obj_1d) {
                tile_index = (uint16_t)(base_tile + tile_row * tiles_w + tile_col);
                if (bpp8) {
                    tile_index = (uint16_t)(base_tile + (tile_row * tiles_w + tile_col) * 2);
                }
            } else {
                tile_index = (uint16_t)(base_tile + tile_row * 32 + tile_col);
                if (bpp8) {
                    tile_index = (uint16_t)(base_tile + tile_row * 32 + tile_col * 2);
                }
            }

            if (bpp8) {
                uint32_t addr = obj_tile_base + (uint32_t)tile_index * 32u + (uint32_t)pixel_y * 8u + (uint32_t)pixel_x;
                color_index = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;
            } else {
                uint32_t addr =
                    obj_tile_base + (uint32_t)tile_index * 32u + (uint32_t)pixel_y * 4u + (uint32_t)(pixel_x / 2);
                uint8_t packed = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;
                color_index = (pixel_x & 1) ? (packed >> 4u) : (packed & 0x0Fu);
            }

            if (color_index == 0u) {
                continue;
            }

            if (mode1_oam_mode(attr) == 2u) {
                /* OBJ-window sprite: invisible, only marks the window mask. */
                virtuappu_mode1_obj_window[screen_x] = 1u;
                continue;
            }

            /* OBJ-vs-OBJ is resolved by OAM index ALONE (GBATEK): the lowest
             * OAM index's opaque pixel wins the OBJ layer, and only that
             * pixel's priority then competes against BGs. This loop walks
             * i = 127..0, so the later (lower-index) sprite must ALWAYS
             * overwrite — the old `priority_buffer < priority` guard let a
             * higher-index sprite with a lower priority number stay on top
             * (wrong sprite visible in the classic quirk case). */

            /* Per-pixel vertical OBJ clip (swamp sink): drop this marked entry's
             * pixels at or below the waterline. `i` is the OAM index, `line` the
             * scanline. */
            if (virtuappu_mode1_obj_clip_enable && virtuappu_mode1_obj_clip_mark[i] &&
                line >= virtuappu_mode1_obj_clip_y) {
                continue;
            }

            size_t pal_idx = bpp8 ? (size_t)color_index : ((size_t)mode1_oam_palette(attr) * 16u + color_index);
            line_buffer[screen_x] = mode1_obj_abgr_lut[pal_idx];
            priority_buffer[screen_x] = priority;
            virtuappu_mode1_obj_semitrans[screen_x] = (mode1_oam_mode(attr) == 1u) ? 1u : 0u;
        }
    }
}

void virtuappu_mode1_composite_line(int line, uint32_t bg_layers[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
                                    uint8_t bg_priority[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH],
                                    uint32_t obj_layer[MODE1_GBA_WIDTH], uint8_t obj_priority[MODE1_GBA_WIDTH],
                                    uint16_t dispcnt) {
    uint32_t backdrop_color = mode1_bg_abgr_lut[0];
    bool bg_enabled[MODE1_GBA_BG_COUNT] = { (dispcnt & MODE1_DISP_BG0_ON) != 0u, (dispcnt & MODE1_DISP_BG1_ON) != 0u,
                                            (dispcnt & MODE1_DISP_BG2_ON) != 0u, (dispcnt & MODE1_DISP_BG3_ON) != 0u };
    bool obj_enabled = (dispcnt & MODE1_DISP_OBJ_ON) != 0u;
    uint16_t bldcnt = virtuappu_mode1_io_read16(MODE1_IO_BLDCNT);
    uint16_t bldalpha = virtuappu_mode1_io_read16(MODE1_IO_BLDALPHA);
    uint16_t bldy = virtuappu_mode1_io_read16(MODE1_IO_BLDY);
    Mode1BlendEffect effect = (Mode1BlendEffect)((bldcnt >> 6u) & 3u);
    int eva = bldalpha & 0x1Fu;
    int evb = (bldalpha >> 8u) & 0x1Fu;
    int evy = bldy & 0x1Fu;
    uint8_t bg_order[MODE1_GBA_BG_COUNT] = { 0, 1, 2, 3 };
    uint8_t bg_order_priority[MODE1_GBA_BG_COUNT];
    bool win0_on = (dispcnt & MODE1_DISP_WIN0_ON) != 0u;
    bool win1_on = (dispcnt & MODE1_DISP_WIN1_ON) != 0u;
    bool objwin_on = (dispcnt & MODE1_DISP_OBJWIN_ON) != 0u;
    bool any_window = win0_on || win1_on || objwin_on;
    uint16_t winin = virtuappu_mode1_io_read16(MODE1_IO_WININ);
    uint16_t winout = virtuappu_mode1_io_read16(MODE1_IO_WINOUT);
    uint16_t win0h = virtuappu_mode1_io_read16(MODE1_IO_WIN0H);
    uint16_t win0v = virtuappu_mode1_io_read16(MODE1_IO_WIN0V);
    int win0_left = win0h >> 8u;
    int win0_right = win0h & 0xFFu;
    int win0_top = win0v >> 8u;
    int win0_bottom = win0v & 0xFFu;
    bool win0_h_wrap;
    bool win0_v_wrap;
    bool win0_v_active;
    uint16_t win1h = virtuappu_mode1_io_read16(MODE1_IO_WIN1H);
    uint16_t win1v = virtuappu_mode1_io_read16(MODE1_IO_WIN1V);
    int win1_left = win1h >> 8u;
    int win1_right = win1h & 0xFFu;
    int win1_top = win1v >> 8u;
    int win1_bottom = win1v & 0xFFu;
    bool win1_h_wrap;
    bool win1_v_wrap;
    bool win1_v_active;
    uint8_t win0_ctrl = (uint8_t)(winin & 0x3Fu);
    uint8_t win1_ctrl = (uint8_t)((winin >> 8u) & 0x3Fu);
    uint8_t outside_ctrl = (uint8_t)(winout & 0x3Fu);
    uint8_t objwin_ctrl = (uint8_t)((winout >> 8u) & 0x3Fu);
    int i;
    int x;
    const int frame_width = mode1_frame_width;
    uint32_t* const out_row = mode1_output_row(line);

    (void)bg_priority;

    if (eva > 16) {
        eva = 16;
    }
    if (evb > 16) {
        evb = 16;
    }
    if (evy > 16) {
        evy = 16;
    }

    if (win0_right > frame_width) {
        win0_right = frame_width;
    }
    if (win0_bottom > MODE1_GBA_HEIGHT) {
        win0_bottom = MODE1_GBA_HEIGHT;
    }
    if (win1_right > frame_width) {
        win1_right = frame_width;
    }
    if (win1_bottom > MODE1_GBA_HEIGHT) {
        win1_bottom = MODE1_GBA_HEIGHT;
    }

    win0_h_wrap = win0_left > win0_right;
    /* GBA: WINxV with top > bottom is an inverted/wrapped vertical span,
     * active for (line >= top || line < bottom) — mirrors the horizontal
     * winX_h_wrap handling. Previously the window went fully inactive on
     * inversion, blanking the digging-cave iris spotlight (src/scroll.c
     * Scroll5Sub2/Sub5) on small-iris frames where an inverted WINxV is packed. */
    win0_v_wrap = win0_top > win0_bottom;
    win0_v_active =
        win0_on && (win0_v_wrap ? (line >= win0_top || line < win0_bottom) : (line >= win0_top && line < win0_bottom));
    win1_h_wrap = win1_left > win1_right;
    win1_v_wrap = win1_top > win1_bottom;
    win1_v_active =
        win1_on && (win1_v_wrap ? (line >= win1_top || line < win1_bottom) : (line >= win1_top && line < win1_bottom));

    for (i = 0; i < MODE1_GBA_BG_COUNT; ++i) {
        bg_order_priority[i] = (uint8_t)(virtuappu_mode1_io_read16((uint16_t)(MODE1_IO_BG0CNT + i * 2)) & 3u);
    }

    /* Stable insertion sort by priority. GBA hardware breaks priority ties by
     * BG index (lower BG number drawn on top), so equal-priority BGs MUST keep
     * their initial 0,1,2,3 order. The previous selection sort swapped
     * non-adjacent entries, so a lower-priority BG (e.g. a disabled BG3 left at
     * priority 0 by a prior dark room) could swap forward and displace BG1 past
     * BG2 — flipping the BG1/BG2 tie-break and hiding a same-priority BG1 layer
     * behind BG2. #139: ToGrimblade's BG1 flame braziers vanished behind the
     * BG2 bowl after a dark-dojo round-trip left BG3CNT at priority 0. Using a
     * stable sort (`>` strict, only shift higher-priority entries) keeps the
     * GBA index tie-break intact regardless of the other BGs' priorities. */
    for (i = 1; i < MODE1_GBA_BG_COUNT; ++i) {
        uint8_t key = bg_order[i];
        uint8_t key_pri = bg_order_priority[key];
        int j = i - 1;
        while (j >= 0 && bg_order_priority[bg_order[j]] > key_pri) {
            bg_order[j + 1] = bg_order[j];
            --j;
        }
        bg_order[j + 1] = key;
    }

    const bool native_no_effect_fast_path =
        frame_width <= MODE1_GBA_BG_CLIP_X && !any_window && effect == MODE1_BLEND_NONE;
    const bool has_second_targets = (bldcnt & 0x3F00u) != 0u;

    for (x = 0; x < frame_width; ++x) {
        uint8_t win_ctrl = 0x3Fu;
        bool visible_bg[MODE1_GBA_BG_COUNT];
        bool visible_obj;
        bool allow_sfx;
        uint32_t top_color = backdrop_color;
        int top_layer = 5;
        uint32_t bottom_color = backdrop_color;
        int bottom_layer = 5;
        bool found_top = false;
        bool found_bottom = false;

        /* Most gameplay pixels have no window and no color effect. Only the
         * top layer can affect those pixels, so avoid finding a second layer.
         * Semi-transparent OBJ pixels retain the full blend path whenever a
         * possible second target is configured. */
        if (native_no_effect_fast_path &&
            !(obj_enabled && obj_layer[x] != 0u && virtuappu_mode1_obj_semitrans[x] && has_second_targets)) {
            const bool obj_candidate = obj_enabled && obj_layer[x] != 0u;
            const unsigned obj_p = obj_priority[x];

            for (int order_index = 0; order_index < MODE1_GBA_BG_COUNT; ++order_index) {
                const int bg = bg_order[order_index];
                if (obj_candidate && bg_order_priority[bg] >= obj_p) {
                    top_color = obj_layer[x];
                    found_top = true;
                    break;
                }
                if (bg_enabled[bg] && bg_layers[bg][x] != 0u) {
                    top_color = bg_layers[bg][x];
                    found_top = true;
                    break;
                }
            }
            if (!found_top && obj_candidate) top_color = obj_layer[x];
            out_row[x] = top_color;
            continue;
        }

        if (any_window) {
            win_ctrl = outside_ctrl;
            /* Window priority is WIN0 > WIN1 > OBJ-window > outside, so OBJ
             * window sits below WIN1/WIN0 (applied after this) and above the
             * plain outside control. */
            if (objwin_on && virtuappu_mode1_obj_window[x]) {
                win_ctrl = objwin_ctrl;
            }
            if (win1_v_active) {
                bool in_h = win1_h_wrap ? (x >= win1_left || x < win1_right) : (x >= win1_left && x < win1_right);
                if (in_h) {
                    win_ctrl = win1_ctrl;
                }
            }
            if (win0_v_active) {
                bool in_h = win0_h_wrap ? (x >= win0_left || x < win0_right) : (x >= win0_left && x < win0_right);
                if (in_h) {
                    win_ctrl = win0_ctrl;
                }
            }
        }

        visible_bg[0] = (win_ctrl & 0x01u) != 0u;
        visible_bg[1] = (win_ctrl & 0x02u) != 0u;
        visible_bg[2] = (win_ctrl & 0x04u) != 0u;
        visible_bg[3] = (win_ctrl & 0x08u) != 0u;
        visible_obj = (win_ctrl & 0x10u) != 0u;
        allow_sfx = (win_ctrl & 0x20u) != 0u;

        /* Single merged pass over the priority-sorted bg_order, inserting OBJ at
         * its priority slot — byte-identical layering to the former
         * priority(0..3) x bg(0..3) double loop (up to 20 iters/pixel), but ~5
         * iters and one loop. OBJ is emitted right before the first bg whose
         * priority >= obj_priority[x] (GBA ties: OBJ over same-priority BG); if
         * no such bg, OBJ is emitted after the walk. Removing per-pixel work with
         * no added data-dependent branch is the A53-correct optimisation. */
        {
            bool obj_candidate = obj_enabled && visible_obj && (obj_layer[x] != 0u);
            unsigned obj_p = obj_priority[x];
            bool obj_emitted = false;
            int order_index;

#define MODE1_CONSIDER(_color, _layer) \
    do {                               \
        if (!found_top) {              \
            top_color = (_color);      \
            top_layer = (_layer);      \
            found_top = true;          \
        } else {                       \
            bottom_color = (_color);   \
            bottom_layer = (_layer);   \
            found_bottom = true;       \
        }                              \
    } while (0)

            for (order_index = 0; order_index < MODE1_GBA_BG_COUNT && !found_bottom; ++order_index) {
                int bg = bg_order[order_index];
                if (obj_candidate && !obj_emitted && bg_order_priority[bg] >= obj_p) {
                    MODE1_CONSIDER(obj_layer[x], 4);
                    obj_emitted = true;
                    if (found_bottom) {
                        break;
                    }
                }
                if (!bg_enabled[bg] || !visible_bg[bg] || bg_layers[bg][x] == 0u) {
                    continue;
                }
                MODE1_CONSIDER(bg_layers[bg][x], bg);
            }
            if (obj_candidate && !obj_emitted && !found_bottom) {
                MODE1_CONSIDER(obj_layer[x], 4);
            }
#undef MODE1_CONSIDER
        }

        if (allow_sfx) {
            if (top_layer == 4 && virtuappu_mode1_obj_semitrans[x]) {
                /* Semi-transparent OBJ (attr0 mode 1): GBA forces it to be an
                 * alpha-blend 1st target regardless of BLDCNT's effect mode and
                 * 1st-target bits. It blends with the layer directly below when
                 * that layer is a BLDCNT 2nd target (the backdrop counts via the
                 * BD bit) and that blend preempts any brighten/darken. When the
                 * layer below is NOT a 2nd target, GBATEK: "the brightness
                 * effect will take place" — the OBJ is still an always-1st
                 * target, so BLDY brighten/darken applies (previously skipped,
                 * leaving sprites unfaded during BLDY screen fades). */
                if (mode1_is_second_target(bldcnt, bottom_layer)) {
                    top_color = mode1_alpha_blend(top_color, bottom_color, eva, evb);
                } else if (effect == MODE1_BLEND_BRIGHTEN) {
                    top_color = mode1_brighten(top_color, evy);
                } else if (effect == MODE1_BLEND_DARKEN) {
                    top_color = mode1_darken(top_color, evy);
                }
            } else {
                switch (effect) {
                    case MODE1_BLEND_ALPHA:
                        if (mode1_is_first_target(bldcnt, top_layer) && mode1_is_second_target(bldcnt, bottom_layer)) {
                            top_color = mode1_alpha_blend(top_color, bottom_color, eva, evb);
                        }
                        break;
                    case MODE1_BLEND_BRIGHTEN:
                        if (mode1_is_first_target(bldcnt, top_layer)) {
                            top_color = mode1_brighten(top_color, evy);
                        }
                        break;
                    case MODE1_BLEND_DARKEN:
                        if (mode1_is_first_target(bldcnt, top_layer)) {
                            top_color = mode1_darken(top_color, evy);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        /* Widescreen Option A: past MODE1_GBA_BG_CLIP_X (240) the reveal
         * columns are valid only where render_text_bg_line wrote real tile
         * data — which happens only when a shadow is registered (gameplay)
         * and the world tile there is non-transparent. Everywhere else
         * (native 240, non-gameplay screens, a narrow room's empty edge)
         * bg_layers stays 0 here, so force-black and let port_ppu.cpp's
         * non-gameplay path fill the margin. */
        if (x >= MODE1_GBA_BG_CLIP_X) {
            bool any_bg_drew_here = false;
            for (int b = 0; b < MODE1_GBA_BG_COUNT; ++b) {
                if (bg_enabled[b] && bg_layers[b][x] != 0u) {
                    any_bg_drew_here = true;
                    break;
                }
            }
            out_row[x] = any_bg_drew_here ? top_color : 0xFF000000u;
        } else {
            out_row[x] = top_color;
        }
    }
}

/* Sub-pixel overlay for OAM affine sprites at internal-render-scale > 1.
 *
 * Called by the PC port AFTER the standard 240x160 render has already been
 * S*S nearest-replicated into `dst` (a 240*S by 160*S buffer). For each
 * affine OAM entry we re-run the matrix at sub-pixel density and write
 * straight to `dst`, which produces visibly smoother diagonals on rotated
 * sprites (Vaati's tornado, screen-shrink cinematic, every spinning enemy).
 *
 * Caveats:
 *   * No priority/blend layering — affine pixels overwrite whatever's at
 *     the target. Acceptable for a first pass because the affine sprite
 *     was already top-of-stack at scale 1; in TMC there are very few
 *     scenes where a non-affine sprite occludes an affine one.
 *   * Source texture is still 1x pixel art — no information to "recover".
 *     What you gain is sub-pixel sampling at the screen-space rotation,
 *     which trades the 240-grid staircase for an S*240-grid one.
 */
void virtuappu_mode1_render_affine_obj_overlay(uint32_t* dst, int dst_w, int dst_h, int scale) {
    if (dst == NULL || scale <= 1) {
        return;
    }
    if ((dst_w % scale) != 0 || dst_h != MODE1_GBA_HEIGHT * scale) {
        return;
    }
    const int viewport_width = dst_w / scale;
    if (viewport_width < 1 || viewport_width > MODE1_GBA_WIDTH) {
        return;
    }

    uint16_t dispcnt = virtuappu_mode1_io_read16(MODE1_IO_DISPCNT);
    if ((dispcnt & MODE1_DISP_FORCED_BLANK) != 0u) {
        return;
    }
    if ((dispcnt & MODE1_DISP_OBJ_ON) == 0u) {
        return;
    }
    bool obj_1d = (dispcnt & MODE1_DISP_OBJ_1D) != 0u;
    const uint32_t obj_tile_base = 0x10000u;

    /* OAM is iterated lowest-priority-first so higher-priority entries
     * overwrite — same convention as virtuappu_mode1_render_obj_line, just
     * applied at sub-pixel resolution. */
    for (int i = MODE1_GBA_OAM_COUNT - 1; i >= 0; --i) {
        Mode1OAMAttr attr;
        attr.attr0 = mode1_memory.oam_mem[i * 4];
        attr.attr1 = mode1_memory.oam_mem[i * 4 + 1];
        attr.attr2 = mode1_memory.oam_mem[i * 4 + 2];

        if (!mode1_oam_affine(attr))
            continue;
        if (mode1_oam_hidden(attr))
            continue;

        uint8_t shape = mode1_oam_shape(attr);
        uint8_t size = mode1_oam_size(attr);
        if (shape >= 3) {
            /* Prohibited OAM shape — absent from the width/height tables;
             * skip like render_obj_line does (degraded room data can feed a
             * shape-3 entry; indexing the [3][4] tables is UB). */
            continue;
        }
        int obj_width = mode1_obj_widths[shape][size];
        int obj_height = mode1_obj_heights[shape][size];
        int bounds_width = obj_width;
        int bounds_height = obj_height;
        if (mode1_oam_double_size(attr)) {
            bounds_width *= 2;
            bounds_height *= 2;
        }

        int obj_y = mode1_oam_y(attr);
        if (obj_y >= MODE1_GBA_HEIGHT)
            obj_y -= 256;
        int obj_x = mode1_oam_x(attr);
        if (obj_x >= viewport_width)
            obj_x -= 512;

        int sy_sub_start = 0;
        int sy_sub_end = bounds_height * scale;
        int sx_sub_start = 0;
        int sx_sub_end = bounds_width * scale;
        const int sprite_top_sub = obj_y * scale;
        const int sprite_left_sub = obj_x * scale;
        if (sprite_top_sub + sy_sub_end <= 0 || sprite_top_sub >= dst_h) {
            continue;
        }
        if (sprite_top_sub < 0) {
            sy_sub_start = -sprite_top_sub;
        }
        if (sprite_top_sub + sy_sub_end > dst_h) {
            sy_sub_end = dst_h - sprite_top_sub;
        }
        if (sprite_left_sub + sx_sub_end <= 0 || sprite_left_sub >= dst_w) {
            continue;
        }
        if (sprite_left_sub < 0) {
            sx_sub_start = -sprite_left_sub;
        }
        if (sprite_left_sub + sx_sub_end > dst_w) {
            sx_sub_end = dst_w - sprite_left_sub;
        }

        bool bpp8 = mode1_oam_bpp8(attr);
        uint16_t base_tile = mode1_oam_tile_index(attr);
        int tiles_w = obj_width / 8;

        int affine_group = mode1_oam_affine_index(attr);
        int16_t pa = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 3];
        int16_t pb = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 7];
        int16_t pc = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 11];
        int16_t pd = (int16_t)mode1_memory.oam_mem[affine_group * 16 + 15];

        int half_width = bounds_width / 2;
        int half_height = bounds_height / 2;
        int sprite_half_w = obj_width / 2;
        int sprite_half_h = obj_height / 2;

        /* Iterate only the visible output sub-pixels. The frustum clamp above
         * removes both the vertical and horizontal bounds branches from the
         * hot affine sprite loop. */
        for (int sy_sub = sy_sub_start; sy_sub < sy_sub_end; ++sy_sub) {
            /* line_sub = output row = sprite-top output row + sub-row within sprite. */
            int line_sub = sprite_top_sub + sy_sub;

            /* input_rel_y in (1/scale) source-pixel units. */
            int input_rel_y_subS = sy_sub - half_height * scale;

            for (int sx_sub = sx_sub_start; sx_sub < sx_sub_end; ++sx_sub) {
                int screen_x_sub = sprite_left_sub + sx_sub;
                int input_rel_x_subS = sx_sub - half_width * scale;
                /* tex_x / tex_y in source-pixel units. pa is 8.8 fixed,
                 * input_rel_*_subS is 1/scale source-pixels, so the product
                 * is (8.8 / scale) source-pixel units. >>8 trims the fixed
                 * fraction; / scale converts back to integer source-pixels. */
                int tex_x = (((int)pa * input_rel_x_subS + (int)pb * input_rel_y_subS) >> 8) / scale + sprite_half_w;
                int tex_y = (((int)pc * input_rel_x_subS + (int)pd * input_rel_y_subS) >> 8) / scale + sprite_half_h;
                if (tex_x < 0 || tex_x >= obj_width)
                    continue;
                if (tex_y < 0 || tex_y >= obj_height)
                    continue;

                int tile_row = tex_y / 8;
                int pixel_y = tex_y % 8;
                int tile_col = tex_x / 8;
                int pixel_x = tex_x % 8;

                uint16_t tile_index;
                if (obj_1d) {
                    tile_index = (uint16_t)(base_tile + tile_row * tiles_w + tile_col);
                    if (bpp8)
                        tile_index = (uint16_t)(base_tile + (tile_row * tiles_w + tile_col) * 2);
                } else {
                    tile_index = (uint16_t)(base_tile + tile_row * 32 + tile_col);
                    if (bpp8)
                        tile_index = (uint16_t)(base_tile + tile_row * 32 + tile_col * 2);
                }

                uint8_t color_index;
                if (bpp8) {
                    uint32_t addr =
                        obj_tile_base + (uint32_t)tile_index * 32u + (uint32_t)pixel_y * 8u + (uint32_t)pixel_x;
                    color_index = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;
                } else {
                    uint32_t addr =
                        obj_tile_base + (uint32_t)tile_index * 32u + (uint32_t)pixel_y * 4u + (uint32_t)(pixel_x / 2);
                    uint8_t packed = (addr < MODE1_VRAM_SIZE) ? mode1_memory.vram[addr] : 0u;
                    color_index = (pixel_x & 1) ? (packed >> 4u) : (packed & 0x0Fu);
                }
                if (color_index == 0u)
                    continue;

                uint16_t rgb555;
                if (bpp8) {
                    rgb555 = mode1_memory.obj_palette[color_index];
                } else {
                    rgb555 = mode1_memory.obj_palette[(size_t)mode1_oam_palette(attr) * 16u + color_index];
                }

                dst[(size_t)line_sub * (size_t)dst_w + (size_t)screen_x_sub] =
                    virtuappu_mode1_rgb555_to_abgr8888(rgb555);
            }
        }
    }
}
static inline int32_t mode1_sign_extend_28(uint32_t v) {
    int32_t s = (int32_t)v;
    if ((v & 0x08000000u) != 0u) {
        s |= (int32_t)0xF0000000u;
    }
    return s;
}

/* Host-set per-frame write strobes (see mode1.h). */
bool virtuappu_mode1_bg2x_hdma_strobe = false;
bool virtuappu_mode1_bg2y_hdma_strobe = false;

void virtuappu_mode1_affine_precompute(int height, int32_t init_ref_x, int32_t init_ref_y, const int32_t* line_ref_x,
                                       const int32_t* line_ref_y, const int16_t* line_pb, const int16_t* line_pd,
                                       bool reload_x_every_line, bool reload_y_every_line, int32_t* out_ref_x,
                                       int32_t* out_ref_y) {
    int32_t aint_x = init_ref_x;
    int32_t aint_y = init_ref_y;
    int32_t aprev_x = init_ref_x;
    int32_t aprev_y = init_ref_y;
    for (int line = 0; line < height; ++line) {
        const int32_t rx = line_ref_x[line];
        const int32_t ry = line_ref_y[line];
        /* A write to BG2X/BG2Y (e.g. the rolling barrel's per-scanline HBlank
         * DMA) reloads the internal reference; otherwise it keeps the value
         * advanced by pb/pd. Value-diff detection alone misses IDEMPOTENT
         * writes — constant-value HDMA pins the layer on hardware — so the
         * reload_*_every_line strobes force the reload when the host knows a
         * write event happens each line. */
        if (reload_x_every_line || rx != aprev_x) {
            aint_x = rx;
        }
        if (reload_y_every_line || ry != aprev_y) {
            aint_y = ry;
        }
        aprev_x = rx;
        aprev_y = ry;
        out_ref_x[line] = aint_x;
        out_ref_y[line] = aint_y;
        aint_x += line_pb[line];
        aint_y += line_pd[line];
    }
}

/* Render one scanline of the affine BG2 (GBA modes 1/2) from a precomputed
 * internal reference (virtuappu_mode1_affine_precompute). Reads pa/pc and
 * BG2CNT through the IO accessors — in the parallel pass the thread override
 * points at this line's snapshot — so it is independent per line. Byte-for-byte
 * the former mode2.c inner loop. */
static void mode1_render_affine_bg2_line(int frame_width, int32_t ref_x, int32_t ref_y, uint32_t* line_buffer,
                                         uint8_t* priority_buffer) {
    static const int affine_sizes[4] = { 128, 256, 512, 1024 };
    VirtuaPPUMode1GbaMemory memory;
    virtuappu_mode1_get_bound_gba_memory(&memory);

    const uint16_t bgcnt = virtuappu_mode1_io_read16(MODE1_IO_BG2CNT);
    const uint8_t bg_priority_value = (uint8_t)(bgcnt & 3u);
    const uint32_t char_base = (uint32_t)((bgcnt >> 2u) & 3u) * 0x4000u;
    const uint32_t screen_base = (uint32_t)((bgcnt >> 8u) & 0x1Fu) * 0x800u;
    const bool wrap = ((bgcnt >> 13u) & 1u) != 0u;
    const uint16_t size_flag = (uint16_t)((bgcnt >> 14u) & 3u);
    const int map_size = affine_sizes[size_flag];
    const int map_tiles = map_size / 8;
    const int16_t pa = (int16_t)virtuappu_mode1_io_read16(0x20u);
    const int16_t pc = (int16_t)virtuappu_mode1_io_read16(0x24u);

    for (int x = 0; x < frame_width; ++x) {
        int32_t src_x = (ref_x + pa * x) >> 8;
        int32_t src_y = (ref_y + pc * x) >> 8;

        if (wrap) {
            /* map_size is a power of two (128..1024) — mask instead of the
             * former double-idiv per axis per pixel (same trick as the text
             * BG path). */
            src_x &= (map_size - 1);
            src_y &= (map_size - 1);
        } else if (src_x < 0 || src_x >= map_size || src_y < 0 || src_y >= map_size) {
            continue;
        }

        const int tile_col = src_x / 8;
        const int tile_row = src_y / 8;
        const int pixel_x = src_x % 8;
        const int pixel_y = src_y % 8;
        const uint32_t map_addr = screen_base + (uint32_t)(tile_row * map_tiles + tile_col);
        const uint8_t tile_index = (map_addr < MODE1_VRAM_SIZE) ? memory.vram[map_addr] : 0u;
        const uint32_t tile_addr = char_base + (uint32_t)tile_index * 64u + (uint32_t)pixel_y * 8u + (uint32_t)pixel_x;
        const uint8_t color_index = (tile_addr < MODE1_VRAM_SIZE) ? memory.vram[tile_addr] : 0u;

        if (color_index == 0u) {
            continue;
        }

        line_buffer[x] = mode1_bg_abgr_lut[color_index];
        if (priority_buffer != NULL) {
            priority_buffer[x] = bg_priority_value; /* see render_text_bg_line */
        }
    }
}

int virtuappu_mode1_prepare_frame(const PPUMemory* ppu, uint8_t* io_per_line, uint16_t* dispcnt_per_line,
                                  int32_t* aff_ref_x, int32_t* aff_ref_y, uint16_t* out_frame_dispcnt) {
    /* Sequential portion of render_frame (see there for the threading contract):
     * run the per-line HDMA callback, snapshot IO + DISPCNT per line, and
     * precompute the affine BG2 per-line reference. No parallel render — the GPU
     * rasterizer does that. Kept byte-identical to render_frame's setup so the
     * GPU path sees exactly the CPU's per-line inputs. */
    const bool affine = (ppu->mode == 2);
    uint16_t dispcnt;
    int line;

    virtuappu_mode1_set_frame_geometry(ppu);

    dispcnt = virtuappu_mode1_io_read16(MODE1_IO_DISPCNT);
    if (out_frame_dispcnt != NULL) {
        *out_frame_dispcnt = dispcnt;
    }

    const bool do_affine_bg2 = affine && ((dispcnt & MODE1_DISP_BG2_ON) != 0u);
    int32_t aff_line_ref_x[MODE1_GBA_HEIGHT];
    int32_t aff_line_ref_y[MODE1_GBA_HEIGHT];
    int16_t aff_pb[MODE1_GBA_HEIGHT];
    int16_t aff_pd[MODE1_GBA_HEIGHT];
    int32_t aff_init_x = 0;
    int32_t aff_init_y = 0;
    if (do_affine_bg2) {
        aff_init_x = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x28u));
        aff_init_y = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x2Cu));
    }

    const bool per_line_io = (virtuappu_mode1_pre_line_callback != NULL);
    for (line = 0; line < MODE1_GBA_HEIGHT; ++line) {
        if (per_line_io) {
            virtuappu_mode1_pre_line_callback(line);
        }
        /* Snapshot IO for the GPU. When there is no per-line HDMA callback,
         * io_mem is identical on every scanline, so snapshot ONLY row 0 — the
         * GPU uploads one row and the shader reads row 0 for all lines. This
         * removes 159 KB of redundant memcpy + bus transfer per frame. With a
         * callback, every line can differ, so snapshot them all. */
        if (per_line_io || line == 0) {
            memcpy(&io_per_line[(size_t)line * MODE1_IO_MEM_SIZE], mode1_memory.io_mem, MODE1_IO_MEM_SIZE);
        }
        dispcnt_per_line[line] = affine ? dispcnt
                                        : (uint16_t)((uint16_t)mode1_memory.io_mem[MODE1_IO_DISPCNT] |
                                                     ((uint16_t)mode1_memory.io_mem[MODE1_IO_DISPCNT + 1] << 8));
        if (do_affine_bg2) {
            aff_line_ref_x[line] = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x28u));
            aff_line_ref_y[line] = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x2Cu));
            aff_pb[line] = (int16_t)virtuappu_mode1_io_read16(0x22u);
            aff_pd[line] = (int16_t)virtuappu_mode1_io_read16(0x26u);
        }
    }
    if (do_affine_bg2 && aff_ref_x != NULL && aff_ref_y != NULL) {
        virtuappu_mode1_affine_precompute(MODE1_GBA_HEIGHT, aff_init_x, aff_init_y, aff_line_ref_x, aff_line_ref_y,
                                          aff_pb, aff_pd, virtuappu_mode1_bg2x_hdma_strobe,
                                          virtuappu_mode1_bg2y_hdma_strobe, aff_ref_x, aff_ref_y);
    }
    return (dispcnt & MODE1_DISP_FORCED_BLANK) != 0u;
}

typedef struct Mode1RenderLinesContext {
    bool affine;
    bool per_line_io;
    uint16_t dispcnt;
    int frame_width;
    const uint16_t* per_line_dispcnt;
    const uint8_t (*io_snapshots)[MODE1_IO_MEM_SIZE];
    const int32_t* aff_ref_x;
    const int32_t* aff_ref_y;
} Mode1RenderLinesContext;

static void mode1_render_lines(const Mode1RenderLinesContext* context, int first_line, int last_line) {
    for (int line = first_line; line < last_line; ++line) {
        uint16_t line_dispcnt = context->affine ? context->dispcnt : context->per_line_dispcnt[line];
        uint32_t bg_layers[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH];
        uint32_t obj_layer[MODE1_GBA_WIDTH];
        uint8_t obj_priority[MODE1_GBA_WIDTH];
        bool obj_1d = (line_dispcnt & MODE1_DISP_OBJ_1D) != 0u;
        const uint8_t* prev_override = virtuappu_mode1_io_thread_override;

        virtuappu_mode1_io_thread_override =
            context->per_line_io ? context->io_snapshots[line] : mode1_memory.io_mem;

        memset(obj_layer, 0, (size_t)context->frame_width * sizeof(uint32_t));
        memset(obj_priority, 0xFF, (size_t)context->frame_width);

        if ((line_dispcnt & MODE1_DISP_BG0_ON) != 0u) {
            memset(bg_layers[0], 0, (size_t)context->frame_width * sizeof(uint32_t));
            virtuappu_mode1_render_text_bg_line(0, line, bg_layers[0], NULL);
        }
        if ((line_dispcnt & MODE1_DISP_BG1_ON) != 0u) {
            memset(bg_layers[1], 0, (size_t)context->frame_width * sizeof(uint32_t));
            virtuappu_mode1_render_text_bg_line(1, line, bg_layers[1], NULL);
        }
        if ((line_dispcnt & MODE1_DISP_BG2_ON) != 0u) {
            memset(bg_layers[2], 0, (size_t)context->frame_width * sizeof(uint32_t));
            if (context->affine) {
                mode1_render_affine_bg2_line(context->frame_width, context->aff_ref_x[line],
                                             context->aff_ref_y[line], bg_layers[2], NULL);
            } else {
                virtuappu_mode1_render_text_bg_line(2, line, bg_layers[2], NULL);
            }
        }
        if ((line_dispcnt & MODE1_DISP_BG3_ON) != 0u) {
            memset(bg_layers[3], 0, (size_t)context->frame_width * sizeof(uint32_t));
            if (!context->affine) {
                virtuappu_mode1_render_text_bg_line(3, line, bg_layers[3], NULL);
            }
        }
        if ((line_dispcnt & MODE1_DISP_OBJ_ON) != 0u) {
            virtuappu_mode1_render_obj_line(line, obj_1d, obj_layer, obj_priority);
        } else {
            memset(virtuappu_mode1_obj_window, 0, (size_t)context->frame_width);
            memset(virtuappu_mode1_obj_semitrans, 0, (size_t)context->frame_width);
        }

        virtuappu_mode1_composite_line(line, bg_layers, NULL, obj_layer, obj_priority, line_dispcnt);
        virtuappu_mode1_io_thread_override = prev_override;
    }
}

#ifdef TMC_3DS
typedef struct Mode1Worker {
    LightEvent start;
    LightEvent done;
    Thread thread;
    const Mode1RenderLinesContext* context;
    volatile bool running;
    uint64_t last_ticks;
    uint64_t max_ticks;
    uint32_t last_lines;
} Mode1Worker;

static Mode1Worker sMode1Workers[2];
static bool sMode1WorkersInitialized;
static volatile int sMode1NextLine;
static uint64_t sMode1StatsFrames;
static uint64_t sMode1MainLastTicks;
static uint64_t sMode1MainMaxTicks;
static uint32_t sMode1MainLastLines;

static uint32_t mode1_render_dynamic(const Mode1RenderLinesContext* context) {
    enum { MODE1_3DS_LINE_CHUNK = 8 };
    uint32_t renderedLines = 0;
    for (;;) {
        const int first = __atomic_fetch_add(&sMode1NextLine, MODE1_3DS_LINE_CHUNK, __ATOMIC_RELAXED);
        if (first >= MODE1_GBA_HEIGHT) return renderedLines;
        const int last = first + MODE1_3DS_LINE_CHUNK < MODE1_GBA_HEIGHT
                             ? first + MODE1_3DS_LINE_CHUNK
                             : MODE1_GBA_HEIGHT;
        mode1_render_lines(context, first, last);
        renderedLines += (uint32_t)(last - first);
    }
}

static void mode1_worker_main(void* argument) {
    Mode1Worker* worker = (Mode1Worker*)argument;
    for (;;) {
        LightEvent_Wait(&worker->start);
        if (!__atomic_load_n(&worker->running, __ATOMIC_ACQUIRE)) break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint64_t startTick = svcGetSystemTick();
        worker->last_lines = mode1_render_dynamic(worker->context);
        worker->last_ticks = svcGetSystemTick() - startTick;
        if (worker->last_ticks > worker->max_ticks) worker->max_ticks = worker->last_ticks;
        LightEvent_Signal(&worker->done);
    }
}

static int mode1_ensure_workers(void) {
    if (sMode1WorkersInitialized) {
        return (sMode1Workers[0].thread != NULL) + (sMode1Workers[1].thread != NULL);
    }
    sMode1WorkersInitialized = true;

    extern bool Platform3DS_IsNew3DS(void);
    extern bool Platform3DS_CanUseCore1(void);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);

    if (Platform3DS_CanUseCore1()) {
        LightEvent_Init(&sMode1Workers[0].start, RESET_ONESHOT);
        LightEvent_Init(&sMode1Workers[0].done, RESET_ONESHOT);
        sMode1Workers[0].running = true;
        sMode1Workers[0].thread = threadCreate(mode1_worker_main, &sMode1Workers[0], 24 * 1024, priority, 1, false);
        if (!sMode1Workers[0].thread) sMode1Workers[0].running = false;
    }

    if (Platform3DS_IsNew3DS()) {
        LightEvent_Init(&sMode1Workers[1].start, RESET_ONESHOT);
        LightEvent_Init(&sMode1Workers[1].done, RESET_ONESHOT);
        sMode1Workers[1].running = true;
        sMode1Workers[1].thread = threadCreate(mode1_worker_main, &sMode1Workers[1], 24 * 1024, priority, 2, false);
        if (!sMode1Workers[1].thread) sMode1Workers[1].running = false;
    }
    return (sMode1Workers[0].thread != NULL) + (sMode1Workers[1].thread != NULL);
}

static void mode1_render_lines_3ds(const Mode1RenderLinesContext* context) {
    mode1_ensure_workers();
    __atomic_store_n(&sMode1NextLine, 0, __ATOMIC_RELAXED);

    for (int i = 0; i < 2; ++i) {
        Mode1Worker* worker = &sMode1Workers[i];
        if (!worker->thread) continue;
        worker->context = context;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        LightEvent_Signal(&worker->start);
    }
    const uint64_t mainStartTick = svcGetSystemTick();
    sMode1MainLastLines = mode1_render_dynamic(context);
    sMode1MainLastTicks = svcGetSystemTick() - mainStartTick;
    if (sMode1MainLastTicks > sMode1MainMaxTicks) sMode1MainMaxTicks = sMode1MainLastTicks;
    for (int i = 0; i < 2; ++i) {
        if (sMode1Workers[i].thread) LightEvent_Wait(&sMode1Workers[i].done);
    }
    ++sMode1StatsFrames;
}

void virtuappu_mode1_get_3ds_stats(VirtuaPPUMode13DSStats* stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->frames = sMode1StatsFrames;
    stats->mainLastTicks = sMode1MainLastTicks;
    stats->mainMaxTicks = sMode1MainMaxTicks;
    stats->mainLastLines = sMode1MainLastLines;
    for (int i = 0; i < 2; ++i) {
        stats->workerLastTicks[i] = sMode1Workers[i].last_ticks;
        stats->workerMaxTicks[i] = sMode1Workers[i].max_ticks;
        stats->workerLastLines[i] = sMode1Workers[i].last_lines;
        if (sMode1Workers[i].thread) ++stats->workerCount;
    }
}

void virtuappu_mode1_shutdown_workers(void) {
    for (int i = 0; i < 2; ++i) {
        Mode1Worker* worker = &sMode1Workers[i];
        if (!worker->thread) continue;
        __atomic_store_n(&worker->running, false, __ATOMIC_RELEASE);
        LightEvent_Signal(&worker->start);
        threadJoin(worker->thread, 2000000000ULL);
        threadFree(worker->thread);
        worker->thread = NULL;
    }
    memset(sMode1Workers, 0, sizeof(sMode1Workers));
    sMode1WorkersInitialized = false;
    sMode1StatsFrames = 0;
    sMode1MainLastTicks = 0;
    sMode1MainMaxTicks = 0;
    sMode1MainLastLines = 0;
}
#else
void virtuappu_mode1_get_3ds_stats(VirtuaPPUMode13DSStats* stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}
void virtuappu_mode1_shutdown_workers(void) {}
#endif

void virtuappu_mode1_render_frame(const PPUMemory* ppu) {
    uint16_t dispcnt;
    int line;
    /* VPPU mode 2 = GBA affine (BG2 is rotation/scaling); mode 1 = all-tiled.
     * The two formerly lived in separate render_frame functions (mode1.c vs
     * mode2.c); they now share this one loop, differing only in how BG2 is
     * drawn and that the affine path latches DISPCNT once per frame. */
    const bool affine = (ppu->mode == 2);

    virtuappu_mode1_set_frame_geometry(ppu);
    const int frame_width = mode1_frame_width;

    dispcnt = virtuappu_mode1_io_read16(MODE1_IO_DISPCNT);
    if ((dispcnt & MODE1_DISP_FORCED_BLANK) != 0u) {
        for (line = 0; line < MODE1_GBA_HEIGHT; ++line) {
            memset(mode1_output_row(line), 0xFF, (size_t)mode1_frame_width * sizeof(uint32_t));
        }
        return;
    }

    /* PORT_PARALLEL_RENDER: scanlines are parallel-renderable but the HDMA
     * pre-line callback (water-FX BG_VOFS scrolls, BLDY fades, etc.) must
     * run sequentially because it mutates the live IO register file. So:
     *   1. Sequential pass — call the callback once per line and snapshot
     *      the (post-callback) IO state for that line.
     *   2. Parallel pass — each thread points its `io_thread_override` at
     *      its line's snapshot, then renders BG / OBJ / composite normally
     *      via the existing single-line functions, which now read IO regs
     *      through the override. */
    static uint8_t io_snapshots[MODE1_GBA_HEIGHT][MODE1_IO_MEM_SIZE];
    uint16_t per_line_dispcnt[MODE1_GBA_HEIGHT];

    /* Affine BG2 carries an internal reference point across scanlines (#132).
     * Gather per-line, post-callback inputs during the sequential pass and
     * precompute the per-line reference so the parallel pass stays independent. */
    const bool do_affine_bg2 = affine && ((dispcnt & MODE1_DISP_BG2_ON) != 0u);
    int32_t aff_ref_x[MODE1_GBA_HEIGHT];
    int32_t aff_ref_y[MODE1_GBA_HEIGHT];
    int32_t aff_line_ref_x[MODE1_GBA_HEIGHT];
    int32_t aff_line_ref_y[MODE1_GBA_HEIGHT];
    int16_t aff_pb[MODE1_GBA_HEIGHT];
    int16_t aff_pd[MODE1_GBA_HEIGHT];
    int32_t aff_init_x = 0;
    int32_t aff_init_y = 0;
    if (do_affine_bg2) {
        aff_init_x = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x28u));
        aff_init_y = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x2Cu));
    }

    /* No HDMA pre-line callback -> IO is identical on every scanline, so the
     * per-line snapshot is redundant: skip 160x1KB of memcpy on the sequential
     * critical path and point every thread's override straight at io_mem in the
     * parallel loop below. Byte-exact: each snapshot equalled io_mem anyway. */
    const bool per_line_io = (virtuappu_mode1_pre_line_callback != NULL);
    for (line = 0; line < MODE1_GBA_HEIGHT; ++line) {
        if (per_line_io) {
            virtuappu_mode1_pre_line_callback(line);
            /* Display rendering only reads registers through BLDY (0x54). */
            memcpy(io_snapshots[line], mode1_memory.io_mem, MODE1_IO_BLDY + 2u);
            per_line_dispcnt[line] =
#ifdef TMC_N64
                *(const uint16_t*)&io_snapshots[line][MODE1_IO_DISPCNT];
#else
                (uint16_t)io_snapshots[line][MODE1_IO_DISPCNT] |
                ((uint16_t)io_snapshots[line][MODE1_IO_DISPCNT + 1] << 8);
#endif
        } else {
            per_line_dispcnt[line] = dispcnt;
        }
        if (do_affine_bg2) {
            /* Live IO is this line's post-callback state here (== the snapshot),
             * matching what the old mode2.c read inline. */
            aff_line_ref_x[line] = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x28u));
            aff_line_ref_y[line] = mode1_sign_extend_28(virtuappu_mode1_io_read32(0x2Cu));
            aff_pb[line] = (int16_t)virtuappu_mode1_io_read16(0x22u);
            aff_pd[line] = (int16_t)virtuappu_mode1_io_read16(0x26u);
        }
    }
    if (do_affine_bg2) {
        virtuappu_mode1_affine_precompute(MODE1_GBA_HEIGHT, aff_init_x, aff_init_y, aff_line_ref_x, aff_line_ref_y,
                                          aff_pb, aff_pd, virtuappu_mode1_bg2x_hdma_strobe,
                                          virtuappu_mode1_bg2y_hdma_strobe, aff_ref_x, aff_ref_y);
    }

    /* Palette RAM is final for the frame here (fade engine wrote it before this
     * render; the pre-line callback above only touches IO regs). Convert it to
     * the ABGR LUTs once so the parallel per-pixel loops below do a single table
     * load instead of the rgb555->ABGR math. */
    virtuappu_mode1_publish_palette_luts();
#ifdef TMC_3DS
    virtuappu_mode1_publish_obj_line_lists();
#endif

    const Mode1RenderLinesContext render_context = {
        affine, per_line_io, dispcnt, frame_width, per_line_dispcnt, io_snapshots, aff_ref_x, aff_ref_y,
    };
#ifdef TMC_3DS
    mode1_render_lines_3ds(&render_context);
#else
#pragma omp parallel for schedule(static)
    for (line = 0; line < MODE1_GBA_HEIGHT; ++line) {
        mode1_render_lines(&render_context, line, line + 1);
    }
#endif
}
