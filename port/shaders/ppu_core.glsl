/*
 * ppu_core.glsl — shared GPU PPU logic (dialect-neutral).
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Constants, storage-buffer accessors, and vec4 ppu_pixel(int x, int line) —
 * a faithful integer reimplementation of port/ppu/src/mode1.c. The storage
 * buffers (bgpal/io/vram/objpal/oam/aff/wss/objcull) and the `params` uniform
 * block are declared by the including wrapper (Vulkan fragment: ppu_raster.frag;
 * GLES compute: ppu_raster.comp / built at runtime), so the same buffer +
 * field names must appear in both. Do NOT put #version, layouts, or an entry
 * point here.
 */

/* DISPCNT / BGCNT / IO offsets (mirror cpu/mode1.h). */
const uint DISP_FORCED_BLANK = 0x0080u;
const uint DISP_OBJ_1D = 0x0040u;
const uint DISP_BG0_ON = 0x0100u;
const uint DISP_OBJ_ON = 0x1000u;
const uint OBJ_TILE_BASE = 0x10000u;
const int IO_DISPCNT = 0x00;
const int IO_BG0CNT = 0x08;
const int IO_BG0HOFS = 0x10;
const int IO_BG0VOFS = 0x12;
const int IO_MOSAIC = 0x4C;
const uint VRAM_SIZE = 0x18000u;

/* ---- storage-buffer accessors (byte/halfword views over u32 words) ---- */
uint bgpal_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (bgpal.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
uint io_u16(int line, int off) {
    /* params.misc.y != 0 => IO is frame-uniform (no per-line HDMA): only row 0
     * was uploaded, so read row 0 for every line. Removes 159 KB/frame upload. */
    int row = (params.misc.y != 0u) ? 0 : line;
    uint b = uint(row) * 0x400u + uint(off);
    return (io.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
uint vram_u8(uint addr) {
    if (addr >= VRAM_SIZE) return 0u;
    return (vram.data[addr >> 2u] >> ((addr & 3u) * 8u)) & 0xFFu;
}
uint vram_u16(uint addr) {
    return vram_u8(addr) | (vram_u8(addr + 1u) << 8u);
}
uint objpal_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (objpal.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
/* OAM is 512 halfwords; entry i uses oam[i*4+0..2] (attr0,attr1,attr2). */
uint oam_u16(int idx) {
    uint b = uint(idx) * 2u;
    return (oam.data[b >> 2u] >> ((b & 2u) * 8u)) & 0xFFFFu;
}
int sx16(uint v) { return (v & 0x8000u) != 0u ? int(v) - 0x10000 : int(v); }

/* Supersample state — set by the wrapper's main() from params.misc.z (scale)
 * and the fragment/invocation coord. At scale 1, gSubX=gSubY=0 so every path
 * below is bit-exact vs the native render; only the affine samplers consume the
 * sub-pixel offset (tile/text BGs stay nearest to preserve pixel art). The GLES
 * compute wrapper never writes these, so it keeps the scale-1 defaults. */
int gPpuScale = 1;
int gSubX = 0;
int gSubY = 0;

vec4 rgb555_to_rgba(uint c) {
    uint r = (c & 0x1Fu) << 3u;
    uint g = ((c >> 5u) & 0x1Fu) << 3u;
    uint b = ((c >> 10u) & 0x1Fu) << 3u;
    return vec4(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0, 1.0);
}

/* Text-BG pixel (native path of virtuappu_mode1_render_text_bg_line).
 * Returns the palette index (0 = transparent) and, when opaque, `abgr`. */
uint bg_text_pixel(int bg, int x, int line, out uint pal_index) {
    pal_index = 0u;
    int cnt_off = IO_BG0CNT + bg * 2;
    uint bgcnt = io_u16(line, cnt_off);
    uint char_base = ((bgcnt >> 2u) & 3u) * 0x4000u;
    bool mosaic_on = ((bgcnt >> 6u) & 1u) != 0u;
    bool bpp8 = ((bgcnt >> 7u) & 1u) != 0u;
    uint screen_base = ((bgcnt >> 8u) & 0x1Fu) * 0x800u;
    uint size_flag = (bgcnt >> 14u) & 3u;
    int map_w = (size_flag & 1u) != 0u ? 64 : 32;
    int map_h = (size_flag & 2u) != 0u ? 64 : 32;

    /* Widescreen Option A (render_text_bg_line). Inert at width 240. */
    int fw = params.geom.x;
    int clip = params.ws.x;       /* MODE1_GBA_BG_CLIP_X (240) */
    int ws_cols = params.ws.y;
    int shadow_base = params.wsbase[bg];
    bool has_shadow = shadow_base >= 0;
    bool ws_shadow_active = (map_w < 64) && has_shadow;
    int render_max_x = (map_w >= 64) ? fw : (has_shadow ? fw : clip);
    if (render_max_x > fw) render_max_x = fw;
    bool hud_anchor = (bg == 0) && (params.ws.z != 0) && (fw > clip);
    int hud_native_x = params.ws.w; /* 176 */
    int hud_dst_x = fw - (clip - hud_native_x);
    int shift = params.wsmsg.x;
    int msg_x0 = params.wsmsg.y;
    int msg_x1 = params.wsmsg.z;
    int msg_y0 = params.wsmsg.w >> 16;
    int msg_y1 = params.wsmsg.w & 0xFFFF;
    bool msg_line = (bg == 0) && (shift != 0) && (fw > clip) && (line >= msg_y0) && (line < msg_y1);
    if (hud_anchor || msg_line) render_max_x = fw;
    if (x >= render_max_x) return 0u;

    int sample_x = x;
    if (msg_line && x >= msg_x0 + shift && x < msg_x1 + shift) {
        sample_x = x - shift;
    } else if (msg_line && x >= msg_x0 && x < msg_x1) {
        return 0u;
    } else if (hud_anchor && !msg_line) {
        if (x >= hud_dst_x) { sample_x = x - (fw - clip); }
        else if (x >= hud_native_x) { return 0u; }
    } else if (msg_line && x >= clip) {
        return 0u;
    }

    int scroll_x = int(io_u16(line, IO_BG0HOFS + bg * 4)) & 0x1FF;
    int scroll_y = int(io_u16(line, IO_BG0VOFS + bg * 4)) & 0x1FF;
    uint mosaic_reg = io_u16(line, IO_MOSAIC);
    int mh = mosaic_on ? int((mosaic_reg & 0xFu) + 1u) : 1;
    int mv = mosaic_on ? int(((mosaic_reg >> 4u) & 0xFu) + 1u) : 1;

    int eff_line = (mv == 1) ? line : (line / mv) * mv;
    int src_y = (eff_line + scroll_y) & (map_h * 8 - 1);
    int tile_row = src_y / 8;
    int pixel_y = src_y % 8;

    int eff_x = (mh == 1) ? sample_x : (sample_x / mh) * mh;
    int src_x = (eff_x + scroll_x) & (map_w * 8 - 1);
    int tile_col = src_x / 8;
    int pixel_x = src_x % 8;

    int local_row = tile_row % 32;
    uint entry;
    bool use_shadow = ws_shadow_active && x >= clip;
    if (use_shadow) {
        int shadow_idx = (tile_col - shadow_base + 32) % 32;
        if (shadow_idx < ws_cols) {
            uint hw = uint((bg * 32 + local_row) * ws_cols + shadow_idx);
            uint word = wss.data[hw >> 1u];
            entry = (hw & 1u) != 0u ? (word >> 16u) : (word & 0xFFFFu);
        } else {
            entry = 0u;
        }
    } else {
        int screen_block_y = tile_row / 32;
        int blocks_per_row = map_w / 32;
        int screen_block_x = tile_col / 32;
        int local_col = tile_col % 32;
        int screen_block_index = screen_block_x + screen_block_y * blocks_per_row;
        uint map_addr = screen_base + uint(screen_block_index) * 0x800u + uint(local_row * 32 + local_col) * 2u;
        entry = vram_u16(map_addr);
    }

    uint tile_index = entry & 0x3FFu;
    bool hflip = ((entry >> 10u) & 1u) != 0u;
    bool vflip = ((entry >> 11u) & 1u) != 0u;
    uint pal_bank = (entry >> 12u) & 0xFu;
    int tpy = vflip ? (7 - pixel_y) : pixel_y;
    int tpx = hflip ? (7 - pixel_x) : pixel_x;

    uint color_index;
    if (bpp8) {
        uint addr = char_base + tile_index * 64u + uint(tpy) * 8u + uint(tpx);
        color_index = vram_u8(addr);
    } else {
        uint addr = char_base + tile_index * 32u + uint(tpy) * 4u + uint(tpx / 2);
        uint pkbyte = vram_u8(addr);
        color_index = ((tpx & 1) != 0) ? (pkbyte >> 4u) : (pkbyte & 0xFu);
    }
    if (color_index == 0u) {
        return 0u;
    }
    uint pi = bpp8 ? color_index : (pal_bank * 16u + color_index);
    /* Widescreen sentinel: past the clip, an "unloaded" 0x7C1F tile is skipped. */
    if (x >= clip && (bgpal_u16(int(pi)) & 0x7FFFu) == 0x7C1Fu) {
        return 0u;
    }
    pal_index = pi;
    return color_index;
}

uint bg_priority(int bg, int line) {
    return io_u16(line, IO_BG0CNT + bg * 2) & 3u;
}

/* Affine BG2 pixel (mode1_render_affine_bg2_line). Always 8bpp, 1-byte tilemap
 * entries, 64-byte tiles. Uses the precomputed per-line reference (ref_x,ref_y)
 * + pa/pc. Returns palette index (0 = transparent). */
uint bg2_affine_pixel(int x, int line) {
    uint bgcnt = io_u16(line, IO_BG0CNT + 2 * 2); /* BG2CNT at 0x0C */
    uint char_base = ((bgcnt >> 2u) & 3u) * 0x4000u;
    uint screen_base = ((bgcnt >> 8u) & 0x1Fu) * 0x800u;
    bool wrap = ((bgcnt >> 13u) & 1u) != 0u;
    uint size_flag = (bgcnt >> 14u) & 3u;
    int map_size = 128 << int(size_flag); /* 128,256,512,1024 */
    int map_tiles = map_size / 8;
    int pa = sx16(io_u16(line, 0x20));
    int pc = sx16(io_u16(line, 0x24));
    int pb = sx16(io_u16(line, 0x22));
    int pd = sx16(io_u16(line, 0x26));
    int ref_x = aff.data[line * 2 + 0];
    int ref_y = aff.data[line * 2 + 1];

    /* Sub-pixel supersample offset (8.8 fixed), zero at scale 1 -> bit-exact.
     * pa/pc scale sub-x within the line; pb/pd scale sub-y between lines. */
    int add_x = (pa * gSubX + pb * gSubY) / gPpuScale;
    int add_y = (pc * gSubX + pd * gSubY) / gPpuScale;
    int src_x = (ref_x + pa * x + add_x) >> 8;
    int src_y = (ref_y + pc * x + add_y) >> 8;
    if (wrap) {
        src_x &= (map_size - 1);
        src_y &= (map_size - 1);
    } else if (src_x < 0 || src_x >= map_size || src_y < 0 || src_y >= map_size) {
        return 0u;
    }
    int tile_col = src_x / 8;
    int tile_row = src_y / 8;
    int pixel_x = src_x % 8;
    int pixel_y = src_y % 8;
    uint map_addr = screen_base + uint(tile_row * map_tiles + tile_col);
    uint tile_index = vram_u8(map_addr);
    uint tile_addr = char_base + tile_index * 64u + uint(pixel_y) * 8u + uint(pixel_x);
    return vram_u8(tile_addr);
}

/* OBJ shape/size dimension tables (mirror mode1_obj_widths/heights). */
const int OBJ_W[12] = int[12](8, 16, 32, 64, 16, 32, 32, 64, 8, 8, 16, 32);
const int OBJ_H[12] = int[12](8, 16, 32, 64, 8, 8, 16, 32, 16, 32, 32, 64);

/* Resolve the OBJ layer at (x,line): lowest-index opaque non-window sprite
 * wins (matches the CPU's i=127..0 overwrite walk). Also reports the OBJ-window
 * mask (any opaque mode-2 sprite) and the semi-transparent flag of the winner.
 * Returns true when an opaque colour sprite covers the pixel. */
bool obj_resolve(int x, int line, uint line_dispcnt, out uint out_col, out int out_pri, out bool out_semi,
                 out bool out_window) {
    out_col = 0u;
    out_pri = 100;
    out_semi = false;
    out_window = false;
    bool found = false;
    bool obj_1d = (line_dispcnt & DISP_OBJ_1D) != 0u;
    int frame_width = params.geom.x;
    int viewport = frame_width; /* MODE1_GBA_VIEWPORT_X == MODE1_GBA_WIDTH >= frame_width */
    uint mosaic_reg = io_u16(line, IO_MOSAIC);
    int mos_h = int((mosaic_reg >> 8u) & 0xFu) + 1;
    int mos_v = int((mosaic_reg >> 12u) & 0xFu) + 1;

    /* Loop only this line's OBJ candidates (port_gpu_obj_cull, stride 129:
     * slot 0 = count, then ascending OAM indices). The candidates already
     * passed the sprite-level + vertical + horizontal tests; the per-pixel
     * tests below still run, so output is identical to the full 0..127 scan. */
    uint cull_base = uint(line) * 129u;
    uint cull_count = objcull.data[cull_base];
    for (uint ci = 0u; ci < cull_count; ++ci) {
        int i = int(objcull.data[cull_base + 1u + ci]);
        uint attr0 = oam_u16(i * 4 + 0);
        uint attr1 = oam_u16(i * 4 + 1);
        uint attr2 = oam_u16(i * 4 + 2);
        bool affine = ((attr0 >> 8u) & 1u) != 0u;
        bool double_size = affine && (((attr0 >> 9u) & 1u) != 0u);
        bool hidden = !affine && (((attr0 >> 9u) & 1u) != 0u);
        if (hidden) {
            continue;
        }
        uint shape = (attr0 >> 14u) & 3u;
        uint size = (attr1 >> 14u) & 3u;
        if (shape >= 3u) {
            continue;
        }
        int obj_w = OBJ_W[int(shape) * 4 + int(size)];
        int obj_h = OBJ_H[int(shape) * 4 + int(size)];
        int bw = obj_w;
        int bh = obj_h;
        if (affine && double_size) {
            bw *= 2;
            bh *= 2;
        }
        int obj_y = int(attr0 & 0xFFu);
        if (obj_y >= 160) {
            obj_y -= 256;
        }
        if (line < obj_y || line >= obj_y + bh) {
            continue;
        }
        int obj_x = int(attr1 & 0x1FFu);
        if (obj_x >= frame_width) {
            obj_x -= 512;
        }
        if (obj_x + bw <= 0 || obj_x >= viewport) {
            continue;
        }
        if (x < obj_x || x >= obj_x + bw) {
            continue;
        }

        bool mosaic_on = (((attr0 >> 12u) & 1u) != 0u) && (mos_h > 1 || mos_v > 1);
        int eff_line = line;
        if (mosaic_on) {
            eff_line = line - (line % mos_v);
            if (eff_line < obj_y) {
                continue;
            }
        }
        int half_w = bw / 2;
        int half_h = bh / 2;
        int sprite_half_w = obj_w / 2;
        int sprite_half_h = obj_h / 2;
        int input_rel_y = eff_line - obj_y - half_h;
        bool mosaic_x_on = mosaic_on && mos_h > 1;

        int eff_sx = x - obj_x;
        if (mosaic_x_on) {
            int snapped_x = x - (x % mos_h);
            eff_sx = snapped_x - obj_x;
            if (eff_sx < 0) {
                continue;
            }
        }

        int tex_x;
        int tex_y;
        if (affine) {
            uint grp = (attr1 >> 9u) & 0x1Fu;
            int pa = sx16(oam_u16(int(grp) * 16 + 3));
            int pb = sx16(oam_u16(int(grp) * 16 + 7));
            int pc = sx16(oam_u16(int(grp) * 16 + 11));
            int pd = sx16(oam_u16(int(grp) * 16 + 15));
            int input_rel_x = eff_sx - half_w;
            /* Sub-pixel supersample offset (zero at scale 1 -> bit-exact). */
            int oadd_x = (pa * gSubX + pb * gSubY) / gPpuScale;
            int oadd_y = (pc * gSubX + pd * gSubY) / gPpuScale;
            tex_x = ((pa * input_rel_x + pb * input_rel_y + oadd_x) >> 8) + sprite_half_w;
            tex_y = ((pc * input_rel_x + pd * input_rel_y + oadd_y) >> 8) + sprite_half_h;
            if (tex_x < 0 || tex_x >= obj_w || tex_y < 0 || tex_y >= obj_h) {
                continue;
            }
        } else {
            bool hflip = ((attr1 >> 12u) & 1u) != 0u;
            bool vflip = ((attr1 >> 13u) & 1u) != 0u;
            int draw_x = hflip ? (obj_w - 1 - eff_sx) : eff_sx;
            int draw_y = eff_line - obj_y;
            if (vflip) {
                draw_y = obj_h - 1 - draw_y;
            }
            tex_x = draw_x;
            tex_y = draw_y;
        }

        int tile_row = tex_y / 8;
        int pixel_y = tex_y % 8;
        int tile_col = tex_x / 8;
        int pixel_x = tex_x % 8;
        int tiles_w = obj_w / 8;
        bool bpp8 = ((attr0 >> 13u) & 1u) != 0u;
        uint base_tile = attr2 & 0x3FFu;
        uint tile_index;
        if (obj_1d) {
            tile_index = bpp8 ? (base_tile + uint(tile_row * tiles_w + tile_col) * 2u)
                              : (base_tile + uint(tile_row * tiles_w + tile_col));
        } else {
            tile_index = bpp8 ? (base_tile + uint(tile_row * 32 + tile_col * 2))
                              : (base_tile + uint(tile_row * 32 + tile_col));
        }
        uint color_index;
        if (bpp8) {
            uint addr = OBJ_TILE_BASE + tile_index * 32u + uint(pixel_y) * 8u + uint(pixel_x);
            color_index = vram_u8(addr);
        } else {
            uint addr = OBJ_TILE_BASE + tile_index * 32u + uint(pixel_y) * 4u + uint(pixel_x / 2);
            uint pkbyte = vram_u8(addr);
            color_index = ((pixel_x & 1) != 0) ? (pkbyte >> 4u) : (pkbyte & 0xFu);
        }
        if (color_index == 0u) {
            continue;
        }
        uint mode = (attr0 >> 10u) & 3u;
        if (mode == 2u) {
            out_window = true;
            continue;
        }
        if (!found) {
            uint pal_idx = bpp8 ? color_index : (((attr2 >> 12u) & 0xFu) * 16u + color_index);
            out_col = objpal_u16(int(pal_idx));
            out_pri = int((attr2 >> 10u) & 3u);
            out_semi = (mode == 1u);
            found = true;
        }
    }
    return found;
}

/* ---- window + blend (composite_line) ---- */
const int IO_WIN0H = 0x40;
const int IO_WIN1H = 0x42;
const int IO_WIN0V = 0x44;
const int IO_WIN1V = 0x46;
const int IO_WININ = 0x48;
const int IO_WINOUT = 0x4A;
const int IO_BLDCNT = 0x50;
const int IO_BLDALPHA = 0x52;
const int IO_BLDY = 0x54;
const uint DISP_WIN0_ON = 0x2000u;
const uint DISP_WIN1_ON = 0x4000u;
const uint DISP_OBJWIN_ON = 0x8000u;

bool is_first_target(uint bldcnt, int layer) { return ((bldcnt >> uint(layer)) & 1u) != 0u; }
bool is_second_target(uint bldcnt, int layer) { return ((bldcnt >> uint(layer + 8)) & 1u) != 0u; }

/* All three operate in 5-bit rgb555 space and return rgb555, matching the
 * CPU's mode1_alpha_blend / brighten / darken exactly (which recover 5-bit
 * via >>3 from the 8-bit channels — identical to the rgb555 channels here). */
uint blend_alpha(uint top, uint bot, int eva, int evb) {
    int tr = int(top & 0x1Fu), tg = int((top >> 5u) & 0x1Fu), tb = int((top >> 10u) & 0x1Fu);
    int br = int(bot & 0x1Fu), bg = int((bot >> 5u) & 0x1Fu), bb = int((bot >> 10u) & 0x1Fu);
    int r = (tr * eva + br * evb) >> 4; if (r > 31) r = 31;
    int g = (tg * eva + bg * evb) >> 4; if (g > 31) g = 31;
    int b = (tb * eva + bb * evb) >> 4; if (b > 31) b = 31;
    return uint(r) | (uint(g) << 5u) | (uint(b) << 10u);
}
uint blend_brighten(uint c, int evy) {
    int r = int(c & 0x1Fu), g = int((c >> 5u) & 0x1Fu), b = int((c >> 10u) & 0x1Fu);
    r += ((31 - r) * evy) >> 4; if (r > 31) r = 31;
    g += ((31 - g) * evy) >> 4; if (g > 31) g = 31;
    b += ((31 - b) * evy) >> 4; if (b > 31) b = 31;
    return uint(r) | (uint(g) << 5u) | (uint(b) << 10u);
}
uint blend_darken(uint c, int evy) {
    int r = int(c & 0x1Fu), g = int((c >> 5u) & 0x1Fu), b = int((c >> 10u) & 0x1Fu);
    r -= (r * evy) >> 4; if (r < 0) r = 0;
    g -= (g * evy) >> 4; if (g < 0) g = 0;
    b -= (b * evy) >> 4; if (b < 0) b = 0;
    return uint(r) | (uint(g) << 5u) | (uint(b) << 10u);
}

/* Compute the composited ABGR pixel at (x, line). Wrapper shaders (fragment for
 * Vulkan, compute for GLES) call this; all layout/entry-point differences live
 * in the wrappers, this core is dialect-neutral. */
vec4 ppu_pixel(int x, int line) {
    if ((params.misc.x & DISP_FORCED_BLANK) != 0u) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }

    /* Affine (GBA modes 1/2): DISPCNT is latched frame-start; BG2 is affine and
     * BG3 is not rendered (matches render_frame's affine branch). */
    bool affine = (params.geom.w != 0);
    uint line_dispcnt = affine ? params.misc.x : io_u16(line, IO_DISPCNT);
    uint backdrop = bgpal_u16(0);

    /* Gather BG layers. */
    bool bg_on[4];
    bool bg_op[4];
    uint bg_col[4];
    int bg_pri[4];
    for (int bg = 0; bg < 4; ++bg) {
        bg_pri[bg] = int(bg_priority(bg, line));
        bg_on[bg] = (line_dispcnt & (DISP_BG0_ON << uint(bg))) != 0u;
        bg_op[bg] = false;
        bg_col[bg] = 0u;
        if (affine && bg == 3) {
            bg_on[bg] = false; /* no BG3 in the affine path */
        }
        if (!bg_on[bg]) {
            continue;
        }
        if (affine && bg == 2) {
            uint ci = bg2_affine_pixel(x, line);
            if (ci != 0u) {
                bg_op[bg] = true;
                bg_col[bg] = bgpal_u16(int(ci)); /* affine BG is 8bpp: direct index */
            }
        } else {
            uint pal_index;
            uint ci = bg_text_pixel(bg, x, line, pal_index);
            if (ci != 0u) {
                bg_op[bg] = true;
                bg_col[bg] = bgpal_u16(int(pal_index));
            }
        }
    }

    /* OBJ layer. */
    bool obj_on = (line_dispcnt & DISP_OBJ_ON) != 0u;
    uint obj_col = 0u;
    int obj_pri = 100;
    bool obj_semi = false;
    bool obj_window = false;
    bool obj_found = obj_on && obj_resolve(x, line, line_dispcnt, obj_col, obj_pri, obj_semi, obj_window);

    int frame_width = params.geom.x;

    /* Window resolution (composite_line): pick the control byte for this pixel
     * in priority order outside < objwin < win1 < win0. */
    bool win0_on = (line_dispcnt & DISP_WIN0_ON) != 0u;
    bool win1_on = (line_dispcnt & DISP_WIN1_ON) != 0u;
    bool objwin_on = (line_dispcnt & DISP_OBJWIN_ON) != 0u;
    bool any_window = win0_on || win1_on || objwin_on;
    uint win_ctrl = 0x3Fu;
    if (any_window) {
        uint winin = io_u16(line, IO_WININ);
        uint winout = io_u16(line, IO_WINOUT);
        uint win0h = io_u16(line, IO_WIN0H);
        uint win0v = io_u16(line, IO_WIN0V);
        uint win1h = io_u16(line, IO_WIN1H);
        uint win1v = io_u16(line, IO_WIN1V);
        int w0l = int(win0h >> 8u), w0r = int(win0h & 0xFFu);
        int w0t = int(win0v >> 8u), w0b = int(win0v & 0xFFu);
        int w1l = int(win1h >> 8u), w1r = int(win1h & 0xFFu);
        int w1t = int(win1v >> 8u), w1b = int(win1v & 0xFFu);
        if (w0r > frame_width) w0r = frame_width;
        if (w0b > 160) w0b = 160;
        if (w1r > frame_width) w1r = frame_width;
        if (w1b > 160) w1b = 160;
        bool w0hw = w0l > w0r, w0vw = w0t > w0b;
        bool w1hw = w1l > w1r, w1vw = w1t > w1b;
        bool w0va = win0_on && (w0vw ? (line >= w0t || line < w0b) : (line >= w0t && line < w0b));
        bool w1va = win1_on && (w1vw ? (line >= w1t || line < w1b) : (line >= w1t && line < w1b));
        win_ctrl = winout & 0x3Fu;
        if (objwin_on && obj_window) { win_ctrl = (winout >> 8u) & 0x3Fu; }
        if (w1va) {
            bool in_h = w1hw ? (x >= w1l || x < w1r) : (x >= w1l && x < w1r);
            if (in_h) { win_ctrl = (winin >> 8u) & 0x3Fu; }
        }
        if (w0va) {
            bool in_h = w0hw ? (x >= w0l || x < w0r) : (x >= w0l && x < w0r);
            if (in_h) { win_ctrl = winin & 0x3Fu; }
        }
    }
    bool vis_bg[4] = bool[4]((win_ctrl & 0x01u) != 0u, (win_ctrl & 0x02u) != 0u,
                             (win_ctrl & 0x04u) != 0u, (win_ctrl & 0x08u) != 0u);
    bool vis_obj = (win_ctrl & 0x10u) != 0u;
    bool allow_sfx = (win_ctrl & 0x20u) != 0u;

    /* Stable priority sort of BG indices (index tie-break). */
    int order[4] = int[4](0, 1, 2, 3);
    for (int i = 1; i < 4; ++i) {
        int key = order[i];
        int kp = bg_pri[key];
        int j = i - 1;
        while (j >= 0 && bg_pri[order[j]] > kp) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    /* Merged walk producing top + bottom layer (composite_line). */
    uint top_col = backdrop;
    int top_layer = 5;
    uint bot_col = backdrop;
    int bot_layer = 5;
    bool found_top = false;
    bool found_bottom = false;
    bool obj_candidate = obj_found && vis_obj;
    bool obj_emitted = false;

    for (int oi = 0; oi < 4 && !found_bottom; ++oi) {
        int bg = order[oi];
        if (obj_candidate && !obj_emitted && bg_pri[bg] >= obj_pri) {
            if (!found_top) { top_col = obj_col; top_layer = 4; found_top = true; }
            else { bot_col = obj_col; bot_layer = 4; found_bottom = true; }
            obj_emitted = true;
            if (found_bottom) { break; }
        }
        if (!bg_on[bg] || !vis_bg[bg] || !bg_op[bg]) {
            continue;
        }
        if (!found_top) { top_col = bg_col[bg]; top_layer = bg; found_top = true; }
        else { bot_col = bg_col[bg]; bot_layer = bg; found_bottom = true; }
    }
    if (obj_candidate && !obj_emitted && !found_bottom) {
        if (!found_top) { top_col = obj_col; top_layer = 4; found_top = true; }
        else { bot_col = obj_col; bot_layer = 4; found_bottom = true; }
    }

    /* Blend (composite_line SFX). */
    uint bldcnt = io_u16(line, IO_BLDCNT);
    int effect = int((bldcnt >> 6u) & 3u);
    int eva = int(io_u16(line, IO_BLDALPHA) & 0x1Fu); if (eva > 16) eva = 16;
    int evb = int((io_u16(line, IO_BLDALPHA) >> 8u) & 0x1Fu); if (evb > 16) evb = 16;
    int evy = int(io_u16(line, IO_BLDY) & 0x1Fu); if (evy > 16) evy = 16;
    uint out_col = top_col;
    if (allow_sfx) {
        if (top_layer == 4 && obj_semi) {
            /* Semi-transparent OBJ is a forced alpha 1st target. */
            if (is_second_target(bldcnt, bot_layer)) {
                out_col = blend_alpha(top_col, bot_col, eva, evb);
            } else if (effect == 2) {
                out_col = blend_brighten(top_col, evy);
            } else if (effect == 3) {
                out_col = blend_darken(top_col, evy);
            }
        } else if (effect == 1) {
            if (is_first_target(bldcnt, top_layer) && is_second_target(bldcnt, bot_layer)) {
                out_col = blend_alpha(top_col, bot_col, eva, evb);
            }
        } else if (effect == 2) {
            if (is_first_target(bldcnt, top_layer)) { out_col = blend_brighten(top_col, evy); }
        } else if (effect == 3) {
            if (is_first_target(bldcnt, top_layer)) { out_col = blend_darken(top_col, evy); }
        }
    }

    /* Widescreen composite: past the clip, force black unless a BG drew a real
     * pixel there (matches composite_line's any_bg_drew_here gate). Inert at 240
     * because x never reaches clip. */
    if (x >= params.ws.x) {
        bool any_bg_drew = false;
        for (int b = 0; b < 4; ++b) {
            if (bg_on[b] && bg_op[b]) { any_bg_drew = true; break; }
        }
        if (!any_bg_drew) {
            return vec4(0.0, 0.0, 0.0, 1.0);
        }
    }
    return rgb555_to_rgba(out_col);
}
