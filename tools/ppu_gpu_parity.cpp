/*
 * tools/ppu_gpu_parity.cpp — CPU-vs-GPU PPU parity harness.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Renders identical GBA memory states through BOTH the CPU software
 * rasterizer (port/ppu/src/mode1.c via virtuappu_render_frame) and the GPU
 * rasterizer (port/port_gpu_raster.cpp), then diffs the two framebuffers
 * pixel-for-pixel. Target is bit-exact; see docs/gpu-rasterizer-design.md.
 *
 * Headless: SDL_GPU on the `offscreen` video driver (Vulkan/Metal/D3D12).
 * Each implementation phase adds scenes here to guard its feature.
 *
 * Usage:
 *   ppu_gpu_parity [vert.spv] [frag.spv]
 * Defaults to port/shaders/build/ppu_raster.{vert,frag}.spv relative to CWD.
 * Exit 0 = all scenes bit-exact (or within documented tolerance), nonzero on
 * mismatch / setup failure.
 */

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "cpu/mode1.h"
#include "virtuappu.h"
}
#include "port_gpu_raster.h"
#include "port_gpu_raster_gl.h"
#include "port_gpu_obj_cull.h"

/* ---- synthetic GBA memory for one scene ---- */
struct Scene {
    const char* name;
    uint8_t io[0x400];
    uint8_t vram[0x18000];
    uint16_t bgpal[256];
    uint16_t objpal[256];
    uint16_t oam[512];
};

static void scene_clear(Scene* s, const char* name) {
    std::memset(s, 0, sizeof(*s));
    s->name = name;
    /* Hidden OAM by default (attr0 bit 9 set, non-affine) so stray sprites
     * don't render; matches how the engine parks unused entries. */
    for (int i = 0; i < 128; ++i) {
        s->oam[i * 4 + 0] = 0x0200; /* disable bit */
    }
    /* Reset the CPU widescreen globals so scenes are independent. */
    for (int b = 0; b < MODE1_GBA_BG_COUNT; ++b) {
        virtuappu_mode1_ws_shadow[b] = NULL;
        virtuappu_mode1_ws_shadow_base_tile[b] = 0;
    }
    virtuappu_mode1_ws_hud_right_anchor = 0;
    virtuappu_mode1_ws_msg_shift = 0;
    virtuappu_mode1_ws_msg_x0 = 0;
    virtuappu_mode1_ws_msg_x1 = 0;
    virtuappu_mode1_ws_msg_y0 = 0;
    virtuappu_mode1_ws_msg_y1 = 0;
}

static void set_io16(Scene* s, int off, uint16_t v) {
    s->io[off] = (uint8_t)(v & 0xFF);
    s->io[off + 1] = (uint8_t)(v >> 8);
}

/* rgb555 -> ABGR8888, identical to virtuappu_mode1_rgb555_to_abgr8888. */
static uint32_t rgb555_to_abgr(uint16_t c) {
    uint32_t r = (uint32_t)((c & 0x1F) << 3);
    uint32_t g = (uint32_t)(((c >> 5) & 0x1F) << 3);
    uint32_t b = (uint32_t)(((c >> 10) & 0x1F) << 3);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* Render `s` on the CPU rasterizer into `out` (WxH, tightly packed). */
static void render_cpu(Scene* s, uint32_t* out, int w, int h) {
    VirtuaPPUMode1GbaMemory mem = { s->io, s->vram, s->bgpal, s->objpal, s->oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;
    uint16_t dispcnt = (uint16_t)(s->io[0] | (s->io[1] << 8));
    int gba_mode = dispcnt & 0x7;
    uint8_t vppu_mode = (gba_mode == 1 || gba_mode == 2) ? 2 : 1;
    virtuappu_registers.mode = vppu_mode;
    virtuappu_registers.frame_width = (uint16_t)w;
    virtuappu_registers.frame_pitch = (uint16_t)w;
    virtuappu_render_frame();
    for (int y = 0; y < h; ++y) {
        std::memcpy(&out[(size_t)y * w], &virtuappu_frame_buffer[(size_t)y * w], (size_t)w * sizeof(uint32_t));
    }
}
/* Build the PortGpuRasterFrame for a scene (shared by the Vulkan + GLES paths).
 * Per-line arrays live in caller-owned statics so they outlive the call. */
static void build_frame(Scene* s, int w, int h, PortGpuRasterFrame* fout, std::vector<uint8_t>& io_per_line,
                        std::vector<uint16_t>& dispcnt_per_line, std::vector<int32_t>& aff_x,
                        std::vector<int32_t>& aff_y, std::vector<uint16_t>& ws_concat) {
    PortGpuRasterFrame& f = *fout;
    io_per_line.assign((size_t)h * 0x400, 0);
    dispcnt_per_line.assign((size_t)h, 0);
    aff_x.assign(h, 0);
    aff_y.assign(h, 0);

    /* Drive the real prepare pass the game uses: bind memory, set the mode, run
     * the sequential IO snapshot + affine precompute. No HDMA callback in these
     * static scenes (matches render_cpu). */
    VirtuaPPUMode1GbaMemory mem = { s->io, s->vram, s->bgpal, s->objpal, s->oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;
    uint16_t dispcnt = (uint16_t)(s->io[0] | (s->io[1] << 8));
    int gba_mode = dispcnt & 0x7;
    PPUMemory ppu;
    std::memset(&ppu, 0, sizeof(ppu));
    ppu.mode = (gba_mode == 1 || gba_mode == 2) ? 2 : 1;
    ppu.frame_width = (uint16_t)w;
    ppu.frame_pitch = (uint16_t)w;
    uint16_t frame_dispcnt = 0;
    virtuappu_mode1_prepare_frame(&ppu, io_per_line.data(), dispcnt_per_line.data(), aff_x.data(), aff_y.data(),
                                  &frame_dispcnt);

    std::memset(&f, 0, sizeof(f));
    f.frame_width = w;
    f.frame_height = h;
    f.frame_pitch = w;
    f.mode = ppu.mode;
    f.affine = (ppu.mode == 2);
    f.frame_dispcnt = frame_dispcnt;
    f.vram = s->vram;
    f.bg_palette = s->bgpal;
    f.obj_palette = s->objpal;
    f.oam = s->oam;
    f.io_per_line = io_per_line.data();
    f.io_uniform = true; /* static scenes: no HDMA callback, IO constant per line */
    f.dispcnt_per_line = dispcnt_per_line.data();
    f.affine_ref_x = f.affine ? aff_x.data() : NULL;
    f.affine_ref_y = f.affine ? aff_y.data() : NULL;

    /* Mirror the CPU's widescreen globals into the GPU frame so both paths see
     * identical reveal state. All NULL/0/-1 at native 240 (branches inert). */
    f.ws_bg_clip_x = MODE1_GBA_BG_CLIP_X;
    f.ws_cols = MODE1_WS_SHADOW_COLS;
    f.ws_hud_right_anchor = virtuappu_mode1_ws_hud_right_anchor;
    f.ws_hud_right_native_x = MODE1_WS_HUD_RIGHT_NATIVE_X;
    f.ws_msg_shift = virtuappu_mode1_ws_msg_shift;
    f.ws_msg_x0 = virtuappu_mode1_ws_msg_x0;
    f.ws_msg_x1 = virtuappu_mode1_ws_msg_x1;
    f.ws_msg_y0 = virtuappu_mode1_ws_msg_y0;
    f.ws_msg_y1 = virtuappu_mode1_ws_msg_y1;
    bool any_shadow = false;
    for (int b = 0; b < 4; ++b) {
        if (virtuappu_mode1_ws_shadow[b] != NULL) {
            any_shadow = true;
        }
    }
    if (any_shadow) {
        int cols = MODE1_WS_SHADOW_COLS;
        ws_concat.assign((size_t)4 * 32 * cols, 0);
        for (int b = 0; b < 4; ++b) {
            f.ws_shadow_base_tile[b] =
                (virtuappu_mode1_ws_shadow[b] != NULL) ? virtuappu_mode1_ws_shadow_base_tile[b] : -1;
            if (virtuappu_mode1_ws_shadow[b] != NULL) {
                std::memcpy(&ws_concat[(size_t)b * 32 * cols], virtuappu_mode1_ws_shadow[b],
                            (size_t)32 * cols * sizeof(uint16_t));
            }
        }
        f.ws_shadow = ws_concat.data();
        f.ws_shadow_halfwords = 4 * 32 * cols;
    } else {
        for (int b = 0; b < 4; ++b)
            f.ws_shadow_base_tile[b] = -1;
        f.ws_shadow = NULL;
        f.ws_shadow_halfwords = 0;
    }
}

/* Scratch buffers reused across scenes for both backends. */
static std::vector<uint8_t> g_io;
static std::vector<uint16_t> g_dispcnt;
static std::vector<int32_t> g_affx, g_affy;
static std::vector<uint16_t> g_wsconcat;

static void render_vk(PortGpuRaster* r, Scene* s, uint32_t* out, int w, int h) {
    PortGpuRasterFrame f;
    build_frame(s, w, h, &f, g_io, g_dispcnt, g_affx, g_affy, g_wsconcat);
    if (!Port_GpuRaster_RenderReadback(r, &f, out, w)) {
        std::fprintf(stderr, "  VK render/readback failed\n");
        std::memset(out, 0xCD, (size_t)w * h * sizeof(uint32_t));
    }
}

static void render_gles(PortGpuRasterGl* r, Scene* s, uint32_t* out, int w, int h) {
    PortGpuRasterFrame f;
    build_frame(s, w, h, &f, g_io, g_dispcnt, g_affx, g_affy, g_wsconcat);
    if (!Port_GpuRasterGl_RenderReadback(r, &f, out, w)) {
        std::fprintf(stderr, "  GLES render/readback failed\n");
        std::memset(out, 0xEF, (size_t)w * h * sizeof(uint32_t));
    }
}

/* Compare; return diff count, log first diff. */
static int diff(const uint32_t* cpu, const uint32_t* gpu, int w, int h, const char* name) {
    int diffs = 0;
    int fx = -1, fy = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t a = cpu[(size_t)y * w + x];
            uint32_t b = gpu[(size_t)y * w + x];
            if (a != b) {
                if (diffs == 0) {
                    fx = x;
                    fy = y;
                }
                ++diffs;
            }
        }
    }
    if (diffs == 0) {
        std::printf("  [PASS] %-24s bit-exact (%dx%d)\n", name, w, h);
    } else {
        std::printf("  [FAIL] %-24s %d/%d px differ; first (%d,%d) cpu=0x%08x gpu=0x%08x\n", name, diffs, w * h, fx, fy,
                    cpu[(size_t)fy * w + fx], gpu[(size_t)fy * w + fx]);
    }
    return diffs;
}

static void* load_file(const char* path, size_t* len) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    void* b = std::malloc((size_t)n);
    if (std::fread(b, 1, (size_t)n, f) != (size_t)n) {
        std::free(b);
        std::fclose(f);
        return NULL;
    }
    std::fclose(f);
    *len = (size_t)n;
    return b;
}

/* ---- scenes ---- */
typedef void (*SceneFn)(Scene*);

static void scene_forced_blank(Scene* s) {
    scene_clear(s, "forced_blank");
    set_io16(s, 0x00, 0x0080); /* DISPCNT: forced blank */
    s->bgpal[0] = 0x1234;      /* ignored under forced blank */
}
static void scene_backdrop_blue(Scene* s) {
    scene_clear(s, "backdrop_blue");
    set_io16(s, 0x00, 0x0000); /* mode 0, all layers off */
    s->bgpal[0] = 0x7C00;      /* pure blue in rgb555 (bit 10-14) */
}
static void scene_backdrop_black(Scene* s) {
    scene_clear(s, "backdrop_black");
    set_io16(s, 0x00, 0x0000);
    s->bgpal[0] = 0x0000;
}
static void scene_backdrop_mixed(Scene* s) {
    scene_clear(s, "backdrop_mixed");
    set_io16(s, 0x00, 0x0000);
    s->bgpal[0] = 0x3DEF; /* arbitrary */
}

/* ---- BG helpers (build tiles/tilemaps in VRAM) ---- */

/* Fill a 16-colour palette bank (16 entries) with distinguishable colours. */
static void fill_palette_bank(uint16_t* pal, int bank, uint16_t backdrop) {
    if (bank == 0) {
        pal[0] = backdrop;
    }
    for (int i = (bank == 0 ? 1 : 0); i < 16; ++i) {
        /* spread across rgb555 so index changes are visible */
        int idx = bank * 16 + i;
        pal[idx] = (uint16_t)(((idx * 3) & 0x1F) | (((idx * 5) & 0x1F) << 5) | (((idx * 7) & 0x1F) << 10));
    }
}

/* Write a 4bpp 8x8 tile at char_base+tile*32. Pixel (px,py) colour index =
 * pattern(px,py) so hflip/vflip are observable. */
static void write_tile_4bpp(uint8_t* vram, uint32_t char_base, int tile) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = (uint8_t)(((px + py * 2) % 15) + 1); /* 1..15, never 0 */
            uint32_t addr = char_base + (uint32_t)tile * 32u + (uint32_t)py * 4u + (uint32_t)(px / 2);
            if (px & 1) {
                vram[addr] = (uint8_t)((vram[addr] & 0x0F) | (ci << 4));
            } else {
                vram[addr] = (uint8_t)((vram[addr] & 0xF0) | ci);
            }
        }
    }
}

/* Write an 8bpp 8x8 tile at char_base+tile*64. */
static void write_tile_8bpp(uint8_t* vram, uint32_t char_base, int tile) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = (uint8_t)(((px * 7 + py * 13) % 200) + 1); /* 1..200 */
            uint32_t addr = char_base + (uint32_t)tile * 64u + (uint32_t)py * 8u + (uint32_t)px;
            vram[addr] = ci;
        }
    }
}

/* Fill a 32x32 screenblock at screen_base with `entry`. */
static void fill_screenblock(uint8_t* vram, uint32_t screen_base, uint16_t entry) {
    for (int i = 0; i < 32 * 32; ++i) {
        uint32_t a = screen_base + (uint32_t)i * 2u;
        vram[a] = (uint8_t)(entry & 0xFF);
        vram[a + 1] = (uint8_t)(entry >> 8);
    }
}

/* Basic single-BG0 4bpp tiled scene: char base 0, screen base 0x4000
 * (block 8), tile 1, palette bank 2, backdrop dark grey. */
static void scene_bg0_tiled(Scene* s) {
    scene_clear(s, "bg0_tiled_4bpp");
    set_io16(s, 0x00, 0x0100);          /* DISPCNT: mode 0, BG0 on */
    set_io16(s, 0x08, 0x0800 | 0x0080); /* BG0CNT: screen_base block 16? see below */
    /* BG0CNT: char_base=0 (bits2-3=0), screen_base block = 8 (bits8-12=8 -> *0x800=0x4000),
     * 4bpp (bit7=0), priority 0. */
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* tile 1, palbank 2 */
}

/* BG0 with scroll (sub-tile + past-tile). */
static void scene_bg0_scroll(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_scroll";
    set_io16(s, 0x10, 13);  /* BG0HOFS = 13 */
    set_io16(s, 0x12, 100); /* BG0VOFS = 100 */
}

/* BG0 with per-tile hflip+vflip set in the tilemap entry. */
static void scene_bg0_flip(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_flip";
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (1u << 10) | (1u << 11) | (2u << 12)));
}

/* BG0 8bpp. */
static void scene_bg0_8bpp(Scene* s) {
    scene_clear(s, "bg0_8bpp");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 0x0080)); /* screen block 8, 8bpp */
    s->bgpal[0] = 0x0421;
    for (int i = 1; i < 256; ++i) {
        s->bgpal[i] = (uint16_t)(((i * 3) & 0x1F) | (((i * 5) & 0x1F) << 5) | (((i * 7) & 0x1F) << 10));
    }
    write_tile_8bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, 1);
}

/* BG0 mosaic (h=4, v=3). */
static void scene_bg0_mosaic(Scene* s) {
    scene_bg0_tiled(s);
    s->name = "bg0_mosaic";
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 0x0040)); /* BG0CNT + mosaic bit 6 */
    set_io16(s, 0x4C, (uint16_t)((3u) | (2u << 4)));   /* MOSAIC: h-1=3, v-1=2 */
}

/* 512x256 BG0 (size flag 1): fill all 2 horizontal blocks + scroll across. */
static void scene_bg0_512(Scene* s) {
    scene_clear(s, "bg0_512wide");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)((8u << 8) | (1u << 14))); /* screen block 8, size 1 (512x256) */
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    write_tile_4bpp(s->vram, 0, 2);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* block 8 */
    fill_screenblock(s->vram, 0x4800, (uint16_t)(2u | (2u << 12))); /* block 9 */
    set_io16(s, 0x10, 260);                                         /* scroll into 2nd block */
}

/* Two BGs with different priorities: BG1 (priority 0) over BG0 (priority 1). */
static void scene_bg_priority(Scene* s) {
    scene_clear(s, "bg_priority");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200)); /* BG0+BG1 on */
    /* BG0: screen block 8, priority 1 */
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 1u));
    /* BG1: screen block 10, priority 0, char base 1 (0x4000) */
    set_io16(s, 0x0A, (uint16_t)((10u << 8) | (1u << 2) | 0u));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    fill_palette_bank(s->bgpal, 3, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);                                 /* BG0 tile, char base 0 */
    write_tile_4bpp(s->vram, 0x4000, 1);                            /* BG1 tile, char base 0x4000 */
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12))); /* BG0 map block 8 */
    fill_screenblock(s->vram, 0x5000, (uint16_t)(1u | (3u << 12))); /* BG1 map block 10 */
}

/* BG0 with a partly-transparent tile (color index 0 shows backdrop). */
static void scene_bg0_transparent(Scene* s) {
    scene_clear(s, "bg0_transparent");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x7FFF; /* white backdrop */
    fill_palette_bank(s->bgpal, 2, 0x7FFF);
    /* tile 1: checkerboard of index 0 (transparent) and index 5 */
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            uint8_t ci = ((px + py) & 1) ? 5 : 0;
            uint32_t addr = 0u + 1u * 32u + (uint32_t)py * 4u + (uint32_t)(px / 2);
            if (px & 1) {
                s->vram[addr] = (uint8_t)((s->vram[addr] & 0x0F) | (ci << 4));
            } else {
                s->vram[addr] = (uint8_t)((s->vram[addr] & 0xF0) | ci);
            }
        }
    }
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
}

/* ---- OBJ helpers ---- */
static void set_oam(Scene* s, int i, uint16_t a0, uint16_t a1, uint16_t a2) {
    s->oam[i * 4 + 0] = a0;
    s->oam[i * 4 + 1] = a1;
    s->oam[i * 4 + 2] = a2;
}
static void set_affine(Scene* s, int g, int16_t pa, int16_t pb, int16_t pc, int16_t pd) {
    s->oam[g * 16 + 3] = (uint16_t)pa;
    s->oam[g * 16 + 7] = (uint16_t)pb;
    s->oam[g * 16 + 11] = (uint16_t)pc;
    s->oam[g * 16 + 15] = (uint16_t)pd;
}
/* Fill OBJ tiles 0..63 at obj base (0x10000) with the 4bpp/8bpp patterns. */
static void fill_obj_tiles(Scene* s) {
    for (int t = 0; t < 64; ++t) {
        write_tile_4bpp(s->vram, 0x10000, t);
    }
    /* OBJ palette: bank 0 for 8bpp direct, plus banks for 4bpp. */
    for (int i = 1; i < 256; ++i) {
        s->objpal[i] = (uint16_t)(((i * 7) & 0x1F) | (((i * 3) & 0x1F) << 5) | (((i * 11) & 0x1F) << 10));
    }
}
/* attr0 helpers: y | (affine<<8) | (mode<<10) | (mosaic<<12) | (bpp8<<13) | (shape<<14) */
static uint16_t A0(int y, int affine, int mode, int mosaic, int bpp8, int shape) {
    return (uint16_t)((y & 0xFF) | (affine << 8) | (mode << 10) | (mosaic << 12) | (bpp8 << 13) | (shape << 14));
}
static uint16_t A0_hidden(int y, int shape) {
    return (uint16_t)((y & 0xFF) | (1 << 9) | (shape << 14));
}
/* attr1 (non-affine): x | (hflip<<12) | (vflip<<13) | (size<<14) */
static uint16_t A1(int x, int hflip, int vflip, int size) {
    return (uint16_t)((x & 0x1FF) | (hflip << 12) | (vflip << 13) | (size << 14));
}
/* attr1 (affine): x | (group<<9) | (size<<14) */
static uint16_t A1a(int x, int group, int size) {
    return (uint16_t)((x & 0x1FF) | (group << 9) | (size << 14));
}
/* attr2: tile | (priority<<10) | (palbank<<12) */
static uint16_t A2(int tile, int prio, int palbank) {
    return (uint16_t)((tile & 0x3FF) | (prio << 10) | (palbank << 12));
}

enum { DISP_OBJ_1D_BIT = 0x0040, DISP_OBJ_ENABLE = 0x1000 };
/* base OBJ scene: backdrop + tiles + 1D mapping, one 16x16 sprite. */
static void scene_obj_setup(Scene* s, const char* name) {
    scene_clear(s, name);
    set_io16(s, 0x00, (uint16_t)(DISP_OBJ_ENABLE | DISP_OBJ_1D_BIT)); /* OBJ on, 1D */
    s->bgpal[0] = 0x0421;
    fill_obj_tiles(s);
}

static void scene_obj_basic(Scene* s) {
    scene_obj_setup(s, "obj_basic_16x16");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2)); /* 16x16, palbank 2 */
}
static void scene_obj_hflip(Scene* s) {
    scene_obj_setup(s, "obj_hflip");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 1, 0, 1), A2(0, 0, 2));
}
static void scene_obj_vflip(Scene* s) {
    scene_obj_setup(s, "obj_vflip");
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 1, 1), A2(0, 0, 2));
}
static void scene_obj_8bpp(Scene* s) {
    scene_obj_setup(s, "obj_8bpp");
    for (int t = 0; t < 64; ++t)
        write_tile_8bpp(s->vram, 0x10000, t);
    set_oam(s, 0, A0(30, 0, 0, 0, 1, 0), A1(60, 0, 0, 1), A2(0, 0, 0)); /* 8bpp 16x16 */
}
static void scene_obj_2d(Scene* s) {
    scene_obj_setup(s, "obj_2d_mapping");
    set_io16(s, 0x00, DISP_OBJ_ENABLE);                                 /* 2D mapping (no bit6) */
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 2), A2(0, 0, 2)); /* 32x32 */
}
static void scene_obj_affine(Scene* s) {
    scene_obj_setup(s, "obj_affine_rot");
    /* ~30deg rotation-ish matrix (8.8 fixed): pa=0xE0,pb=-0x80,pc=0x80,pd=0xE0 */
    set_affine(s, 0, 0x00E0, (int16_t)0xFF80, 0x0080, 0x00E0);
    set_oam(s, 0, A0(40, 1, 0, 0, 0, 0), A1a(60, 0, 2), A2(0, 0, 2)); /* affine 32x32, group 0 */
}
static void scene_obj_affine_double(Scene* s) {
    scene_obj_setup(s, "obj_affine_double");
    set_affine(s, 1, 0x0100, 0, 0, 0x0100); /* identity, double-size bounds */
    /* attr0 affine + double-size (bit9) */
    uint16_t a0 = (uint16_t)((40 & 0xFF) | (1 << 8) | (1 << 9));
    set_oam(s, 0, a0, A1a(60, 1, 2), A2(0, 0, 2));
}
static void scene_obj_over_bg(Scene* s) {
    scene_obj_setup(s, "obj_over_bg");
    set_io16(s, 0x00, (uint16_t)(DISP_OBJ_ENABLE | DISP_OBJ_1D_BIT | 0x0100)); /* + BG0 */
    set_io16(s, 0x08, (uint16_t)(8u << 8));                                    /* BG0 screen block 8, prio 0 */
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2)); /* obj prio 0 -> over BG prio 0 */
}
static void scene_obj_two_overlap(Scene* s) {
    scene_obj_setup(s, "obj_two_overlap");
    /* Two overlapping sprites; lower OAM index (0) must win the overlap. */
    set_oam(s, 0, A0(40, 0, 0, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2));
    set_oam(s, 1, A0(44, 0, 0, 0, 0, 0), A1(54, 0, 0, 1), A2(4, 0, 3));
}
static void scene_obj_wrap(Scene* s) {
    scene_obj_setup(s, "obj_xy_wrap");
    /* x near right edge wraps via -512; y>=160 wraps via -256. */
    set_oam(s, 0, A0(250, 0, 0, 0, 0, 1), A1(500, 0, 0, 1), A2(0, 0, 2)); /* y=250 -> -6 */
}
static void scene_obj_mosaic(Scene* s) {
    scene_obj_setup(s, "obj_mosaic");
    set_io16(s, 0x4C, (uint16_t)((3u << 8) | (2u << 12)));              /* OBJ mosaic h-1=3, v-1=2 */
    set_oam(s, 0, A0(40, 0, 0, 1, 0, 0), A1(50, 0, 0, 2), A2(0, 0, 2)); /* mosaic bit set, 32x32 */
}

/* ---- composite: window + blend scenes ---- */
/* Two BGs so blends/windows have both a first and second target. BG0 (prio 1,
 * palbank 2) and BG1 (prio 2, palbank 3), both opaque. */
static void two_bg_setup(Scene* s, const char* name) {
    scene_clear(s, name);
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200));             /* BG0+BG1 on */
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 1u));              /* BG0 block 8, prio 1 */
    set_io16(s, 0x0A, (uint16_t)((10u << 8) | (1u << 2) | 2u)); /* BG1 block 10, char 0x4000, prio 2 */
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    fill_palette_bank(s->bgpal, 3, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    write_tile_4bpp(s->vram, 0x4000, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    fill_screenblock(s->vram, 0x5000, (uint16_t)(1u | (3u << 12)));
}
static void scene_win0_inside(Scene* s) {
    two_bg_setup(s, "win0_inside");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200 | 0x2000)); /* +WIN0 */
    set_io16(s, 0x40, (uint16_t)((40u << 8) | 200u));        /* WIN0H: left 40, right 200 */
    set_io16(s, 0x44, (uint16_t)((30u << 8) | 130u));        /* WIN0V: top 30, bottom 130 */
    set_io16(s, 0x48, (uint16_t)(0x01));                     /* WININ: inside shows BG0 only */
    set_io16(s, 0x4A, (uint16_t)(0x02));                     /* WINOUT: outside shows BG1 only */
}
static void scene_win0_hwrap(Scene* s) {
    two_bg_setup(s, "win0_hwrap");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200 | 0x2000));
    set_io16(s, 0x40, (uint16_t)((200u << 8) | 40u)); /* left>right: wrap span */
    set_io16(s, 0x44, (uint16_t)((0u << 8) | 160u));
    set_io16(s, 0x48, (uint16_t)(0x01));
    set_io16(s, 0x4A, (uint16_t)(0x0F));
}
static void scene_win0_vwrap(Scene* s) {
    two_bg_setup(s, "win0_vwrap");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200 | 0x2000));
    set_io16(s, 0x40, (uint16_t)((0u << 8) | 240u));
    set_io16(s, 0x44, (uint16_t)((120u << 8) | 40u)); /* top>bottom: wrap span */
    set_io16(s, 0x48, (uint16_t)(0x01));
    set_io16(s, 0x4A, (uint16_t)(0x02));
}
static void scene_win1(Scene* s) {
    two_bg_setup(s, "win1");
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200 | 0x4000)); /* WIN1 */
    set_io16(s, 0x42, (uint16_t)((60u << 8) | 180u));        /* WIN1H */
    set_io16(s, 0x46, (uint16_t)((20u << 8) | 140u));        /* WIN1V */
    set_io16(s, 0x48, (uint16_t)(0x02 << 8));                /* WININ high byte = win1 ctrl */
    set_io16(s, 0x4A, (uint16_t)(0x01));
}
static void scene_objwin(Scene* s) {
    two_bg_setup(s, "objwin");
    fill_obj_tiles(s);
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x0200 | 0x1000 | 0x0040 | 0x8000)); /* OBJ on + OBJWIN */
    /* mode-2 (window) sprite carves the OBJ window. */
    set_oam(s, 0, A0(40, 0, 2, 0, 0, 0), A1(50, 0, 0, 2), A2(0, 0, 2)); /* 16x16 mode2 */
    set_io16(s, 0x48, (uint16_t)(0x01));                                /* WININ (unused here) */
    set_io16(s, 0x4A, (uint16_t)(0x02 | (0x01 << 8)));                  /* WINOUT: outside BG1; objwin inside BG0 */
}
static void scene_blend_alpha(Scene* s) {
    two_bg_setup(s, "blend_alpha");
    /* alpha effect (1), BG0 1st target, BG1 2nd target, eva=8 evb=8. */
    set_io16(s, 0x50, (uint16_t)((1u << 6) | 0x01 | (0x02 << 8))); /* BLDCNT: effect1, tgt1=BG0, tgt2=BG1 */
    set_io16(s, 0x52, (uint16_t)(8u | (8u << 8)));                 /* BLDALPHA eva=8 evb=8 */
}
static void scene_blend_brighten(Scene* s) {
    two_bg_setup(s, "blend_brighten");
    set_io16(s, 0x50, (uint16_t)((2u << 6) | 0x01)); /* effect2 brighten, tgt1=BG0 */
    set_io16(s, 0x54, (uint16_t)10u);                /* BLDY evy=10 */
}
static void scene_blend_darken(Scene* s) {
    two_bg_setup(s, "blend_darken");
    set_io16(s, 0x50, (uint16_t)((3u << 6) | 0x01)); /* effect3 darken, tgt1=BG0 */
    set_io16(s, 0x54, (uint16_t)20u);                /* BLDY evy=20 (clamps to 16) */
}
static void scene_blend_alpha_backdrop(Scene* s) {
    /* alpha with BG over backdrop (2nd-target BD bit). */
    scene_clear(s, "blend_alpha_backdrop");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 0u));
    s->bgpal[0] = 0x3DEF;
    fill_palette_bank(s->bgpal, 2, 0x3DEF);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    set_io16(s, 0x50, (uint16_t)((1u << 6) | 0x01 | (0x20 << 8))); /* effect1, tgt1=BG0, tgt2=BD */
    set_io16(s, 0x52, (uint16_t)(10u | (6u << 8)));
}
static void scene_obj_semitrans(Scene* s) {
    /* Semi-transparent OBJ (mode 1) over an opaque BG that is a 2nd target. */
    scene_clear(s, "obj_semitrans");
    fill_obj_tiles(s);
    set_io16(s, 0x00, (uint16_t)(0x0100 | 0x1000 | 0x0040)); /* BG0 + OBJ 1D */
    set_io16(s, 0x08, (uint16_t)((8u << 8) | 1u));           /* BG0 prio 1 */
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    set_io16(s, 0x50, (uint16_t)(0x20 << 8));                           /* BLDCNT: tgt2 = BG0? no; set BG0 2nd target */
    set_io16(s, 0x50, (uint16_t)((0x01 << 8)));                         /* 2nd target = BG0 (bit 8) */
    set_io16(s, 0x52, (uint16_t)(9u | (7u << 8)));                      /* eva/evb for the forced obj-alpha */
    set_oam(s, 0, A0(40, 0, 1, 0, 0, 0), A1(50, 0, 0, 1), A2(0, 0, 2)); /* mode1 semi-trans 16x16 */
}

/* ---- affine BG2 (GBA mode 1/2) scenes ---- */
/* Affine BG uses 8bpp 64-byte tiles and 1-byte tilemap entries. */
static void affine_bg2_setup(Scene* s, const char* name, int size_flag, int wrap) {
    scene_clear(s, name);
    /* DISPCNT mode 1 (affine), BG2 on. */
    set_io16(s, 0x00, (uint16_t)(1u | 0x0400));
    /* BG2CNT: char base 0, screen base block 8 (0x4000), size, wrap, prio 0. */
    set_io16(s, 0x0C, (uint16_t)((8u << 8) | (uint16_t)(size_flag << 14) | (uint16_t)(wrap << 13)));
    /* 8bpp palette. */
    s->bgpal[0] = 0x0421;
    for (int i = 1; i < 256; ++i) {
        s->bgpal[i] = (uint16_t)(((i * 3) & 0x1F) | (((i * 5) & 0x1F) << 5) | (((i * 7) & 0x1F) << 10));
    }
    /* 8bpp tiles 0..63 at char base 0. */
    for (int t = 0; t < 64; ++t)
        write_tile_8bpp(s->vram, 0, t);
    /* Affine tilemap at 0x4000: 1 byte/entry. Fill map_tiles*map_tiles. */
    int map_size = 128 << size_flag;
    int map_tiles = map_size / 8;
    for (int i = 0; i < map_tiles * map_tiles; ++i) {
        s->vram[0x4000 + i] = (uint8_t)((i % 63) + 1);
    }
}
static void set_io32(Scene* s, int off, uint32_t v) {
    set_io16(s, off, (uint16_t)(v & 0xFFFF));
    set_io16(s, off + 2, (uint16_t)(v >> 16));
}
static void scene_affine_identity(Scene* s) {
    affine_bg2_setup(s, "affine_identity", 1, 0); /* 256x256, no wrap */
    set_io16(s, 0x20, 0x0100);                    /* PA = 1.0 */
    set_io16(s, 0x22, 0x0000);                    /* PB = 0 */
    set_io16(s, 0x24, 0x0000);                    /* PC = 0 */
    set_io16(s, 0x26, 0x0100);                    /* PD = 1.0 */
    set_io32(s, 0x28, 0);                         /* BG2X = 0 */
    set_io32(s, 0x2C, 0);                         /* BG2Y = 0 */
}
static void scene_affine_scaled(Scene* s) {
    affine_bg2_setup(s, "affine_scaled", 1, 0);
    set_io16(s, 0x20, 0x0080); /* PA = 0.5 -> 2x zoom */
    set_io16(s, 0x22, 0x0000);
    set_io16(s, 0x24, 0x0000);
    set_io16(s, 0x26, 0x0080);
    set_io32(s, 0x28, 0);
    set_io32(s, 0x2C, 0);
}
static void scene_affine_rotated(Scene* s) {
    affine_bg2_setup(s, "affine_rotated", 1, 0);
    set_io16(s, 0x20, 0x00E0);           /* PA */
    set_io16(s, 0x22, (uint16_t)0xFF80); /* PB = -0.5 */
    set_io16(s, 0x24, 0x0080);           /* PC = 0.5 */
    set_io16(s, 0x26, 0x00E0);           /* PD */
    set_io32(s, 0x28, 0x00000800);       /* BG2X = 8.0 */
    set_io32(s, 0x2C, 0x00001000);       /* BG2Y = 16.0 */
}
static void scene_affine_wrap(Scene* s) {
    affine_bg2_setup(s, "affine_wrap", 0, 1); /* 128x128, wrap on */
    set_io16(s, 0x20, 0x0100);
    set_io16(s, 0x22, 0x0000);
    set_io16(s, 0x24, 0x0000);
    set_io16(s, 0x26, 0x0100);
    set_io32(s, 0x28, 0x00004000); /* BG2X = 64.0 -> wraps within 128 */
    set_io32(s, 0x2C, 0x00004000);
}
static void scene_affine_oob(Scene* s) {
    affine_bg2_setup(s, "affine_oob_transparent", 1, 0); /* no wrap: OOB -> backdrop */
    set_io16(s, 0x20, 0x0100);
    set_io16(s, 0x22, 0x0000);
    set_io16(s, 0x24, 0x0000);
    set_io16(s, 0x26, 0x0100);
    set_io32(s, 0x28, (uint32_t)(-0x00002000)); /* BG2X negative -> left cols OOB */
    set_io32(s, 0x2C, 0);
}

#if MODE1_GBA_WIDTH > 240
/* ---- widescreen Option A scenes (only meaningful when built >240) ---- */
/* Persistent shadow storage; the CPU stores the pointer, so it must outlive
 * the scene setup call. */
static uint16_t g_ws_shadow0[32 * MODE1_WS_SHADOW_COLS];

/* BG0 32-tile with a shadow tilemap revealing tiles past x=240. */
static void scene_ws_shadow_reveal(Scene* s) {
    scene_clear(s, "ws_shadow_reveal");
    set_io16(s, 0x00, 0x0100);              /* BG0 on, mode 0 */
    set_io16(s, 0x08, (uint16_t)(8u << 8)); /* BG0 screen block 8, prio 0 */
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    write_tile_4bpp(s->vram, 0, 2);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    /* Shadow: reveal columns use tile 2, palbank 2. */
    for (int i = 0; i < 32 * MODE1_WS_SHADOW_COLS; ++i) {
        g_ws_shadow0[i] = (uint16_t)(2u | (2u << 12));
    }
    virtuappu_mode1_ws_shadow[0] = g_ws_shadow0;
    virtuappu_mode1_ws_shadow_base_tile[0] = MODE1_GBA_BG_CLIP_X / 8;
}
/* Some reveal columns unloaded (0x7C1F sentinel) -> skipped -> force black. */
static void scene_ws_shadow_sentinel(Scene* s) {
    scene_ws_shadow_reveal(s);
    s->name = "ws_shadow_sentinel";
    /* palbank 2 index 2 -> palette entry becomes the sentinel. */
    s->bgpal[2 * 16 + 2] = 0x7C1F;
}
/* HUD right-anchor: BG0 cols 176..239 drawn at the far right. */
static void scene_ws_hud_anchor(Scene* s) {
    scene_clear(s, "ws_hud_anchor");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    virtuappu_mode1_ws_hud_right_anchor = 1;
}
/* Message-box centering: shift BG0 box band right by shift px. */
static void scene_ws_msg_center(Scene* s) {
    scene_clear(s, "ws_msg_center");
    set_io16(s, 0x00, 0x0100);
    set_io16(s, 0x08, (uint16_t)(8u << 8));
    s->bgpal[0] = 0x0421;
    fill_palette_bank(s->bgpal, 2, 0x0421);
    write_tile_4bpp(s->vram, 0, 1);
    fill_screenblock(s->vram, 0x4000, (uint16_t)(1u | (2u << 12)));
    virtuappu_mode1_ws_msg_shift = (MODE1_GBA_WIDTH - 240) / 2;
    virtuappu_mode1_ws_msg_x0 = 8;
    virtuappu_mode1_ws_msg_x1 = 216;
    virtuappu_mode1_ws_msg_y0 = 112;
    virtuappu_mode1_ws_msg_y1 = 152;
}
#endif

int main(int argc, char** argv) {
    const char* vpath = argc > 1 ? argv[1] : "port/shaders/build/ppu_raster.vert.spv";
    const char* fpath = argc > 2 ? argv[2] : "port/shaders/build/ppu_raster.frag.spv";

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) {
        std::fprintf(stderr, "CreateGPUDevice failed: %s\n", SDL_GetError());
        return 2;
    }
    std::printf("GPU parity harness — backend=%s\n", SDL_GetGPUDeviceDriver(dev));

    size_t vlen = 0, flen = 0;
    void* vspv = load_file(vpath, &vlen);
    void* fspv = load_file(fpath, &flen);
    if (!vspv || !fspv) {
        return 2;
    }
    PortGpuRaster* r = Port_GpuRaster_Create(dev, SDL_GPU_SHADERFORMAT_SPIRV, "main", vspv, vlen, fspv, flen);
    if (!r) {
        std::fprintf(stderr, "Port_GpuRaster_Create failed\n");
        return 2;
    }

    /* GLES backend: load the shared core GLSL and build the compute rasterizer
     * (optional — skipped if EGL/GLES 3.1 unavailable). */
    size_t clen = 0;
    void* core = load_file("port/shaders/ppu_core.glsl", &clen);
    PortGpuRasterGl* rgl = core ? Port_GpuRasterGl_Create((const char*)core, (int)clen) : nullptr;
    std::printf("GLES backend: %s\n", rgl ? "active" : "unavailable (Vulkan-only run)");

    const int W = MODE1_GBA_WIDTH, H = 160;
    static Scene scene;
    std::vector<uint32_t> cpu((size_t)W * H), gpu((size_t)W * H), gles((size_t)W * H);

    /* PPU_SSDUMP=<scale>: render the rotated-affine scene at 1x and Sx via the
     * GPU supersampler, dump raw RGBA (W H then pixels) to /tmp for a visual
     * before/after. Proves sub-pixel affine BG quality vs nearest-replicate. */
    if (const char* sd = getenv("PPU_SSDUMP")) {
        int S = atoi(sd);
        if (S < 2)
            S = 4;
        scene_affine_rotated(&scene);
        auto dump = [&](int scale, const char* path) {
            PortGpuRasterFrame f;
            build_frame(&scene, W, H, &f, g_io, g_dispcnt, g_affx, g_affy, g_wsconcat);
            f.scale = scale;
            std::vector<uint32_t> buf((size_t)W * scale * H * scale);
            if (!Port_GpuRaster_RenderReadback(r, &f, buf.data(), W * scale)) {
                std::fprintf(stderr, "  SSDUMP scale %d failed\n", scale);
                return;
            }
            FILE* fp = fopen(path, "wb");
            int dw = W * scale, dh = H * scale;
            fwrite(&dw, sizeof(int), 1, fp);
            fwrite(&dh, sizeof(int), 1, fp);
            fwrite(buf.data(), sizeof(uint32_t), buf.size(), fp);
            fclose(fp);
            std::printf("  dumped %dx%d -> %s\n", dw, dh, path);
        };
        dump(1, "/tmp/ss_native.raw");
        dump(S, "/tmp/ss_super.raw");
        Port_GpuRaster_Destroy(r);
        if (rgl)
            Port_GpuRasterGl_Destroy(rgl);
        SDL_DestroyGPUDevice(dev);
        SDL_Quit();
        return 0;
    }

    /* PPU_BENCH=<iters>: micro-benchmark the Vulkan raster phases (render =
     * upload+dispatch+fence, readback = 2nd submit+fence+map+copy) on a busy
     * scene, to isolate where time goes before optimizing. */
    if (const char* bn = getenv("PPU_BENCH")) {
        int iters = atoi(bn);
        if (iters < 1)
            iters = 500;
        scene_bg_priority(&scene); /* 2 BGs + composite, representative */
        PortGpuRasterFrame f;
        build_frame(&scene, W, H, &f, g_io, g_dispcnt, g_affx, g_affy, g_wsconcat);
        /* warmup */
        for (int i = 0; i < 20; ++i) {
            Port_GpuRaster_Render(r, &f);
            Port_GpuRaster_Readback(r, gpu.data(), W, H, W);
        }
        /* CPU rasterizer cost for the same scene — the bar GPU must beat. */
        double t_cpu = 0;
        for (int i = 0; i < iters; ++i) {
            uint64_t a = SDL_GetTicksNS();
            render_cpu(&scene, cpu.data(), W, H);
            t_cpu += (SDL_GetTicksNS() - a) / 1e6;
        }
        double t_render = 0, t_read = 0, t_merged = 0;
        for (int i = 0; i < iters; ++i) {
            uint64_t a = SDL_GetTicksNS();
            Port_GpuRaster_Render(r, &f);
            uint64_t b = SDL_GetTicksNS();
            Port_GpuRaster_Readback(r, gpu.data(), W, H, W);
            uint64_t c = SDL_GetTicksNS();
            Port_GpuRaster_RenderReadback(r, &f, gpu.data(), W);
            uint64_t d = SDL_GetTicksNS();
            t_render += (b - a) / 1e6;
            t_read += (c - b) / 1e6;
            t_merged += (d - c) / 1e6;
        }
        double t_defer = 0;
        for (int i = 0; i < iters; ++i) {
            uint64_t a = SDL_GetTicksNS();
            Port_GpuRaster_RenderReadbackDeferred(r, &f, gpu.data(), W);
            t_defer += (SDL_GetTicksNS() - a) / 1e6;
        }
        /* CPU-side pieces the live game pays every frame (bench builds the
         * frame once, so isolate them here). */
        double t_cull = 0, t_prep = 0;
        static std::vector<uint32_t> cullbuf((size_t)H * PORT_GPU_OBJ_CULL_STRIDE);
        for (int i = 0; i < iters; ++i) {
            uint64_t a = SDL_GetTicksNS();
            Port_GpuObjCull_Build(scene.oam, W, H, cullbuf.data());
            t_cull += (SDL_GetTicksNS() - a) / 1e6;
        }
        for (int i = 0; i < iters; ++i) {
            PortGpuRasterFrame ff;
            uint64_t a = SDL_GetTicksNS();
            build_frame(&scene, W, H, &ff, g_io, g_dispcnt, g_affx, g_affy, g_wsconcat);
            t_prep += (SDL_GetTicksNS() - a) / 1e6;
        }
        /* Bare submit+fence floor: acquire an empty command buffer, submit,
         * wait. Isolates the driver's per-frame command-submission cost. */
        double t_bare = 0;
        for (int i = 0; i < iters; ++i) {
            uint64_t a = SDL_GetTicksNS();
            SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev);
            SDL_GPUFence* fn = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
            if (fn) {
                SDL_WaitForGPUFences(dev, true, &fn, 1);
                SDL_ReleaseGPUFence(dev, fn);
            }
            t_bare += (SDL_GetTicksNS() - a) / 1e6;
        }
        std::printf("[bench] %dx %dx%d: CPU=%.4f | vk DEFERRED(submit)=%.4f MERGED=%.4f | "
                    "bare_submit=%.4f cull=%.4f prep=%.4f ms\n",
                    iters, W, H, t_cpu / iters, t_defer / iters, t_merged / iters, t_bare / iters, t_cull / iters,
                    t_prep / iters);
        Port_GpuRaster_Destroy(r);
        if (rgl)
            Port_GpuRasterGl_Destroy(rgl);
        SDL_DestroyGPUDevice(dev);
        SDL_Quit();
        return 0;
    }

    SceneFn scenes[] = {
        scene_forced_blank,
        scene_backdrop_blue,
        scene_backdrop_black,
        scene_backdrop_mixed,
        scene_bg0_tiled,
        scene_bg0_scroll,
        scene_bg0_flip,
        scene_bg0_8bpp,
        scene_bg0_mosaic,
        scene_bg0_512,
        scene_bg_priority,
        scene_bg0_transparent,
        scene_obj_basic,
        scene_obj_hflip,
        scene_obj_vflip,
        scene_obj_8bpp,
        scene_obj_2d,
        scene_obj_affine,
        scene_obj_affine_double,
        scene_obj_over_bg,
        scene_obj_two_overlap,
        scene_obj_wrap,
        scene_obj_mosaic,
        scene_win0_inside,
        scene_win0_hwrap,
        scene_win0_vwrap,
        scene_win1,
        scene_objwin,
        scene_blend_alpha,
        scene_blend_brighten,
        scene_blend_darken,
        scene_blend_alpha_backdrop,
        scene_obj_semitrans,
        scene_affine_identity,
        scene_affine_scaled,
        scene_affine_rotated,
        scene_affine_wrap,
        scene_affine_oob,
#if MODE1_GBA_WIDTH > 240
        scene_ws_shadow_reveal,
        scene_ws_shadow_sentinel,
        scene_ws_hud_anchor,
        scene_ws_msg_center,
#endif
    };
    int total_diffs = 0;
    char label[64];
    for (SceneFn fn : scenes) {
        fn(&scene);
        render_cpu(&scene, cpu.data(), W, H);
        render_vk(r, &scene, gpu.data(), W, H);
        std::snprintf(label, sizeof(label), "%s [vk]", scene.name);
        total_diffs += diff(cpu.data(), gpu.data(), W, H, label);
        if (rgl) {
            render_gles(rgl, &scene, gles.data(), W, H);
            std::snprintf(label, sizeof(label), "%s [gles]", scene.name);
            total_diffs += diff(cpu.data(), gles.data(), W, H, label);
        }
    }

    if (rgl) {
        Port_GpuRasterGl_Destroy(rgl);
    }
    if (core) {
        std::free(core);
    }
    Port_GpuRaster_Destroy(r);
    SDL_DestroyGPUDevice(dev);
    SDL_Quit();
    std::free(vspv);
    std::free(fspv);

    if (total_diffs == 0) {
        std::printf("ALL SCENES BIT-EXACT\n");
        return 0;
    }
    std::printf("MISMATCH: %d total differing pixels\n", total_diffs);
    return 1;
}
