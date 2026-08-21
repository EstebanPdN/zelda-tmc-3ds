#define _POSIX_C_SOURCE 200809L
/* Host-side benchmark for the Old 3DS PICA200 command builder.
 *
 * Feeds a quick-dump memory snapshot (vram.bin / palettes.bin / oam.bin /
 * io-registers.bin, as written by L+R+A on the console) through
 * PpuGpu3DS_BuildCommands so the CPU cost of the geometry build can be
 * measured and profiled without a 3DS or an emulator in the loop.
 *
 *   port_ppu_gpu_3ds_bench <dump-dir> [iterations] [width]
 */

#include "port_ppu_gpu_3ds_model.h"
#include "virtuappu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    BENCH_MAX_VERTICES = 49152,
    BENCH_MAX_INDICES = 73728,
    BENCH_MAX_BATCHES = 4096,
    /* The per-band path needs far more room on an HDMA frame than the console
     * ships; PPU_BENCH_BIG gives it enough to build one anyway, so its output
     * can be diffed against the map-space path. 16-bit indices cap this. */
    BENCH_BIG_VERTICES = 65536,
    BENCH_BIG_INDICES = 98304,
};

/* ---------------------------------------------------------------------------
 * Rasterization record
 *
 * The refactors this bench guards (OAM prepass, band merging, map-space BG
 * geometry) all change HOW the command buffer is built while having to leave
 * what it paints identical. Comparing vertex counts is not enough and the
 * command buffer legitimately changes shape, so the bench replays the built
 * commands the way the Citro3D backend would and records, per pixel, what
 * would end up on screen: the winning layer, its priority and effect, and the
 * atlas texel actually sampled. Two builds that produce the same record paint
 * the same frame.
 * ------------------------------------------------------------------------ */
typedef struct RecordPixel {
    uint16_t texel;
    uint8_t layer, priority, effect, objectIndex;
    /* A blended layer is drawn twice -- once through the blender and once as
     * a stencil complement -- and which one survives depends on GPU stencil
     * state this replay does not model. Such pixels are excluded from the
     * software comparison rather than reported as false mismatches. */
    bool blendInvolved;
} RecordPixel;

static RecordPixel gRecord[MODE1_GBA_HEIGHT][512];

static void record_clear(void) {
    memset(gRecord, 0, sizeof(gRecord));
}

/* ---- stencil ------------------------------------------------------------
 * The backend gates most batches on an 8-bit stencil: bit 0x04 marks the OBJ
 * window, bit 0x08 marks "the pixel below is blend target 2". Replaying the
 * geometry without it compares the one part of the pipeline that was never in
 * doubt, so the passes below mirror port_ppu_gpu_3ds.c exactly.
 * ---------------------------------------------------------------------- */
enum { REC_KEEP, REC_REPLACE, REC_INVERT };
enum { REC_ALWAYS, REC_EQUAL };

typedef struct RecordStencil {
    bool enable;
    unsigned func, op;
    uint8_t ref, inputMask, writeMask;
    bool writeColor, alphaTest;
} RecordStencil;

static uint8_t gStencil[MODE1_GBA_HEIGHT][512];
static uint16_t gClearColor;
/* Mirrors sHasObjWindow in the backend. */
static bool gHasObjWindow;

/* Mirrors SetColorStencil(). */
static RecordStencil record_color_stencil(const PpuGpu3DSBatch* b) {
    unsigned region = b->color >> 14u;
    if (!gHasObjWindow && region < 2u) region = 2u;
    const bool alphaBranch = b->effect == PPU_GPU3DS_EFFECT_ALPHA ||
                             (b->color & (1u << 13u)) != 0;
    const bool oldTarget = b->effect == PPU_GPU3DS_EFFECT_ALPHA;
    unsigned mask = region < 2u ? 0x04u : 0u;
    unsigned ref = region == 1u ? 0x04u : 0u;
    RecordStencil st;
    memset(&st, 0, sizeof(st));
    if (alphaBranch) {
        mask |= 0x08u;
        if (oldTarget) ref |= 0x08u;
        st.op = (oldTarget == (b->target2 != 0)) ? REC_KEEP : REC_INVERT;
    } else {
        if (b->target2) ref |= 0x08u;
        st.op = REC_REPLACE;
    }
    st.enable = true;
    st.func = mask ? REC_EQUAL : REC_ALWAYS;
    st.ref = (uint8_t)ref;
    st.inputMask = (uint8_t)mask;
    st.writeMask = 0x08u;
    st.writeColor = true;
    st.alphaTest = true;
    return st;
}

/* Nearest-sample the atlas the way the GPU would for this quad. */
static uint16_t record_sample(const uint16_t* atlas, const PpuGpu3DSVertex* quad,
                              const PpuGpu3DSBatch* batch, float px, float py,
                              unsigned width, unsigned height) {
    /* The vertex shader adds the batch offset, so the record has to as well. */
    const float x0 = (quad[0].x + batch->offsetX + 1.0f) * 0.5f * (float)width;
    const float x1 = (quad[1].x + batch->offsetX + 1.0f) * 0.5f * (float)width;
    const float y0 = (1.0f - quad[0].y - batch->offsetY) * 0.5f * (float)height;
    const float y1 = (1.0f - quad[2].y - batch->offsetY) * 0.5f * (float)height;
    const float spanX = x1 - x0, spanY = y1 - y0;
    const float tx = spanX != 0.0f ? (px - x0) / spanX : 0.0f;
    const float ty = spanY != 0.0f ? (py - y0) / spanY : 0.0f;
    const float qu0 = PpuGpu3DS_UnpackUV(quad[0].u);
    const float qv0 = PpuGpu3DS_UnpackUV(quad[0].v);
    const float u = qu0 + (PpuGpu3DS_UnpackUV(quad[1].u) - qu0) * tx;
    const float v = qv0 + (PpuGpu3DS_UnpackUV(quad[2].v) - qv0) * ty;
    int atlasX = (int)(u * PPU_GPU3DS_ATLAS_SIDE);
    int atlasY = (int)((1.0f - v) * PPU_GPU3DS_ATLAS_SIDE);
    if (atlasX < 0) atlasX = 0;
    if (atlasY < 0) atlasY = 0;
    if (atlasX >= PPU_GPU3DS_ATLAS_SIDE) atlasX = PPU_GPU3DS_ATLAS_SIDE - 1;
    if (atlasY >= PPU_GPU3DS_ATLAS_SIDE) atlasY = PPU_GPU3DS_ATLAS_SIDE - 1;
    const unsigned slot = (unsigned)(atlasY / PPU_GPU3DS_TILE_SIDE) *
                                  (PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE) +
                          (unsigned)(atlasX / PPU_GPU3DS_TILE_SIDE);
    return atlas[(size_t)slot * 64u +
                 PpuGpu3DS_MortonIndex((unsigned)atlasX % PPU_GPU3DS_TILE_SIDE,
                                       (unsigned)atlasY % PPU_GPU3DS_TILE_SIDE)];
}

static void record_batch(const PpuGpu3DSCommandBuffer* cmd, const uint16_t* atlas,
                         const PpuGpu3DSBatch* batch, unsigned width,
                         unsigned height, const RecordStencil* st) {
    for (uint32_t index = 0; index + 5u < batch->indexCount; index += 6u) {
        const uint16_t base = cmd->indices[batch->firstIndex + index];
        const PpuGpu3DSVertex* quad = &cmd->vertices[base];
        float left = (quad[0].x + batch->offsetX + 1.0f) * 0.5f * (float)width;
        float right = (quad[1].x + batch->offsetX + 1.0f) * 0.5f * (float)width;
        float top = (1.0f - quad[0].y - batch->offsetY) * 0.5f * (float)height;
        float bottom = (1.0f - quad[2].y - batch->offsetY) * 0.5f * (float)height;
        if (right < left) { const float swap = left; left = right; right = swap; }
        if (bottom < top) { const float swap = top; top = bottom; bottom = swap; }
        int x0 = (int)(left + 0.5f), x1 = (int)(right + 0.5f);
        int y0 = (int)(top + 0.5f), y1 = (int)(bottom + 0.5f);
        if (x0 < (int)batch->scissorLeft) x0 = batch->scissorLeft;
        if (x1 > (int)batch->scissorRight) x1 = batch->scissorRight;
        if (y0 < (int)batch->firstLine) y0 = batch->firstLine;
        if (y1 > (int)(batch->firstLine + batch->lineCount))
            y1 = batch->firstLine + batch->lineCount;
        if (x1 > (int)width) x1 = (int)width;
        if (y1 > MODE1_GBA_HEIGHT) y1 = MODE1_GBA_HEIGHT;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const uint16_t texel =
                        record_sample(atlas, quad, batch, (float)x + 0.5f,
                                      (float)y + 0.5f, width, height);
                if (st->alphaTest && (texel & 1u) == 0u)
                    continue;  /* transparent */
                if (st->enable && st->func == REC_EQUAL &&
                    (gStencil[y][x] & st->inputMask) !=
                            (st->ref & st->inputMask))
                    continue;  /* rejected by the stencil test */
                if (st->op == REC_REPLACE)
                    gStencil[y][x] = (uint8_t)((gStencil[y][x] & ~st->writeMask) |
                                               (st->ref & st->writeMask));
                else if (st->op == REC_INVERT)
                    gStencil[y][x] ^= st->writeMask;
                if (!st->writeColor) continue;
                if ((texel & 1u) == 0u) continue;
                const bool blend =
                        batch->effect == PPU_GPU3DS_EFFECT_ALPHA ||
                        (batch->color & (1u << 13u)) != 0 ||
                        batch->semiTransparent || batch->target2;
                gRecord[y][x] = (RecordPixel){ texel,
                                               batch->layer,
                                               batch->priority,
                                               batch->effect,
                                               batch->objectIndex,
                                               blend || gRecord[y][x].blendInvolved };
            }
        }
    }
}

/* Batch 0 is the backdrop clear; object-window batches only write stencil. */
static void record_frame(const PpuGpu3DSCommandBuffer* cmd, const uint16_t* atlas,
                         unsigned width, unsigned height) {
    record_clear();
    memset(gStencil, 0, sizeof(gStencil));
    gClearColor = cmd->batches[0].color;
    gHasObjWindow = false;
    for (size_t i = 1; i < cmd->batchCount; ++i)
        if (cmd->batches[i].objWindow) { gHasObjWindow = true; break; }
    RecordStencil st;

    /* Pass 1: object-window batches write bit 0x04 and no colour. */
    for (size_t i = 1; i < cmd->batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &cmd->batches[i];
        if (!batch->objWindow) continue;
        memset(&st, 0, sizeof(st));
        st.enable = true;
        st.func = REC_ALWAYS;
        st.ref = 0x04u;
        st.inputMask = 0xffu;
        st.writeMask = 0x04u;
        st.op = REC_REPLACE;
        st.alphaTest = true;
        record_batch(cmd, atlas, batch, width, height, &st);
    }

    /* The OBJ depth prepass writes only depth, which this replay does not
     * model; it cannot affect the stencil. */

    /* Pass 3: colour. */
    for (size_t i = 1; i < cmd->batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &cmd->batches[i];
        if (batch->objWindow) continue;
        if (batch->layer == PPU_GPU3DS_BACKDROP) {
            memset(&st, 0, sizeof(st));
            st.enable = true;
            st.func = REC_ALWAYS;
            st.ref = 0x08u;
            st.inputMask = 0u;
            st.writeMask = 0x08u;
            st.op = REC_REPLACE;
            st.writeColor = true;
            st.alphaTest = false;  /* the backdrop pass disables the alpha test */
            record_batch(cmd, atlas, batch, width, height, &st);
            continue;
        }
        st = record_color_stencil(batch);
        record_batch(cmd, atlas, batch, width, height, &st);
    }
}

static uint64_t record_hash(void) {
    const uint8_t* bytes = (const uint8_t*)gRecord;
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof(gRecord); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/* ---------------------------------------------------------------------------
 * Software-rasterizer comparison
 *
 * The record diff compares the map-space path against the per-band path --
 * both of which are this GPU model. A fault they share is invisible to it, and
 * the console proved that: parity against the software rasterizer failed by
 * thousands of pixels on a screen this bench called identical. This renders
 * the same state through the software PPU, the renderer the port must match,
 * and reports where the GPU model's sampled texel disagrees.
 * ------------------------------------------------------------------------ */
static uint32_t gSoftwarePixels[MODE1_GBA_HEIGHT * 512];

static int compare_against_software(PpuGpu3DSFrameView* frame,
                                    const uint16_t* atlas, unsigned width) {
    PPUMemory ppu;
    memset(&ppu, 0, sizeof(ppu));
    ppu.frame_width = (uint16_t)width;
    ppu.frame_pitch = 512;
    ppu.mode = (frame->frameDispcnt & 7u) == 0u ? 1 : 2;

    virtuappu_mode1_bind_gba_memory(&frame->memory);
    virtuappu_mode1_set_output_buffer(gSoftwarePixels, 512);
    virtuappu_mode1_set_color_correction(false);
    virtuappu_mode1_render_frame(&ppu);

    unsigned differing = 0, firstX = 0, firstY = 0;
    unsigned skipEffect = 0, skipBlend = 0, empty = 0, uncovered = 0;
    unsigned firstUx = 0, firstUy = 0;
    unsigned byLayer[8] = { 0 };
    for (unsigned y = 0; y < MODE1_GBA_HEIGHT; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const uint32_t abgr = gSoftwarePixels[(size_t)y * 512u + x];
            const uint16_t expected = PpuGpu3DS_PackAbgr8888(abgr);
            const RecordPixel got = gRecord[y][x];
            /* Compare the opaque colour the GPU model would sample; effects
             * are applied by the GPU, so only effect-free pixels are exact. */
            if (got.effect != PPU_GPU3DS_EFFECT_NONE) { ++skipEffect; continue; }
            if (got.blendInvolved) { ++skipBlend; continue; }
            /* An empty pixel is not "no data" -- it is the clear colour, which
             * is what the console actually shows there. Comparing it against
             * the software frame is the whole point: content the stencil
             * rejected shows up here and nowhere else. */
            if (got.texel == 0u) {
                ++empty;
                if ((gClearColor & ~1u) != (expected & ~1u)) {
                    if (uncovered == 0) { firstUx = x; firstUy = y; }
                    ++uncovered;
                }
                continue;
            }
            if ((got.texel & ~1u) == (expected & ~1u)) continue;
            if (differing == 0) { firstX = x; firstY = y; }
            if (differing < 6u)
                printf("    mismatch %3u,%3u layer=%u pri=%u software=%04x gpu=%04x\n",
                       x, y, got.layer, got.priority, expected, got.texel);
            ++differing;
            if (got.layer < 8u) ++byLayer[got.layer];
        }
    }
    {
        const char* out = getenv("PPU_BENCH_IMAGES");
        if (out) {
            char path[256];
            snprintf(path, sizeof(path), "%s-software.bin", out);
            FILE* f = fopen(path, "wb");
            if (f) { fwrite(gSoftwarePixels, 4, MODE1_GBA_HEIGHT * 512, f); fclose(f); }
            snprintf(path, sizeof(path), "%s-gpu.bin", out);
            f = fopen(path, "wb");
            if (f) { fwrite(gRecord, sizeof(gRecord), 1, f); fclose(f); }
        }
    }
    printf("software comparison: %u differing pixels", differing);
    if (differing) {
        printf(", first at %u,%u; by layer:", firstX, firstY);
        for (unsigned layer = 0; layer < 6u; ++layer)
            if (byLayer[layer]) printf(" L%u=%u", layer, byLayer[layer]);
    }
    printf("\n");
    printf("  skipped: effect=%u blend=%u empty=%u | UNCOVERED=%u", skipEffect,
           skipBlend, empty, uncovered);
    if (uncovered) printf(" first at %u,%u", firstUx, firstUy);
    printf("\n");
    return (differing == 0 && uncovered == 0) ? 0 : 1;
}

static bool read_file(const char* dir, const char* name, void* out, size_t bytes) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    const size_t read = fread(out, 1, bytes, file);
    fclose(file);
    if (read != bytes) {
        fprintf(stderr, "%s: expected %zu bytes, got %zu\n", path, bytes, read);
        return false;
    }
    return true;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int compare_double(const void* left, const void* right) {
    const double a = *(const double*)left, b = *(const double*)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* Retention regression sweep.
 *
 * Walks VRAM a tile at a time, edits it the way the game animates graphics in
 * place, and rebuilds the frame twice so the second build is the one that may
 * reuse geometry. The result must equal what the per-band path produces from
 * the same memory. Any offset where they differ is a frame the console would
 * have drawn stale -- the flicker this is here to prevent.
 */
static int sweep_retention(PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                           uint16_t* atlas, PpuGpu3DSVertex* vertices,
                           uint16_t* indices, PpuGpu3DSBatch* batches,
                           size_t vertexCapacity, size_t indexCapacity,
                           uint8_t* vram, unsigned step) {
    PpuGpu3DSCommandBuffer command;
    unsigned checked = 0, stale = 0, firstStale = 0;
    for (unsigned offset = 0; offset < MODE1_VRAM_SIZE; offset += step) {
        vram[offset] ^= 0xffu;

        uint64_t hashes[2];
        for (unsigned pass = 0; pass < 2u; ++pass) {
            PpuGpu3DS_SetMapSpaceEnabled(pass == 0u);
            PpuGpu3DS_CacheInit(cache);
            memset(atlas, 0, (size_t)PPU_GPU3DS_ATLAS_PIXELS * sizeof(*atlas));
            for (unsigned build = 0; build < 2u; ++build) {
                PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity,
                                      indices, indexCapacity, batches,
                                      BENCH_MAX_BATCHES);
                PpuGpu3DS_CacheBeginFrame(cache, frame->memory.bg_palette,
                                          frame->memory.obj_palette, build + 1u);
                if (!PpuGpu3DS_BuildCommands(frame, cache, atlas, &command)) {
                    command.batchCount = 0;
                    break;
                }
            }
            record_clear();
            if (command.batchCount != 0)
                record_frame(&command, atlas, frame->width, MODE1_GBA_HEIGHT);
            hashes[pass] = record_hash();
        }

        vram[offset] ^= 0xffu;
        ++checked;
        if (hashes[0] != hashes[1]) {
            if (stale == 0) firstStale = offset;
            ++stale;
        }
    }
    PpuGpu3DS_SetMapSpaceEnabled(true);
    printf("retention sweep: %u tiles checked, %u stale\n", checked, stale);
    if (stale != 0) printf("  first stale VRAM offset 0x%05x\n", firstStale);
    return stale == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <dump-dir> [iterations] [width] [hdma-bands]\n",
                argv[0]);
        return 2;
    }
    const char* dir = argv[1];
    const unsigned iterations = argc > 2 ? (unsigned)atoi(argv[2]) : 200u;
    const unsigned width = argc > 3 ? (unsigned)atoi(argv[3]) : 240u;
    /* Per-line scroll writes are what the game's HDMA effects produce, and
     * they are the case that overruns the command buffers on hardware. */
    const unsigned hdmaBands = argc > 4 ? (unsigned)atoi(argv[4]) : 0u;

    static uint8_t vram[MODE1_VRAM_SIZE];
    static uint16_t palettes[MODE1_PALETTE_COLORS * 2];
    static uint16_t oam[MODE1_IO_MEM_SIZE / 2];
    static uint8_t io[MODE1_IO_MEM_SIZE];
    if (!read_file(dir, "vram.bin", vram, sizeof(vram)) ||
        !read_file(dir, "palettes.bin", palettes, sizeof(palettes)) ||
        !read_file(dir, "oam.bin", oam, sizeof(oam)) ||
        !read_file(dir, "io-registers.bin", io, sizeof(io)))
        return 1;

    /* A dump taken by a build that records per-line registers replays the real
     * HDMA frame; otherwise every line replays the single captured register
     * set, which is the cheapest possible band layout. */
    static uint8_t ioPerLine[MODE1_GBA_HEIGHT][MODE1_IO_MEM_SIZE];
    static uint16_t dispcntPerLine[MODE1_GBA_HEIGHT];
    static int32_t affineRefX[MODE1_GBA_HEIGHT], affineRefY[MODE1_GBA_HEIGHT];
    static int32_t affineRef[MODE1_GBA_HEIGHT * 2];
    const bool perLine =
            read_file(dir, "io-per-line.bin", ioPerLine, sizeof(ioPerLine)) &&
            read_file(dir, "dispcnt-per-line.bin", dispcntPerLine,
                      sizeof(dispcntPerLine));
    if (perLine && read_file(dir, "affine-ref.bin", affineRef, sizeof(affineRef))) {
        memcpy(affineRefX, affineRef, sizeof(affineRefX));
        memcpy(affineRefY, affineRef + MODE1_GBA_HEIGHT, sizeof(affineRefY));
    }
    const uint16_t dispcnt = perLine
                                     ? dispcntPerLine[0]
                                     : (uint16_t)(io[0] | ((uint16_t)io[1] << 8u));
    for (unsigned line = 0; perLine ? false : line < MODE1_GBA_HEIGHT; ++line) {
        memcpy(ioPerLine[line], io, sizeof(io));
        dispcntPerLine[line] = dispcnt;
        if (hdmaBands > 1u) {
            const unsigned step = (MODE1_GBA_HEIGHT + hdmaBands - 1u) / hdmaBands;
            const uint16_t scroll = (uint16_t)(line / (step ? step : 1u));
            ioPerLine[line][MODE1_IO_BG0HOFS] = (uint8_t)scroll;
            ioPerLine[line][MODE1_IO_BG0HOFS + 1] = (uint8_t)(scroll >> 8u);
        }
    }

    PpuGpu3DSFrameView frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = width;
    frame.height = MODE1_GBA_HEIGHT;
    /* The runtime maps GBA modes 1 and 2 onto the affine path, and the
     * affine reference point advances per line, which is what splits a frame
     * into bands here -- not per-line scroll. */
    frame.affine = (dispcnt & 7u) == 1u || (dispcnt & 7u) == 2u;
    frame.ioUniform = !perLine && hdmaBands <= 1u;
    frame.frameDispcnt = dispcnt;
    /* Both renderers must see the same registers. The dump's io-registers.bin
     * is sampled when the dump is written, which is after the frame was built,
     * so the per-line snapshot is the authority here. */
    frame.memory.io_mem = perLine ? &ioPerLine[0][0] : io;
    frame.memory.vram = vram;
    frame.memory.bg_palette = palettes;
    frame.memory.obj_palette = palettes + MODE1_PALETTE_COLORS;
    frame.memory.oam_mem = oam;
    frame.ioPerLine = &ioPerLine[0][0];
    frame.dispcntPerLine = dispcntPerLine;
    frame.affineRefX = affineRefX;
    frame.affineRefY = affineRefY;
    frame.wsCols = 4;
    for (unsigned bg = 0; bg < 4; ++bg) frame.wsShadowBaseTile[bg] = -1;

    PpuGpu3DSCache* cache = malloc(sizeof(*cache));
    uint16_t* atlas = malloc((size_t)PPU_GPU3DS_ATLAS_PIXELS * sizeof(*atlas));
    const bool bigBuffers = getenv("PPU_BENCH_BIG") != NULL;
    const size_t vertexCapacity =
            bigBuffers ? BENCH_BIG_VERTICES : BENCH_MAX_VERTICES;
    const size_t indexCapacity =
            bigBuffers ? BENCH_BIG_INDICES : BENCH_MAX_INDICES;
    PpuGpu3DSVertex* vertices = malloc(vertexCapacity * sizeof(*vertices));
    uint16_t* indices = malloc(indexCapacity * sizeof(*indices));
    PpuGpu3DSBatch* batches = malloc(BENCH_MAX_BATCHES * sizeof(*batches));
    if (!cache || !atlas || !vertices || !indices || !batches) return 1;
    PpuGpu3DS_FillStaticIndices(indices, indexCapacity);
    PpuGpu3DS_SetMapSpaceEnabled(getenv("PPU_BENCH_NO_MAPSPACE") == NULL);
    PpuGpu3DS_CacheInit(cache);
    memset(atlas, 0, (size_t)PPU_GPU3DS_ATLAS_PIXELS * sizeof(*atlas));

    PpuGpu3DSBand bands[MODE1_GBA_HEIGHT];
    printf("dispcnt=%04x mode=%u width=%u bands=%zu source=%s\n", dispcnt,
           dispcnt & 7u, width, PpuGpu3DS_BuildBands(&frame, bands),
           perLine ? "recorded per-line registers" : "synthetic");

    if (getenv("PPU_BENCH_HAZARDS")) {
        PpuGpu3DSCommandBuffer command;
        PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                              indexCapacity, batches, BENCH_MAX_BATCHES);
        PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                  frame.memory.obj_palette, 1u);
        if (!PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command)) {
            printf("build failed %u\n", command.failReason); return 1;
        }
        unsigned empty = 0, underflow = 0;
        /* The draw loops start at batch 1, so batch 0 never reaches DrawBatch. */
        for (size_t i = 1; i < command.batchCount; ++i) {
            const PpuGpu3DSBatch* b = &command.batches[i];
            if (b->indexCount == 0) {
                if (empty < 4)
                    printf("  EMPTY batch %zu L%u p%u line=%u+%u\n", i, b->layer,
                           b->priority, b->firstLine, b->lineCount);
                ++empty;
            }
            /* top = yOffset + height - (firstLine + lineCount) as u32 */
            if ((unsigned)b->firstLine + b->lineCount > MODE1_GBA_HEIGHT) {
                if (underflow < 4)
                    printf("  SCISSOR UNDERFLOW batch %zu L%u line=%u+%u > %u\n",
                           i, b->layer, b->firstLine, b->lineCount,
                           MODE1_GBA_HEIGHT);
                ++underflow;
            }
        }
        printf("batches=%zu emptyDraws=%u scissorUnderflows=%u\n",
               command.batchCount, empty, underflow);
        return (empty || underflow) ? 1 : 0;
    }

    if (getenv("PPU_BENCH_BOUNDS")) {
        /* A PICA200 that never retires a command list is often fetching vertex
         * data outside what was written. Check every index every batch draws
         * against the vertices the builder actually produced, and against the
         * flushed region. */
        PpuGpu3DSCommandBuffer command;
        PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                              indexCapacity, batches, BENCH_MAX_BATCHES);
        PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                  frame.memory.obj_palette, 1u);
        if (!PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command)) {
            printf("build failed, reason %u\n", command.failReason);
            return 1;
        }
        unsigned bad = 0, maxIndex = 0;
        size_t worstBatch = 0;
        for (size_t i = 0; i < command.batchCount; ++i) {
            const PpuGpu3DSBatch* b = &command.batches[i];
            if (b->firstIndex + b->indexCount > command.indexCapacity) {
                printf("batch %zu: index range %u+%u exceeds capacity %zu\n", i,
                       b->firstIndex, b->indexCount, command.indexCapacity);
                ++bad;
                continue;
            }
            for (uint32_t k = 0; k < b->indexCount; ++k) {
                const uint16_t v = command.indices[b->firstIndex + k];
                if (v > maxIndex) { maxIndex = v; worstBatch = i; }
                if ((size_t)v >= command.vertexCount) {
                    if (bad < 6)
                        printf("batch %zu L%u idx[%u]=%u >= vertexCount %zu "
                               "(UNWRITTEN VERTEX)\n", i, b->layer, k, v,
                               command.vertexCount);
                    ++bad;
                }
            }
        }
        printf("vertices=%zu indices=%zu batches=%zu maxIndex=%u (batch %zu) "
               "out-of-range=%u\n", command.vertexCount, command.indexCount,
               command.batchCount, maxIndex, worstBatch, bad);
        return bad ? 1 : 0;
    }

    if (getenv("PPU_BENCH_LRU")) {
        /* A hang on hardware with audio still running points at a loop that
         * never ends. The eviction walk follows lruPrev from the tail, so a
         * cycle or a dangling link in that list spins forever. Build frames
         * under churn and check the list after every one. */
        PpuGpu3DSCommandBuffer command;
        const unsigned frames = 400u;
        for (unsigned i = 0; i < frames; ++i) {
            for (unsigned long t = 0; t < 400ul; ++t) {
                const size_t offset = (size_t)(t * 32u + (i & 31u) * 2048u) %
                                      (MODE1_VRAM_SIZE - 1u);
                vram[offset] ^= 0xffu;
            }
            PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                                  indexCapacity, batches, BENCH_MAX_BATCHES);
            PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                      frame.memory.obj_palette, i + 1u);
            PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command);

            /* Forward from head and backward from tail must both terminate,
             * visit the same count, and agree with each other. */
            unsigned forward = 0, backward = 0;
            uint16_t slot = cache->lruHead, prev = PPU_GPU3DS_CACHE_NIL;
            while (slot != PPU_GPU3DS_CACHE_NIL) {
                if (cache->entries[slot].lruPrev != prev) {
                    printf("frame %u: broken back-link at slot %u\n", i, slot);
                    return 1;
                }
                if (cache->entries[slot].pinned) {
                    printf("frame %u: PINNED slot %u is on the LRU list\n", i, slot);
                    return 1;
                }
                prev = slot;
                slot = cache->entries[slot].lruNext;
                if (++forward > PPU_GPU3DS_SLOT_COUNT) {
                    printf("frame %u: CYCLE walking forward from head\n", i);
                    return 1;
                }
            }
            if (prev != cache->lruTail) {
                printf("frame %u: tail is %u but forward walk ended at %u\n", i,
                       cache->lruTail, prev);
                return 1;
            }
            slot = cache->lruTail;
            while (slot != PPU_GPU3DS_CACHE_NIL) {
                slot = cache->entries[slot].lruPrev;
                if (++backward > PPU_GPU3DS_SLOT_COUNT) {
                    printf("frame %u: CYCLE walking backward from tail -- this is "
                           "the eviction walk, and it would never return\n", i);
                    return 1;
                }
            }
            if (forward != backward) {
                printf("frame %u: forward %u != backward %u\n", i, forward, backward);
                return 1;
            }
        }
        printf("LRU list intact across %u frames (list length %u)\n", frames,
               (unsigned)0);
        return 0;
    }

    if (getenv("PPU_BENCH_REGIONS")) {
        /* Regions 0 and 1 gate on stencil bit 0x04, which only OBJWIN batches
         * ever write. A frame that asks for those regions without emitting an
         * OBJWIN batch fails the stencil everywhere it is tested. */
        PpuGpu3DSCommandBuffer command;
        PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                              indexCapacity, batches, BENCH_MAX_BATCHES);
        PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                  frame.memory.obj_palette, 1u);
        if (!PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command)) {
            printf("build failed, reason %u\n", command.failReason);
            return 1;
        }
        unsigned perRegion[4] = { 0, 0, 0, 0 };
        unsigned objwin = 0, region1Indices = 0;
        for (size_t i = 1; i < command.batchCount; ++i) {
            const PpuGpu3DSBatch* b = &command.batches[i];
            if (b->objWindow) { objwin += 1u; continue; }
            const unsigned region = b->color >> 14u;
            perRegion[region] += 1u;
            if (region < 2u) region1Indices += b->indexCount;
        }
        for (size_t i = 0; i < command.batchCount; ++i) {
            const PpuGpu3DSBatch* b = &command.batches[i];
            const bool alphaBranch =
                    b->effect == PPU_GPU3DS_EFFECT_ALPHA ||
                    (b->color & (1u << 13u)) != 0;
            if (b->layer == PPU_GPU3DS_BACKDROP || alphaBranch || b->target2)
                printf("  b%zu L%u p%u idx=%u+%u eff=%u t2=%u region=%u "
                       "alphaBranch=%d eva=%u evb=%u evy=%u\n",
                       i, b->layer, b->priority, b->firstIndex, b->indexCount,
                       b->effect, b->target2, b->color >> 14u, (int)alphaBranch,
                       b->eva, b->evb, b->evy);
        }
        printf("clear=%04x\n", command.batches[0].color);
        for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
            const uint32_t first = cache->retained[bg].tileFirst;
            const uint32_t last = cache->retained[bg].tileLast;
            if (last > first)
                printf("retain bg%u: [%u..%u] %u bytes, %u slots\n", bg, first,
                       last, last - first, cache->retained[bg].slotCount);
        }
        {
            static const char* kReject[] = { "affine", "control", "screenSpace",
                                             "disabled", "tooLarge", "coverage" };
            printf("mapReject:");
            for (unsigned r = 0; r < PPU_GPU3DS_MAP_REJECT_COUNT; ++r)
                if (command.mapReject[r])
                    printf(" %s=%u", kReject[r], command.mapReject[r]);
            printf("\n");
        }
        printf("batches=%zu objwin=%u region0=%u region1=%u region2=%u "
               "region3=%u gatedIndices=%u\n",
               command.batchCount, objwin, perRegion[0], perRegion[1],
               perRegion[2], perRegion[3], region1Indices);
        if ((perRegion[0] || perRegion[1]) && objwin == 0u)
            printf("STENCIL HAZARD: %u window-gated batches, no OBJWIN writer\n",
                   perRegion[0] + perRegion[1]);
        return 0;
    }

    if (getenv("PPU_BENCH_SOFTWARE")) {
        PpuGpu3DSCommandBuffer command;
        PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                              indexCapacity, batches, BENCH_MAX_BATCHES);
        PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                  frame.memory.obj_palette, 1u);
        if (!PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command)) {
            printf("build failed, reason %u\n", command.failReason);
            return 1;
        }
        record_frame(&command, atlas, width, MODE1_GBA_HEIGHT);
        return compare_against_software(&frame, atlas, width);
    }

    if (getenv("PPU_BENCH_SWEEP")) {
        const unsigned step = (unsigned)atoi(getenv("PPU_BENCH_SWEEP"));
        /* The per-band path needs more room than the console ships to build an
         * HDMA frame at all; without it every comparison would be against an
         * empty frame. */
        PpuGpu3DSVertex* big = malloc(BENCH_BIG_VERTICES * sizeof(*big));
        uint16_t* bigIndices = malloc(BENCH_BIG_INDICES * sizeof(*bigIndices));
        if (!big || !bigIndices) return 1;
        /* This buffer is separate from the one filled in main, and indices are
         * static now, so it needs the pattern too. */
        PpuGpu3DS_FillStaticIndices(bigIndices, BENCH_BIG_INDICES);
        return sweep_retention(&frame, cache, atlas, big, bigIndices, batches,
                               BENCH_BIG_VERTICES, BENCH_BIG_INDICES, vram,
                               step ? step : 32u);
    }

    double* samples = malloc(iterations * sizeof(*samples));
    if (!samples) return 1;
    unsigned failures = 0;
    PpuGpu3DSCommandBuffer command;
    for (unsigned i = 0; i < iterations; ++i) {
        PpuGpu3DS_CommandInit(&command, vertices, vertexCapacity, indices,
                              indexCapacity, batches, BENCH_MAX_BATCHES);
        /* PPU_BENCH_MUTATE=<vram offset> rewrites a byte between frames, the
         * way the game animates tiles in place. Retained geometry that does
         * not notice is exactly the stale-frame bug this guards against. */
        /* PPU_BENCH_CHURN=<n> dirties one byte in each of n consecutive tiles
         * per frame, the way the game animates tile data in place. It forces
         * about n re-decodes, so the bench can replay the mix the console
         * actually runs (~95 decodes against ~520 hits) instead of only the
         * fully-warm or fully-cold extremes. */
        const char* churn = getenv("PPU_BENCH_CHURN");
        if (churn && i != 0) {
            const unsigned long tiles = strtoul(churn, NULL, 0);
            for (unsigned long t = 0; t < tiles; ++t) {
                const size_t offset = (size_t)(t * 32u + (i & 31u) * 2048u) %
                                      (MODE1_VRAM_SIZE - 1u);
                vram[offset] ^= 0xffu;
            }
        }
        const char* mutate = getenv("PPU_BENCH_MUTATE");
        if (mutate && i != 0) {
            const unsigned long offset = strtoul(mutate, NULL, 0);
            if (offset < MODE1_VRAM_SIZE) vram[offset] ^= 0xffu;
        }
        /* PPU_BENCH_COLD empties the tile cache between frames. Replaying one
         * static frame otherwise warms the cache after the first iteration and
         * measures a decode rate no console ever sees: hardware sustains ~95
         * decodes per frame, this bench does ~1.5. */
        if (getenv("PPU_BENCH_COLD")) PpuGpu3DS_CacheInit(cache);
        PpuGpu3DS_CacheBeginFrame(cache, frame.memory.bg_palette,
                                  frame.memory.obj_palette, i + 1u);
        const double start = now_seconds();
        const bool ok = PpuGpu3DS_BuildCommands(&frame, cache, atlas, &command);
        samples[i] = (now_seconds() - start) * 1000.0;
        if (!ok) ++failures;
    }

#ifdef PPU_GPU3DS_PROFILE
    {
        static const char* names[PPU_GPU3DS_PHASE_COUNT] = {
            "bands", "merge", "maps", "mapsig", "mapretain", "scene",
            "  objwin", "regions", "bg", "obj"
        };
        printf("  phases (ms/frame):");
        for (unsigned phase = 0; phase < PPU_GPU3DS_PHASE_COUNT; ++phase)
            printf("  %s %.4f", names[phase],
                   gPpuGpu3DSPhase[phase] / (double)iterations);
        printf("\n");
    }
#endif
    qsort(samples, iterations, sizeof(*samples), compare_double);
    double total = 0.0;
    for (unsigned i = 0; i < iterations; ++i) total += samples[i];
    printf("build: %u iterations, %u failed\n", iterations, failures);
    printf("  mean %.3f ms  median %.3f ms  min %.3f ms  max %.3f ms\n",
           total / iterations, samples[iterations / 2], samples[0],
           samples[iterations - 1]);
    printf("  last frame: %zu vertices, %zu indices, %zu batches (bands %u)\n",
           command.vertexCount, command.indexCount, command.batchCount,
           command.bandCount);
    printf("  wanted %u vertices, fail reason %u\n", command.requiredVertices,
           command.failReason);
    if (getenv("PPU_BENCH_BATCHES")) {
        for (size_t i = 0; i < command.batchCount && i < 8u; ++i) {
            const PpuGpu3DSBatch* b = &command.batches[i];
            printf("    batch %zu layer=%u pri=%u first=%u count=%u line=%u+%u off=(%.4f,%.4f)\n",
                   i, b->layer, b->priority, b->firstIndex, b->indexCount,
                   b->firstLine, b->lineCount, b->offsetX, b->offsetY);
        }
        printf("    mapLayerMask=%u mapDirtyMask=%u dynFirstV=%u\n",
               command.mapLayerMask, command.mapDirtyMask,
               command.dynamicFirstVertex);
    }
    printf("  map layers %u, rejects a/c/s/o/l/v %u/%u/%u/%u/%u/%u\n",
           command.mapLayerMask, command.mapReject[0], command.mapReject[1],
           command.mapReject[2], command.mapReject[3], command.mapReject[4],
           command.mapReject[5]);
    printf("  cache: %llu hits, %llu decodes\n",
           (unsigned long long)cache->hits, (unsigned long long)cache->decodes);
    if (command.batchCount != 0) {
        record_frame(&command, atlas, width, MODE1_GBA_HEIGHT);
        printf("  raster record: %016llx\n",
               (unsigned long long)record_hash());
        const char* recordPath = getenv("PPU_BENCH_RECORD");
        if (recordPath) {
            FILE* file = fopen(recordPath, "wb");
            if (file) {
                fwrite(gRecord, 1, sizeof(gRecord), file);
                fclose(file);
                printf("  raster record written to %s\n", recordPath);
            }
        }
    }
    printf("  Old 3DS budget is 16.67 ms per frame for EVERYTHING;\n"
           "  an Old 3DS ARM11 core is roughly 20-40x slower than this host.\n");
    return 0;
}
