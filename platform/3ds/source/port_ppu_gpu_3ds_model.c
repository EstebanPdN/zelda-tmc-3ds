#include "port_ppu_gpu_3ds_model.h"

#include <string.h>

static bool keys_equal(PpuGpu3DSTileKey left, PpuGpu3DSTileKey right) {
    return left.vramOffset == right.vramOffset && left.paletteBank == right.paletteBank &&
           left.bpp8 == right.bpp8 && left.domain == right.domain;
}

static void update_palette(uint16_t* shadow, uint32_t* bankGenerations,
                           uint32_t* fullGeneration, const uint16_t* palette) {
    bool changed = false;

    for (unsigned bank = 0; bank < 16; ++bank) {
        uint16_t* shadowBank = shadow + bank * 16;
        const uint16_t* paletteBank = palette + bank * 16;
        if (memcmp(shadowBank, paletteBank, 16 * sizeof(*shadowBank)) != 0) {
            memcpy(shadowBank, paletteBank, 16 * sizeof(*shadowBank));
            ++bankGenerations[bank];
            changed = true;
        }
    }
    if (changed) {
        ++*fullGeneration;
    }
}

uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y) {
    return (uint8_t)((x & 1u) | ((y & 1u) << 1u) | ((x & 2u) << 1u) |
                     ((y & 2u) << 2u) | ((x & 4u) << 2u) | ((y & 4u) << 3u));
}

uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque) {
    return (uint16_t)(((gbaColor & 0x001fu) << 11u) | ((gbaColor & 0x03e0u) << 1u) |
                      ((gbaColor & 0x7c00u) >> 9u) | (opaque ? 1u : 0u));
}

void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache) {
    memset(cache, 0, sizeof(*cache));
}

void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette,
                               const uint16_t* objPalette, uint32_t frame) {
    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        cache->entries[slot].pinned = false;
    }

    update_palette(cache->bgPalette, cache->bgBankGeneration, &cache->bg256Generation,
                   bgPalette);
    update_palette(cache->objPalette, cache->objBankGeneration, &cache->obj256Generation,
                   objPalette);
    cache->frame = frame;
}

bool PpuGpu3DS_CacheTile(PpuGpu3DSCache* cache, const uint8_t* vram, PpuGpu3DSTileKey key,
                         uint16_t* atlas, uint16_t* outSlot) {
    const size_t tileBytes = key.bpp8 ? 64u : 32u;
    if (key.paletteBank >= 16 ||
        (key.domain != PPU_GPU3DS_PALETTE_BG && key.domain != PPU_GPU3DS_PALETTE_OBJ) ||
        key.vramOffset > MODE1_VRAM_SIZE - tileBytes) {
        return false;
    }

    const uint32_t* bankGenerations = key.domain == PPU_GPU3DS_PALETTE_BG
                                              ? cache->bgBankGeneration
                                              : cache->objBankGeneration;
    const uint32_t fullGeneration =
            key.domain == PPU_GPU3DS_PALETTE_BG ? cache->bg256Generation : cache->obj256Generation;
    const uint32_t paletteGeneration =
            key.bpp8 ? fullGeneration : bankGenerations[key.paletteBank];
    const uint8_t* source = vram + key.vramOffset;

    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
        if (entry->valid && entry->paletteGeneration == paletteGeneration &&
            keys_equal(entry->key, key) && memcmp(entry->source, source, tileBytes) == 0) {
            entry->lastUseFrame = cache->frame;
            entry->pinned = true;
            *outSlot = (uint16_t)slot;
            return true;
        }
    }

    unsigned selected = PPU_GPU3DS_SLOT_COUNT;
    uint32_t oldestFrame = 0;
    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
        if (entry->pinned) {
            continue;
        }
        if (!entry->valid) {
            selected = slot;
            break;
        }
        if (selected == PPU_GPU3DS_SLOT_COUNT || entry->lastUseFrame < oldestFrame) {
            selected = slot;
            oldestFrame = entry->lastUseFrame;
        }
    }
    if (selected == PPU_GPU3DS_SLOT_COUNT) {
        return false;
    }

    PpuGpu3DSCacheEntry* entry = &cache->entries[selected];
    const uint16_t* palette =
            key.domain == PPU_GPU3DS_PALETTE_BG ? cache->bgPalette : cache->objPalette;
    memcpy(entry->source, source, tileBytes);
    for (unsigned y = 0; y < PPU_GPU3DS_TILE_SIDE; ++y) {
        for (unsigned x = 0; x < PPU_GPU3DS_TILE_SIDE; ++x) {
            const unsigned pixel = y * PPU_GPU3DS_TILE_SIDE + x;
            const uint8_t colorIndex = key.bpp8
                                               ? source[pixel]
                                               : (uint8_t)((source[pixel / 2] >>
                                                            ((pixel & 1u) * 4u)) &
                                                           0x0fu);
            const unsigned paletteIndex =
                    key.bpp8 ? colorIndex : key.paletteBank * 16u + colorIndex;
            atlas[(size_t)selected * 64 + PpuGpu3DS_MortonIndex(x, y)] =
                    colorIndex ? PpuGpu3DS_PackRgba5551(palette[paletteIndex], true) : 0;
        }
    }

    entry->key = key;
    entry->paletteGeneration = paletteGeneration;
    entry->lastUseFrame = cache->frame;
    entry->valid = true;
    entry->pinned = true;
    *outSlot = (uint16_t)selected;
    return true;
}

static bool io_rows_equal(const uint8_t* left, const uint8_t* right) {
    return memcmp(left + MODE1_IO_BG0CNT, right + MODE1_IO_BG0CNT, 0x18) == 0 &&
           memcmp(left + 0x20, right + 0x20, 2) == 0 &&
           memcmp(left + 0x24, right + 0x24, 2) == 0 &&
           memcmp(left + 0x30, right + 0x30, 2) == 0 &&
           memcmp(left + 0x34, right + 0x34, 2) == 0 &&
           memcmp(left + MODE1_IO_WIN0H, right + MODE1_IO_WIN0H, 0x0e) == 0 &&
           memcmp(left + MODE1_IO_BLDCNT, right + MODE1_IO_BLDCNT, 6) == 0;
}

static bool line_states_equal(const PpuGpu3DSFrameView* frame, unsigned left,
                              unsigned right) {
    const unsigned leftRow = frame->ioUniform ? 0 : left;
    const unsigned rightRow = frame->ioUniform ? 0 : right;
    if (frame->dispcntPerLine[left] != frame->dispcntPerLine[right] ||
        !io_rows_equal(frame->ioPerLine + leftRow * MODE1_IO_MEM_SIZE,
                       frame->ioPerLine + rightRow * MODE1_IO_MEM_SIZE)) {
        return false;
    }
    return !frame->affine ||
           (frame->affineRefX[left] == frame->affineRefX[right] &&
            frame->affineRefY[left] == frame->affineRefY[right]);
}

size_t PpuGpu3DS_BuildBands(const PpuGpu3DSFrameView* frame, PpuGpu3DSBand out[160]) {
    if (frame->height == 0 || frame->height > MODE1_GBA_HEIGHT) {
        return 0;
    }
    size_t count = 1;
    out[0] = (PpuGpu3DSBand){ .firstLine = 0,
                             .lineCount = 1,
                             .ioRow = 0 };
    for (unsigned line = 1; line < frame->height; ++line) {
        if (line_states_equal(frame, line - 1, line)) {
            ++out[count - 1].lineCount;
        } else {
            out[count++] = (PpuGpu3DSBand){ .firstLine = (uint16_t)line,
                                           .lineCount = 1,
                                           .ioRow = (uint8_t)(frame->ioUniform ? 0 : line) };
        }
    }
    return count;
}

size_t PpuGpu3DS_WindowIntervals(unsigned left, unsigned right, unsigned width,
                                 PpuGpu3DSInterval out[2]) {
    if (left == right || width == 0) {
        return 0;
    }

    const unsigned clippedLeft = left < width ? left : width;
    const unsigned clippedRight = right < width ? right : width;
    size_t count = 0;
    if (left < right) {
        if (clippedLeft < clippedRight) {
            out[count++] = (PpuGpu3DSInterval){ (uint16_t)clippedLeft,
                                                (uint16_t)clippedRight };
        }
    } else {
        if (clippedLeft < width) {
            out[count++] = (PpuGpu3DSInterval){ (uint16_t)clippedLeft,
                                                (uint16_t)width };
        }
        if (clippedRight > 0) {
            out[count++] = (PpuGpu3DSInterval){ 0, (uint16_t)clippedRight };
        }
    }
    return count;
}

void PpuGpu3DS_CommandInit(PpuGpu3DSCommandBuffer* cmd, PpuGpu3DSVertex* vertices,
                           size_t vertexCapacity, uint16_t* indices, size_t indexCapacity,
                           PpuGpu3DSBatch* batches, size_t batchCapacity) {
    *cmd = (PpuGpu3DSCommandBuffer){
        .vertices = vertices,
        .indices = indices,
        .batches = batches,
        .vertexCapacity = vertexCapacity,
        .indexCapacity = indexCapacity,
        .batchCapacity = batchCapacity,
    };
}

bool PpuGpu3DS_CommandReserve(PpuGpu3DSCommandBuffer* cmd, size_t vertices, size_t indices,
                              size_t batches) {
    if (cmd->vertexCount > cmd->vertexCapacity ||
        vertices > cmd->vertexCapacity - cmd->vertexCount ||
        cmd->indexCount > cmd->indexCapacity ||
        indices > cmd->indexCapacity - cmd->indexCount ||
        cmd->batchCount > cmd->batchCapacity ||
        batches > cmd->batchCapacity - cmd->batchCount) {
        return false;
    }
    cmd->vertexCount += vertices;
    cmd->indexCount += indices;
    cmd->batchCount += batches;
    return true;
}
