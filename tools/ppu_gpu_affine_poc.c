/*
 * tools/ppu_gpu_affine_poc.c — Phase 6 GPU PoC.
 *
 * Proves the GPU can rasterize GBA affine OBJ sprites bit-identically to the
 * CPU software PPU. Builds a synthetic affine-sprite scene, runs the CPU oracle
 * (virtuappu_mode1_render_affine_obj_overlay) and an SDL_GPU compute port
 * (ppu_gpu_affine.comp) at the same upscale factor, and compares every pixel.
 *
 * Headless: needs SDL_VIDEODRIVER=offscreen (dummy exposes no GPU backend).
 *
 * Build:
 *   glslangValidator -V tools/ppu_gpu_affine.comp -o /tmp/ppu_gpu_affine.spv
 *   cc -O2 -fopenmp -I port/ppu/include -DMODE1_GBA_WIDTH=240 \
 *      tools/ppu_gpu_affine_poc.c port/ppu/src/mode1.c port/ppu/src/virtuappu.c \
 *      $(pkg-config --cflags --libs sdl3) -o /tmp/ppu_gpu_affine_poc -lm
 * Run:
 *   SDL_VIDEODRIVER=offscreen /tmp/ppu_gpu_affine_poc /tmp/ppu_gpu_affine.spv
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/mode1.h"
#include "virtuappu.h"

#define SCALE 2
#define VPW   240
#define DSTW  (VPW * SCALE)
#define DSTH  (MODE1_GBA_HEIGHT * SCALE)
#define VRAM_BYTES 0x18000
#define OBJ_TILE_BASE 0x10000

/* Synthetic GBA memory. */
static uint8_t  s_io[0x400];
static uint8_t  s_vram[VRAM_BYTES];
static uint16_t s_bgpal[256];
static uint16_t s_objpal[256];
static uint16_t s_oam[512];

static int g_obj1d = 0; /* set from argv: 0 = 2D OBJ mapping, 1 = 1D */

static void build_scene(void) {
    memset(s_io, 0, sizeof s_io);
    /* DISPCNT: OBJ on (0x1000); bit6 (0x40) selects 1D OBJ tile mapping. */
    s_io[0] = (uint8_t)(g_obj1d ? 0x40 : 0x00);
    s_io[1] = 0x10;

    /* OBJ tile data: deterministic pattern across the whole OBJ region; some
     * nibbles are 0 (transparent) to exercise the skip path. */
    for (int n = 0; n < VRAM_BYTES - OBJ_TILE_BASE; ++n)
        s_vram[OBJ_TILE_BASE + n] = (uint8_t)((n * 7 + 3) & 0xFF);

    for (int k = 0; k < 256; ++k) s_objpal[k] = (uint16_t)((k * 0x123) & 0x7FFF);

    memset(s_oam, 0, sizeof s_oam);
    /* OAM[0]: 16x16 (shape 0, size 1), 4bpp, affine, palette bank 1, at (120,100). */
    s_oam[0] = (uint16_t)(100 | (1u << 8));          /* attr0: y, affine */
    s_oam[1] = (uint16_t)(120 | (1u << 14));         /* attr1: x, size=1, affine_index 0 */
    s_oam[2] = (uint16_t)(0 | (1u << 12));           /* attr2: tile 0, palette 1 */
    /* Affine group 0 = ~30deg rotation, 8.8 fixed (pa,pb,pc,pd at +3,+7,+11,+15). */
    s_oam[0 * 16 + 3]  = (uint16_t)221;              /* pa =  cos */
    s_oam[0 * 16 + 7]  = (uint16_t)(int16_t)-128;    /* pb = -sin */
    s_oam[0 * 16 + 11] = (uint16_t)128;              /* pc =  sin */
    s_oam[0 * 16 + 15] = (uint16_t)221;              /* pd =  cos */

    /* OAM[1]: 32x32 (shape 0, size 2), 8bpp, affine + DOUBLE-size, at (40,30),
     * affine group 1 = ~0.75x scale. Exercises 8bpp sampling, double-size
     * bounds, and a second affine group. (Mapping 1D vs 2D is a frame-wide
     * DISPCNT bit, covered by a separate run.) */
    s_oam[4] = (uint16_t)(30 | (1u << 8) | (1u << 9) | (1u << 13)); /* y, affine, double, 8bpp */
    s_oam[5] = (uint16_t)(40 | (2u << 14) | (1u << 9));            /* x, size=2, affine_index 1 */
    s_oam[6] = (uint16_t)(8);                                       /* tile 8 */
    s_oam[1 * 16 + 3]  = (uint16_t)341;              /* pa = 1/0.75 ~ 1.333 * 256 */
    s_oam[1 * 16 + 7]  = (uint16_t)0;                /* pb */
    s_oam[1 * 16 + 11] = (uint16_t)0;               /* pc */
    s_oam[1 * 16 + 15] = (uint16_t)341;             /* pd */
}

static SDL_GPUBuffer *upload_buffer(SDL_GPUDevice *dev, SDL_GPUCommandBuffer *cmd,
                                    SDL_GPUCopyPass *cp, SDL_GPUBufferUsageFlags usage,
                                    const void *data, uint32_t size) {
    SDL_GPUBufferCreateInfo bci; memset(&bci, 0, sizeof bci);
    bci.usage = usage; bci.size = size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(dev, &bci);
    SDL_GPUTransferBufferCreateInfo tci; memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; tci.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tci);
    void *map = SDL_MapGPUTransferBuffer(dev, tb, false);
    memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_GPUTransferBufferLocation src; memset(&src, 0, sizeof src);
    src.transfer_buffer = tb; src.offset = 0;
    SDL_GPUBufferRegion dst; memset(&dst, 0, sizeof dst);
    dst.buffer = buf; dst.offset = 0; dst.size = size;
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    (void)cmd;
    return buf; /* transfer buffer leaked intentionally (process-lifetime PoC) */
}

int main(int argc, char **argv) {
    const char *spv_path = argc > 1 ? argv[1] : "/tmp/ppu_gpu_affine.spv";
    g_obj1d = (argc > 2) ? atoi(argv[2]) : 0;

    build_scene();

    /* ---- CPU oracle ---- */
    VirtuaPPUMode1GbaMemory mem = { s_io, s_vram, s_bgpal, s_objpal, s_oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;
    static uint32_t cpu_dst[DSTW * DSTH];
    memset(cpu_dst, 0, sizeof cpu_dst);
    virtuappu_mode1_render_affine_obj_overlay(cpu_dst, DSTW, DSTH, SCALE);

    /* ---- GPU port ---- */
    if (!SDL_Init(SDL_INIT_VIDEO)) { printf("SDL_Init: %s\n", SDL_GetError()); return 2; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) { printf("CreateGPUDevice: %s (try SDL_VIDEODRIVER=offscreen)\n", SDL_GetError()); return 2; }

    size_t spv_size = 0;
    void *spv = SDL_LoadFile(spv_path, &spv_size);
    if (!spv) { printf("load spv %s: %s\n", spv_path, SDL_GetError()); return 2; }

    SDL_GPUComputePipelineCreateInfo pci; memset(&pci, 0, sizeof pci);
    pci.code = (const Uint8 *)spv; pci.code_size = spv_size;
    pci.entrypoint = "main"; pci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    pci.num_readonly_storage_buffers = 3;
    pci.num_readwrite_storage_buffers = 1;
    pci.num_uniform_buffers = 1;
    pci.threadcount_x = 8; pci.threadcount_y = 8; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { printf("CreateGPUComputePipeline: %s\n", SDL_GetError()); return 2; }

    /* host-side packed inputs: one u16 per uint for oam/pal; raw bytes for vram */
    static uint32_t oam_u32[512]; for (int k = 0; k < 512; ++k) oam_u32[k] = s_oam[k];
    static uint32_t pal_u32[256]; for (int k = 0; k < 256; ++k) pal_u32[k] = s_objpal[k];

    SDL_GPUBufferCreateInfo obci; memset(&obci, 0, sizeof obci);
    obci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; obci.size = DSTW * DSTH * 4;
    SDL_GPUBuffer *out_buf = SDL_CreateGPUBuffer(dev, &obci);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUBuffer *oam_buf = upload_buffer(dev, cmd, cp, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, oam_u32, sizeof oam_u32);
    SDL_GPUBuffer *vram_buf = upload_buffer(dev, cmd, cp, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, s_vram, sizeof s_vram);
    SDL_GPUBuffer *pal_buf = upload_buffer(dev, cmd, cp, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, pal_u32, sizeof pal_u32);
    SDL_EndGPUCopyPass(cp);

    struct { int scale, dst_w, dst_h, viewport_width, obj_1d, obj_tile_base; } ubo =
        { SCALE, DSTW, DSTH, VPW, g_obj1d, OBJ_TILE_BASE };
    SDL_PushGPUComputeUniformData(cmd, 0, &ubo, sizeof ubo);

    SDL_GPUStorageBufferReadWriteBinding rw; memset(&rw, 0, sizeof rw);
    rw.buffer = out_buf;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, pipe);
    SDL_GPUBuffer *ro[3] = { oam_buf, vram_buf, pal_buf };
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro, 3);
    SDL_DispatchGPUCompute(pass, (DSTW + 7) / 8, (DSTH + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUTransferBufferCreateInfo dci; memset(&dci, 0, sizeof dci);
    dci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; dci.size = DSTW * DSTH * 4;
    SDL_GPUTransferBuffer *dl = SDL_CreateGPUTransferBuffer(dev, &dci);
    SDL_GPUCopyPass *cp2 = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUBufferRegion bsrc; memset(&bsrc, 0, sizeof bsrc);
    bsrc.buffer = out_buf; bsrc.offset = 0; bsrc.size = DSTW * DSTH * 4;
    SDL_GPUTransferBufferLocation bdst; memset(&bdst, 0, sizeof bdst); bdst.transfer_buffer = dl; bdst.offset = 0;
    SDL_DownloadFromGPUBuffer(cp2, &bsrc, &bdst);
    SDL_EndGPUCopyPass(cp2);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);

    uint32_t *gpu = (uint32_t *)SDL_MapGPUTransferBuffer(dev, dl, false);

    /* ---- compare ---- */
    int mismatches = 0, cpu_nonzero = 0, gpu_nonzero = 0, first = -1;
    for (int p = 0; p < DSTW * DSTH; ++p) {
        if (cpu_dst[p]) cpu_nonzero++;
        if (gpu[p]) gpu_nonzero++;
        if (cpu_dst[p] != gpu[p]) { if (first < 0) first = p; mismatches++; }
    }
    SDL_UnmapGPUTransferBuffer(dev, dl);

    printf("scale=%d dst=%dx%d  cpu_nonzero=%d gpu_nonzero=%d  mismatches=%d\n",
           SCALE, DSTW, DSTH, cpu_nonzero, gpu_nonzero, mismatches);
    if (first >= 0)
        printf("first mismatch @ (%d,%d): cpu=0x%08x gpu=0x%08x\n",
               first % DSTW, first / DSTW, cpu_dst[first], gpu[first]);
    int ok = (mismatches == 0 && cpu_nonzero > 0);
    printf(ok ? "GPU PoC: BIT-EXACT MATCH vs CPU oracle (%d sprite px)\n"
              : "GPU PoC: MISMATCH\n", cpu_nonzero);
    return ok ? 0 : 1;
}
