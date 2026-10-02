#version 450
/*
 * ppu_raster.frag — Vulkan (SDL_GPU) fragment wrapper for the GPU PPU
 * rasterizer. Declares the storage buffers + `params` UBO, then includes the
 * shared logic (ppu_core.glsl) and calls ppu_pixel per fragment. GPL-3.0-or-later.
 *
 * Binding order (must match port_gpu_raster.cpp SSBO slots):
 *   set=2 b0 bg_palette, b1 io_per_line, b2 vram, b3 obj_palette, b4 oam,
 *   b5 affine_ref, b6 ws_shadow, b7 obj_cull; set=3 b0 params.
 */
#extension GL_GOOGLE_include_directive : require

layout(set = 2, binding = 0, std430) readonly buffer BgPalette { uint data[]; } bgpal;
layout(set = 2, binding = 1, std430) readonly buffer IoPerLine { uint data[]; } io;
layout(set = 2, binding = 2, std430) readonly buffer Vram { uint data[]; } vram;
layout(set = 2, binding = 3, std430) readonly buffer ObjPalette { uint data[]; } objpal;
layout(set = 2, binding = 4, std430) readonly buffer Oam { uint data[]; } oam;
layout(set = 2, binding = 5, std430) readonly buffer AffineRef { int data[]; } aff;
/* Widescreen Option A shadow tilemaps (4 BGs concatenated), rows=32,
 * cols=ws_cols. Halfword entries packed in u32 words. */
layout(set = 2, binding = 6, std430) readonly buffer WsShadow { uint data[]; } wss;
/* Per-line OBJ candidate lists (port_gpu_obj_cull): stride 129, slot 0 = count,
 * slots 1..count = OAM indices ascending. */
layout(set = 2, binding = 7, std430) readonly buffer ObjCull { uint data[]; } objcull;

layout(set = 3, binding = 0, std140) uniform Params {
    /* geom: x=frame_width, y=height, z=mode(GBA), w=affine(0/1) */
    ivec4 geom;
    /* misc: x=frame_dispcnt (frame-start DISPCNT; forced-blank + backdrop) */
    uvec4 misc;
    /* ws: x=bg_clip_x(240), y=ws_cols, z=hud_right_anchor, w=hud_right_native_x(176) */
    ivec4 ws;
    /* wsmsg: x=msg_shift, y=msg_x0, z=msg_x1, w=(msg_y0<<16 | msg_y1) */
    ivec4 wsmsg;
    /* wsbase: per-BG shadow_base_tile (x=bg0..w=bg3); a value <0 = no shadow */
    ivec4 wsbase;
} params;

layout(location = 0) out vec4 oColor;

#include "ppu_core.glsl"

void main() {
    /* params.misc.z = supersample scale S (target is S*W x S*H). Map this
     * fragment to its native pixel (nearest for tile/text) + sub-pixel offset
     * in [0,S) that the affine samplers use. S<=1 -> native, bit-exact. */
    int S = int(params.misc.z);
    if (S <= 1) {
        oColor = ppu_pixel(int(gl_FragCoord.x), int(gl_FragCoord.y));
        return;
    }
    gPpuScale = S;
    int fx = int(gl_FragCoord.x);
    int fy = int(gl_FragCoord.y);
    gSubX = fx % S;
    gSubY = fy % S;
    oColor = ppu_pixel(fx / S, fy / S);
}
