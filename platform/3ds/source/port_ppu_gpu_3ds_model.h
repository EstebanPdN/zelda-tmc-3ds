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

typedef enum PpuGpu3DSPaletteDomain {
    PPU_GPU3DS_BG,
    PPU_GPU3DS_OBJ
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

void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache);
void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette,
                               const uint16_t* objPalette, uint32_t frame);
bool PpuGpu3DS_CacheTile(PpuGpu3DSCache* cache, const uint8_t* vram, PpuGpu3DSTileKey key,
                         uint16_t* atlas, uint16_t* outSlot);
uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y);
uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque);
