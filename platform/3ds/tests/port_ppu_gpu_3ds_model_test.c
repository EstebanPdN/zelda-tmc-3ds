#include "port_ppu_gpu_3ds_model.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                 \
    do {                                                                                            \
        if (!(expr)) {                                                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);                               \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

static PpuGpu3DSTileKey cache_key(unsigned index) {
    return (PpuGpu3DSTileKey){
        .vramOffset = (index % (MODE1_VRAM_SIZE / 32u)) * 32u,
        .paletteBank = (uint8_t)(index / (MODE1_VRAM_SIZE / 32u)),
        .bpp8 = false,
        .domain = PPU_GPU3DS_BG,
    };
}

int main(void) {
    static uint8_t vram[MODE1_VRAM_SIZE];
    static uint16_t bg[MODE1_PALETTE_COLORS];
    static uint16_t obj[MODE1_PALETTE_COLORS];
    static uint16_t atlas[PPU_GPU3DS_ATLAS_PIXELS];
    static PpuGpu3DSCache cache;
    uint16_t slot0;
    uint16_t slot1;

    CHECK(PpuGpu3DS_MortonIndex(0, 0) == 0);
    CHECK(PpuGpu3DS_MortonIndex(1, 0) == 1);
    CHECK(PpuGpu3DS_MortonIndex(0, 1) == 2);
    CHECK(PpuGpu3DS_MortonIndex(2, 0) == 4);
    CHECK(PpuGpu3DS_MortonIndex(7, 7) == 63);

    CHECK(PpuGpu3DS_PackRgba5551(0x001f, true) == 0xf801);
    CHECK(PpuGpu3DS_PackRgba5551(0x03e0, true) == 0x07c1);
    CHECK(PpuGpu3DS_PackRgba5551(0x7c00, true) == 0x003f);
    CHECK(PpuGpu3DS_PackRgba5551(0x7fff, false) == 0xfffe);

    bg[1] = 0x001f;
    bg[2] = 0x03e0;
    bg[17] = 0x03e0;
    vram[0] = 0x21;
    PpuGpu3DS_CacheInit(&cache);
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 1);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot0));
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(0, 0)] == 0xf801);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(1, 0)] == 0x07c1);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(2, 0)] == 0x0000);

    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot1));
    CHECK(slot1 == slot0);

    bg[17] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 2);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot1));
    CHECK(slot1 == slot0);

    bg[1] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 3);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot1));
    CHECK(atlas[(size_t)slot1 * 64] == 0x003f);

    obj[1] = 0x001f;
    obj[17] = 0x03e0;
    vram[64] = 1;
    vram[65] = 17;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 4);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 64,
                                                 .paletteBank = 0,
                                                 .bpp8 = true,
                                                 .domain = PPU_GPU3DS_OBJ },
                              atlas, &slot0));
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(0, 0)] == 0xf801);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(1, 0)] == 0x07c1);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(2, 0)] == 0x0000);

    vram[64] = 17;
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 64,
                                                 .paletteBank = 0,
                                                 .bpp8 = true,
                                                 .domain = PPU_GPU3DS_OBJ },
                              atlas, &slot1));
    CHECK(slot1 != slot0);
    CHECK(atlas[(size_t)slot1 * 64] == 0x07c1);

    obj[17] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 5);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 64,
                                                 .paletteBank = 0,
                                                 .bpp8 = true,
                                                 .domain = PPU_GPU3DS_OBJ },
                              atlas, &slot1));
    CHECK(atlas[(size_t)slot1 * 64] == 0x003f);

    PpuGpu3DS_CacheInit(&cache);
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 6);
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = MODE1_VRAM_SIZE - 31u,
                                                  .paletteBank = 0,
                                                  .bpp8 = false,
                                                  .domain = PPU_GPU3DS_BG },
                               atlas, &slot0));
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = MODE1_VRAM_SIZE - 63u,
                                                  .paletteBank = 0,
                                                  .bpp8 = true,
                                                  .domain = PPU_GPU3DS_BG },
                               atlas, &slot0));
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                  .paletteBank = 16,
                                                  .bpp8 = false,
                                                  .domain = PPU_GPU3DS_BG },
                               atlas, &slot0));
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                  .paletteBank = 0,
                                                  .bpp8 = false,
                                                  .domain = (PpuGpu3DSPaletteDomain)2 },
                               atlas, &slot0));
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = MODE1_VRAM_SIZE - 32u,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot0));

    PpuGpu3DS_CacheInit(&cache);
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 7);
    for (unsigned i = 0; i < PPU_GPU3DS_SLOT_COUNT; ++i) {
        CHECK(PpuGpu3DS_CacheTile(&cache, vram, cache_key(i), atlas, &slot0));
        CHECK(slot0 == i);
    }

    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 8);
    for (unsigned i = 0; i < PPU_GPU3DS_SLOT_COUNT; ++i) {
        if (i != 1) {
            CHECK(PpuGpu3DS_CacheTile(&cache, vram, cache_key(i), atlas, &slot0));
            CHECK(slot0 == i);
        }
    }

    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 9);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 2,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_BG },
                              atlas, &slot0));
    CHECK(slot0 == 1);
    for (unsigned i = 0; i < PPU_GPU3DS_SLOT_COUNT; ++i) {
        if (i != 1) {
            CHECK(PpuGpu3DS_CacheTile(&cache, vram, cache_key(i), atlas, &slot1));
            CHECK(slot1 == i);
        }
    }
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = 32,
                                                  .paletteBank = 2,
                                                  .bpp8 = false,
                                                  .domain = PPU_GPU3DS_BG },
                               atlas, &slot1));

    puts("port_ppu_gpu_3ds_model_test: PASS");
    return 0;
}
