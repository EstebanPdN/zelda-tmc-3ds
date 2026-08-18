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
    entry->dirty = true;
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

static uint16_t read16(const uint8_t* bytes, unsigned offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1] << 8u);
}

static bool frame_features_supported(const PpuGpu3DSFrameView* frame,
                                     const PpuGpu3DSBand* bands, size_t bandCount,
                                     bool forcedBlank) {
    if (frame->affine) {
        return false;
    }

    for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        const PpuGpu3DSBand* band = &bands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        const uint8_t* io = frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
        if ((dispcnt & (MODE1_DISP_OBJ_ON | MODE1_DISP_WIN0_ON | MODE1_DISP_WIN1_ON |
                        MODE1_DISP_OBJWIN_ON)) != 0 ||
            ((dispcnt & MODE1_DISP_FORCED_BLANK) != 0) != forcedBlank ||
            (read16(io, MODE1_IO_BLDCNT) & 0x00c0u) != 0) {
            return false;
        }
        for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
            if ((read16(io, MODE1_IO_BG0CNT + bg * 2u) & (1u << 6u)) != 0) {
                return false;
            }
        }
    }
    return true;
}

static bool build_text_bg(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                          uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                          const PpuGpu3DSBand* band, unsigned bg, bool emit,
                          size_t* vertexCursor, size_t* indexCursor,
                          size_t* batchCursor) {
    const uint8_t* io = frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t bgcnt = read16(io, MODE1_IO_BG0CNT + bg * 2u);
    const unsigned priority = bgcnt & 3u;
    const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;
    const bool bpp8 = ((bgcnt >> 7u) & 1u) != 0;
    const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
    const unsigned size = (bgcnt >> 14u) & 3u;
    const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
    const unsigned mapHeightTiles = (size & 2u) ? 64u : 32u;
    const unsigned scrollX =
            read16(io, MODE1_IO_BG0HOFS + bg * 4u) & 0x1ffu;
    const unsigned scrollY =
            read16(io, MODE1_IO_BG0VOFS + bg * 4u) & 0x1ffu;
    const unsigned sourceY = band->firstLine + scrollY;
    const unsigned firstTileY =
            (sourceY >> 3u) & (mapHeightTiles - 1u);
    const int firstY = (int)band->firstLine - (int)(sourceY & 7u);
    const unsigned firstTileX = (scrollX >> 3u) & (mapWidthTiles - 1u);
    const int firstX = -(int)(scrollX & 7u);
    const int bandBottom = (int)band->firstLine + (int)band->lineCount;
    const size_t firstIndex = *indexCursor;

    if (emit) {
        cmd->batches[*batchCursor] = (PpuGpu3DSBatch){
            .firstIndex = (uint32_t)firstIndex,
            .firstLine = band->firstLine,
            .lineCount = band->lineCount,
            .scissorLeft = 0,
            .scissorRight = (uint16_t)frame->width,
            .layer = (uint8_t)(PPU_GPU3DS_BG0 + bg),
            .priority = (uint8_t)priority,
            .effect = PPU_GPU3DS_EFFECT_NONE,
            .objectIndex = UINT8_MAX,
        };
    }

    unsigned tileRowOffset = 0;
    for (int y = firstY; y < bandBottom; y += PPU_GPU3DS_TILE_SIDE, ++tileRowOffset) {
        const unsigned tileY = (firstTileY + tileRowOffset) & (mapHeightTiles - 1u);
        unsigned tileColumnOffset = 0;
        for (int x = firstX; x < (int)frame->width;
             x += PPU_GPU3DS_TILE_SIDE, ++tileColumnOffset) {
            const unsigned tileX =
                    (firstTileX + tileColumnOffset) & (mapWidthTiles - 1u);
            const unsigned blockX = tileX / 32u;
            const unsigned blockY = tileY / 32u;
            const uint32_t mapOffset =
                    (blockX + blockY * (mapWidthTiles / 32u)) * 0x800u +
                    ((tileY & 31u) * 32u + (tileX & 31u)) * 2u;
            if (screenBase > MODE1_VRAM_SIZE - 2u ||
                mapOffset > MODE1_VRAM_SIZE - 2u - screenBase) {
                return false;
            }

            const uint16_t entry = read16(frame->memory.vram, screenBase + mapOffset);
            const uint32_t tileBytes = bpp8 ? 64u : 32u;
            const uint32_t tileOffset =
                    charBase + (uint32_t)(entry & 0x03ffu) * tileBytes;
            if (tileOffset > MODE1_VRAM_SIZE - tileBytes) {
                return false;
            }

            if (emit) {
                uint16_t slot;
                if (!PpuGpu3DS_CacheTile(
                            cache, frame->memory.vram,
                            (PpuGpu3DSTileKey){
                                .vramOffset = tileOffset,
                                .paletteBank = (uint8_t)(entry >> 12u),
                                .bpp8 = bpp8,
                                .domain = PPU_GPU3DS_PALETTE_BG,
                            },
                            atlas, &slot)) {
                    return false;
                }

                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                const float slotLeft =
                        (float)((slot % (PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                const float slotTop =
                        (float)((slot / (PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                float u0 = slotLeft;
                float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
                float v0 = slotTop;
                float v1 = slotTop + PPU_GPU3DS_TILE_SIDE * invAtlas;
                if ((entry & (1u << 10u)) != 0) {
                    const float swap = u0;
                    u0 = u1;
                    u1 = swap;
                }
                if ((entry & (1u << 11u)) != 0) {
                    const float swap = v0;
                    v0 = v1;
                    v1 = swap;
                }

                const float left = 2.0f * (float)x / (float)frame->width - 1.0f;
                const float right =
                        2.0f * (float)(x + PPU_GPU3DS_TILE_SIDE) /
                                (float)frame->width -
                        1.0f;
                const float top = 1.0f - 2.0f * (float)y / (float)frame->height;
                const float bottom =
                        1.0f - 2.0f * (float)(y + PPU_GPU3DS_TILE_SIDE) /
                                       (float)frame->height;
                const uint16_t base = (uint16_t)*vertexCursor;
                cmd->vertices[*vertexCursor + 0] =
                        (PpuGpu3DSVertex){ left, top, 0.0f, 1.0f, u0, v0 };
                cmd->vertices[*vertexCursor + 1] =
                        (PpuGpu3DSVertex){ right, top, 0.0f, 1.0f, u1, v0 };
                cmd->vertices[*vertexCursor + 2] =
                        (PpuGpu3DSVertex){ right, bottom, 0.0f, 1.0f, u1, v1 };
                cmd->vertices[*vertexCursor + 3] =
                        (PpuGpu3DSVertex){ left, bottom, 0.0f, 1.0f, u0, v1 };
                cmd->indices[*indexCursor + 0] = base;
                cmd->indices[*indexCursor + 1] = (uint16_t)(base + 1u);
                cmd->indices[*indexCursor + 2] = (uint16_t)(base + 2u);
                cmd->indices[*indexCursor + 3] = base;
                cmd->indices[*indexCursor + 4] = (uint16_t)(base + 2u);
                cmd->indices[*indexCursor + 5] = (uint16_t)(base + 3u);
            }
            *vertexCursor += 4;
            *indexCursor += 6;
        }
    }

    if (emit) {
        cmd->batches[*batchCursor].indexCount =
                (uint32_t)(*indexCursor - firstIndex);
    }
    ++*batchCursor;
    return true;
}

bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                             uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd) {
    if (!frame || !cache || !atlas || !cmd || !cmd->vertices || !cmd->indices ||
        !cmd->batches || !frame->memory.vram || !frame->memory.bg_palette ||
        !frame->ioPerLine || !frame->dispcntPerLine || frame->affine ||
        (frame->frameDispcnt &
         (MODE1_DISP_OBJ_ON | MODE1_DISP_WIN0_ON | MODE1_DISP_WIN1_ON |
          MODE1_DISP_OBJWIN_ON)) != 0 ||
        frame->width == 0 || frame->width > PPU_GPU3DS_ATLAS_SIDE ||
        frame->height == 0 || frame->height > MODE1_GBA_HEIGHT) {
        return false;
    }

    PpuGpu3DSBand bands[MODE1_GBA_HEIGHT];
    const size_t bandCount = PpuGpu3DS_BuildBands(frame, bands);
    const bool forcedBlank =
            (frame->frameDispcnt & MODE1_DISP_FORCED_BLANK) != 0;
    if (bandCount == 0 ||
        !frame_features_supported(frame, bands, bandCount, forcedBlank)) {
        return false;
    }

    const size_t startVertex = cmd->vertexCount;
    const size_t startIndex = cmd->indexCount;
    const size_t startBatch = cmd->batchCount;
    size_t requiredVertices = 0;
    size_t requiredIndices = 0;
    size_t requiredBatches = 1;

    if (!forcedBlank) {
        for (int priority = 3; priority >= 0; --priority) {
            for (int bg = MODE1_GBA_BG_COUNT - 1; bg >= 0; --bg) {
                for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
                    const PpuGpu3DSBand* band = &bands[bandIndex];
                    const uint16_t dispcnt =
                            frame->dispcntPerLine[band->firstLine];
                    const uint8_t* io =
                            frame->ioPerLine +
                            (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
                    const uint16_t bgcnt =
                            read16(io, MODE1_IO_BG0CNT + (unsigned)bg * 2u);
                    if ((dispcnt & (MODE1_DISP_BG0_ON << bg)) == 0 ||
                        (bgcnt & 3u) != (unsigned)priority) {
                        continue;
                    }
                    if (!build_text_bg(frame, cache, atlas, cmd, band,
                                       (unsigned)bg, false, &requiredVertices,
                                       &requiredIndices, &requiredBatches)) {
                        return false;
                    }
                }
            }
        }
    }

    PpuGpu3DSCommandBuffer capacity = *cmd;
    if (!PpuGpu3DS_CommandReserve(&capacity, requiredVertices, requiredIndices,
                                  requiredBatches) ||
        startVertex > UINT16_MAX ||
        requiredVertices > (size_t)UINT16_MAX + 1u - startVertex) {
        return false;
    }

    size_t vertexCursor = startVertex;
    size_t indexCursor = startIndex;
    size_t batchCursor = startBatch;
    cmd->batches[batchCursor++] = (PpuGpu3DSBatch){
        .firstIndex = (uint32_t)startIndex,
        .layer = PPU_GPU3DS_BACKDROP,
        .priority = UINT8_MAX,
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .objectIndex = UINT8_MAX,
        .color = forcedBlank ? 0x7fffu
                             : (uint16_t)(frame->memory.bg_palette[0] & 0x7fffu),
        .scissorRight = (uint16_t)frame->width,
        .lineCount = (uint16_t)frame->height,
    };

    if (!forcedBlank) {
        for (int priority = 3; priority >= 0; --priority) {
            for (int bg = MODE1_GBA_BG_COUNT - 1; bg >= 0; --bg) {
                for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
                    const PpuGpu3DSBand* band = &bands[bandIndex];
                    const uint16_t dispcnt =
                            frame->dispcntPerLine[band->firstLine];
                    const uint8_t* io =
                            frame->ioPerLine +
                            (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
                    const uint16_t bgcnt =
                            read16(io, MODE1_IO_BG0CNT + (unsigned)bg * 2u);
                    if ((dispcnt & (MODE1_DISP_BG0_ON << bg)) == 0 ||
                        (bgcnt & 3u) != (unsigned)priority) {
                        continue;
                    }
                    if (!build_text_bg(frame, cache, atlas, cmd, band,
                                       (unsigned)bg, true, &vertexCursor,
                                       &indexCursor, &batchCursor)) {
                        return false;
                    }
                }
            }
        }
    }

    cmd->vertexCount = vertexCursor;
    cmd->indexCount = indexCursor;
    cmd->batchCount = batchCursor;
    return true;
}
