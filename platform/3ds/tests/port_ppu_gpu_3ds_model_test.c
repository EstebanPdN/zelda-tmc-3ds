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

static void write16(uint8_t* bytes, unsigned offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8u);
}

static size_t find_batch(const PpuGpu3DSCommandBuffer* command, uint8_t layer,
                         uint8_t objectIndex) {
    for (size_t i = 0; i < command->batchCount; ++i) {
        if (command->batches[i].layer == layer &&
            command->batches[i].objectIndex == objectIndex)
            return i;
    }
    return SIZE_MAX;
}

static size_t find_window_batch(const PpuGpu3DSCommandBuffer* command,
                                uint16_t left, uint16_t right, uint8_t control) {
    for (size_t i = 0; i < command->batchCount; ++i) {
        if (command->batches[i].scissorLeft == left &&
            command->batches[i].scissorRight == right &&
            command->batches[i].windowControl == control)
            return i;
    }
    return SIZE_MAX;
}

static size_t find_window_lines(const PpuGpu3DSCommandBuffer* command,
                                uint16_t left, uint16_t right, uint8_t control,
                                uint16_t firstLine, uint16_t lineCount) {
    for (size_t i = 0; i < command->batchCount; ++i) {
        if (command->batches[i].scissorLeft == left &&
            command->batches[i].scissorRight == right &&
            command->batches[i].windowControl == control &&
            command->batches[i].firstLine == firstLine &&
            command->batches[i].lineCount == lineCount)
            return i;
    }
    return SIZE_MAX;
}

static size_t find_object_scissor(const PpuGpu3DSCommandBuffer* command,
                                  uint8_t objectIndex, uint16_t left,
                                  uint16_t right, uint16_t firstLine,
                                  uint16_t lineCount) {
    for (size_t i = 0; i < command->batchCount; ++i) {
        if (command->batches[i].layer == PPU_GPU3DS_OBJ &&
            command->batches[i].objectIndex == objectIndex &&
            command->batches[i].scissorLeft == left &&
            command->batches[i].scissorRight == right &&
            command->batches[i].firstLine == firstLine &&
            command->batches[i].lineCount == lineCount)
            return i;
    }
    return SIZE_MAX;
}

static bool cache_contains(const PpuGpu3DSCache* cache, uint32_t offset, bool bpp8) {
    for (size_t i = 0; i < PPU_GPU3DS_SLOT_COUNT; ++i) {
        if (cache->entries[i].valid &&
            cache->entries[i].key.domain == PPU_GPU3DS_PALETTE_OBJ &&
            cache->entries[i].key.vramOffset == offset &&
            cache->entries[i].key.bpp8 == bpp8)
            return true;
    }
    return false;
}

static void set_oam(uint16_t* oam, unsigned index, uint16_t attr0,
                    uint16_t attr1, uint16_t attr2) {
    oam[index * 4] = attr0;
    oam[index * 4 + 1] = attr1;
    oam[index * 4 + 2] = attr2;
}

static float vertex_screen_x(const PpuGpu3DSVertex* vertex, unsigned width) {
    return (vertex->x + 1.0f) * (float)width * 0.5f;
}

static float vertex_screen_y(const PpuGpu3DSVertex* vertex, unsigned height) {
    return (1.0f - vertex->y) * (float)height * 0.5f;
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

    {
        static uint8_t renderVram[MODE1_VRAM_SIZE];
        static uint16_t renderBg[MODE1_PALETTE_COLORS];
        static uint16_t renderObj[MODE1_PALETTE_COLORS];
        static uint8_t renderIo[MODE1_GBA_HEIGHT][MODE1_IO_MEM_SIZE];
        static uint16_t renderDispcnt[MODE1_GBA_HEIGHT];
        static uint16_t renderOam[MODE1_OAM_HALFWORDS];
        static PpuGpu3DSVertex renderVertices[32768];
        static uint16_t renderIndices[49152];
        static PpuGpu3DSBatch renderBatches[4096];
        PpuGpu3DSCommandBuffer renderCommand;
        PpuGpu3DSFrameView renderView = {
            .width = 240,
            .height = MODE1_GBA_HEIGHT,
            .frameDispcnt = MODE1_DISP_BG0_ON,
            .memory = { renderIo[0], renderVram, renderBg, renderObj, renderOam },
            .ioPerLine = renderIo[0],
            .ioUniform = true,
            .dispcntPerLine = renderDispcnt,
        };

        memset(renderVram, 0, sizeof(renderVram));
        memset(renderBg, 0, sizeof(renderBg));
        memset(renderObj, 0, sizeof(renderObj));
        memset(renderIo, 0, sizeof(renderIo));
        for (unsigned object = 0; object < MODE1_GBA_OAM_COUNT; ++object)
            renderOam[object * 4] = 0x0200u;
        for (unsigned line = 0; line < MODE1_GBA_HEIGHT; ++line) {
            renderDispcnt[line] = MODE1_DISP_BG0_ON;
        }
        renderBg[0] = 0x001f;
        renderBg[3 * 16 + 1] = 0x03e0;
        renderVram[32] = 1;
        write16(renderIo[0], MODE1_IO_BG0CNT, (uint16_t)(2u | (1u << 8u)));
        write16(renderVram, 0x800, (uint16_t)(1u | (1u << 10u) | (3u << 12u)));

        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 10);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 2);
        CHECK(renderCommand.batches[0].layer == PPU_GPU3DS_BACKDROP);
        CHECK(renderCommand.batches[0].color == 0x001f);
        CHECK(renderCommand.batches[1].layer == PPU_GPU3DS_BG0);
        CHECK(renderCommand.batches[1].priority == 2);
        CHECK(renderCommand.batches[1].scissorLeft == 0 &&
              renderCommand.batches[1].scissorRight == 240);
        CHECK(renderCommand.batches[1].firstLine == 0 &&
              renderCommand.batches[1].lineCount == MODE1_GBA_HEIGHT);
        CHECK(renderVertices[0].u > renderVertices[1].u);
        CHECK(renderVertices[0].v == renderVertices[1].v);
        CHECK(cache.entries[0].dirty);
        for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
            cache.entries[slot].dirty = false;
        }
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 11);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(!cache.entries[0].dirty);

        renderView.frameDispcnt = MODE1_DISP_FORCED_BLANK;
        for (unsigned line = 0; line < MODE1_GBA_HEIGHT; ++line) {
            renderDispcnt[line] = MODE1_DISP_FORCED_BLANK;
        }
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 11);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 1 && renderCommand.vertexCount == 0 &&
              renderCommand.indexCount == 0);
        CHECK(renderCommand.batches[0].layer == PPU_GPU3DS_BACKDROP);
        CHECK(renderCommand.batches[0].color == 0x7fff);

        memset(renderVram, 0, sizeof(renderVram));
        memset(renderIo, 0, sizeof(renderIo));
        renderView.width = 8;
        renderView.height = 8;
        renderView.frameDispcnt = MODE1_DISP_BG0_ON;
        renderView.ioUniform = true;
        for (unsigned line = 0; line < 8; ++line) {
            renderDispcnt[line] = MODE1_DISP_BG0_ON;
        }
        write16(renderIo[0], MODE1_IO_BG0CNT,
                (uint16_t)((3u << 14u) | (8u << 8u)));
        write16(renderIo[0], MODE1_IO_BG0HOFS, 511);
        write16(renderIo[0], MODE1_IO_BG0VOFS, 511);
        write16(renderVram, 8u * 0x800u + 3u * 0x800u + 0x7feu, 5);
        renderVram[5 * 32] = 1;

        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 12);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.vertexCount == 16 && renderCommand.indexCount == 24);
        CHECK(renderCommand.batches[1].indexCount == 24);
        CHECK(cache.entries[0].key.vramOffset == 5 * 32);

        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 16, renderIndices, 26,
                              renderBatches, 3);
        renderCommand.vertexCount = 1;
        renderCommand.indexCount = 2;
        renderCommand.batchCount = 1;
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.vertexCount == 1 && renderCommand.indexCount == 2 &&
              renderCommand.batchCount == 1);

        write16(renderIo[0], MODE1_IO_BG0CNT,
                (uint16_t)((3u << 14u) | (8u << 8u) | (1u << 7u) | (3u << 2u)));
        write16(renderVram, 8u * 0x800u + 3u * 0x800u + 0x7feu, 1023);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.vertexCount == 0 && renderCommand.indexCount == 0 &&
              renderCommand.batchCount == 0);

        write16(renderIo[0], MODE1_IO_BG0CNT, 0);
        write16(renderIo[0], MODE1_IO_BG0HOFS, 0);
        write16(renderIo[0], MODE1_IO_BG0VOFS, 0);
        write16(renderIo[0], MODE1_IO_BLDCNT, 1u << 6u);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 0);
        write16(renderIo[0], MODE1_IO_BLDCNT, 0);


        renderView.frameDispcnt = MODE1_DISP_BG0_ON;
        for (unsigned line = 0; line < 8; ++line) {
            renderDispcnt[line] = renderView.frameDispcnt;
        }
        renderView.affine = true;
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 0);
        renderView.affine = false;

        write16(renderIo[0], MODE1_IO_BG0CNT, 1u << 6u);
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 0);
        write16(renderIo[0], MODE1_IO_BG0CNT, 0);

        renderView.height = 4;
        renderView.ioUniform = false;
        memset(renderIo, 0, 4 * MODE1_IO_MEM_SIZE);
        write16(renderIo[0], MODE1_IO_BG0CNT, 2);
        write16(renderIo[1], MODE1_IO_BG0CNT, 2);
        write16(renderIo[2], MODE1_IO_BG0CNT, 2);
        write16(renderIo[3], MODE1_IO_BG0CNT, 2);
        write16(renderIo[2], MODE1_IO_BG0HOFS, 1);
        write16(renderIo[3], MODE1_IO_BG0HOFS, 1);
        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 13);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 3);
        CHECK(renderCommand.batches[1].priority == 2 &&
              renderCommand.batches[1].firstLine == 0 &&
              renderCommand.batches[1].lineCount == 2);
        CHECK(renderCommand.batches[2].priority == 2 &&
              renderCommand.batches[2].firstLine == 2 &&
              renderCommand.batches[2].lineCount == 2);

        renderView.height = 8;
        renderView.ioUniform = true;
        memset(renderIo[0], 0, MODE1_IO_MEM_SIZE);
        renderView.frameDispcnt = MODE1_DISP_BG0_ON | MODE1_DISP_BG1_ON |
                                  MODE1_DISP_BG2_ON | MODE1_DISP_BG3_ON;
        for (unsigned line = 0; line < 8; ++line) {
            renderDispcnt[line] = renderView.frameDispcnt;
        }
        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 14);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.batchCount == 5);
        CHECK(renderCommand.batches[1].layer == PPU_GPU3DS_BG3);
        CHECK(renderCommand.batches[2].layer == PPU_GPU3DS_BG2);
        CHECK(renderCommand.batches[3].layer == PPU_GPU3DS_BG1);
        CHECK(renderCommand.batches[4].layer == PPU_GPU3DS_BG0);

        memset(renderVram, 0, sizeof(renderVram));
        memset(renderIo, 0, sizeof(renderIo));
        memset(renderOam, 0, sizeof(renderOam));
        for (unsigned object = 0; object < MODE1_GBA_OAM_COUNT; ++object)
            renderOam[object * 4] = 0x0200u;
        renderView.width = 40;
        renderView.height = 40;
        renderView.ioUniform = true;
        renderView.frameDispcnt = MODE1_DISP_BG0_ON | MODE1_DISP_BG1_ON |
                                  MODE1_DISP_BG2_ON | MODE1_DISP_BG3_ON |
                                  MODE1_DISP_OBJ_ON | MODE1_DISP_OBJ_1D |
                                  MODE1_DISP_WIN0_ON | MODE1_DISP_WIN1_ON |
                                  MODE1_DISP_OBJWIN_ON;
        for (unsigned line = 0; line < renderView.height; ++line)
            renderDispcnt[line] = renderView.frameDispcnt;
        for (unsigned bgIndex = 0; bgIndex < MODE1_GBA_BG_COUNT; ++bgIndex)
            write16(renderIo[0], MODE1_IO_BG0CNT + bgIndex * 2u, 1);

        const uint8_t win0Control = 0x11u;
        const uint8_t win1Control = 0x12u;
        const uint8_t outsideControl = 0x18u;
        const uint8_t objwinControl = 0x14u;
        write16(renderIo[0], MODE1_IO_WIN0H, (uint16_t)((30u << 8u) | 40u));
        write16(renderIo[0], MODE1_IO_WIN1H, (uint16_t)((20u << 8u) | 35u));
        write16(renderIo[0], MODE1_IO_WIN0V, 40);
        write16(renderIo[0], MODE1_IO_WIN1V, 40);
        write16(renderIo[0], MODE1_IO_WININ,
                (uint16_t)(win0Control | (win1Control << 8u)));
        write16(renderIo[0], MODE1_IO_WINOUT,
                (uint16_t)(outsideControl | (objwinControl << 8u)));

        set_oam(renderOam, 0, (uint16_t)((1u << 10u) | (1u << 14u)),
                (uint16_t)((1u << 12u) | (1u << 13u)),
                (uint16_t)(4u | (1u << 10u) | (2u << 12u)));
        set_oam(renderOam, 1, 1u << 14u, 0, (uint16_t)(6u | (1u << 10u)));
        set_oam(renderOam, 2, 2u << 10u, 10, (uint16_t)(8u | (1u << 10u)));
        set_oam(renderOam, 3, (uint16_t)(252u | (1u << 8u) | (1u << 9u)),
                (uint16_t)(508u | (1u << 9u)), (uint16_t)(10u | (1u << 10u)));
        renderOam[1u * 16u + 3u] = 0;
        renderOam[1u * 16u + 7u] = 0x0100u;
        renderOam[1u * 16u + 11u] = (uint16_t)-0x0100;
        renderOam[1u * 16u + 15u] = 0;
        set_oam(renderOam, 4, (uint16_t)(8u | (1u << 13u) | (1u << 14u)),
                24, (uint16_t)(3u | (2u << 10u)));
        set_oam(renderOam, 5, 1u << 9u, 0, 0);
        set_oam(renderOam, 6, 3u << 14u, 0, 0);
        set_oam(renderOam, 7, 3u << 10u, 0, 0);
        set_oam(renderOam, 8, (uint16_t)(16u | (2u << 14u)), 32,
                (uint16_t)(20u | (2u << 10u)));
        for (unsigned offset = 0x10000u; offset < 0x11000u; offset += 32u)
            renderVram[offset] = 1;

        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 15);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_BG3, UINT8_MAX) <
              find_batch(&renderCommand, PPU_GPU3DS_BG2, UINT8_MAX));
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_BG2, UINT8_MAX) <
              find_batch(&renderCommand, PPU_GPU3DS_BG1, UINT8_MAX));
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_BG1, UINT8_MAX) <
              find_batch(&renderCommand, PPU_GPU3DS_BG0, UINT8_MAX));
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_BG0, UINT8_MAX) <
              find_batch(&renderCommand, PPU_GPU3DS_OBJ, 1));
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_OBJ, 1) <
              find_batch(&renderCommand, PPU_GPU3DS_OBJ, 0));
        CHECK(find_window_batch(&renderCommand, 0, 10, outsideControl) != SIZE_MAX);
        CHECK(find_window_batch(&renderCommand, 10, 18, objwinControl) != SIZE_MAX);
        CHECK(find_window_batch(&renderCommand, 20, 30, win1Control) != SIZE_MAX);
        CHECK(find_window_batch(&renderCommand, 30, 40, win0Control) != SIZE_MAX);

        const size_t objwinBatch =
                find_batch(&renderCommand, PPU_GPU3DS_OBJ, 2);
        CHECK(objwinBatch != SIZE_MAX);
        CHECK(renderCommand.batches[objwinBatch].objWindow);
        CHECK(renderCommand.batches[objwinBatch].scissorLeft == 10 &&
              renderCommand.batches[objwinBatch].scissorRight == 18);
        CHECK(renderCommand.batches[objwinBatch].windowControl == objwinControl);
        CHECK(renderCommand.batches[find_batch(&renderCommand, PPU_GPU3DS_OBJ, 0)]
                      .semiTransparent);
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_OBJ, 5) == SIZE_MAX);
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_OBJ, 6) == SIZE_MAX);
        CHECK(find_batch(&renderCommand, PPU_GPU3DS_OBJ, 7) == SIZE_MAX);
        CHECK(cache_contains(&cache, 0x10000u + 4u * 32u, false));
        CHECK(cache_contains(&cache, 0x10000u + 5u * 32u, false));
        CHECK(cache_contains(&cache, 0x10000u + 2u * 32u, true));
        CHECK(cache_contains(&cache, 0x10000u + 4u * 32u, true));
        CHECK(cache_contains(&cache, 0x10000u + 20u * 32u, false));
        CHECK(cache_contains(&cache, 0x10000u + 21u * 32u, false));

        const size_t flippedBatch =
                find_batch(&renderCommand, PPU_GPU3DS_OBJ, 0);
        const uint16_t flippedVertex =
                renderIndices[renderCommand.batches[flippedBatch].firstIndex];
        CHECK(vertex_screen_x(&renderVertices[flippedVertex], renderView.width) > 7.99f &&
              vertex_screen_x(&renderVertices[flippedVertex], renderView.width) < 8.01f);
        CHECK(renderVertices[flippedVertex].u >
              renderVertices[flippedVertex + 1u].u);
        CHECK(renderVertices[flippedVertex].v >
              renderVertices[flippedVertex + 3u].v);
        const size_t otherObjectBatch =
                find_batch(&renderCommand, PPU_GPU3DS_OBJ, 1);
        const uint16_t otherObjectVertex =
                renderIndices[renderCommand.batches[otherObjectBatch].firstIndex];
        CHECK(renderVertices[flippedVertex].z >
              renderVertices[otherObjectVertex].z);

        const size_t affineBatch =
                find_batch(&renderCommand, PPU_GPU3DS_OBJ, 3);
        const uint16_t affineVertex =
                renderIndices[renderCommand.batches[affineBatch].firstIndex];
        CHECK(vertex_screen_x(&renderVertices[affineVertex], renderView.width) > 7.99f &&
              vertex_screen_x(&renderVertices[affineVertex], renderView.width) < 8.01f);
        CHECK(vertex_screen_y(&renderVertices[affineVertex], renderView.height) > -0.01f &&
              vertex_screen_y(&renderVertices[affineVertex], renderView.height) < 0.01f);
        CHECK(find_object_scissor(&renderCommand, 3, 0, 10, 0, 12) != SIZE_MAX);
        CHECK(find_object_scissor(&renderCommand, 3, 10, 12, 0, 12) != SIZE_MAX);

        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 8, renderIndices, 12,
                              renderBatches, 2);
        renderCommand.vertexCount = 1;
        renderCommand.indexCount = 2;
        renderCommand.batchCount = 1;
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.vertexCount == 1 && renderCommand.indexCount == 2 &&
              renderCommand.batchCount == 1);

        for (unsigned object = 0; object < MODE1_GBA_OAM_COUNT; ++object)
            renderOam[object * 4] = 0x0200u;
        set_oam(renderOam, 0, 2u << 14u, 0, 20);
        renderView.width = 8;
        renderView.height = 16;
        renderView.frameDispcnt = MODE1_DISP_OBJ_ON;
        for (unsigned line = 0; line < renderView.height; ++line)
            renderDispcnt[line] = renderView.frameDispcnt;
        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 16);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(cache_contains(&cache, 0x10000u + 20u * 32u, false));
        CHECK(cache_contains(&cache, 0x10000u + 52u * 32u, false));

        renderOam[0] |= 1u << 12u;
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(!PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(renderCommand.vertexCount == 0 && renderCommand.indexCount == 0 &&
              renderCommand.batchCount == 0);

        renderOam[0] = 0x0200u;
        memset(renderIo[0], 0, MODE1_IO_MEM_SIZE);
        renderView.width = 40;
        renderView.height = 40;
        renderView.frameDispcnt = MODE1_DISP_BG0_ON | MODE1_DISP_WIN0_ON;
        for (unsigned line = 0; line < renderView.height; ++line)
            renderDispcnt[line] = renderView.frameDispcnt;
        write16(renderIo[0], MODE1_IO_WIN0H, (uint16_t)((30u << 8u) | 10u));
        write16(renderIo[0], MODE1_IO_WIN0V, (uint16_t)((30u << 8u) | 10u));
        write16(renderIo[0], MODE1_IO_WININ, 1);
        write16(renderIo[0], MODE1_IO_WINOUT, 0);
        PpuGpu3DS_CacheInit(&cache);
        PpuGpu3DS_CacheBeginFrame(&cache, renderBg, renderObj, 17);
        PpuGpu3DS_CommandInit(&renderCommand, renderVertices, 32768, renderIndices, 49152,
                              renderBatches, 4096);
        CHECK(PpuGpu3DS_BuildCommands(&renderView, &cache, atlas, &renderCommand));
        CHECK(find_window_lines(&renderCommand, 0, 10, 1, 0, 10) != SIZE_MAX);
        CHECK(find_window_lines(&renderCommand, 30, 40, 1, 0, 10) != SIZE_MAX);
        CHECK(find_window_lines(&renderCommand, 0, 10, 1, 30, 10) != SIZE_MAX);
        CHECK(find_window_lines(&renderCommand, 30, 40, 1, 30, 10) != SIZE_MAX);
    }

    puts("port_ppu_gpu_3ds_model_test: PASS");
    return 0;
}
