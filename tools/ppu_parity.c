/*
 * tools/ppu_parity.c — mode-aware static PPU parity oracle (Tier A).
 *
 * Loads a captured PPU snapshot (the "PPU1" format written by the
 * TMC_PERFCAP harness in port/port_repro_perfcap.c), dispatches to the
 * mode1 or mode2 renderer EXACTLY like port_ppu.cpp's GBA->VPPU mode
 * mapping (GBA mode 0 -> VPPU mode 1; GBA modes 1/2 -> VPPU mode 2),
 * renders one frame from the final register state, and prints the FNV-1a
 * 64-bit framebuffer checksum.
 *
 * This is the byte-exact regression oracle for the PPU ownership/refactor
 * work: the same snapshot + same renderer source MUST produce the same
 * checksum. Unlike tools/ppu_bench.c (a mode1-only microbenchmark), this
 * tool covers BOTH renderers and exercises the real virtuappu_render_frame
 * dispatch.
 *
 * Static limitation: a snapshot is a single final-state capture, so this
 * oracle does NOT replay per-scanline HBlank-DMA register changes (the
 * pre-line callback is left NULL). Per-scanline HDMA effects are covered by
 * the end-to-end Tier B oracle (the frame hash emitted by TMC_PERFCAP).
 *
 * Build / run: see tools/ppu_parity_check.sh (links virtuappu.c + all
 * mode*.c, matching the game's flags).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cpu/mode1.h"
#include "virtuappu.h"

/* virtuappu_frame_buffer / virtuappu_vram / virtuappu_registers are defined
 * in virtuappu.c, which this tool links (so the dispatch path is identical
 * to the game build). */

static uint8_t  s_io[0x400];
static uint8_t  s_vram[0x18000];
static uint16_t s_bgpal[256];
static uint16_t s_objpal[256];
static uint16_t s_oam[512];

static uint64_t fb_checksum(int w, int h, int pitch) {
    uint64_t hsh = 1469598103934665603ULL; /* FNV-1a 64 */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = virtuappu_frame_buffer[(size_t)y * pitch + x];
            hsh ^= v;
            hsh *= 1099511628211ULL;
        }
    }
    return hsh;
}

static int write_ppm(const char* path, int w, int h, int pitch) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror("open output");
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t abgr = virtuappu_frame_buffer[(size_t)y * pitch + x];
            uint8_t rgb[3] = {
                (uint8_t)(abgr & 0xffu),
                (uint8_t)((abgr >> 8u) & 0xffu),
                (uint8_t)((abgr >> 16u) & 0xffu),
            };
            if (fwrite(rgb, 1, sizeof(rgb), f) != sizeof(rgb)) {
                fclose(f);
                return 0;
            }
        }
    }
    return fclose(f) == 0;
}

static int load_snapshot(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror("open snapshot"); return 0; }
    char magic[4];
    uint32_t sz[5];
    if (fread(magic, 1, 4, f) != 4 ||
        (memcmp(magic, "PPU1", 4) != 0 && memcmp(magic, "TMCP", 4) != 0)) {
        fprintf(stderr, "bad snapshot magic\n");
        fclose(f);
        return 0;
    }
    if (fread(sz, sizeof(uint32_t), 5, f) != 5) { fclose(f); return 0; }
    size_t n = 0;
    n += fread(s_io,    1, 0x400,   f);
    n += fread(s_vram,  1, 0x18000, f);
    n += fread(s_bgpal, 1, 0x200,   f);
    n += fread(s_objpal,1, 0x200,   f);
    n += fread(s_oam,   1, 0x400,   f);
    fclose(f);
    return n == (0x400 + 0x18000 + 0x200 + 0x200 + 0x400);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/tmp/tmc_ppu_snapshot.bin";
    if (!load_snapshot(path)) return 2;

    VirtuaPPUMode1GbaMemory mem = { s_io, s_vram, s_bgpal, s_objpal, s_oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL; /* static: no HDMA replay */

    uint16_t dispcnt = (uint16_t)(s_io[0] | (s_io[1] << 8));
    int gba_mode = dispcnt & 0x7;
    /* Mirror port_ppu.cpp's GBA->VPPU mode routing. */
    uint8_t vppu_mode = (gba_mode == 1 || gba_mode == 2) ? 2 : 1;

    virtuappu_registers.mode = vppu_mode;
    virtuappu_registers.frame_width = MODE1_GBA_WIDTH;
    virtuappu_registers.frame_pitch = MODE1_GBA_WIDTH;

    virtuappu_render_frame();

    uint64_t csum = fb_checksum(MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT, MODE1_GBA_WIDTH);

    if (argc > 2 && !write_ppm(argv[2], MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT, MODE1_GBA_WIDTH)) {
        return 3;
    }

    fprintf(stderr, "snapshot=%s dispcnt=0x%04x gba_mode=%d vppu_mode=%u width=%d\n",
            path, dispcnt, gba_mode, (unsigned)vppu_mode, MODE1_GBA_WIDTH);
    printf("0x%016llx\n", (unsigned long long)csum);
    return 0;
}
