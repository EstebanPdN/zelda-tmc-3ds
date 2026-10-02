/*
 * tools/ppu_gpu_composite_poc.c — Phase 6b PoC (increment 1: priority only).
 *
 * Proves a GPU compositor reproduces the priority-resolution core of the
 * software composite (virtuappu_mode1_composite_line) bit-identically, with
 * windows and blend disabled. Builds synthetic overlapping BG/OBJ layers +
 * BGxCNT priorities, runs the CPU oracle for one scanline, runs the GPU
 * compute compositor, compares every pixel.
 *
 * Headless: needs SDL_VIDEODRIVER=offscreen.
 *
 * Build:
 *   glslangValidator -V tools/ppu_gpu_composite.comp -o /tmp/ppu_gpu_composite.spv
 *   cc -O2 -fopenmp -I port/ppu/include -DMODE1_GBA_WIDTH=240 \
 *      tools/ppu_gpu_composite_poc.c port/ppu/src/mode1.c port/ppu/src/virtuappu.c \
 *      $(pkg-config --cflags --libs sdl3) -o /tmp/ppu_gpu_composite_poc -lm
 * Run:
 *   SDL_VIDEODRIVER=offscreen /tmp/ppu_gpu_composite_poc /tmp/ppu_gpu_composite.spv
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/mode1.h"
#include "virtuappu.h"

#define W MODE1_GBA_WIDTH

static uint8_t  s_io[0x400];
static uint8_t  s_vram[0x18000];
static uint16_t s_bgpal[256];
static uint16_t s_objpal[256];
static uint16_t s_oam[512];

static uint32_t bg_layers[MODE1_GBA_BG_COUNT][W];
static uint8_t  bg_prio[MODE1_GBA_BG_COUNT][W];   /* ignored by composite, required by signature */
static uint32_t obj_layer[W];
static uint8_t  obj_priority[W];

static void put16(int off, uint16_t v) { s_io[off] = v & 0xFF; s_io[off + 1] = v >> 8; }

static SDL_GPUBuffer *mkbuf(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage, uint32_t size) {
    SDL_GPUBufferCreateInfo bci; memset(&bci, 0, sizeof bci);
    bci.usage = usage; bci.size = size;
    return SDL_CreateGPUBuffer(dev, &bci);
}

static void upload(SDL_GPUDevice *dev, SDL_GPUCopyPass *cp, SDL_GPUBuffer *buf,
                   const void *data, uint32_t size) {
    SDL_GPUTransferBufferCreateInfo tci; memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; tci.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tci);
    void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
    memcpy(m, data, size);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_GPUTransferBufferLocation s; memset(&s, 0, sizeof s); s.transfer_buffer = tb;
    SDL_GPUBufferRegion d; memset(&d, 0, sizeof d); d.buffer = buf; d.size = size;
    SDL_UploadToGPUBuffer(cp, &s, &d, false);
}

int main(int argc, char **argv) {
    const char *spv_path = argc > 1 ? argv[1] : "/tmp/ppu_gpu_composite.spv";

    /* ---- synthetic scene ---- */
    memset(s_io, 0, sizeof s_io);
    memset(s_bgpal, 0, sizeof s_bgpal);
    s_bgpal[0] = 0x0421;                       /* backdrop */
    /* BG priorities: BG0=3, BG1=1, BG2=1, BG3=2 (tie at pri1 between BG1<BG2). */
    put16(MODE1_IO_BG0CNT, 3);
    put16(MODE1_IO_BG1CNT, 1);
    put16(MODE1_IO_BG2CNT, 1);
    put16(MODE1_IO_BG3CNT, 2);

    VirtuaPPUMode1GbaMemory mem = { s_io, s_vram, s_bgpal, s_objpal, s_oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;

    for (int x = 0; x < W; ++x) {
        bg_layers[0][x] = (x < 200)            ? 0xFF0000AAu : 0u;
        bg_layers[1][x] = (x >= 50)            ? 0xFF00AA00u : 0u;
        bg_layers[2][x] = (x % 3 != 0)         ? 0xFFAA0000u : 0u; /* gaps -> transparency */
        bg_layers[3][x] = (x >= 100)           ? 0xFFAAAA00u : 0u;
        obj_layer[x]    = (x >= 80 && x < 160) ? 0xFF00AAAAu : 0u;
        obj_priority[x] = (x < 120) ? 0 : 2;   /* obj above all left half, mid-priority right */
    }

    /* DISPCNT: BG0-3 + OBJ on, all windows off. */
    uint16_t dispcnt = (uint16_t)(MODE1_DISP_BG0_ON | MODE1_DISP_BG1_ON | MODE1_DISP_BG2_ON |
                                  MODE1_DISP_BG3_ON | MODE1_DISP_OBJ_ON);

    /* ---- CPU oracle (line 0) ---- */
    virtuappu_mode1_composite_line(0, bg_layers, bg_prio, obj_layer, obj_priority, dispcnt);
    uint32_t cpu_row[W];
    memcpy(cpu_row, &virtuappu_frame_buffer[0], sizeof cpu_row);

    /* host-side priority sort (must match composite_line exactly) */
    int bg_order[4] = {0, 1, 2, 3}, bg_order_pri[4];
    for (int i = 0; i < 4; ++i)
        bg_order_pri[i] = s_io[MODE1_IO_BG0CNT + i * 2] & 3;
    for (int i = 1; i < 4; ++i) {
        int key = bg_order[i], kp = bg_order_pri[key], j = i - 1;
        while (j >= 0 && bg_order_pri[bg_order[j]] > kp) { bg_order[j + 1] = bg_order[j]; --j; }
        bg_order[j + 1] = key;
    }

    /* ---- GPU compositor ---- */
    if (!SDL_Init(SDL_INIT_VIDEO)) { printf("SDL_Init: %s\n", SDL_GetError()); return 2; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!dev) { printf("CreateGPUDevice: %s (try SDL_VIDEODRIVER=offscreen)\n", SDL_GetError()); return 2; }
    size_t spv_size = 0; void *spv = SDL_LoadFile(spv_path, &spv_size);
    if (!spv) { printf("load spv: %s\n", SDL_GetError()); return 2; }

    SDL_GPUComputePipelineCreateInfo pci; memset(&pci, 0, sizeof pci);
    pci.code = spv; pci.code_size = spv_size; pci.entrypoint = "main";
    pci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    pci.num_readonly_storage_buffers = 3; pci.num_readwrite_storage_buffers = 1;
    pci.num_uniform_buffers = 1; pci.threadcount_x = 64; pci.threadcount_y = 1; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { printf("CreateGPUComputePipeline: %s\n", SDL_GetError()); return 2; }

    /* flatten bg planes [b*W + x]; obj priority -> uint */
    static uint32_t bg_flat[MODE1_GBA_BG_COUNT * W];
    for (int b = 0; b < 4; ++b) for (int x = 0; x < W; ++x) bg_flat[b * W + x] = bg_layers[b][x];
    static uint32_t objp32[W]; for (int x = 0; x < W; ++x) objp32[x] = obj_priority[x];

    SDL_GPUBuffer *bg_buf   = mkbuf(dev, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, sizeof bg_flat);
    SDL_GPUBuffer *obj_buf  = mkbuf(dev, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, sizeof obj_layer);
    SDL_GPUBuffer *objp_buf = mkbuf(dev, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, sizeof objp32);
    SDL_GPUBuffer *out_buf  = mkbuf(dev, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, W * 4);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    upload(dev, cp, bg_buf, bg_flat, sizeof bg_flat);
    upload(dev, cp, obj_buf, obj_layer, sizeof obj_layer);
    upload(dev, cp, objp_buf, objp32, sizeof objp32);
    SDL_EndGPUCopyPass(cp);

    struct { int bg_enabled[4]; int bg_order[4]; int bg_order_pri[4];
             int width; int obj_enabled; uint32_t backdrop; int pad; } ubo;
    memset(&ubo, 0, sizeof ubo);
    for (int b = 0; b < 4; ++b) {
        ubo.bg_enabled[b] = (dispcnt >> (8 + b)) & 1;
        ubo.bg_order[b] = bg_order[b];
        ubo.bg_order_pri[b] = bg_order_pri[b];
    }
    ubo.width = W; ubo.obj_enabled = (dispcnt & MODE1_DISP_OBJ_ON) ? 1 : 0;
    ubo.backdrop = virtuappu_mode1_rgb555_to_abgr8888(s_bgpal[0]);
    SDL_PushGPUComputeUniformData(cmd, 0, &ubo, sizeof ubo);

    SDL_GPUStorageBufferReadWriteBinding rw; memset(&rw, 0, sizeof rw); rw.buffer = out_buf;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, pipe);
    SDL_GPUBuffer *ro[3] = { bg_buf, obj_buf, objp_buf };
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro, 3);
    SDL_DispatchGPUCompute(pass, (W + 63) / 64, 1, 1);
    SDL_EndGPUComputePass(pass);

    SDL_GPUTransferBufferCreateInfo dci; memset(&dci, 0, sizeof dci);
    dci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; dci.size = W * 4;
    SDL_GPUTransferBuffer *dl = SDL_CreateGPUTransferBuffer(dev, &dci);
    SDL_GPUCopyPass *cp2 = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUBufferRegion bsrc; memset(&bsrc, 0, sizeof bsrc); bsrc.buffer = out_buf; bsrc.size = W * 4;
    SDL_GPUTransferBufferLocation bdst; memset(&bdst, 0, sizeof bdst); bdst.transfer_buffer = dl;
    SDL_DownloadFromGPUBuffer(cp2, &bsrc, &bdst);
    SDL_EndGPUCopyPass(cp2);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);

    uint32_t *gpu = SDL_MapGPUTransferBuffer(dev, dl, false);
    int mism = 0, first = -1;
    for (int x = 0; x < W; ++x)
        if (cpu_row[x] != gpu[x]) { if (first < 0) first = x; mism++; }
    SDL_UnmapGPUTransferBuffer(dev, dl);

    printf("width=%d  mismatches=%d\n", W, mism);
    if (first >= 0)
        printf("first mismatch @ x=%d: cpu=0x%08x gpu=0x%08x\n", first, cpu_row[first], gpu[first]);
    printf(mism == 0 ? "GPU composite PoC: BIT-EXACT MATCH vs CPU oracle (priority resolution)\n"
                     : "GPU composite PoC: MISMATCH\n");
    return mism == 0 ? 0 : 1;
}
