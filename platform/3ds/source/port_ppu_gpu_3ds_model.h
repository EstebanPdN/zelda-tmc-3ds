#pragma once

#include "cpu/mode1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PPU_GPU3DS_ATLAS_SIDE = 512,
    PPU_GPU3DS_TILE_SIDE = 8,
    PPU_GPU3DS_SLOT_COUNT = 4096,
    PPU_GPU3DS_ATLAS_PIXELS = PPU_GPU3DS_ATLAS_SIDE * PPU_GPU3DS_ATLAS_SIDE
};

typedef enum PpuGpu3DSLayer {
    PPU_GPU3DS_BG0,
    PPU_GPU3DS_BG1,
    PPU_GPU3DS_BG2,
    PPU_GPU3DS_BG3,
    PPU_GPU3DS_OBJ,
    PPU_GPU3DS_BACKDROP
} PpuGpu3DSLayer;

typedef enum PpuGpu3DSPaletteDomain {
    PPU_GPU3DS_PALETTE_BG,
    PPU_GPU3DS_PALETTE_OBJ
} PpuGpu3DSPaletteDomain;

typedef struct PpuGpu3DSTileKey {
    uint32_t vramOffset;
    uint8_t paletteBank;
    bool bpp8;
    PpuGpu3DSPaletteDomain domain;
} PpuGpu3DSTileKey;

typedef struct PpuGpu3DSCacheEntry {
    PpuGpu3DSTileKey key;
    uint8_t source[64];
    uint32_t paletteGeneration;
    uint32_t lastUseFrame;
    bool valid;
    bool pinned;
    bool dirty;
} PpuGpu3DSCacheEntry;

typedef struct PpuGpu3DSCache {
    PpuGpu3DSCacheEntry entries[PPU_GPU3DS_SLOT_COUNT];
    uint16_t bgPalette[MODE1_PALETTE_COLORS];
    uint16_t objPalette[MODE1_PALETTE_COLORS];
    uint32_t bgBankGeneration[16];
    uint32_t objBankGeneration[16];
    uint32_t bg256Generation;
    uint32_t obj256Generation;
    uint32_t frame;
} PpuGpu3DSCache;

typedef struct PpuGpu3DSFrameView {
    unsigned width, height;
    bool affine, ioUniform;
    uint16_t frameDispcnt;
    VirtuaPPUMode1GbaMemory memory;
    const uint8_t* ioPerLine;
    const uint16_t* dispcntPerLine;
    const int32_t* affineRefX;
    const int32_t* affineRefY;
    const uint16_t* wsShadow;
    int wsShadowBaseTile[4];
    int wsCols, wsShadowHalfwords;
    int wsHudRightAnchor, wsHudRightNativeX;
    int wsMsgShift, wsMsgX0, wsMsgX1, wsMsgY0, wsMsgY1;
    bool objClipEnable;
    const uint8_t* objClipMark;
    int objClipY;
} PpuGpu3DSFrameView;

typedef struct PpuGpu3DSInterval {
    uint16_t left, right;
} PpuGpu3DSInterval;

typedef struct PpuGpu3DSBand {
    uint16_t firstLine, lineCount;
    uint8_t ioRow;
} PpuGpu3DSBand;

typedef struct PpuGpu3DSVertex {
    float x, y, z, w, u, v;
} PpuGpu3DSVertex;


typedef enum PpuGpu3DSEffect {
    PPU_GPU3DS_EFFECT_NONE,
    PPU_GPU3DS_EFFECT_ALPHA,
    PPU_GPU3DS_EFFECT_BRIGHTEN,
    PPU_GPU3DS_EFFECT_DARKEN
} PpuGpu3DSEffect;

typedef struct PpuGpu3DSBatch {
    uint32_t firstIndex, indexCount;
    uint16_t firstLine, lineCount, scissorLeft, scissorRight;
    uint8_t layer, priority, windowControl, target2;
    uint8_t effect, eva, evb, evy, objectIndex;
    uint16_t color;
    bool objWindow, semiTransparent;
} PpuGpu3DSBatch;

typedef struct PpuGpu3DSCommandBuffer {
    PpuGpu3DSVertex* vertices;
    uint16_t* indices;
    PpuGpu3DSBatch* batches;
    size_t vertexCount, vertexCapacity;
    size_t indexCount, indexCapacity;
    size_t batchCount, batchCapacity;
} PpuGpu3DSCommandBuffer;

void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache);
void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette,
                               const uint16_t* objPalette, uint32_t frame);
bool PpuGpu3DS_CacheTile(PpuGpu3DSCache* cache, const uint8_t* vram, PpuGpu3DSTileKey key,
                         uint16_t* atlas, uint16_t* outSlot);
uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y);
uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque);
int32_t PpuGpu3DS_AffineSample(int32_t reference, int16_t coefficient,
                               int screenCoordinate);
int PpuGpu3DS_RemapBgX(const PpuGpu3DSFrameView* frame, unsigned bg,
                       unsigned line, int nativeX);
bool PpuGpu3DS_ShadowEntry(const PpuGpu3DSFrameView* frame, unsigned bg,
                           unsigned row, unsigned column, uint16_t* entry);
size_t PpuGpu3DS_BuildBands(const PpuGpu3DSFrameView* frame, PpuGpu3DSBand out[160]);
size_t PpuGpu3DS_WindowIntervals(unsigned left, unsigned right, unsigned width,
                                 PpuGpu3DSInterval out[2]);
void PpuGpu3DS_CommandInit(PpuGpu3DSCommandBuffer* cmd, PpuGpu3DSVertex* vertices,
                           size_t vertexCapacity, uint16_t* indices, size_t indexCapacity,
                           PpuGpu3DSBatch* batches, size_t batchCapacity);
bool PpuGpu3DS_CommandReserve(PpuGpu3DSCommandBuffer* cmd, size_t vertices, size_t indices,
                              size_t batches);
bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                             uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd);
