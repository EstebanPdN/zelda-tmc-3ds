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

enum {
    PPU_GPU3DS_REGION_OUTSIDE,
    PPU_GPU3DS_REGION_OBJWIN,
    PPU_GPU3DS_REGION_WIN1,
    PPU_GPU3DS_REGION_WIN0,
    PPU_GPU3DS_REGION_SHIFT = 6,
    PPU_GPU3DS_OBJ_TILE_BASE = 0x10000
};

typedef struct PpuGpu3DSObj {
    int x, y, width, height, boundsWidth, boundsHeight;
    uint16_t baseTile;
    uint8_t priority, palette, mode, index;
    bool affine, bpp8, hflip, vflip, doubleSize;
    int16_t pa, pb, pc, pd;
} PpuGpu3DSObj;

typedef struct PpuGpu3DSRegion {
    uint16_t left, right;
    uint8_t control, kind;
} PpuGpu3DSRegion;

static bool read_obj(const PpuGpu3DSFrameView* frame, unsigned index,
                     PpuGpu3DSObj* obj) {
    static const uint8_t widths[3][4] = {
        { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 }
    };
    static const uint8_t heights[3][4] = {
        { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 }
    };
    const uint16_t attr0 = frame->memory.oam_mem[index * 4u];
    const uint16_t attr1 = frame->memory.oam_mem[index * 4u + 1u];
    const uint16_t attr2 = frame->memory.oam_mem[index * 4u + 2u];
    const bool affine = (attr0 & (1u << 8u)) != 0;
    const unsigned shape = attr0 >> 14u;
    const unsigned size = attr1 >> 14u;
    const unsigned mode = (attr0 >> 10u) & 3u;
    if (shape >= 3u || (!affine && (attr0 & (1u << 9u)) != 0) || mode == 3u)
        return false;

    *obj = (PpuGpu3DSObj){
        .x = attr1 & 0x1ffu,
        .y = attr0 & 0xffu,
        .width = widths[shape][size],
        .height = heights[shape][size],
        .baseTile = attr2 & 0x03ffu,
        .priority = (uint8_t)((attr2 >> 10u) & 3u),
        .palette = (uint8_t)(attr2 >> 12u),
        .mode = (uint8_t)mode,
        .index = (uint8_t)index,
        .affine = affine,
        .bpp8 = (attr0 & (1u << 13u)) != 0,
        .hflip = !affine && (attr1 & (1u << 12u)) != 0,
        .vflip = !affine && (attr1 & (1u << 13u)) != 0,
        .doubleSize = affine && (attr0 & (1u << 9u)) != 0,
        .pa = 0x100,
        .pd = 0x100,
    };
    obj->boundsWidth = obj->width * (obj->doubleSize ? 2 : 1);
    obj->boundsHeight = obj->height * (obj->doubleSize ? 2 : 1);
    if (obj->y >= MODE1_GBA_HEIGHT) obj->y -= 256;
    if (obj->x >= (int)frame->width) obj->x -= 512;
    if (affine) {
        const unsigned group = (attr1 >> 9u) & 0x1fu;
        obj->pa = (int16_t)frame->memory.oam_mem[group * 16u + 3u];
        obj->pb = (int16_t)frame->memory.oam_mem[group * 16u + 7u];
        obj->pc = (int16_t)frame->memory.oam_mem[group * 16u + 11u];
        obj->pd = (int16_t)frame->memory.oam_mem[group * 16u + 15u];
    }
    return true;
}

static bool window_vertical_active(uint16_t dispcnt, uint16_t enable,
                                   uint16_t vertical, unsigned line) {
    unsigned top = vertical >> 8u;
    unsigned bottom = vertical & 0xffu;
    if ((dispcnt & enable) == 0) return false;
    if (bottom > MODE1_GBA_HEIGHT) bottom = MODE1_GBA_HEIGHT;
    return top > bottom ? line >= top || line < bottom
                        : line >= top && line < bottom;
}

static unsigned window_state(const uint8_t* io, uint16_t dispcnt, unsigned line) {
    return (window_vertical_active(dispcnt, MODE1_DISP_WIN0_ON,
                                   read16(io, MODE1_IO_WIN0V), line)
                    ? 1u
                    : 0u) |
           (window_vertical_active(dispcnt, MODE1_DISP_WIN1_ON,
                                   read16(io, MODE1_IO_WIN1V), line)
                    ? 2u
                    : 0u);
}

static size_t split_window_bands(const PpuGpu3DSFrameView* frame,
                                 const PpuGpu3DSBand* input, size_t inputCount,
                                 PpuGpu3DSBand out[MODE1_GBA_HEIGHT]) {
    size_t count = 0;
    for (size_t i = 0; i < inputCount; ++i) {
        const PpuGpu3DSBand* source = &input[i];
        const uint8_t* io =
                frame->ioPerLine + (size_t)source->ioRow * MODE1_IO_MEM_SIZE;
        const uint16_t dispcnt = frame->dispcntPerLine[source->firstLine];
        unsigned first = source->firstLine;
        const unsigned end = first + source->lineCount;
        while (first < end) {
            const unsigned state = window_state(io, dispcnt, first);
            unsigned next = first + 1u;
            while (next < end && window_state(io, dispcnt, next) == state) ++next;
            out[count++] = (PpuGpu3DSBand){
                .firstLine = (uint16_t)first,
                .lineCount = (uint16_t)(next - first),
                .ioRow = source->ioRow,
            };
            first = next;
        }
    }
    return count;
}

static bool frame_features_supported(const PpuGpu3DSFrameView* frame,
                                     const PpuGpu3DSBand* bands, size_t bandCount,
                                     bool forcedBlank) {
    if (frame->affine) return false;
    for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        const PpuGpu3DSBand* band = &bands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        const uint8_t* io =
                frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
        if (((dispcnt & MODE1_DISP_FORCED_BLANK) != 0) != forcedBlank ||
            (read16(io, MODE1_IO_BLDCNT) & 0x00c0u) != 0)
            return false;
        for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
            if ((read16(io, MODE1_IO_BG0CNT + bg * 2u) & (1u << 6u)) != 0)
                return false;
        }
        if ((dispcnt & MODE1_DISP_OBJ_ON) != 0) {
            if (!frame->memory.oam_mem) return false;
            for (unsigned index = 0; index < MODE1_GBA_OAM_COUNT; ++index) {
                PpuGpu3DSObj obj;
                if (read_obj(frame, index, &obj) &&
                    (frame->memory.oam_mem[index * 4u] & (1u << 12u)) != 0)
                    return false;
            }
        }
    }
    return true;
}

static bool in_horizontal_window(unsigned x, unsigned left, unsigned right,
                                 unsigned width) {
    if (left == right) return false;
    if (right > width) right = width;
    return left > right ? x >= left || x < right : x >= left && x < right;
}

static size_t build_regions(const PpuGpu3DSFrameView* frame,
                            const PpuGpu3DSBand* band,
                            PpuGpu3DSRegion out[PPU_GPU3DS_ATLAS_SIDE * 2u]) {
    uint8_t fixed[PPU_GPU3DS_ATLAS_SIDE] = { 0 };
    bool objCandidate[PPU_GPU3DS_ATLAS_SIDE] = { false };
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
    const bool win0Active = window_vertical_active(
            dispcnt, MODE1_DISP_WIN0_ON, read16(io, MODE1_IO_WIN0V),
            band->firstLine);
    const bool win1Active = window_vertical_active(
            dispcnt, MODE1_DISP_WIN1_ON, read16(io, MODE1_IO_WIN1V),
            band->firstLine);
    const bool objwinActive =
            (dispcnt & (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON)) ==
            (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON);
    const bool anyWindow =
            (dispcnt & (MODE1_DISP_WIN0_ON | MODE1_DISP_WIN1_ON |
                        MODE1_DISP_OBJWIN_ON)) != 0;
    if (!anyWindow) {
        out[0] = (PpuGpu3DSRegion){
            .right = (uint16_t)frame->width,
            .control = 0x3f,
            .kind = PPU_GPU3DS_REGION_WIN0,
        };
        return 1;
    }

    const uint16_t win0h = read16(io, MODE1_IO_WIN0H);
    const uint16_t win1h = read16(io, MODE1_IO_WIN1H);
    for (unsigned x = 0; x < frame->width; ++x) {
        if (win1Active &&
            in_horizontal_window(x, win1h >> 8u, win1h & 0xffu, frame->width))
            fixed[x] = PPU_GPU3DS_REGION_WIN1;
        if (win0Active &&
            in_horizontal_window(x, win0h >> 8u, win0h & 0xffu, frame->width))
            fixed[x] = PPU_GPU3DS_REGION_WIN0;
    }

    if (objwinActive) {
        const int bandTop = band->firstLine;
        const int bandBottom = bandTop + band->lineCount;
        for (unsigned index = 0; index < MODE1_GBA_OAM_COUNT; ++index) {
            PpuGpu3DSObj obj;
            if (!read_obj(frame, index, &obj) || obj.mode != 2u ||
                obj.y >= bandBottom || obj.y + obj.boundsHeight <= bandTop)
                continue;
            int left = obj.x;
            int right = obj.x + obj.boundsWidth;
            if (left < 0) left = 0;
            if (right > (int)frame->width) right = (int)frame->width;
            for (int x = left; x < right; ++x) objCandidate[x] = true;
        }
    }

    const uint16_t winin = read16(io, MODE1_IO_WININ);
    const uint16_t winout = read16(io, MODE1_IO_WINOUT);
    const uint8_t controls[4] = {
        (uint8_t)(winout & 0x3fu),
        (uint8_t)((winout >> 8u) & 0x3fu),
        (uint8_t)((winin >> 8u) & 0x3fu),
        (uint8_t)(winin & 0x3fu),
    };
    size_t count = 0;
    unsigned left = 0;
    while (left < frame->width) {
        const unsigned kind = fixed[left];
        const bool candidate = kind == PPU_GPU3DS_REGION_OUTSIDE &&
                               objCandidate[left];
        unsigned right = left + 1u;
        while (right < frame->width && fixed[right] == kind &&
               (kind != PPU_GPU3DS_REGION_OUTSIDE ||
                objCandidate[right] == candidate))
            ++right;
        out[count++] = (PpuGpu3DSRegion){
            .left = (uint16_t)left,
            .right = (uint16_t)right,
            .control = controls[kind],
            .kind = (uint8_t)kind,
        };
        if (candidate) {
            out[count++] = (PpuGpu3DSRegion){
                .left = (uint16_t)left,
                .right = (uint16_t)right,
                .control = controls[PPU_GPU3DS_REGION_OBJWIN],
                .kind = PPU_GPU3DS_REGION_OBJWIN,
            };
        }
        left = right;
    }
    return count;
}

static bool layer_has_region(const PpuGpu3DSRegion* regions, size_t regionCount,
                             uint8_t layer, uint16_t left, uint16_t right) {
    const unsigned bit = 1u << layer;
    for (size_t i = 0; i < regionCount; ++i) {
        if ((regions[i].control & bit) != 0 && regions[i].left < right &&
            regions[i].right > left)
            return true;
    }
    return false;
}

static void append_layer_batches(PpuGpu3DSCommandBuffer* cmd,
                                 const PpuGpu3DSBatch* base,
                                 const PpuGpu3DSRegion* regions,
                                 size_t regionCount, bool emit,
                                 size_t* batchCursor) {
    const unsigned bit = 1u << base->layer;
    for (size_t i = 0; i < regionCount; ++i) {
        if ((regions[i].control & bit) == 0) continue;
        uint16_t left = base->scissorLeft > regions[i].left
                                ? base->scissorLeft
                                : regions[i].left;
        uint16_t right = base->scissorRight < regions[i].right
                                 ? base->scissorRight
                                 : regions[i].right;
        if (left >= right) continue;
        if (emit) {
            cmd->batches[*batchCursor] = *base;
            cmd->batches[*batchCursor].scissorLeft = left;
            cmd->batches[*batchCursor].scissorRight = right;
            cmd->batches[*batchCursor].windowControl = regions[i].control;
            cmd->batches[*batchCursor].target2 =
                    (uint8_t)((base->target2 & 0x3fu) |
                              (regions[i].kind << PPU_GPU3DS_REGION_SHIFT));
        }
        ++*batchCursor;
    }
}

static bool build_text_bg(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                          uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                          const PpuGpu3DSBand* band, unsigned bg,
                          const PpuGpu3DSRegion* regions, size_t regionCount,
                          bool emit, size_t* vertexCursor, size_t* indexCursor,
                          size_t* batchCursor) {
    if (!layer_has_region(regions, regionCount, (uint8_t)(PPU_GPU3DS_BG0 + bg),
                          0, (uint16_t)frame->width))
        return true;
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
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
    const unsigned firstTileY = (sourceY >> 3u) & (mapHeightTiles - 1u);
    const int firstY = (int)band->firstLine - (int)(sourceY & 7u);
    const unsigned firstTileX = (scrollX >> 3u) & (mapWidthTiles - 1u);
    const int firstX = -(int)(scrollX & 7u);
    const int bandBottom = (int)band->firstLine + band->lineCount;
    const size_t firstIndex = *indexCursor;

    unsigned tileRowOffset = 0;
    for (int y = firstY; y < bandBottom;
         y += PPU_GPU3DS_TILE_SIDE, ++tileRowOffset) {
        const unsigned tileY =
                (firstTileY + tileRowOffset) & (mapHeightTiles - 1u);
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
                mapOffset > MODE1_VRAM_SIZE - 2u - screenBase)
                return false;
            const uint16_t entry =
                    read16(frame->memory.vram, screenBase + mapOffset);
            const uint32_t tileBytes = bpp8 ? 64u : 32u;
            const uint32_t tileOffset =
                    charBase + (uint32_t)(entry & 0x03ffu) * tileBytes;
            if (tileOffset > MODE1_VRAM_SIZE - tileBytes) return false;

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
                            atlas, &slot))
                    return false;
                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                const float slotLeft =
                        (float)((slot % (PPU_GPU3DS_ATLAS_SIDE /
                                        PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                const float slotTop =
                        (float)((slot / (PPU_GPU3DS_ATLAS_SIDE /
                                        PPU_GPU3DS_TILE_SIDE)) *
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
                const float left =
                        2.0f * (float)x / (float)frame->width - 1.0f;
                const float right =
                        2.0f * (float)(x + PPU_GPU3DS_TILE_SIDE) /
                                (float)frame->width -
                        1.0f;
                const float top =
                        1.0f - 2.0f * (float)y / (float)frame->height;
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

    const PpuGpu3DSBatch base = {
        .firstIndex = (uint32_t)firstIndex,
        .indexCount = (uint32_t)(*indexCursor - firstIndex),
        .firstLine = band->firstLine,
        .lineCount = band->lineCount,
        .scissorRight = (uint16_t)frame->width,
        .layer = (uint8_t)(PPU_GPU3DS_BG0 + bg),
        .priority = (uint8_t)priority,
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .target2 = (uint8_t)((read16(io, MODE1_IO_BLDCNT) >> 8u) & 0x3fu),
        .objectIndex = UINT8_MAX,
    };
    append_layer_batches(cmd, &base, regions, regionCount, emit, batchCursor);
    return true;
}

static bool build_obj(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                      uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                      const PpuGpu3DSBand* band, const PpuGpu3DSObj* obj,
                      const PpuGpu3DSRegion* regions, size_t regionCount,
                      bool stencilOnly, bool emit, size_t* vertexCursor,
                      size_t* indexCursor, size_t* batchCursor) {
    if ((obj->mode == 2u) != stencilOnly) return true;
    const int bandTop = band->firstLine;
    const int bandBottom = bandTop + band->lineCount;
    int clipLeft = obj->x;
    int clipRight = obj->x + obj->boundsWidth;
    int clipTop = obj->y > bandTop ? obj->y : bandTop;
    int clipBottom =
            obj->y + obj->boundsHeight < bandBottom
                    ? obj->y + obj->boundsHeight
                    : bandBottom;
    if (clipLeft < 0) clipLeft = 0;
    if (clipRight > (int)frame->width) clipRight = (int)frame->width;
    if (clipTop < 0) clipTop = 0;
    if (clipBottom > (int)frame->height) clipBottom = (int)frame->height;
    if (!stencilOnly && frame->objClipEnable && frame->objClipMark &&
        frame->objClipMark[obj->index] && clipBottom > frame->objClipY)
        clipBottom = frame->objClipY;
    if (clipLeft >= clipRight || clipTop >= clipBottom) return true;
    if (!stencilOnly &&
        !layer_has_region(regions, regionCount, PPU_GPU3DS_OBJ,
                          (uint16_t)clipLeft, (uint16_t)clipRight))
        return true;

    const int64_t determinant =
            (int64_t)obj->pa * obj->pd - (int64_t)obj->pb * obj->pc;
    if (obj->affine && determinant == 0) return false;
    const size_t firstIndex = *indexCursor;
    const int tilesWide = obj->width / PPU_GPU3DS_TILE_SIDE;
    const int tilesHigh = obj->height / PPU_GPU3DS_TILE_SIDE;
    const unsigned tileScale = obj->bpp8 ? 2u : 1u;
    const uint16_t baseTile =
            obj->bpp8 ? (uint16_t)(obj->baseTile & ~1u) : obj->baseTile;
    for (int tileRow = 0; tileRow < tilesHigh; ++tileRow) {
        for (int tileCol = 0; tileCol < tilesWide; ++tileCol) {
            const bool obj1d =
                    (frame->dispcntPerLine[band->firstLine] &
                     MODE1_DISP_OBJ_1D) != 0;
            const uint32_t tileIndex =
                    obj1d ? baseTile +
                                    (unsigned)(tileRow * tilesWide + tileCol) *
                                            tileScale
                          : baseTile + (unsigned)tileRow * 32u +
                                    (unsigned)tileCol * tileScale;
            const uint32_t tileBytes = obj->bpp8 ? 64u : 32u;
            const uint32_t tileOffset =
                    PPU_GPU3DS_OBJ_TILE_BASE + tileIndex * 32u;
            if (tileOffset > MODE1_VRAM_SIZE - tileBytes) return false;

            if (emit) {
                uint16_t slot;
                if (!PpuGpu3DS_CacheTile(
                            cache, frame->memory.vram,
                            (PpuGpu3DSTileKey){
                                .vramOffset = tileOffset,
                                .paletteBank =
                                        obj->bpp8 ? 0 : obj->palette,
                                .bpp8 = obj->bpp8,
                                .domain = PPU_GPU3DS_PALETTE_OBJ,
                            },
                            atlas, &slot))
                    return false;
                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                const float slotLeft =
                        (float)((slot % (PPU_GPU3DS_ATLAS_SIDE /
                                        PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                const float slotTop =
                        (float)((slot / (PPU_GPU3DS_ATLAS_SIDE /
                                        PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                float u0 = slotLeft;
                float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
                float v0 = slotTop;
                float v1 = slotTop + PPU_GPU3DS_TILE_SIDE * invAtlas;
                float px[4], py[4];
                if (obj->affine) {
                    const int sourceX[4] = {
                        tileCol * PPU_GPU3DS_TILE_SIDE,
                        (tileCol + 1) * PPU_GPU3DS_TILE_SIDE,
                        (tileCol + 1) * PPU_GPU3DS_TILE_SIDE,
                        tileCol * PPU_GPU3DS_TILE_SIDE,
                    };
                    const int sourceY[4] = {
                        tileRow * PPU_GPU3DS_TILE_SIDE,
                        tileRow * PPU_GPU3DS_TILE_SIDE,
                        (tileRow + 1) * PPU_GPU3DS_TILE_SIDE,
                        (tileRow + 1) * PPU_GPU3DS_TILE_SIDE,
                    };
                    const float centerX =
                            obj->x + obj->boundsWidth * 0.5f;
                    const float centerY =
                            obj->y + obj->boundsHeight * 0.5f;
                    for (unsigned corner = 0; corner < 4; ++corner) {
                        const int sx = sourceX[corner] - obj->width / 2;
                        const int sy = sourceY[corner] - obj->height / 2;
                        px[corner] =
                                centerX +
                                (float)(((int64_t)obj->pd * sx -
                                         (int64_t)obj->pb * sy) *
                                        256) /
                                        (float)determinant;
                        py[corner] =
                                centerY +
                                (float)((-(int64_t)obj->pc * sx +
                                         (int64_t)obj->pa * sy) *
                                        256) /
                                        (float)determinant;
                    }
                } else {
                    const int left =
                            obj->x +
                            (obj->hflip ? obj->width -
                                                  (tileCol + 1) *
                                                          PPU_GPU3DS_TILE_SIDE
                                        : tileCol * PPU_GPU3DS_TILE_SIDE);
                    const int top =
                            obj->y +
                            (obj->vflip ? obj->height -
                                                  (tileRow + 1) *
                                                          PPU_GPU3DS_TILE_SIDE
                                        : tileRow * PPU_GPU3DS_TILE_SIDE);
                    px[0] = px[3] = (float)left;
                    px[1] = px[2] = (float)(left + PPU_GPU3DS_TILE_SIDE);
                    py[0] = py[1] = (float)top;
                    py[2] = py[3] = (float)(top + PPU_GPU3DS_TILE_SIDE);
                    if (obj->hflip) {
                        const float swap = u0;
                        u0 = u1;
                        u1 = swap;
                    }
                    if (obj->vflip) {
                        const float swap = v0;
                        v0 = v1;
                        v1 = swap;
                    }
                }
                const float z = (128.0f - obj->index) / 129.0f;
                const float uv[4][2] = {
                    { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 }
                };
                const uint16_t base = (uint16_t)*vertexCursor;
                for (unsigned corner = 0; corner < 4; ++corner) {
                    cmd->vertices[*vertexCursor + corner] =
                            (PpuGpu3DSVertex){
                                2.0f * px[corner] / frame->width - 1.0f,
                                1.0f - 2.0f * py[corner] / frame->height,
                                z,
                                1.0f,
                                uv[corner][0],
                                uv[corner][1],
                            };
                }
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

    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const PpuGpu3DSBatch base = {
        .firstIndex = (uint32_t)firstIndex,
        .indexCount = (uint32_t)(*indexCursor - firstIndex),
        .firstLine = (uint16_t)clipTop,
        .lineCount = (uint16_t)(clipBottom - clipTop),
        .scissorLeft = (uint16_t)clipLeft,
        .scissorRight = (uint16_t)clipRight,
        .layer = PPU_GPU3DS_OBJ,
        .priority = obj->priority,
        .windowControl = stencilOnly
                                 ? (uint8_t)(read16(io, MODE1_IO_WINOUT) >> 8u) &
                                           0x3fu
                                 : 0,
        .target2 = (uint8_t)((read16(io, MODE1_IO_BLDCNT) >> 8u) & 0x3fu) |
                   (stencilOnly
                            ? PPU_GPU3DS_REGION_OBJWIN
                                      << PPU_GPU3DS_REGION_SHIFT
                            : 0),
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .objectIndex = obj->index,
        .objWindow = stencilOnly,
        .semiTransparent = obj->mode == 1u,
    };
    if (stencilOnly) {
        if (emit) cmd->batches[*batchCursor] = base;
        ++*batchCursor;
    } else {
        append_layer_batches(cmd, &base, regions, regionCount, emit,
                             batchCursor);
    }
    return true;
}

static bool build_scene(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                        uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                        const PpuGpu3DSBand* bands, size_t bandCount, bool emit,
                        size_t* vertexCursor, size_t* indexCursor,
                        size_t* batchCursor) {
    PpuGpu3DSRegion regions[PPU_GPU3DS_ATLAS_SIDE * 2u];
    for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        const PpuGpu3DSBand* band = &bands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        if ((dispcnt & (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON)) !=
            (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON))
            continue;
        for (int index = MODE1_GBA_OAM_COUNT - 1; index >= 0; --index) {
            PpuGpu3DSObj obj;
            if (read_obj(frame, (unsigned)index, &obj) && obj.mode == 2u &&
                !build_obj(frame, cache, atlas, cmd, band, &obj, NULL, 0,
                           true, emit, vertexCursor, indexCursor, batchCursor))
                return false;
        }
    }

    for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        const PpuGpu3DSBand* band = &bands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        const uint8_t* io =
                frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
        const size_t regionCount = build_regions(frame, band, regions);
        for (int priority = 3; priority >= 0; --priority) {
            for (int bg = MODE1_GBA_BG_COUNT - 1; bg >= 0; --bg) {
                const uint16_t bgcnt =
                        read16(io, MODE1_IO_BG0CNT + (unsigned)bg * 2u);
                if ((dispcnt & (MODE1_DISP_BG0_ON << bg)) == 0 ||
                    (bgcnt & 3u) != (unsigned)priority)
                    continue;
                if (!build_text_bg(frame, cache, atlas, cmd, band,
                                   (unsigned)bg, regions, regionCount, emit,
                                   vertexCursor, indexCursor, batchCursor))
                    return false;
            }
            if ((dispcnt & MODE1_DISP_OBJ_ON) == 0) continue;
            for (int index = MODE1_GBA_OAM_COUNT - 1; index >= 0; --index) {
                PpuGpu3DSObj obj;
                if (!read_obj(frame, (unsigned)index, &obj) || obj.mode == 2u ||
                    obj.priority != (unsigned)priority)
                    continue;
                if (!build_obj(frame, cache, atlas, cmd, band, &obj, regions,
                               regionCount, false, emit, vertexCursor,
                               indexCursor, batchCursor))
                    return false;
            }
        }
    }
    return true;
}

bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame,
                             PpuGpu3DSCache* cache, uint16_t* atlas,
                             PpuGpu3DSCommandBuffer* cmd) {
    if (!frame || !cache || !atlas || !cmd || !cmd->vertices || !cmd->indices ||
        !cmd->batches || !frame->memory.vram || !frame->memory.bg_palette ||
        !frame->ioPerLine || !frame->dispcntPerLine || frame->affine ||
        frame->width == 0 || frame->width > PPU_GPU3DS_ATLAS_SIDE ||
        frame->height == 0 || frame->height > MODE1_GBA_HEIGHT)
        return false;

    PpuGpu3DSBand sourceBands[MODE1_GBA_HEIGHT];
    PpuGpu3DSBand bands[MODE1_GBA_HEIGHT];
    const size_t sourceBandCount = PpuGpu3DS_BuildBands(frame, sourceBands);
    const bool forcedBlank =
            (frame->frameDispcnt & MODE1_DISP_FORCED_BLANK) != 0;
    if (sourceBandCount == 0 ||
        !frame_features_supported(frame, sourceBands, sourceBandCount,
                                  forcedBlank))
        return false;
    const size_t bandCount =
            split_window_bands(frame, sourceBands, sourceBandCount, bands);

    const size_t startVertex = cmd->vertexCount;
    const size_t startIndex = cmd->indexCount;
    const size_t startBatch = cmd->batchCount;
    size_t requiredVertices = 0;
    size_t requiredIndices = 0;
    size_t requiredBatches = 1;
    if (!forcedBlank &&
        !build_scene(frame, cache, atlas, cmd, bands, bandCount, false,
                     &requiredVertices, &requiredIndices, &requiredBatches))
        return false;

    PpuGpu3DSCommandBuffer capacity = *cmd;
    if (!PpuGpu3DS_CommandReserve(&capacity, requiredVertices, requiredIndices,
                                  requiredBatches) ||
        startVertex > UINT16_MAX ||
        requiredVertices > (size_t)UINT16_MAX + 1u - startVertex)
        return false;

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
    if (!forcedBlank &&
        !build_scene(frame, cache, atlas, cmd, bands, bandCount, true,
                     &vertexCursor, &indexCursor, &batchCursor))
        return false;

    cmd->vertexCount = vertexCursor;
    cmd->indexCount = indexCursor;
    cmd->batchCount = batchCursor;
    return true;
}
