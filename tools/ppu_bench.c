/*
 * tools/ppu_bench.c — standalone microbenchmark for the ViruaPPU mode1
 * software rasterizer, the port's #1 CPU hotspot (~80% of frame time, see
 * the TMC_PROFILE phase timer in port/port_bios.c).
 *
 * It feeds a real in-game PPU snapshot (IO + VRAM + BG/OBJ palette + OAM,
 * captured by the TMC_PERFCAP harness in port/port_repro_perfcap.c) straight
 * to the render functions with no engine running, so render cost is measured
 * in isolation and optimizations can be A/B'd against a byte-exact framebuffer
 * checksum (the GBA-parity guard: the optimized render MUST produce the same
 * checksum as the baseline).
 *
 * Build (match the game's flags):
 *   gcc -O3 -mavx2 -mfma -fopenmp -I port/ppu/include \
 *       -DMODE1_GBA_WIDTH=240 tools/ppu_bench.c port/ppu/src/mode1.c \
 *       -o /tmp/ppu_bench -lm
 *
 * Run:
 *   /tmp/ppu_bench /tmp/tmc_ppu_snapshot.bin [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "cpu/mode1.h"
#include "virtuappu.h"

/* mode1.c references this extern (defined in virtuappu.c in the real build). */
uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];

static uint8_t s_io[0x400];
static uint8_t s_vram[0x18000];
static uint16_t s_bgpal[256];
static uint16_t s_objpal[256];
static uint16_t s_oam[512];

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint64_t fb_checksum(int w, int h) {
    uint64_t hsh = 1469598103934665603ULL; /* FNV-1a 64 */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = virtuappu_frame_buffer[(size_t)y * MODE1_GBA_WIDTH + x];
            hsh ^= v;
            hsh *= 1099511628211ULL;
        }
    }
    return hsh;
}

static int load_snapshot(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror("open snapshot");
        return 0;
    }
    char magic[4];
    uint32_t sz[5];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "PPU1", 4) != 0) {
        fprintf(stderr, "bad snapshot magic\n");
        fclose(f);
        return 0;
    }
    if (fread(sz, sizeof(uint32_t), 5, f) != 5) {
        fclose(f);
        return 0;
    }
    size_t n = 0;
    n += fread(s_io, 1, 0x400, f);
    n += fread(s_vram, 1, 0x18000, f);
    n += fread(s_bgpal, 1, 0x200, f);
    n += fread(s_objpal, 1, 0x200, f);
    n += fread(s_oam, 1, 0x400, f);
    fclose(f);
    return n == (0x400 + 0x18000 + 0x200 + 0x200 + 0x400);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/tmp/tmc_ppu_snapshot.bin";
    int iters = argc > 2 ? atoi(argv[2]) : 2000;
    if (iters < 1)
        iters = 1;
    if (!load_snapshot(path))
        return 1;

    VirtuaPPUMode1GbaMemory mem = { s_io, s_vram, s_bgpal, s_objpal, s_oam };
    virtuappu_mode1_bind_gba_memory(&mem);
    virtuappu_mode1_pre_line_callback = NULL;

    uint16_t dispcnt = (uint16_t)(s_io[0] | (s_io[1] << 8));
    int bg_on[4] = { (dispcnt >> 8) & 1, (dispcnt >> 9) & 1, (dispcnt >> 10) & 1, (dispcnt >> 11) & 1 };
    int obj_on = (dispcnt >> 12) & 1;
    int obj_1d = (dispcnt >> 6) & 1;
    int bg_count = bg_on[0] + bg_on[1] + bg_on[2] + bg_on[3];

#ifdef _OPENMP
    int maxthreads = omp_get_max_threads();
#else
    int maxthreads = 1;
#endif

    printf("snapshot: dispcnt=0x%04x  BG0=%d BG1=%d BG2=%d BG3=%d OBJ=%d  width=%d  iters=%d\n", dispcnt, bg_on[0],
           bg_on[1], bg_on[2], bg_on[3], obj_on, MODE1_GBA_WIDTH, iters);

    /* render_frame dereferences ppu->mode (mode1/mode2 merge); build a real
     * PPUMemory from the snapshot dispcnt (BG mode = low 3 bits) at native
     * geometry. Mode 0/1 -> tiled path, mode 2 -> affine BG2. */
    PPUMemory ppu = { 0 };
    ppu.frame_width = MODE1_GBA_WIDTH;
    ppu.frame_pitch = MODE1_GBA_WIDTH;
    ppu.mode = (uint8_t)(dispcnt & 0x7u);

    /* Warm caches + capture the parity checksum. */
    for (int i = 0; i < 8; i++)
        virtuappu_mode1_render_frame(&ppu);
    uint64_t csum = fb_checksum(MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT);

    /* Full frame, all threads. */
    double t0 = now_ms();
    for (int i = 0; i < iters; i++)
        virtuappu_mode1_render_frame(&ppu);
    double mt = (now_ms() - t0) / iters;

    /* Full frame, single thread (models a P4 / Pi3 core). */
#ifdef _OPENMP
    omp_set_num_threads(1);
#endif
    for (int i = 0; i < 8; i++)
        virtuappu_mode1_render_frame(&ppu);
    t0 = now_ms();
    for (int i = 0; i < iters; i++)
        virtuappu_mode1_render_frame(&ppu);
    double st = (now_ms() - t0) / iters;
#ifdef _OPENMP
    omp_set_num_threads(maxthreads);
#endif

    /* Per-component serial breakdown (single thread): replicate render_frame's
     * per-line buffers and time each stage separately. */
    double bg_ms = 0, obj_ms = 0, comp_ms = 0;
    {
        static uint32_t bg_layers[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH];
        static uint8_t bg_priority[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH];
        static uint32_t obj_layer[MODE1_GBA_WIDTH];
        static uint8_t obj_priority[MODE1_GBA_WIDTH];
        int citers = iters / 4 ? iters / 4 : 1;
        for (int it = 0; it < citers; it++) {
            for (int line = 0; line < MODE1_GBA_HEIGHT; line++) {
                memset(bg_layers, 0, sizeof(bg_layers));
                memset(bg_priority, 0, sizeof(bg_priority));
                memset(obj_layer, 0, sizeof(obj_layer));
                memset(obj_priority, 0xFF, sizeof(obj_priority));
                double a = now_ms();
                for (int b = 0; b < 4; b++)
                    if (bg_on[b])
                        virtuappu_mode1_render_text_bg_line(b, line, bg_layers[b], bg_priority[b]);
                double b1 = now_ms();
                if (obj_on)
                    virtuappu_mode1_render_obj_line(line, obj_1d, obj_layer, obj_priority);
                double c1 = now_ms();
                virtuappu_mode1_composite_line(line, bg_layers, bg_priority, obj_layer, obj_priority, dispcnt);
                double d1 = now_ms();
                bg_ms += b1 - a;
                obj_ms += c1 - b1;
                comp_ms += d1 - c1;
            }
        }
        bg_ms /= citers;
        obj_ms /= citers;
        comp_ms /= citers;
    }

    printf("checksum     : 0x%016llx\n", (unsigned long long)csum);
    printf("render_frame : %.4f ms/frame  (%.0f fps)   [%d threads]\n", mt, mt > 0 ? 1000.0 / mt : 0, maxthreads);
    printf("render_frame : %.4f ms/frame  (%.0f fps)   [1 thread]\n", st, st > 0 ? 1000.0 / st : 0);
    printf("per-component (1 thread, serial):\n");
    printf("  bg_line x%-2d : %.4f ms/frame\n", bg_count, bg_ms);
    printf("  obj_line    : %.4f ms/frame\n", obj_ms);
    printf("  composite   : %.4f ms/frame\n", comp_ms);
    printf("  (sum)       : %.4f ms/frame\n", bg_ms + obj_ms + comp_ms);
    return 0;
}
