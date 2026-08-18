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
        .domain = PPU_GPU3DS_PALETTE_BG,
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
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
                              atlas, &slot0));
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(0, 0)] == 0xf801);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(1, 0)] == 0x07c1);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(2, 0)] == 0x0000);

    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
                              atlas, &slot1));
    CHECK(slot1 == slot0);

    bg[17] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 2);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
                              atlas, &slot1));
    CHECK(slot1 == slot0);

    bg[1] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 3);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                 .paletteBank = 0,
                                                 .bpp8 = false,
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
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
                                                 .domain = PPU_GPU3DS_PALETTE_OBJ },
                              atlas, &slot0));
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(0, 0)] == 0xf801);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(1, 0)] == 0x07c1);
    CHECK(atlas[(size_t)slot0 * 64 + PpuGpu3DS_MortonIndex(2, 0)] == 0x0000);

    vram[64] = 17;
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 64,
                                                 .paletteBank = 0,
                                                 .bpp8 = true,
                                                 .domain = PPU_GPU3DS_PALETTE_OBJ },
                              atlas, &slot1));
    CHECK(slot1 != slot0);
    CHECK(atlas[(size_t)slot1 * 64] == 0x07c1);

    obj[17] = 0x7c00;
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 5);
    CHECK(PpuGpu3DS_CacheTile(&cache, vram,
                              (PpuGpu3DSTileKey){ .vramOffset = 64,
                                                 .paletteBank = 0,
                                                 .bpp8 = true,
                                                 .domain = PPU_GPU3DS_PALETTE_OBJ },
                              atlas, &slot1));
    CHECK(atlas[(size_t)slot1 * 64] == 0x003f);

    PpuGpu3DS_CacheInit(&cache);
    PpuGpu3DS_CacheBeginFrame(&cache, bg, obj, 6);
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = MODE1_VRAM_SIZE - 31u,
                                                  .paletteBank = 0,
                                                  .bpp8 = false,
                                                  .domain = PPU_GPU3DS_PALETTE_BG },
                               atlas, &slot0));
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = MODE1_VRAM_SIZE - 63u,
                                                  .paletteBank = 0,
                                                  .bpp8 = true,
                                                  .domain = PPU_GPU3DS_PALETTE_BG },
                               atlas, &slot0));
    CHECK(!PpuGpu3DS_CacheTile(&cache, vram,
                               (PpuGpu3DSTileKey){ .vramOffset = 0,
                                                  .paletteBank = 16,
                                                  .bpp8 = false,
                                                  .domain = PPU_GPU3DS_PALETTE_BG },
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
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
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
                                                 .domain = PPU_GPU3DS_PALETTE_BG },
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
                                                  .domain = PPU_GPU3DS_PALETTE_BG },
                               atlas, &slot1));

    {
        uint8_t io[4][MODE1_IO_MEM_SIZE] = { 0 };
        uint16_t dispcnt[4] = {
            MODE1_DISP_BG0_ON, MODE1_DISP_BG0_ON, MODE1_DISP_BG0_ON, MODE1_DISP_BG0_ON
        };
        int32_t affX[4] = { 0 };
        int32_t affY[4] = { 0 };
        PpuGpu3DSBand bands[160];
        PpuGpu3DSInterval intervals[2];
        PpuGpu3DSVertex vertices[4];
        uint16_t indices[6];
        PpuGpu3DSBatch batchesOut[1];
        PpuGpu3DSCommandBuffer command;

        io[2][MODE1_IO_BG0HOFS] = 1;
        io[3][MODE1_IO_BG0HOFS] = 1;
        io[3][0x100] = 1;
        PpuGpu3DSFrameView view = {
            .width = 240,
            .height = 4,
            .ioPerLine = &io[0][0],
            .ioUniform = false,
            .dispcntPerLine = dispcnt,
            .affineRefX = affX,
            .affineRefY = affY,
        };
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 2);
        CHECK(bands[0].firstLine == 0 && bands[0].lineCount == 2 && bands[0].ioRow == 0);
        CHECK(bands[1].firstLine == 2 && bands[1].lineCount == 2 && bands[1].ioRow == 2);

        io[3][MODE1_IO_WIN0V] = 1;
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 3);
        CHECK(bands[2].firstLine == 3 && bands[2].lineCount == 1 && bands[2].ioRow == 3);

        io[3][MODE1_IO_WIN0V] = 0;
        dispcnt[3] = MODE1_DISP_BG1_ON;
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 3);
        dispcnt[3] = MODE1_DISP_BG0_ON;

        view.affine = true;
        affX[3] = 1;
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 3);
        affX[3] = 0;
        view.affine = false;
        view.ioUniform = true;
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 1);
        CHECK(bands[0].firstLine == 0 && bands[0].lineCount == 4 && bands[0].ioRow == 0);

        dispcnt[2] = MODE1_DISP_BG1_ON;
        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 3);
        CHECK(bands[0].ioRow == 0 && bands[1].ioRow == 0 && bands[2].ioRow == 0);
        dispcnt[2] = MODE1_DISP_BG0_ON;

        CHECK(PpuGpu3DS_WindowIntervals(16, 32, 240, intervals) == 1);
        CHECK(intervals[0].left == 16 && intervals[0].right == 32);
        CHECK(PpuGpu3DS_WindowIntervals(220, 20, 240, intervals) == 2);
        CHECK(intervals[0].left == 220 && intervals[0].right == 240);
        CHECK(intervals[1].left == 0 && intervals[1].right == 20);
        CHECK(PpuGpu3DS_WindowIntervals(8, 8, 240, intervals) == 0);
        CHECK(PpuGpu3DS_WindowIntervals(230, 250, 240, intervals) == 1);
        CHECK(intervals[0].left == 230 && intervals[0].right == 240);
        CHECK(PpuGpu3DS_WindowIntervals(250, 20, 240, intervals) == 1);
        CHECK(intervals[0].left == 0 && intervals[0].right == 20);
        CHECK(PpuGpu3DS_WindowIntervals(0, 1, 0, intervals) == 0);

        PpuGpu3DS_CommandInit(&command, vertices, 4, indices, 6, batchesOut, 1);
        CHECK(PpuGpu3DS_CommandReserve(&command, 4, 6, 1));
        CHECK(!PpuGpu3DS_CommandReserve(&command, 1, 0, 0));
        CHECK(!PpuGpu3DS_CommandReserve(&command, 0, 1, 0));
        CHECK(!PpuGpu3DS_CommandReserve(&command, 0, 0, 1));
        CHECK(!PpuGpu3DS_CommandReserve(&command, SIZE_MAX, SIZE_MAX, SIZE_MAX));
        CHECK(command.vertexCount == 4 && command.indexCount == 6 && command.batchCount == 1);
        CHECK(PpuGpu3DS_CommandReserve(&command, 0, 0, 0));
    }

    {
        uint8_t io[MODE1_IO_MEM_SIZE] = { 0 };
        uint16_t dispcnt[161];
        PpuGpu3DSBand bands[161];
        PpuGpu3DSBand unchanged[161];
        for (unsigned line = 0; line < 161; ++line) {
            dispcnt[line] = (uint16_t)(line & 1u);
        }
        PpuGpu3DSFrameView view = {
            .height = 160,
            .ioPerLine = io,
            .ioUniform = true,
            .dispcntPerLine = dispcnt,
        };

        CHECK(PpuGpu3DS_BuildBands(&view, bands) == 160);
        CHECK(bands[159].firstLine == 159 && bands[159].lineCount == 1 &&
              bands[159].ioRow == 0);

        memset(bands, 0xa5, sizeof(bands));
        memcpy(unchanged, bands, sizeof(bands));
        view.height = 161;
        const size_t bandCount = PpuGpu3DS_BuildBands(&view, bands);
        CHECK(memcmp(bands, unchanged, sizeof(bands)) == 0);
        CHECK(bandCount == 0);
    }

    puts("port_ppu_gpu_3ds_model_test: PASS");
    return 0;
}
