#include "port_ppu_gpu_3ds_model.h"

#if defined(__3DS__)
#include "arm11_fast_mem.h"
#else
#define Arm11FastMemcpy(d, s, n) memcpy((d), (s), (n))
#endif

#include <string.h>

/* Phase timing, compiled only into the host benchmark (PPU_GPU3DS_PROFILE).
 * The console build has no timers in the builder at all. */
#ifdef PPU_GPU3DS_PROFILE
double gPpuGpu3DSPhase[PPU_GPU3DS_PHASE_COUNT];
#ifdef __3DS__
#include <3ds.h>
/* System ticks on the console; the caller converts. */
static double profile_now(void) { return (double)svcGetSystemTick(); }
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
static double profile_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif
#define PROFILE_BEGIN(phase) const double profileStart##phase = profile_now()
#define PROFILE_END(phase) \
    gPpuGpu3DSPhase[phase] += profile_now() - profileStart##phase
#else
#define PROFILE_BEGIN(phase) ((void)0)
#define PROFILE_END(phase) ((void)0)
#endif

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

/* Morton position of each row-major pixel in a tile. The atlas is sampled in
 * this order, and deriving it cost a call and six shift-mask pairs per pixel. */
static const uint8_t kTileMorton[64] = {
     0,  1,  4,  5, 16, 17, 20, 21,
     2,  3,  6,  7, 18, 19, 22, 23,
     8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53,
    34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61,
    42, 43, 46, 47, 58, 59, 62, 63
};

uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque) {
    return (uint16_t)(((gbaColor & 0x001fu) << 11u) | ((gbaColor & 0x03e0u) << 1u) |
                      ((gbaColor & 0x7c00u) >> 9u) | (opaque ? 1u : 0u));
}

uint16_t PpuGpu3DS_PackAbgr8888(uint32_t abgr) {
    return (uint16_t)((((abgr >> 0u) & 0xffu) >> 3u) << 11u |
                      (((abgr >> 8u) & 0xffu) >> 3u) << 6u |
                      (((abgr >> 16u) & 0xffu) >> 3u) << 1u | 1u);
}

static unsigned cache_bucket(PpuGpu3DSTileKey key) {
    /* Tiles are 32- or 64-byte aligned, so the low VRAM bits carry no
     * information; mix what is left with the palette selection. */
    uint32_t hash = (key.vramOffset >> 5u) * 0x9e3779b1u;
    hash ^= ((uint32_t)key.paletteBank + 1u) * 0x85ebca6bu;
    hash ^= ((uint32_t)key.domain << 1u) ^ (uint32_t)key.bpp8;
    hash ^= hash >> 15u;
    return hash & (PPU_GPU3DS_CACHE_BUCKETS - 1u);
}

static void cache_lru_unlink(PpuGpu3DSCache* cache, uint16_t slot) {
    PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
    if (entry->lruPrev == PPU_GPU3DS_CACHE_NIL)
        cache->lruHead = entry->lruNext;
    else
        cache->entries[entry->lruPrev].lruNext = entry->lruNext;
    if (entry->lruNext == PPU_GPU3DS_CACHE_NIL)
        cache->lruTail = entry->lruPrev;
    else
        cache->entries[entry->lruNext].lruPrev = entry->lruPrev;
}

static void cache_lru_push_head(PpuGpu3DSCache* cache, uint16_t slot) {
    PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
    entry->lruPrev = PPU_GPU3DS_CACHE_NIL;
    entry->lruNext = cache->lruHead;
    if (cache->lruHead != PPU_GPU3DS_CACHE_NIL)
        cache->entries[cache->lruHead].lruPrev = slot;
    cache->lruHead = slot;
    if (cache->lruTail == PPU_GPU3DS_CACHE_NIL) cache->lruTail = slot;
}

static void cache_touch(PpuGpu3DSCache* cache, uint16_t slot) {
    if (cache->lruHead == slot) return;
    cache_lru_unlink(cache, slot);
    cache_lru_push_head(cache, slot);
}

static void cache_bucket_unlink(PpuGpu3DSCache* cache, uint16_t slot) {
    uint16_t* link = &cache->buckets[cache_bucket(cache->entries[slot].key)];
    while (*link != PPU_GPU3DS_CACHE_NIL) {
        if (*link == slot) {
            *link = cache->entries[slot].hashNext;
            return;
        }
        link = &cache->entries[*link].hashNext;
    }
}

static void cache_mark_dirty(PpuGpu3DSCache* cache, uint16_t slot) {
    if (cache->entries[slot].dirty) return;
    cache->entries[slot].dirty = true;
    if (cache->dirtyCount < PPU_GPU3DS_SLOT_COUNT)
        cache->dirtySlots[cache->dirtyCount++] = slot;
}

void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache) {
    memset(cache, 0, sizeof(*cache));
    for (unsigned bucket = 0; bucket < PPU_GPU3DS_CACHE_BUCKETS; ++bucket)
        cache->buckets[bucket] = PPU_GPU3DS_CACHE_NIL;
    for (unsigned slot = 0; slot < PPU_GPU3DS_SLOT_COUNT; ++slot) {
        PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
        entry->hashNext = PPU_GPU3DS_CACHE_NIL;
        entry->lruPrev =
                (uint16_t)(slot == 0 ? PPU_GPU3DS_CACHE_NIL : slot - 1u);
        entry->lruNext = (uint16_t)(slot + 1u == PPU_GPU3DS_SLOT_COUNT
                                            ? PPU_GPU3DS_CACHE_NIL
                                            : slot + 1u);
    }
    cache->lruHead = 0;
    cache->lruTail = PPU_GPU3DS_SLOT_COUNT - 1u;
}

void PpuGpu3DS_CacheClearDirty(PpuGpu3DSCache* cache) {
    for (unsigned index = 0; index < cache->dirtyCount; ++index)
        cache->entries[cache->dirtySlots[index]].dirty = false;
    cache->dirtyCount = 0;
}

void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette,
                               const uint16_t* objPalette, uint32_t frame) {
    /* Slots used during this frame are the most recent entries in the LRU
     * order, so eviction recognizes them by lastUseFrame without a sweep. */
    cache->exhausted = false;
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

    const unsigned bucket = cache_bucket(key);
    uint16_t selected = PPU_GPU3DS_CACHE_NIL;
    for (uint16_t slot = cache->buckets[bucket]; slot != PPU_GPU3DS_CACHE_NIL;
         slot = cache->entries[slot].hashNext) {
        PpuGpu3DSCacheEntry* entry = &cache->entries[slot];
        if (!entry->valid || !keys_equal(entry->key, key)) continue;
        if (entry->paletteGeneration == paletteGeneration &&
            memcmp(cache->sources[slot], source, tileBytes) == 0) {
            entry->lastUseFrame = cache->frame;
            if (!entry->pinned) cache_touch(cache, slot);
            ++cache->hits;
            *outSlot = slot;
            return true;
        }
        /* Same tile address, but its palette or its VRAM bytes moved on since
         * the decode. Refresh this slot rather than adding a second entry for
         * a key that can never match again — unless geometry emitted earlier
         * this frame already samples it. */
        if (!entry->pinned && entry->lastUseFrame != cache->frame)
            selected = slot;
        break;
    }

    if (selected == PPU_GPU3DS_CACHE_NIL) {
        /* Walk up from the least recently used slot, skipping anything this
         * frame already uses -- including slots only referenced by retained
         * map geometry, which is touched without being looked up. */
        selected = cache->lruTail;
        while (selected != PPU_GPU3DS_CACHE_NIL &&
               cache->entries[selected].valid &&
               cache->entries[selected].lastUseFrame == cache->frame)
            selected = cache->entries[selected].lruPrev;
        if (selected == PPU_GPU3DS_CACHE_NIL) {
            cache->exhausted = true;
            return false;
        }
        PpuGpu3DSCacheEntry* victim = &cache->entries[selected];
        if (victim->valid) cache_bucket_unlink(cache, selected);
        cache_lru_unlink(cache, selected);
        victim->lruPrev = PPU_GPU3DS_CACHE_NIL;
        victim->lruNext = cache->lruHead;
        if (cache->lruHead != PPU_GPU3DS_CACHE_NIL)
            cache->entries[cache->lruHead].lruPrev = selected;
        cache->lruHead = selected;
        if (cache->lruTail == PPU_GPU3DS_CACHE_NIL) cache->lruTail = selected;
        victim->hashNext = cache->buckets[bucket];
        cache->buckets[bucket] = selected;
    }

    PpuGpu3DSCacheEntry* entry = &cache->entries[selected];
    ++cache->decodes;
    const uint16_t* palette =
            key.domain == PPU_GPU3DS_PALETTE_BG ? cache->bgPalette : cache->objPalette;
    memcpy(cache->sources[selected], source, tileBytes);
    /* Every pixel of a 4bpp tile shares one 16-colour bank, so the pack that
     * used to run 64 times runs 16, each source byte is read once for its two
     * pixels instead of twice with a shift chosen per pixel, and the Morton
     * position is a table lookup. Decoding is the part of the build that
     * hardware does ~95 times a frame while a static-frame bench does ~1.5. */
    uint16_t* const dest = atlas + (size_t)selected * 64;
    unsigned opaqueMask = 0;
    if (!key.bpp8) {
        const unsigned domain =
                key.domain == PPU_GPU3DS_PALETTE_BG ? 0u : 1u;
        const unsigned bankIndex = key.paletteBank & 15u;
        uint16_t* const lut = cache->bankLut[domain][bankIndex];
        if (!cache->bankLutValid[domain][bankIndex] ||
            cache->bankLutGeneration[domain][bankIndex] != paletteGeneration) {
            const uint16_t* const bank = palette + bankIndex * 16u;
            lut[0] = 0;
            for (unsigned index = 1; index < 16u; ++index)
                lut[index] = PpuGpu3DS_PackRgba5551(bank[index], true);
            cache->bankLutGeneration[domain][bankIndex] = paletteGeneration;
            cache->bankLutValid[domain][bankIndex] = true;
        }
        for (unsigned pixel = 0; pixel < 64u; pixel += 2u) {
            const uint8_t pair = source[pixel >> 1u];
            const unsigned low = pair & 0x0fu;
            const unsigned high = pair >> 4u;
            dest[kTileMorton[pixel]] = lut[low];
            dest[kTileMorton[pixel + 1u]] = lut[high];
            opaqueMask |= low | high;
        }
    } else {
        for (unsigned pixel = 0; pixel < 64u; ++pixel) {
            const uint8_t colorIndex = source[pixel];
            dest[kTileMorton[pixel]] =
                    colorIndex ? PpuGpu3DS_PackRgba5551(palette[colorIndex], true)
                               : 0;
            opaqueMask |= colorIndex;
        }
    }
    entry->transparent = opaqueMask == 0;

    entry->key = key;
    entry->paletteGeneration = paletteGeneration;
    entry->lastUseFrame = cache->frame;
    entry->valid = true;
    cache_mark_dirty(cache, selected);
    cache_touch(cache, selected);
    *outSlot = selected;
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

int32_t PpuGpu3DS_AffineSample(int32_t reference, int16_t coefficient,
                               int screenCoordinate) {
    const int64_t fixed =
            (int64_t)reference + (int64_t)coefficient * screenCoordinate;
    int64_t sample = fixed / 256;
    if (fixed < 0 && fixed % 256 != 0) --sample;
    if (sample < INT32_MIN) return INT32_MIN;
    if (sample > INT32_MAX) return INT32_MAX;
    return (int32_t)sample;
}

static bool message_line(const PpuGpu3DSFrameView* frame, unsigned bg,
                         unsigned line) {
    return bg == 0 && frame->width > MODE1_GBA_BG_CLIP_X &&
           frame->wsMsgShift != 0 && (int)line >= frame->wsMsgY0 &&
           (int)line < frame->wsMsgY1;
}

int PpuGpu3DS_RemapBgX(const PpuGpu3DSFrameView* frame, unsigned bg,
                       unsigned line, int nativeX) {
    if (!frame || bg >= MODE1_GBA_BG_COUNT) return nativeX;
    if (message_line(frame, bg, line) && nativeX >= frame->wsMsgX0 &&
        nativeX < frame->wsMsgX1)
        return nativeX + frame->wsMsgShift;
    if (bg == 0 && frame->width > MODE1_GBA_BG_CLIP_X &&
        frame->wsHudRightAnchor && !message_line(frame, bg, line) &&
        nativeX >= frame->wsHudRightNativeX &&
        nativeX < MODE1_GBA_BG_CLIP_X)
        return nativeX + (int)frame->width - MODE1_GBA_BG_CLIP_X;
    return nativeX;
}

bool PpuGpu3DS_ShadowEntry(const PpuGpu3DSFrameView* frame, unsigned bg,
                           unsigned row, unsigned column, uint16_t* entry) {
    if (!frame || !entry || !frame->wsShadow ||
        bg >= MODE1_GBA_BG_COUNT || row >= 32u ||
        frame->wsShadowBaseTile[bg] < 0 || frame->wsCols <= 0 ||
        column >= (unsigned)frame->wsCols || frame->wsShadowHalfwords <= 0)
        return false;
    const size_t shadowRow = (size_t)bg * 32u + row;
    const size_t stride = (size_t)frame->wsCols;
    if (shadowRow > (SIZE_MAX - column) / stride) return false;
    const size_t index = shadowRow * stride + column;
    if (index >= (size_t)frame->wsShadowHalfwords) return false;
    *entry = frame->wsShadow[index];
    return true;
}

enum {
    PPU_GPU3DS_REGION_OUTSIDE,
    PPU_GPU3DS_REGION_OBJWIN,
    PPU_GPU3DS_REGION_WIN1,
    PPU_GPU3DS_REGION_WIN0,
    PPU_GPU3DS_REGION_SHIFT = 14,
    PPU_GPU3DS_ALPHA_COMPLEMENT = 1u << 13u,
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

/* Decoding OAM is band-independent, but the band loops used to redo all 128
 * entries for every band and every priority -- over 100k decodes on a frame
 * with per-line HDMA. The frame decodes them once and the loops walk the
 * result. Entries stay in OAM order so draw order is unchanged. */
typedef struct PpuGpu3DSOamSet {
    PpuGpu3DSObj entries[MODE1_GBA_OAM_COUNT];
    unsigned count;
    /* The draw loops run priority-major and walk every entry inside each band,
     * so a frame with per-line HDMA filtered all 128 entries four times per
     * band. Bucketing once per frame leaves each entry visited only in the
     * priority that will draw it; the buckets keep the descending order the
     * loops used, so draw order is unchanged. */
    uint8_t byPriority[4][MODE1_GBA_OAM_COUNT];
    uint8_t priorityCount[4];
} PpuGpu3DSOamSet;

static void build_oam_set(const PpuGpu3DSFrameView* frame,
                          PpuGpu3DSOamSet* set) {
    set->count = 0;
    for (unsigned priority = 0; priority < 4u; ++priority)
        set->priorityCount[priority] = 0;
    if (!frame->memory.oam_mem) return;
    for (unsigned index = 0; index < MODE1_GBA_OAM_COUNT; ++index) {
        if (read_obj(frame, index, &set->entries[set->count]))
            ++set->count;
    }
    for (int index = (int)set->count - 1; index >= 0; --index) {
        const PpuGpu3DSObj* obj = &set->entries[index];
        if (obj->mode == 2u) continue;
        const unsigned priority = obj->priority & 3u;
        set->byPriority[priority][set->priorityCount[priority]++] =
                (uint8_t)index;
    }
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
    for (size_t bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        const PpuGpu3DSBand* band = &bands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        if (((dispcnt & MODE1_DISP_FORCED_BLANK) != 0) != forcedBlank ||
            ((dispcnt & MODE1_DISP_OBJ_ON) != 0 && !frame->memory.oam_mem))
            return false;
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
                            const PpuGpu3DSOamSet* oam,
                            const PpuGpu3DSBand* band,
                            PpuGpu3DSRegion out[PPU_GPU3DS_ATLAS_SIDE * 2u]) {
    /* Cleared after the no-window early-out, and only across the visible
     * width: zeroing 1 KB per band was pure overhead on HDMA frames. */
    uint8_t fixed[PPU_GPU3DS_ATLAS_SIDE];
    bool objCandidate[PPU_GPU3DS_ATLAS_SIDE];
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

    memset(fixed, 0, frame->width);
    memset(objCandidate, 0, frame->width * sizeof(*objCandidate));
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
        for (unsigned entry = 0; entry < oam->count; ++entry) {
            const PpuGpu3DSObj obj = oam->entries[entry];
            if (obj.mode != 2u || obj.y >= bandBottom ||
                obj.y + obj.boundsHeight <= bandTop)
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

static uint8_t clamp_coefficient(uint16_t value) {
    return (uint8_t)(value > 16u ? 16u : value);
}

static bool quad_room(PpuGpu3DSCommandBuffer* cmd, const size_t* vertexCursor,
                      const size_t* indexCursor) {
    if (*vertexCursor + 4u > cmd->vertexCapacity ||
        *indexCursor + 6u > cmd->indexCapacity ||
        *vertexCursor + 4u > (size_t)UINT16_MAX + 1u) {
        cmd->overflow = true;
        return false;
    }
    return true;
}

/* Once a write has been dropped the frame is lost, so stop walking it. */
static bool build_abandoned(const PpuGpu3DSCommandBuffer* cmd) {
    return cmd->overflow;
}

static void store_batch(PpuGpu3DSCommandBuffer* cmd, size_t index,
                        const PpuGpu3DSBatch* batch, bool emit) {
    if (!emit) return;
    if (index >= cmd->batchCapacity) {
        cmd->overflow = true;
        return;
    }
    cmd->batches[index] = *batch;
}

/* Every quad's six indices are the same pattern at 6*(vertex/4), and the
 * cursors only ever advance four vertices to six indices -- verified across
 * every captured frame under churn and mutation. So the whole buffer can be
 * filled once at startup and never touched again: it saves writing ~52 KB of
 * indices a frame, and it saves flushing them, which is one GSP round trip of
 * roughly 330 us on an Old 3DS and one fewer wakeup on core 1, where the audio
 * thread lives. */
void PpuGpu3DS_FillStaticIndices(uint16_t* indices, size_t capacity) {
    const size_t quads = capacity / 6u;
    for (size_t quad = 0; quad < quads; ++quad) {
        const size_t base = quad * 4u;
        if (base + 3u > 0xffffu) break;
        uint16_t* out = indices + quad * 6u;
        out[0] = (uint16_t)base;
        out[1] = (uint16_t)(base + 1u);
        out[2] = (uint16_t)(base + 2u);
        out[3] = (uint16_t)base;
        out[4] = (uint16_t)(base + 2u);
        out[5] = (uint16_t)(base + 3u);
    }
}

static void emit_indices(PpuGpu3DSCommandBuffer* cmd, size_t* vertexCursor,
                         size_t* indexCursor) {
    /* Filled once by PpuGpu3DS_FillStaticIndices. */
    (void)cmd;
    (void)vertexCursor;
    (void)indexCursor;
}

static void append_layer_batches(PpuGpu3DSCommandBuffer* cmd,
                                 const PpuGpu3DSBatch* base,
                                 const PpuGpu3DSRegion* regions,
                                 size_t regionCount, uint16_t bldcnt,
                                 uint16_t bldalpha, uint16_t bldy, bool emit,
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

        PpuGpu3DSBatch batch = *base;
        batch.scissorLeft = left;
        batch.scissorRight = right;
        batch.windowControl = regions[i].control;
        batch.color = (uint16_t)(regions[i].kind << PPU_GPU3DS_REGION_SHIFT);
        batch.target2 =
                (uint8_t)((bldcnt >> (base->layer + 8u)) & 1u);
        const unsigned effect = (bldcnt >> 6u) & 3u;
        const bool effectsEnabled = (regions[i].control & 0x20u) != 0;
        const bool firstTarget = (bldcnt & bit) != 0;
        const bool alpha =
                effectsEnabled &&
                (base->semiTransparent ||
                 (effect == PPU_GPU3DS_EFFECT_ALPHA && firstTarget)) &&
                (bldcnt & 0x3f00u) != 0;
        if (alpha) {
            batch.effect = PPU_GPU3DS_EFFECT_ALPHA;
            batch.eva = clamp_coefficient(bldalpha & 0x1fu);
            batch.evb = clamp_coefficient((bldalpha >> 8u) & 0x1fu);
            PpuGpu3DSBatch complement = batch;
            complement.effect = PPU_GPU3DS_EFFECT_NONE;
            complement.color |= PPU_GPU3DS_ALPHA_COMPLEMENT;
            store_batch(cmd, *batchCursor,
                        batch.target2 ? &batch : &complement, emit);
            ++*batchCursor;
            store_batch(cmd, *batchCursor,
                        batch.target2 ? &complement : &batch, emit);
            ++*batchCursor;
            continue;
        }
        if (effectsEnabled && firstTarget && !base->semiTransparent &&
            (effect == PPU_GPU3DS_EFFECT_BRIGHTEN ||
             effect == PPU_GPU3DS_EFFECT_DARKEN)) {
            batch.effect = (uint8_t)effect;
            batch.evy = clamp_coefficient(bldy & 0x1fu);
        } else {
            batch.effect = PPU_GPU3DS_EFFECT_NONE;
        }
        store_batch(cmd, *batchCursor, &batch, emit);
        ++*batchCursor;
    }
}

static bool tile_sample_uv(PpuGpu3DSCache* cache, const uint8_t* vram,
                           PpuGpu3DSTileKey key, uint16_t* atlas,
                           unsigned pixelX, unsigned pixelY, bool emit,
                           float* u, float* v) {
    if (!emit) return true;
    uint16_t slot;
    if (!PpuGpu3DS_CacheTile(cache, vram, key, atlas, &slot)) return false;
    const unsigned tilesPerRow =
            PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE;
    const unsigned slotX =
            (slot % tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
    const unsigned slotY =
            (slot / tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
    *u = ((float)(slotX + pixelX) + 0.5f) / PPU_GPU3DS_ATLAS_SIDE;
    *v = 1.0f - ((float)(slotY + pixelY) + 0.5f) / PPU_GPU3DS_ATLAS_SIDE;
    return true;
}

static void emit_sample_quad(const PpuGpu3DSFrameView* frame,
                             PpuGpu3DSCommandBuffer* cmd, bool emit,
                             float left, float top, float right, float bottom,
                             float z, float u, float v,
                             size_t* vertexCursor, size_t* indexCursor) {
    if (emit && quad_room(cmd, vertexCursor, indexCursor)) {
        const float invWidth = 2.0f / (float)frame->width;
        const float invHeight = 2.0f / (float)frame->height;
        const float x0 = left * invWidth - 1.0f;
        const float x1 = right * invWidth - 1.0f;
        const float y0 = 1.0f - top * invHeight;
        const float y1 = 1.0f - bottom * invHeight;
        const int16_t pu = PpuGpu3DS_PackUV(u);
        const int16_t pv = PpuGpu3DS_PackUV(v);
        cmd->vertices[*vertexCursor + 0] =
                (PpuGpu3DSVertex){ x0, y0, z, pu, pv };
        cmd->vertices[*vertexCursor + 1] =
                (PpuGpu3DSVertex){ x1, y0, z, pu, pv };
        cmd->vertices[*vertexCursor + 2] =
                (PpuGpu3DSVertex){ x1, y1, z, pu, pv };
        cmd->vertices[*vertexCursor + 3] =
                (PpuGpu3DSVertex){ x0, y1, z, pu, pv };
        emit_indices(cmd, vertexCursor, indexCursor);
    }
    *vertexCursor += 4;
    *indexCursor += 6;
}
static void emit_uv_quad(const PpuGpu3DSFrameView* frame,
                         PpuGpu3DSCommandBuffer* cmd, bool emit, float left,
                         float top, float right, float bottom, float z,
                         float u0, float v0, float u1, float v1,
                         bool horizontalStrip, size_t* vertexCursor,
                         size_t* indexCursor) {
    if (emit && quad_room(cmd, vertexCursor, indexCursor)) {
        const float invWidth = 2.0f / (float)frame->width;
        const float invHeight = 2.0f / (float)frame->height;
        const float x0 = left * invWidth - 1.0f;
        const float x1 = right * invWidth - 1.0f;
        const float y0 = 1.0f - top * invHeight;
        const float y1 = 1.0f - bottom * invHeight;
        const int16_t pu0 = PpuGpu3DS_PackUV(u0);
        const int16_t pu1 = PpuGpu3DS_PackUV(u1);
        const int16_t pv0 = PpuGpu3DS_PackUV(v0);
        const int16_t pv1 = PpuGpu3DS_PackUV(v1);
        cmd->vertices[*vertexCursor + 0] =
                (PpuGpu3DSVertex){ x0, y0, z, pu0, pv0 };
        cmd->vertices[*vertexCursor + 1] =
                (PpuGpu3DSVertex){
                    x1, y0, z, pu1, horizontalStrip ? pv1 : pv0
                };
        cmd->vertices[*vertexCursor + 2] =
                (PpuGpu3DSVertex){ x1, y1, z, pu1, pv1 };
        cmd->vertices[*vertexCursor + 3] =
                (PpuGpu3DSVertex){
                    x0, y1, z, pu0, horizontalStrip ? pv0 : pv1
                };
        emit_indices(cmd, vertexCursor, indexCursor);
    }
    *vertexCursor += 4;
    *indexCursor += 6;
}

typedef struct PpuGpu3DSTextSample {
    PpuGpu3DSTileKey key;
    uint8_t pixelX, pixelY;
    bool visible;
} PpuGpu3DSTextSample;

/* Digest of everything a background's map-space geometry depends on, so an
 * unchanged layer can keep last frame's vertices instead of rebuilding them.
 * Reading the map and tile bytes costs a fraction of re-emitting the quads. */
static uint32_t map_signature(const PpuGpu3DSFrameView* frame, uint16_t bgcnt,
                              uint32_t screenBase, uint32_t screenBytes,
                              uint32_t charBase, unsigned rowLo, unsigned colLo,
                              unsigned rows, unsigned cols,
                              uint32_t paletteGeneration) {
    uint32_t hash = 2166136261u;
    const uint32_t header[8] = { bgcnt,     screenBase, charBase, rowLo,
                                 colLo,     rows,       cols,     paletteGeneration };
    for (unsigned i = 0; i < 8u; ++i) {
        hash = (hash ^ header[i]) * 16777619u;
    }
    if (screenBase + screenBytes > MODE1_VRAM_SIZE) return 0;
    const uint8_t* map = frame->memory.vram + screenBase;
    /* This walks the whole tilemap every frame, and the obvious loop is one
     * dependent multiply per four bytes -- the CPU cannot start the next load
     * until the current hash lands, so each L1 miss costs its full FCRAM
     * latency with nothing overlapping it, and an Old 3DS has no L2 to soften
     * that. Four independent lanes let four loads be in flight, and a prefetch
     * a few cache lines ahead covers the rest. The result only has to be
     * self-consistent within a run -- it is compared against the signature this
     * same code produced last frame -- so the mixing order is free to change. */
    uint32_t lane[4] = { hash, hash ^ 0x9e3779b9u, hash ^ 0x85ebca6bu,
                         hash ^ 0xc2b2ae35u };
    uint32_t offset = 0;
    for (; offset + 15u < screenBytes; offset += 16u) {
        __builtin_prefetch(map + offset + 96, 0, 0);
        uint32_t word[4];
        memcpy(word, map + offset, sizeof(word));
        lane[0] = (lane[0] ^ word[0]) * 16777619u;
        lane[1] = (lane[1] ^ word[1]) * 16777619u;
        lane[2] = (lane[2] ^ word[2]) * 16777619u;
        lane[3] = (lane[3] ^ word[3]) * 16777619u;
    }
    hash = lane[0];
    for (unsigned i = 1; i < 4u; ++i) hash = (hash ^ lane[i]) * 16777619u;
    for (; offset + 3u < screenBytes; offset += 4u) {
        uint32_t word;
        memcpy(&word, map + offset, sizeof(word));
        hash = (hash ^ word) * 16777619u;
    }
    return hash != 0 ? hash : 1u;
}


typedef struct PpuGpu3DSBgMap {
    uint32_t firstIndex;
    uint16_t rowLo, colLo, rows, cols;
    uint16_t bgcnt;
    bool valid;
} PpuGpu3DSBgMap;


static bool destination_sample_x(const PpuGpu3DSFrameView* frame, unsigned bg,
                                 unsigned line, int destinationX, int* sampleX,
                                 bool* trueExtension) {
    const bool message = message_line(frame, bg, line);
    if (message && destinationX >= frame->wsMsgX0 + frame->wsMsgShift &&
        destinationX < frame->wsMsgX1 + frame->wsMsgShift) {
        *sampleX = destinationX - frame->wsMsgShift;
        *trueExtension = false;
        return true;
    }
    if (message && destinationX >= frame->wsMsgX0 &&
        destinationX < frame->wsMsgX1)
        return false;
    if (bg == 0 && frame->width > MODE1_GBA_BG_CLIP_X &&
        frame->wsHudRightAnchor && !message) {
        const int destination =
                (int)frame->width -
                (MODE1_GBA_BG_CLIP_X - frame->wsHudRightNativeX);
        if (destinationX >= destination) {
            *sampleX =
                    destinationX - ((int)frame->width - MODE1_GBA_BG_CLIP_X);
            *trueExtension = false;
            return true;
        }
        if (destinationX >= frame->wsHudRightNativeX) return false;
    } else if (message && destinationX >= MODE1_GBA_BG_CLIP_X) {
        return false;
    }
    *sampleX = destinationX;
    *trueExtension = destinationX >= MODE1_GBA_BG_CLIP_X;
    return true;
}

static bool text_bg_sample(const PpuGpu3DSFrameView* frame, unsigned bg,
                           uint32_t charBase, uint32_t screenBase, bool bpp8,
                           unsigned mapWidthTiles, unsigned mapHeightTiles,
                           unsigned scrollX, unsigned scrollY,
                           unsigned mosaicWidth, unsigned mosaicHeight,
                           unsigned destinationX, unsigned destinationY,
                           PpuGpu3DSTextSample* sample) {
    sample->visible = false;
    int sampleX;
    bool trueExtension;
    if (!destination_sample_x(frame, bg, destinationY, (int)destinationX,
                              &sampleX, &trueExtension))
        return true;
    const int effectiveX =
            mosaicWidth == 1u
                    ? sampleX
                    : (sampleX / (int)mosaicWidth) * (int)mosaicWidth;
    const unsigned effectiveY =
            mosaicHeight == 1u
                    ? destinationY
                    : (destinationY / mosaicHeight) * mosaicHeight;
    const unsigned sourceX =
            ((unsigned)(effectiveX + (int)scrollX)) &
            (mapWidthTiles * PPU_GPU3DS_TILE_SIDE - 1u);
    const unsigned sourceY =
            (effectiveY + scrollY) &
            (mapHeightTiles * PPU_GPU3DS_TILE_SIDE - 1u);
    const unsigned tileX = sourceX / PPU_GPU3DS_TILE_SIDE;
    const unsigned tileY = sourceY / PPU_GPU3DS_TILE_SIDE;
    uint16_t entry;
    if (mapWidthTiles == 32u && trueExtension &&
        frame->wsShadowBaseTile[bg] >= 0) {
        const unsigned column =
                (unsigned)((int64_t)tileX - frame->wsShadowBaseTile[bg] + 32) &
                31u;
        if (!PpuGpu3DS_ShadowEntry(frame, bg, tileY & 31u, column, &entry))
            return true;
    } else {
        const unsigned blockX = tileX / 32u;
        const unsigned blockY = tileY / 32u;
        const uint32_t mapOffset =
                (blockX + blockY * (mapWidthTiles / 32u)) * 0x800u +
                ((tileY & 31u) * 32u + (tileX & 31u)) * 2u;
        if (screenBase > MODE1_VRAM_SIZE - 2u ||
            mapOffset > MODE1_VRAM_SIZE - 2u - screenBase)
            return false;
        entry = read16(frame->memory.vram, screenBase + mapOffset);
    }
    const uint32_t tileBytes = bpp8 ? 64u : 32u;
    const uint32_t tileOffset =
            charBase + (uint32_t)(entry & 0x03ffu) * tileBytes;
    if (tileOffset > MODE1_VRAM_SIZE - tileBytes) return false;
    unsigned pixelX = sourceX & 7u;
    unsigned pixelY = sourceY & 7u;
    if ((entry & (1u << 10u)) != 0) pixelX = 7u - pixelX;
    if ((entry & (1u << 11u)) != 0) pixelY = 7u - pixelY;
    sample->key = (PpuGpu3DSTileKey){
        .vramOffset = tileOffset,
        .paletteBank = bpp8 ? 0 : (uint8_t)(entry >> 12u),
        .bpp8 = bpp8,
        .domain = PPU_GPU3DS_PALETTE_BG,
    };
    sample->pixelX = (uint8_t)pixelX;
    sample->pixelY = (uint8_t)pixelY;
    sample->visible = true;
    return true;
}


static bool build_widescreen_bg_geometry(
        const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
        uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
        const PpuGpu3DSBand* band, uint16_t bgcnt, unsigned bg, bool emit,
        size_t* vertexCursor, size_t* indexCursor) {
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
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
    const uint16_t mosaic = read16(io, MODE1_IO_MOSAIC);
    const bool mosaicEnabled =
            (bgcnt & (1u << 6u)) != 0 &&
            ((mosaic & 0x0fu) != 0 || (mosaic & 0xf0u) != 0);
    const unsigned mosaicWidth =
            mosaicEnabled ? (mosaic & 0x0fu) + 1u : 1u;
    const unsigned mosaicHeight =
            mosaicEnabled ? ((mosaic >> 4u) & 0x0fu) + 1u : 1u;
    unsigned nativeRenderWidth =
            mapWidthTiles == 64u || frame->wsShadowBaseTile[bg] >= 0
                    ? frame->width
                    : MODE1_GBA_BG_CLIP_X;
    if (nativeRenderWidth > frame->width) nativeRenderWidth = frame->width;

    const unsigned bandBottom = band->firstLine + band->lineCount;
    for (unsigned y = band->firstLine; y < bandBottom;) {
        const unsigned effectiveY =
                mosaicHeight == 1u ? y : (y / mosaicHeight) * mosaicHeight;
        const unsigned sourceY =
                (effectiveY + scrollY) &
                (mapHeightTiles * PPU_GPU3DS_TILE_SIDE - 1u);
        unsigned height =
                mosaicHeight == 1u ? PPU_GPU3DS_TILE_SIDE - (sourceY & 7u)
                                   : mosaicHeight - y % mosaicHeight;
        if (height > bandBottom - y) height = bandBottom - y;
        if (bg == 0 && frame->wsMsgShift != 0) {
            if (frame->wsMsgY0 > (int)y &&
                frame->wsMsgY0 < (int)(y + height))
                height = (unsigned)frame->wsMsgY0 - y;
            if (frame->wsMsgY1 > (int)y &&
                frame->wsMsgY1 < (int)(y + height))
                height = (unsigned)frame->wsMsgY1 - y;
        }
        const unsigned renderWidth =
                (bg == 0 &&
                 (frame->wsHudRightAnchor || message_line(frame, bg, y)))
                        ? frame->width
                        : nativeRenderWidth;
        unsigned x = 0;
        while (x < renderWidth) {
            PpuGpu3DSTextSample first;
            if (!text_bg_sample(frame, bg, charBase, screenBase, bpp8,
                                mapWidthTiles, mapHeightTiles, scrollX, scrollY,
                                mosaicWidth, mosaicHeight, x, y, &first))
                return false;
            if (!first.visible) {
                ++x;
                continue;
            }
            int dx = 0;
            unsigned width = 1;
            PpuGpu3DSTextSample next;
            if (x + 1u < renderWidth &&
                !text_bg_sample(frame, bg, charBase, screenBase, bpp8,
                                mapWidthTiles, mapHeightTiles, scrollX, scrollY,
                                mosaicWidth, mosaicHeight, x + 1u, y, &next))
                return false;
            if (x + 1u < renderWidth && next.visible &&
                keys_equal(first.key, next.key) &&
                first.pixelY == next.pixelY) {
                dx = (int)next.pixelX - first.pixelX;
                width = 2;
                while (x + width < renderWidth) {
                    PpuGpu3DSTextSample candidate;
                    if (!text_bg_sample(
                                frame, bg, charBase, screenBase, bpp8,
                                mapWidthTiles, mapHeightTiles, scrollX, scrollY,
                                mosaicWidth, mosaicHeight, x + width, y,
                                &candidate))
                        return false;
                    if (!candidate.visible ||
                        !keys_equal(first.key, candidate.key) ||
                        candidate.pixelY != first.pixelY ||
                        (int)candidate.pixelX !=
                                (int)first.pixelX + dx * (int)width)
                        break;
                    ++width;
                }
            }
            int dy = 0;
            if (height > 1u) {
                PpuGpu3DSTextSample below;
                if (!text_bg_sample(frame, bg, charBase, screenBase, bpp8,
                                    mapWidthTiles, mapHeightTiles, scrollX,
                                    scrollY, mosaicWidth, mosaicHeight, x,
                                    y + 1u, &below))
                    return false;
                if (!below.visible || !keys_equal(first.key, below.key) ||
                    below.pixelX != first.pixelX)
                    return false;
                dy = (int)below.pixelY - first.pixelY;
            }
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            if (emit) {
                uint16_t slot;
                if (!PpuGpu3DS_CacheTile(cache, frame->memory.vram, first.key,
                                         atlas, &slot))
                    return false;
                const unsigned tilesPerRow =
                        PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE;
                const unsigned slotX =
                        (slot % tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
                const unsigned slotY =
                        (slot / tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                u0 = (slotX + first.pixelX + 0.5f - 0.5f * dx) * invAtlas;
                v0 = 1.0f -
                     (slotY + first.pixelY + 0.5f - 0.5f * dy) * invAtlas;
                u1 = u0 + dx * (float)width * invAtlas;
                v1 = v0 - dy * (float)height * invAtlas;
            }
            emit_uv_quad(frame, cmd, emit, (float)x, (float)y,
                         (float)(x + width), (float)(y + height), 0.0f, u0, v0,
                         u1, v1, false, vertexCursor, indexCursor);
            x += width;
        }
        y += height;
    }
    return true;
}

typedef struct PpuGpu3DSAffineSample {
    PpuGpu3DSTileKey key;
    uint8_t pixelX, pixelY;
    bool visible;
} PpuGpu3DSAffineSample;

static bool affine_bg_sample(const PpuGpu3DSFrameView* frame,
                             const uint8_t* io, uint16_t bgcnt, unsigned line,
                             unsigned x, PpuGpu3DSAffineSample* sample) {
    static const int mapSizes[4] = { 128, 256, 512, 1024 };
    const int mapSize = mapSizes[(bgcnt >> 14u) & 3u];
    const int mapTiles = mapSize / PPU_GPU3DS_TILE_SIDE;
    int32_t sourceX = PpuGpu3DS_AffineSample(
            frame->affineRefX[line], (int16_t)read16(io, 0x20), (int)x);
    int32_t sourceY = PpuGpu3DS_AffineSample(
            frame->affineRefY[line], (int16_t)read16(io, 0x24), (int)x);
    if ((bgcnt & (1u << 13u)) != 0) {
        sourceX = (int32_t)((uint32_t)sourceX & (unsigned)(mapSize - 1));
        sourceY = (int32_t)((uint32_t)sourceY & (unsigned)(mapSize - 1));
    } else if (sourceX < 0 || sourceX >= mapSize || sourceY < 0 ||
               sourceY >= mapSize) {
        sample->visible = false;
        return true;
    }
    const unsigned tileX = (unsigned)sourceX / PPU_GPU3DS_TILE_SIDE;
    const unsigned tileY = (unsigned)sourceY / PPU_GPU3DS_TILE_SIDE;
    const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
    const uint32_t mapOffset = tileY * (unsigned)mapTiles + tileX;
    uint8_t tile = 0;
    if (screenBase < MODE1_VRAM_SIZE &&
        mapOffset < MODE1_VRAM_SIZE - screenBase)
        tile = frame->memory.vram[screenBase + mapOffset];
    const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;
    sample->key = (PpuGpu3DSTileKey){
        .vramOffset = charBase + (uint32_t)tile * 64u,
        .paletteBank = 0,
        .bpp8 = true,
        .domain = PPU_GPU3DS_PALETTE_BG,
    };
    sample->pixelX = (uint8_t)((unsigned)sourceX & 7u);
    sample->pixelY = (uint8_t)((unsigned)sourceY & 7u);
    sample->visible = true;
    return true;
}

/* An affine background whose matrix is the identity and whose reference point
 * advances exactly one source line per scanline is a plainly scrolled layer:
 * the game uses BG2 that way in mode 1. Recognising it matters because the
 * general affine path re-samples every screen pixel of every scanline, which
 * is what a per-line reference turns into 160 one-line bands. */
static bool affine_is_scrolled_layer(const uint8_t* io) {
    return (int16_t)read16(io, 0x20) == 0x100 &&
           (int16_t)read16(io, 0x22) == 0 &&
           (int16_t)read16(io, 0x24) == 0 &&
           (int16_t)read16(io, 0x26) == 0x100;
}

/* Source row for a scanline, when the reference advances linearly. */
static bool affine_scroll_base(const PpuGpu3DSFrameView* frame, unsigned line,
                               int32_t* base) {
    if ((frame->affineRefX[line] & 0xff) != 0 ||
        (frame->affineRefY[line] & 0xff) != 0)
        return false;
    *base = (frame->affineRefY[line] >> 8) - (int32_t)line;
    return true;
}

static bool build_affine_bg(const PpuGpu3DSFrameView* frame,
                            PpuGpu3DSCache* cache, uint16_t* atlas,
                            PpuGpu3DSCommandBuffer* cmd,
                            const PpuGpu3DSBand* band,
                            const PpuGpu3DSBgMap* map,
                            const PpuGpu3DSRegion* regions,
                            size_t regionCount, bool emit,
                            size_t* vertexCursor, size_t* indexCursor,
                            size_t* batchCursor) {
    if (!layer_has_region(regions, regionCount, PPU_GPU3DS_BG2, 0,
                          (uint16_t)frame->width))
        return true;
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t bgcnt = read16(io, MODE1_IO_BG2CNT);
    const size_t firstIndex = *indexCursor;

    float offsetX = 0.0f, offsetY = 0.0f;
    uint32_t mapFirstIndex = 0, mapIndexCount = 0;
    bool fromMap = false;
    if (map && map->valid && (map->bgcnt & ~3u) == (bgcnt & ~3u)) {
        int32_t base = 0;
        if (affine_scroll_base(frame, band->firstLine, &base) && base >= 0) {
            const int32_t scrollX = frame->affineRefX[band->firstLine] >> 8;
            const unsigned rTop = (unsigned)(base + band->firstLine) >> 3u;
            const unsigned rBot =
                    (unsigned)(base + band->firstLine + band->lineCount - 1) >> 3u;
            if (rTop >= map->rowLo && rBot < map->rowLo + map->rows) {
                mapFirstIndex =
                        map->firstIndex + (rTop - map->rowLo) * map->cols * 6u;
                mapIndexCount = (rBot - rTop + 1u) * map->cols * 6u;
                offsetX = -(1.0f + 2.0f * (float)scrollX / (float)frame->width);
                offsetY = 1.0f + 2.0f * (float)base / (float)frame->height;
                fromMap = true;
            }
        }
        if (!fromMap) cmd->mapReject[PPU_GPU3DS_MAP_REJECT_COVERAGE] += 1u;
    }
    if (!fromMap) {
    /* Each scanline has its own reference point. Where the reference holds
     * still across the band, one quad covers it exactly as before; where it
     * advances -- a band merged because the layer is really a scrolled one --
     * the band is walked line by line under a single batch. */
    const unsigned bandEnd = (unsigned)band->firstLine + band->lineCount;
    bool referenceHolds = true;
    for (unsigned line = band->firstLine + 1u; line < bandEnd; ++line) {
        if (frame->affineRefX[line] != frame->affineRefX[band->firstLine] ||
            frame->affineRefY[line] != frame->affineRefY[band->firstLine]) {
            referenceHolds = false;
            break;
        }
    }
    const unsigned lineStep = referenceHolds ? band->lineCount : 1u;
    for (unsigned line = band->firstLine; line < bandEnd; line += lineStep) {
        unsigned x = 0;
        while (x < frame->width) {
            PpuGpu3DSAffineSample first;
            if (!affine_bg_sample(frame, io, bgcnt, line, x, &first))
                return false;
            if (!first.visible) {
                ++x;
                continue;
            }
            int dx = 0;
            int dy = 0;
            unsigned width = 1;
            PpuGpu3DSAffineSample next;
            if (x + 1u < frame->width &&
                !affine_bg_sample(frame, io, bgcnt, line, x + 1u, &next))
                return false;
            if (x + 1u < frame->width && next.visible &&
                keys_equal(first.key, next.key)) {
                dx = (int)next.pixelX - first.pixelX;
                dy = (int)next.pixelY - first.pixelY;
                width = 2;
                while (x + width < frame->width) {
                    PpuGpu3DSAffineSample candidate;
                    if (!affine_bg_sample(frame, io, bgcnt, line, x + width,
                                          &candidate))
                        return false;
                    if (!candidate.visible ||
                        !keys_equal(first.key, candidate.key) ||
                        (int)candidate.pixelX !=
                                (int)first.pixelX + dx * (int)width ||
                        (int)candidate.pixelY !=
                                (int)first.pixelY + dy * (int)width)
                        break;
                    ++width;
                }
            }
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            if (emit) {
                uint16_t slot;
                if (!PpuGpu3DS_CacheTile(cache, frame->memory.vram, first.key,
                                         atlas, &slot))
                    return false;
                const unsigned tilesPerRow =
                        PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE;
                const unsigned slotX =
                        (slot % tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
                const unsigned slotY =
                        (slot / tilesPerRow) * PPU_GPU3DS_TILE_SIDE;
                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                u0 = (slotX + first.pixelX + 0.5f - 0.5f * dx) * invAtlas;
                v0 = 1.0f -
                     (slotY + first.pixelY + 0.5f - 0.5f * dy) * invAtlas;
                u1 = u0 + dx * (float)width * invAtlas;
                v1 = v0 - dy * (float)width * invAtlas;
            }
            emit_uv_quad(frame, cmd, emit, (float)x, (float)line,
                         (float)(x + width), (float)(line + lineStep), 0.0f, u0,
                         v0, u1, v1, true, vertexCursor, indexCursor);
            x += width;
        }
    }

    }

    const uint16_t bldcnt = read16(io, MODE1_IO_BLDCNT);
    const PpuGpu3DSBatch base = {
        .firstIndex = fromMap ? mapFirstIndex : (uint32_t)firstIndex,
        .indexCount = fromMap ? mapIndexCount
                              : (uint32_t)(*indexCursor - firstIndex),
        .firstLine = band->firstLine,
        .lineCount = band->lineCount,
        .scissorRight = (uint16_t)frame->width,
        .layer = PPU_GPU3DS_BG2,
        .offsetX = offsetX,
        .offsetY = offsetY,
        .priority = (uint8_t)(bgcnt & 3u),
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .target2 =
                (uint8_t)((bldcnt >> (PPU_GPU3DS_BG2 + 8u)) & 1u),
        .objectIndex = UINT8_MAX,
    };
    append_layer_batches(cmd, &base, regions, regionCount, bldcnt,
                         read16(io, MODE1_IO_BLDALPHA),
                         read16(io, MODE1_IO_BLDY), emit, batchCursor);
    return true;
}

static bool build_mosaic_bg_geometry(
        const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
        uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
        const PpuGpu3DSBand* band, uint16_t bgcnt, unsigned bg, bool emit,
        size_t* vertexCursor, size_t* indexCursor) {
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t mosaic = read16(io, MODE1_IO_MOSAIC);
    const unsigned blockWidth = (mosaic & 0x0fu) + 1u;
    const unsigned blockHeight = ((mosaic >> 4u) & 0x0fu) + 1u;
    const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;
    const bool bpp8 = ((bgcnt >> 7u) & 1u) != 0;
    const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
    const unsigned size = (bgcnt >> 14u) & 3u;
    const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
    const unsigned mapHeightTiles = (size & 2u) ? 64u : 32u;
    const unsigned mapWidth = mapWidthTiles * PPU_GPU3DS_TILE_SIDE;
    const unsigned mapHeight = mapHeightTiles * PPU_GPU3DS_TILE_SIDE;
    const unsigned scrollX =
            read16(io, MODE1_IO_BG0HOFS + bg * 4u) & 0x1ffu;
    const unsigned scrollY =
            read16(io, MODE1_IO_BG0VOFS + bg * 4u) & 0x1ffu;
    const unsigned firstY =
            (band->firstLine / blockHeight) * blockHeight;
    const unsigned bottom = band->firstLine + band->lineCount;
    for (unsigned y = firstY; y < bottom; y += blockHeight) {
        const unsigned sourceY = (y + scrollY) & (mapHeight - 1u);
        const unsigned tileY = sourceY / PPU_GPU3DS_TILE_SIDE;
        for (unsigned x = 0; x < frame->width; x += blockWidth) {
            const unsigned sourceX = (x + scrollX) & (mapWidth - 1u);
            const unsigned tileX = sourceX / PPU_GPU3DS_TILE_SIDE;
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
            unsigned pixelX = sourceX & 7u;
            unsigned pixelY = sourceY & 7u;
            if ((entry & (1u << 10u)) != 0) pixelX = 7u - pixelX;
            if ((entry & (1u << 11u)) != 0) pixelY = 7u - pixelY;
            float u = 0.0f;
            float v = 0.0f;
            if (!tile_sample_uv(
                        cache, frame->memory.vram,
                        (PpuGpu3DSTileKey){
                            .vramOffset = tileOffset,
                            .paletteBank = (uint8_t)(entry >> 12u),
                            .bpp8 = bpp8,
                            .domain = PPU_GPU3DS_PALETTE_BG,
                        },
                        atlas, pixelX, pixelY, emit, &u, &v))
                return false;
            unsigned right = x + blockWidth;
            unsigned quadBottom = y + blockHeight;
            if (right > frame->width) right = frame->width;
            emit_sample_quad(frame, cmd, emit, (float)x, (float)y,
                             (float)right, (float)quadBottom, 0.0f, u, v,
                             vertexCursor, indexCursor);
        }
    }
    return true;
}

static bool build_text_bg(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                          uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                          const PpuGpu3DSBand* band, unsigned bg,
                          const PpuGpu3DSBgMap* map,
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
    const uint16_t mosaic = read16(io, MODE1_IO_MOSAIC);
    const bool mosaicEnabled =
            (bgcnt & (1u << 6u)) != 0 &&
            ((mosaic & 0x0fu) != 0 || (mosaic & 0xf0u) != 0);
    const bool widescreenRules =
            frame->width > MODE1_GBA_BG_CLIP_X &&
            (mapWidthTiles == 32u ||
             (bg == 0 &&
              (frame->wsHudRightAnchor || frame->wsMsgShift != 0)));
    /* The frame already emitted this layer's tiles in map space: the band only
     * needs the index range covering its rows and its scroll offset. */
    float offsetX = 0.0f, offsetY = 0.0f;
    bool fromMap = false;
    uint32_t mapFirstIndex = 0, mapIndexCount = 0;
    if (map && map->valid && (map->bgcnt & ~3u) == (bgcnt & ~3u)) {
        const unsigned rTop = sourceY >> 3u;
        const unsigned rBot = (unsigned)(bandBottom - 1 + (int)scrollY) >> 3u;
        const unsigned cL = scrollX >> 3u;
        const unsigned cR = (scrollX + frame->width - 1u) >> 3u;
        if (rTop >= map->rowLo && rBot < map->rowLo + map->rows &&
            cL >= map->colLo && cR < map->colLo + map->cols) {
            const unsigned r0 = rTop - map->rowLo;
            const unsigned r1 = rBot - map->rowLo;
            if (r0 == r1) {
                /* One tile row: draw only the columns this band can see. */
                const unsigned c0 = cL - map->colLo;
                mapFirstIndex =
                        map->firstIndex + (r0 * map->cols + c0) * 6u;
                mapIndexCount = (cR - cL + 1u) * 6u;
            } else {
                mapFirstIndex = map->firstIndex + r0 * map->cols * 6u;
                mapIndexCount = (r1 - r0 + 1u) * map->cols * 6u;
            }
            offsetX = -(1.0f + 2.0f * (float)scrollX / (float)frame->width);
            offsetY = 1.0f + 2.0f * (float)scrollY / (float)frame->height;
            fromMap = true;
        } else {
            cmd->mapReject[PPU_GPU3DS_MAP_REJECT_COVERAGE] += 1u;
        }
    }

    if (fromMap) {
        /* nothing to emit: the geometry is already in the buffer */
    } else if (widescreenRules) {
        if (!build_widescreen_bg_geometry(
                    frame, cache, atlas, cmd, band, bgcnt, bg, emit,
                    vertexCursor, indexCursor))
            return false;
    } else if (mosaicEnabled) {
        if (!build_mosaic_bg_geometry(frame, cache, atlas, cmd, band, bgcnt,
                                      bg, emit, vertexCursor, indexCursor))
            return false;
    } else {

    const float invScreenWidth = 2.0f / (float)frame->width;
    const float invScreenHeight = 2.0f / (float)frame->height;
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

            if (emit && quad_room(cmd, vertexCursor, indexCursor)) {
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
                        1.0f -
                        (float)((slot / (PPU_GPU3DS_ATLAS_SIDE /
                                         PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                                invAtlas;
                float u0 = slotLeft;
                float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
                float v0 = slotTop;
                float v1 = slotTop - PPU_GPU3DS_TILE_SIDE * invAtlas;
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
                const float left = (float)x * invScreenWidth - 1.0f;
                const float right =
                        (float)(x + PPU_GPU3DS_TILE_SIDE) * invScreenWidth - 1.0f;
                const float top = 1.0f - (float)y * invScreenHeight;
                const float bottom =
                        1.0f - (float)(y + PPU_GPU3DS_TILE_SIDE) * invScreenHeight;
                cmd->vertices[*vertexCursor + 0] = (PpuGpu3DSVertex){
                        left, top, 0.0f, PpuGpu3DS_PackUV(u0), PpuGpu3DS_PackUV(v0) };
                cmd->vertices[*vertexCursor + 1] = (PpuGpu3DSVertex){
                        right, top, 0.0f, PpuGpu3DS_PackUV(u1), PpuGpu3DS_PackUV(v0) };
                cmd->vertices[*vertexCursor + 2] = (PpuGpu3DSVertex){
                        right, bottom, 0.0f, PpuGpu3DS_PackUV(u1), PpuGpu3DS_PackUV(v1) };
                cmd->vertices[*vertexCursor + 3] = (PpuGpu3DSVertex){
                        left, bottom, 0.0f, PpuGpu3DS_PackUV(u0), PpuGpu3DS_PackUV(v1) };
                emit_indices(cmd, vertexCursor, indexCursor);
            }
            *vertexCursor += 4;
            *indexCursor += 6;
        }
    }
    }

    const uint16_t bldcnt = read16(io, MODE1_IO_BLDCNT);
    const PpuGpu3DSBatch base = {
        .firstIndex = fromMap ? mapFirstIndex : (uint32_t)firstIndex,
        .indexCount = fromMap ? mapIndexCount
                              : (uint32_t)(*indexCursor - firstIndex),
        .firstLine = band->firstLine,
        .lineCount = band->lineCount,
        .scissorRight = (uint16_t)frame->width,
        .layer = (uint8_t)(PPU_GPU3DS_BG0 + bg),
        .priority = (uint8_t)priority,
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .target2 = (uint8_t)((bldcnt >> (PPU_GPU3DS_BG0 + bg + 8u)) & 1u),
        .objectIndex = UINT8_MAX,
        .offsetX = offsetX,
        .offsetY = offsetY,
    };
    append_layer_batches(cmd, &base, regions, regionCount, bldcnt,
                         read16(io, MODE1_IO_BLDALPHA),
                         read16(io, MODE1_IO_BLDY), emit, batchCursor);
    return true;
}

static bool build_mosaic_obj_geometry(
        const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
        uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
        const PpuGpu3DSBand* band, const PpuGpu3DSObj* obj,
        int clipLeft, int clipRight, int clipTop, int clipBottom, bool emit,
        size_t* vertexCursor, size_t* indexCursor) {
    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t mosaic = read16(io, MODE1_IO_MOSAIC);
    const int blockWidth = (int)((mosaic >> 8u) & 0x0fu) + 1;
    const int blockHeight = (int)((mosaic >> 12u) & 0x0fu) + 1;
    const int firstX = (clipLeft / blockWidth) * blockWidth;
    const int firstY = (clipTop / blockHeight) * blockHeight;
    const int tilesWide = obj->width / PPU_GPU3DS_TILE_SIDE;
    const unsigned tileScale = obj->bpp8 ? 2u : 1u;
    const uint16_t baseTile =
            obj->bpp8 ? (uint16_t)(obj->baseTile & ~1u) : obj->baseTile;
    const bool obj1d =
            (frame->dispcntPerLine[band->firstLine] & MODE1_DISP_OBJ_1D) != 0;
    const float z = -(128.0f - (float)obj->index) * (1.0f / 129.0f);
    for (int y = firstY; y < clipBottom; y += blockHeight) {
        if (y < obj->y) continue;
        for (int x = firstX; x < clipRight; x += blockWidth) {
            if (x < obj->x) continue;
            int texX;
            int texY;
            if (obj->affine) {
                const int inputX = x - obj->x - obj->boundsWidth / 2;
                const int inputY = y - obj->y - obj->boundsHeight / 2;
                texX = ((obj->pa * inputX + obj->pb * inputY) >> 8) +
                       obj->width / 2;
                texY = ((obj->pc * inputX + obj->pd * inputY) >> 8) +
                       obj->height / 2;
            } else {
                texX = x - obj->x;
                texY = y - obj->y;
                if (obj->hflip) texX = obj->width - 1 - texX;
                if (obj->vflip) texY = obj->height - 1 - texY;
            }
            if (texX < 0 || texX >= obj->width ||
                texY < 0 || texY >= obj->height)
                continue;
            const unsigned tileRow =
                    (unsigned)texY / PPU_GPU3DS_TILE_SIDE;
            const unsigned tileCol =
                    (unsigned)texX / PPU_GPU3DS_TILE_SIDE;
            const uint32_t tileIndex =
                    obj1d ? baseTile +
                                    (tileRow * (unsigned)tilesWide + tileCol) *
                                            tileScale
                          : baseTile + tileRow * 32u + tileCol * tileScale;
            const uint32_t tileBytes = obj->bpp8 ? 64u : 32u;
            const uint32_t tileOffset =
                    PPU_GPU3DS_OBJ_TILE_BASE + tileIndex * 32u;
            if (tileOffset > MODE1_VRAM_SIZE - tileBytes) return false;
            float u = 0.0f;
            float v = 0.0f;
            if (!tile_sample_uv(
                        cache, frame->memory.vram,
                        (PpuGpu3DSTileKey){
                            .vramOffset = tileOffset,
                            .paletteBank = obj->bpp8 ? 0 : obj->palette,
                            .bpp8 = obj->bpp8,
                            .domain = PPU_GPU3DS_PALETTE_OBJ,
                        },
                        atlas, (unsigned)texX & 7u, (unsigned)texY & 7u, emit,
                        &u, &v))
                return false;
            emit_sample_quad(frame, cmd, emit, (float)x, (float)y,
                             (float)(x + blockWidth),
                             (float)(y + blockHeight), z, u, v,
                             vertexCursor, indexCursor);
        }
    }
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
    /* Loop-invariant: a VFP divide costs ~15 cycles on an ARM11 and this used
     * to run nine times per sprite tile. */
    const float invWidth = 2.0f / (float)frame->width;
    const float invHeight = 2.0f / (float)frame->height;
    const float objDepth = -(128.0f - (float)obj->index) * (1.0f / 129.0f);
    const size_t firstIndex = *indexCursor;
    const uint8_t* bandIo =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t mosaic = read16(bandIo, MODE1_IO_MOSAIC);
    const bool mosaicEnabled =
            (frame->memory.oam_mem[obj->index * 4u] & (1u << 12u)) != 0 &&
            ((mosaic & 0x0f00u) != 0 || (mosaic & 0xf000u) != 0);
    if (mosaicEnabled) {
        if (!build_mosaic_obj_geometry(
                    frame, cache, atlas, cmd, band, obj, clipLeft, clipRight,
                    clipTop, clipBottom, emit, vertexCursor, indexCursor))
            return false;
    } else {
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

            if (emit && quad_room(cmd, vertexCursor, indexCursor)) {
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
                /* Nothing of this tile would survive the alpha test. */
                if (cache->entries[slot].transparent) continue;
                const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
                const float slotLeft =
                        (float)((slot % (PPU_GPU3DS_ATLAS_SIDE /
                                        PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                        invAtlas;
                const float slotTop =
                        1.0f -
                        (float)((slot / (PPU_GPU3DS_ATLAS_SIDE /
                                         PPU_GPU3DS_TILE_SIDE)) *
                                PPU_GPU3DS_TILE_SIDE) *
                                invAtlas;
                float u0 = slotLeft;
                float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
                float v0 = slotTop;
                float v1 = slotTop - PPU_GPU3DS_TILE_SIDE * invAtlas;
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
                const int16_t uv[4][2] = {
                    { PpuGpu3DS_PackUV(u0), PpuGpu3DS_PackUV(v0) },
                    { PpuGpu3DS_PackUV(u1), PpuGpu3DS_PackUV(v0) },
                    { PpuGpu3DS_PackUV(u1), PpuGpu3DS_PackUV(v1) },
                    { PpuGpu3DS_PackUV(u0), PpuGpu3DS_PackUV(v1) }
                };
                for (unsigned corner = 0; corner < 4; ++corner) {
                    cmd->vertices[*vertexCursor + corner] =
                            (PpuGpu3DSVertex){
                                px[corner] * invWidth - 1.0f,
                                1.0f - py[corner] * invHeight,
                                objDepth,
                                uv[corner][0],
                                uv[corner][1],
                            };
                }
                emit_indices(cmd, vertexCursor, indexCursor);
            }
            *vertexCursor += 4;
            *indexCursor += 6;
        }
    }
    }

    const uint8_t* io =
            frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
    const uint16_t bldcnt = read16(io, MODE1_IO_BLDCNT);
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
        .target2 = (uint8_t)((bldcnt >> (PPU_GPU3DS_OBJ + 8u)) & 1u),
        .color = stencilOnly
                         ? (uint16_t)(PPU_GPU3DS_REGION_OBJWIN
                                      << PPU_GPU3DS_REGION_SHIFT)
                         : 0,
        .effect = PPU_GPU3DS_EFFECT_NONE,
        .objectIndex = obj->index,
        .objWindow = stencilOnly,
        .semiTransparent = obj->mode == 1u,
    };
    if (stencilOnly) {
        store_batch(cmd, *batchCursor, &base, emit);
        ++*batchCursor;
    } else {
        append_layer_batches(cmd, &base, regions, regionCount, bldcnt,
                             read16(io, MODE1_IO_BLDALPHA),
                             read16(io, MODE1_IO_BLDY), emit, batchCursor);
    }
    return true;
}

static bool build_backdrop_target2(const PpuGpu3DSFrameView* frame,
                                   PpuGpu3DSCommandBuffer* cmd,
                                   const PpuGpu3DSBand* band, const uint8_t* io,
                                   bool emit, size_t* vertexCursor,
                                   size_t* indexCursor, size_t* batchCursor) {
    const uint16_t bldcnt = read16(io, MODE1_IO_BLDCNT);
    if ((bldcnt & (1u << (PPU_GPU3DS_BACKDROP + 8u))) == 0) return true;
    const size_t firstIndex = *indexCursor;
    /* v = 1 is atlas row 0 under the flipped V axis; this quad only writes
     * stencil, but it must still name a real texel. */
    emit_sample_quad(frame, cmd, emit, 0.0f, band->firstLine,
                     frame->width, band->firstLine + band->lineCount,
                     0.0f, 0.0f, 1.0f, vertexCursor, indexCursor);
    const PpuGpu3DSBatch backdrop = {
        .firstIndex = (uint32_t)firstIndex,
        .indexCount = 6,
        .firstLine = band->firstLine,
        .lineCount = band->lineCount,
        .scissorRight = (uint16_t)frame->width,
        .layer = PPU_GPU3DS_BACKDROP,
        .priority = UINT8_MAX,
        .target2 = 1,
        .objectIndex = UINT8_MAX,
    };
    store_batch(cmd, *batchCursor, &backdrop, emit);
    ++*batchCursor;
    return true;
}
/* ---------------------------------------------------------------------------
 * Map-space background geometry
 *
 * Per-line scroll means a layer's tiles are the same every band -- only where
 * they land changes. Emitting them once in MAP space and giving each band its
 * scroll as a shader offset turns a band from "walk the tilemap again" into
 * "draw this index range with this offset", which is what makes the cost of a
 * 160-band frame close to that of a 1-band frame.
 *
 * Quad (r, c) covers map tile (rowLo + r, colLo + c), row-major, so a run of
 * whole rows or a column slice of one row is a contiguous index range. Map
 * rows and columns are UNWRAPPED: the tilemap is read at the wrapped
 * coordinate, so wrap needs no separate geometry.
 * ------------------------------------------------------------------------ */

/* Test and benchmark switch: with map space off, every background walks the
 * tilemap per band as it did before, so the two paths can be diffed. */
static bool sMapSpaceEnabled = true;

void PpuGpu3DS_SetMapSpaceEnabled(bool enabled) {
    sMapSpaceEnabled = enabled;
}

static void retain_release(PpuGpu3DSCache* cache, unsigned bg) {
    PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
    for (unsigned index = 0; index < retained->slotCount; ++index) {
        const uint16_t slot = retained->slots[index];
        /* slots[] can name one tile twice; only the first release relists it. */
        if (!cache->entries[slot].pinned) continue;
        cache->entries[slot].pinned = false;
        cache_lru_push_head(cache, slot);
    }
    retained->slotCount = 0;
    retained->tileFirst = MODE1_VRAM_SIZE;
    retained->tileLast = 0;
    retained->valid = false;
}

static void retain_record(PpuGpu3DSCache* cache, unsigned bg, uint16_t slot,
                          uint32_t tileOffset, uint32_t tileBytes) {
    PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
    if (retained->slotCount >= PPU_GPU3DS_MAP_MAX_QUADS) return;
    retained->slots[retained->slotCount++] = slot;
    /* A pinned slot leaves the LRU list. It can never be an eviction victim,
     * so keeping it listed only made every eviction walk past as many as 2400
     * of them, and required a per-frame stamp across all of them to keep the
     * stale-slot refresh above off them. */
    if (!cache->entries[slot].pinned) {
        cache->entries[slot].pinned = true;
        cache_lru_unlink(cache, slot);
        cache->entries[slot].lruPrev = PPU_GPU3DS_CACHE_NIL;
        cache->entries[slot].lruNext = PPU_GPU3DS_CACHE_NIL;
    }
    if (tileOffset < retained->tileFirst) retained->tileFirst = tileOffset;
    if (tileOffset + tileBytes > retained->tileLast)
        retained->tileLast = tileOffset + tileBytes;
}

/* Number of character bytes the retained quads sample, or 0 when the range is
 * unusable or larger than the snapshot can hold. */
static uint32_t retain_tile_bytes(const PpuGpu3DSRetainedMap* retained) {
    if (retained->tileLast <= retained->tileFirst ||
        retained->tileLast > MODE1_VRAM_SIZE)
        return 0;
    const uint32_t bytes = retained->tileLast - retained->tileFirst;
    return bytes <= PPU_GPU3DS_MAP_TILE_SNAPSHOT ? bytes : 0;
}

static void retain_snapshot(PpuGpu3DSCache* cache, unsigned bg,
                            const PpuGpu3DSFrameView* frame) {
    const uint32_t bytes = retain_tile_bytes(&cache->retained[bg]);
    if (bytes == 0) {
        cache->retained[bg].tileLast = cache->retained[bg].tileFirst;
        return;
    }
    /* Up to 24 KB, and the largest single copy the builder makes. */
    Arm11FastMemcpy(cache->retainedTiles[bg],
                    frame->memory.vram + cache->retained[bg].tileFirst, bytes);
}

/* The retained snapshot compare walks tens of kilobytes of VRAM every frame.
 * An Old 3DS has 32 KiB of L1 and **no L2**, so every line this touches is a
 * direct FCRAM stall of 100-200 cycles, and newlib's memcmp is a plain word
 * loop with no prefetching -- it eats that latency line by line. Issuing PLD a
 * few lines ahead lets the loads overlap the stalls. Cache lines are 32 bytes.
 * On any other target the library routine is better than anything written here.
 */
#if defined(__3DS__) && defined(__ARM_ARCH_6__)
static bool retain_bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t n) {
    const uint32_t* wa = (const uint32_t*)(const void*)a;
    const uint32_t* wb = (const uint32_t*)(const void*)b;
    uint32_t words = n >> 2u;
    while (words >= 8u) {
        __builtin_prefetch(wa + 24, 0, 0);
        __builtin_prefetch(wb + 24, 0, 0);
        if (wa[0] != wb[0] || wa[1] != wb[1] || wa[2] != wb[2] ||
            wa[3] != wb[3] || wa[4] != wb[4] || wa[5] != wb[5] ||
            wa[6] != wb[6] || wa[7] != wb[7])
            return false;
        wa += 8;
        wb += 8;
        words -= 8u;
    }
    while (words--) {
        if (*wa++ != *wb++) return false;
    }
    const uint8_t* ta = (const uint8_t*)wa;
    const uint8_t* tb = (const uint8_t*)wb;
    for (uint32_t i = 0; i < (n & 3u); ++i) {
        if (ta[i] != tb[i]) return false;
    }
    return true;
}
#else
static bool retain_bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t n) {
    return memcmp(a, b, n) == 0;
}
#endif

static bool retain_tiles_current(PpuGpu3DSCache* cache, unsigned bg,
                                 const PpuGpu3DSFrameView* frame) {
    const PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
    /* Every slot named here is valid and pinned by construction: `pinned` is
     * cleared only by retain_release (which empties slotCount with it), `valid`
     * only by CacheInit's memset (which does the same), a pinned slot is not on
     * the LRU list so it can never be an eviction victim, and the stale-slot
     * refresh in the lookup skips pinned entries. Re-checking all 600 slots per
     * layer cost 2400 scattered reads through a ~330 KB array every frame --
     * the same cache-miss pattern retain_touch used to have -- to confirm
     * something that cannot have changed. */
    const uint32_t bytes = retain_tile_bytes(retained);
    if (bytes == 0) return false;
    const uint32_t first = retained->tileFirst;
    const uint32_t last = retained->tileLast;
    if (cache->verifiedFrame != cache->frame) {
        cache->verifiedFrame = cache->frame;
        cache->verifiedCount = 0;
    }
    /* Contained in a range already matched this frame. That range's snapshot
     * equals VRAM now, and it equalled this layer's snapshot when this one was
     * taken -- the outer layer was retained then, so its snapshot matched VRAM,
     * and so did this one. Therefore this layer still matches, without reading
     * the bytes a second time. */
    for (unsigned index = 0; index < cache->verifiedCount; ++index) {
        if (first >= cache->verifiedFirst[index] &&
            last <= cache->verifiedLast[index])
            return true;
    }
    if (!retain_bytes_equal(cache->retainedTiles[bg],
                            frame->memory.vram + first, bytes))
        return false;
    if (cache->verifiedCount < MODE1_GBA_BG_COUNT) {
        cache->verifiedFirst[cache->verifiedCount] = first;
        cache->verifiedLast[cache->verifiedCount] = last;
        ++cache->verifiedCount;
    }
    return true;
}

static void retain_store(PpuGpu3DSCache* cache, unsigned bg, uint32_t signature,
                         const PpuGpu3DSBgMap* map) {
    PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
    retained->signature = signature;
    retained->firstIndex = map->firstIndex;
    retained->rowLo = map->rowLo;
    retained->colLo = map->colLo;
    retained->rows = map->rows;
    retained->cols = map->cols;
    retained->bgcnt = map->bgcnt;
    retained->valid = map->valid;
}

static bool retain_matches(const PpuGpu3DSCache* cache, unsigned bg,
                           uint32_t signature) {
    const PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
    return retained->valid && signature != 0 && retained->signature == signature;
}

static bool bg_map_uses_screen_space(const PpuGpu3DSFrameView* frame,
                                     unsigned bg, uint16_t bgcnt,
                                     const uint8_t* io) {
    const unsigned size = (bgcnt >> 14u) & 3u;
    const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
    const uint16_t mosaic = read16(io, MODE1_IO_MOSAIC);
    /* Both remap screen columns to source columns, so one shared copy of the
     * tiles cannot serve every band. */
    const bool widescreenRules =
            frame->width > MODE1_GBA_BG_CLIP_X &&
            (mapWidthTiles == 32u ||
             (bg == 0 &&
              (frame->wsHudRightAnchor || frame->wsMsgShift != 0)));
    const bool mosaicEnabled =
            (bgcnt & (1u << 6u)) != 0 &&
            ((mosaic & 0x0fu) != 0 || (mosaic & 0xf0u) != 0);
    return widescreenRules || mosaicEnabled;
}

static bool build_bg_map(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                         uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                         unsigned bg, const PpuGpu3DSBand* bands,
                         size_t bandCount, PpuGpu3DSBgMap* map,
                         size_t* vertexCursor, size_t* indexCursor) {
    map->valid = false;
    if (!sMapSpaceEnabled) return true;
    if (frame->affine && (bg == 2u || bg == 3u)) {
        cmd->mapReject[PPU_GPU3DS_MAP_REJECT_AFFINE] += 1u;
        return true;
    }

    unsigned rowLo = ~0u, rowHi = 0, colLo = ~0u, colHi = 0;
    uint16_t sharedBgcnt = 0;
    bool any = false;
    for (size_t index = 0; index < bandCount; ++index) {
        const PpuGpu3DSBand* band = &bands[index];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        if ((dispcnt & (MODE1_DISP_BG0_ON << bg)) == 0) continue;
        const uint8_t* io =
                frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
        const uint16_t bgcnt = read16(io, MODE1_IO_BG0CNT + bg * 2u);
        if (any && (bgcnt & ~3u) != (sharedBgcnt & ~3u)) {
            cmd->mapReject[PPU_GPU3DS_MAP_REJECT_CONTROL] += 1u;
            return true;
        }
        if (!any && bg_map_uses_screen_space(frame, bg, bgcnt, io)) {
            cmd->mapReject[PPU_GPU3DS_MAP_REJECT_SCREEN_SPACE] += 1u;
            return true;
        }
        sharedBgcnt = bgcnt;
        any = true;

        const unsigned size = (bgcnt >> 14u) & 3u;
        const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
        const unsigned scrollX = read16(io, MODE1_IO_BG0HOFS + bg * 4u) & 0x1ffu;
        const unsigned scrollY = read16(io, MODE1_IO_BG0VOFS + bg * 4u) & 0x1ffu;
        const unsigned rTop = (band->firstLine + scrollY) >> 3u;
        const unsigned rBot =
                (band->firstLine + band->lineCount - 1u + scrollY) >> 3u;
        const unsigned cL = scrollX >> 3u;
        const unsigned cR = (scrollX + frame->width - 1u) >> 3u;
        if (rTop < rowLo) rowLo = rTop;
        if (rBot > rowHi) rowHi = rBot;
        if (cL < colLo) colLo = cL;
        if (cR > colHi) colHi = cR;
        (void)mapWidthTiles;
    }
    if (!any) {
        cmd->mapReject[PPU_GPU3DS_MAP_REJECT_DISABLED] += 1u;
        return true;
    }

    const unsigned rows = rowHi - rowLo + 1u;
    const unsigned cols = colHi - colLo + 1u;
    if ((size_t)rows * cols > PPU_GPU3DS_MAP_MAX_QUADS) {
        cmd->mapReject[PPU_GPU3DS_MAP_REJECT_TOO_LARGE] += 1u;
        if (rows * cols > cmd->mapLargestQuads)
            cmd->mapLargestQuads = (uint32_t)(rows * cols);
        return true;
    }

    const uint16_t bgcnt = sharedBgcnt;
    {
        const unsigned mapSize = (bgcnt >> 14u) & 3u;
        const uint32_t screenBytes =
                ((mapSize & 1u) ? 64u : 32u) * ((mapSize & 2u) ? 64u : 32u) * 2u;
        uint32_t paletteGeneration = cache->bg256Generation;
        for (unsigned bank = 0; bank < 16u; ++bank)
            paletteGeneration = paletteGeneration * 31u + cache->bgBankGeneration[bank];
        PROFILE_BEGIN(PPU_GPU3DS_PHASE_MAPSIG);
        const uint32_t signature = map_signature(
                frame, bgcnt, ((bgcnt >> 8u) & 0x1fu) * 0x800u, screenBytes,
                ((bgcnt >> 2u) & 3u) * 0x4000u, rowLo, colLo, rows, cols,
                paletteGeneration);
        PROFILE_END(PPU_GPU3DS_PHASE_MAPSIG);
        PROFILE_BEGIN(PPU_GPU3DS_PHASE_MAPRETAIN);
        const bool retainCurrent = retain_matches(cache, bg, signature) &&
                                   retain_tiles_current(cache, bg, frame);
        PROFILE_END(PPU_GPU3DS_PHASE_MAPRETAIN);
        if (retainCurrent) {
            const PpuGpu3DSRetainedMap* retained = &cache->retained[bg];
            if (retained->rows == rows && retained->cols == cols &&
                retained->rowLo == rowLo && retained->colLo == colLo) {
                map->firstIndex = retained->firstIndex;
                map->rowLo = retained->rowLo;
                map->colLo = retained->colLo;
                map->rows = retained->rows;
                map->cols = retained->cols;
                map->bgcnt = retained->bgcnt;
                map->valid = true;
                cmd->mapLayerMask |= (uint8_t)(1u << bg);
                return true;
            }
        }
        retain_release(cache, bg);
        cache->retained[bg].signature = signature;
    }
    *vertexCursor = (size_t)bg * PPU_GPU3DS_MAP_SLICE_VERTICES;
    *indexCursor = (size_t)bg * PPU_GPU3DS_MAP_SLICE_INDICES;
    cmd->mapDirtyMask |= (uint8_t)(1u << bg);
    const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;
    const bool bpp8 = ((bgcnt >> 7u) & 1u) != 0;
    const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
    const unsigned size = (bgcnt >> 14u) & 3u;
    const unsigned mapWidthTiles = (size & 1u) ? 64u : 32u;
    const unsigned mapHeightTiles = (size & 2u) ? 64u : 32u;
    const uint32_t tileBytes = bpp8 ? 64u : 32u;
    const float invWidth = 2.0f / (float)frame->width;
    const float invHeight = 2.0f / (float)frame->height;
    const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;

    const uint32_t firstIndex = (uint32_t)*indexCursor;
    for (unsigned r = 0; r < rows; ++r) {
        const unsigned tileY = (rowLo + r) & (mapHeightTiles - 1u);
        const float y0 = -((float)((rowLo + r) * PPU_GPU3DS_TILE_SIDE) * invHeight);
        const float y1 = y0 - PPU_GPU3DS_TILE_SIDE * invHeight;
        for (unsigned c = 0; c < cols; ++c) {
            const unsigned tileX = (colLo + c) & (mapWidthTiles - 1u);
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
            const uint32_t tileOffset =
                    charBase + (uint32_t)(entry & 0x03ffu) * tileBytes;
            if (tileOffset > MODE1_VRAM_SIZE - tileBytes) return false;
            if (!quad_room(cmd, vertexCursor, indexCursor)) return false;

            uint16_t slot;
            if (!PpuGpu3DS_CacheTile(cache, frame->memory.vram,
                                     (PpuGpu3DSTileKey){
                                             .vramOffset = tileOffset,
                                             .paletteBank = (uint8_t)(entry >> 12u),
                                             .bpp8 = bpp8,
                                             .domain = PPU_GPU3DS_PALETTE_BG,
                                     },
                                     atlas, &slot))
                return false;
            retain_record(cache, bg, slot, tileOffset, tileBytes);
            const unsigned tilesPerRow =
                    PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE;
            const float slotLeft =
                    (float)((slot % tilesPerRow) * PPU_GPU3DS_TILE_SIDE) * invAtlas;
            const float slotTop =
                    1.0f - (float)((slot / tilesPerRow) * PPU_GPU3DS_TILE_SIDE) *
                                   invAtlas;
            float u0 = slotLeft;
            float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
            float v0 = slotTop;
            float v1 = slotTop - PPU_GPU3DS_TILE_SIDE * invAtlas;
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
            const float x0 =
                    (float)((colLo + c) * PPU_GPU3DS_TILE_SIDE) * invWidth;
            const float x1 = x0 + PPU_GPU3DS_TILE_SIDE * invWidth;
            PpuGpu3DSVertex* vertices = cmd->vertices + *vertexCursor;
            const int16_t pu0 = PpuGpu3DS_PackUV(u0);
            const int16_t pu1 = PpuGpu3DS_PackUV(u1);
            const int16_t pv0 = PpuGpu3DS_PackUV(v0);
            const int16_t pv1 = PpuGpu3DS_PackUV(v1);
            vertices[0] = (PpuGpu3DSVertex){ x0, y0, 0.0f, pu0, pv0 };
            vertices[1] = (PpuGpu3DSVertex){ x1, y0, 0.0f, pu1, pv0 };
            vertices[2] = (PpuGpu3DSVertex){ x1, y1, 0.0f, pu1, pv1 };
            vertices[3] = (PpuGpu3DSVertex){ x0, y1, 0.0f, pu0, pv1 };
            emit_indices(cmd, vertexCursor, indexCursor);
            *vertexCursor += 4;
            *indexCursor += 6;
        }
    }

    map->firstIndex = firstIndex;
    map->rowLo = (uint16_t)rowLo;
    map->colLo = (uint16_t)colLo;
    map->rows = (uint16_t)rows;
    map->cols = (uint16_t)cols;
    map->bgcnt = bgcnt;
    map->valid = true;
    cmd->mapLayerMask |= (uint8_t)(1u << bg);
    cmd->mapSliceVertices[bg] =
            (uint32_t)(*vertexCursor - (size_t)bg * PPU_GPU3DS_MAP_SLICE_VERTICES);
    retain_store(cache, bg, cache->retained[bg].signature, map);
    retain_snapshot(cache, bg, frame);
    return true;
}

static bool build_affine_map(const PpuGpu3DSFrameView* frame,
                             PpuGpu3DSCache* cache, uint16_t* atlas,
                             PpuGpu3DSCommandBuffer* cmd,
                             const PpuGpu3DSBand* bands, size_t bandCount,
                             PpuGpu3DSBgMap* map, size_t* vertexCursor,
                             size_t* indexCursor) {
    map->valid = false;
    if (!sMapSpaceEnabled) return true;
    uint16_t sharedBgcnt = 0;
    int32_t base = 0, scrollX = 0;
    bool any = false;
    for (size_t index = 0; index < bandCount; ++index) {
        const PpuGpu3DSBand* band = &bands[index];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        if ((dispcnt & MODE1_DISP_BG2_ON) == 0) continue;
        const uint8_t* io =
                frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
        const uint16_t bgcnt = read16(io, MODE1_IO_BG2CNT);
        /* Wrapping keeps every sample in range; without it the out-of-range
         * area is transparent and needs the general path. */
        if (!affine_is_scrolled_layer(io) || (bgcnt & (1u << 13u)) == 0)
            return true;
        int32_t bandBase = 0;
        if (!affine_scroll_base(frame, band->firstLine, &bandBase)) return true;
        for (unsigned line = band->firstLine + 1u;
             line < (unsigned)band->firstLine + band->lineCount; ++line) {
            int32_t lineBase = 0;
            if (!affine_scroll_base(frame, line, &lineBase) ||
                lineBase != bandBase)
                return true;
        }
        const int32_t bandScrollX = frame->affineRefX[band->firstLine] >> 8;
        if (any && ((bgcnt & ~3u) != (sharedBgcnt & ~3u) || bandBase != base ||
                    bandScrollX != scrollX))
            return true;
        sharedBgcnt = bgcnt;
        base = bandBase;
        scrollX = bandScrollX;
        any = true;
    }
    if (!any) {
        cmd->mapReject[PPU_GPU3DS_MAP_REJECT_DISABLED] += 1u;
        return true;
    }
    if (scrollX < 0 || base < 0) return true;

    static const int mapSizes[4] = { 128, 256, 512, 1024 };
    const uint16_t bgcnt = sharedBgcnt;
    const int mapSize = mapSizes[(bgcnt >> 14u) & 3u];
    const unsigned mapTiles = (unsigned)mapSize / PPU_GPU3DS_TILE_SIDE;
    const uint32_t screenBase = ((bgcnt >> 8u) & 0x1fu) * 0x800u;
    const uint32_t charBase = ((bgcnt >> 2u) & 3u) * 0x4000u;

    const unsigned rowLo = (unsigned)base >> 3u;
    const unsigned rowHi = (unsigned)(base + (int32_t)frame->height - 1) >> 3u;
    const unsigned colLo = (unsigned)scrollX >> 3u;
    const unsigned colHi = (unsigned)(scrollX + (int32_t)frame->width - 1) >> 3u;
    const unsigned rows = rowHi - rowLo + 1u;
    const unsigned cols = colHi - colLo + 1u;
    if ((size_t)rows * cols > PPU_GPU3DS_MAP_MAX_QUADS) {
        cmd->mapReject[PPU_GPU3DS_MAP_REJECT_TOO_LARGE] += 1u;
        return true;
    }
    {
        uint32_t paletteGeneration = cache->bg256Generation;
        for (unsigned bank = 0; bank < 16u; ++bank)
            paletteGeneration = paletteGeneration * 31u + cache->bgBankGeneration[bank];
        PROFILE_BEGIN(PPU_GPU3DS_PHASE_MAPSIG);
        const uint32_t signature = map_signature(
                frame, bgcnt, screenBase, (uint32_t)(mapTiles * mapTiles),
                charBase, rowLo, colLo, rows, cols, paletteGeneration);
        PROFILE_END(PPU_GPU3DS_PHASE_MAPSIG);
        PROFILE_BEGIN(PPU_GPU3DS_PHASE_MAPRETAIN);
        const bool retainCurrent = retain_matches(cache, 2u, signature) &&
                                   retain_tiles_current(cache, 2u, frame);
        PROFILE_END(PPU_GPU3DS_PHASE_MAPRETAIN);
        if (retainCurrent) {
            const PpuGpu3DSRetainedMap* retained = &cache->retained[2];
            if (retained->rows == rows && retained->cols == cols &&
                retained->rowLo == rowLo && retained->colLo == colLo) {
                map->firstIndex = retained->firstIndex;
                map->rowLo = retained->rowLo;
                map->colLo = retained->colLo;
                map->rows = retained->rows;
                map->cols = retained->cols;
                map->bgcnt = retained->bgcnt;
                map->valid = true;
                cmd->mapLayerMask |= (uint8_t)(1u << 2u);
                return true;
            }
        }
        retain_release(cache, 2u);
        cache->retained[2].signature = signature;
    }
    *vertexCursor = 2u * PPU_GPU3DS_MAP_SLICE_VERTICES;
    *indexCursor = 2u * PPU_GPU3DS_MAP_SLICE_INDICES;
    cmd->mapDirtyMask |= (uint8_t)(1u << 2u);

    const float invWidth = 2.0f / (float)frame->width;
    const float invHeight = 2.0f / (float)frame->height;
    const float invAtlas = 1.0f / PPU_GPU3DS_ATLAS_SIDE;
    const uint32_t firstIndex = (uint32_t)*indexCursor;
    for (unsigned r = 0; r < rows; ++r) {
        const unsigned tileY = (rowLo + r) & (mapTiles - 1u);
        const float y0 = -((float)((rowLo + r) * PPU_GPU3DS_TILE_SIDE) * invHeight);
        const float y1 = y0 - PPU_GPU3DS_TILE_SIDE * invHeight;
        for (unsigned c = 0; c < cols; ++c) {
            const unsigned tileX = (colLo + c) & (mapTiles - 1u);
            const uint32_t mapOffset = tileY * mapTiles + tileX;
            if (screenBase >= MODE1_VRAM_SIZE ||
                mapOffset >= MODE1_VRAM_SIZE - screenBase)
                return false;
            const uint8_t tile = frame->memory.vram[screenBase + mapOffset];
            if (!quad_room(cmd, vertexCursor, indexCursor)) return false;
            uint16_t slot;
            if (!PpuGpu3DS_CacheTile(cache, frame->memory.vram,
                                     (PpuGpu3DSTileKey){
                                             .vramOffset =
                                                     charBase + (uint32_t)tile * 64u,
                                             .paletteBank = 0,
                                             .bpp8 = true,
                                             .domain = PPU_GPU3DS_PALETTE_BG,
                                     },
                                     atlas, &slot))
                return false;
            retain_record(cache, 2u, slot, charBase + (uint32_t)tile * 64u, 64u);
            const unsigned tilesPerRow =
                    PPU_GPU3DS_ATLAS_SIDE / PPU_GPU3DS_TILE_SIDE;
            const float slotLeft =
                    (float)((slot % tilesPerRow) * PPU_GPU3DS_TILE_SIDE) * invAtlas;
            const float slotTop =
                    1.0f - (float)((slot / tilesPerRow) * PPU_GPU3DS_TILE_SIDE) *
                                   invAtlas;
            const float u0 = slotLeft;
            const float u1 = slotLeft + PPU_GPU3DS_TILE_SIDE * invAtlas;
            const float v0 = slotTop;
            const float v1 = slotTop - PPU_GPU3DS_TILE_SIDE * invAtlas;
            const float x0 =
                    (float)((colLo + c) * PPU_GPU3DS_TILE_SIDE) * invWidth;
            const float x1 = x0 + PPU_GPU3DS_TILE_SIDE * invWidth;
            PpuGpu3DSVertex* vertices = cmd->vertices + *vertexCursor;
            const int16_t pu0 = PpuGpu3DS_PackUV(u0);
            const int16_t pu1 = PpuGpu3DS_PackUV(u1);
            const int16_t pv0 = PpuGpu3DS_PackUV(v0);
            const int16_t pv1 = PpuGpu3DS_PackUV(v1);
            vertices[0] = (PpuGpu3DSVertex){ x0, y0, 0.0f, pu0, pv0 };
            vertices[1] = (PpuGpu3DSVertex){ x1, y0, 0.0f, pu1, pv0 };
            vertices[2] = (PpuGpu3DSVertex){ x1, y1, 0.0f, pu1, pv1 };
            vertices[3] = (PpuGpu3DSVertex){ x0, y1, 0.0f, pu0, pv1 };
            emit_indices(cmd, vertexCursor, indexCursor);
            *vertexCursor += 4;
            *indexCursor += 6;
        }
    }

    map->firstIndex = firstIndex;
    map->rowLo = (uint16_t)rowLo;
    map->colLo = (uint16_t)colLo;
    map->rows = (uint16_t)rows;
    map->cols = (uint16_t)cols;
    map->bgcnt = bgcnt;
    map->valid = true;
    cmd->mapLayerMask |= (uint8_t)(1u << 2u);
    cmd->mapSliceVertices[2] =
            (uint32_t)(*vertexCursor - 2u * PPU_GPU3DS_MAP_SLICE_VERTICES);
    retain_store(cache, 2u, cache->retained[2].signature, map);
    retain_snapshot(cache, 2u, frame);
    return true;
}

/* ---------------------------------------------------------------------------
 * Per-layer band merging
 *
 * A band is a run of scanlines whose PPU registers all match, so per-line HDMA
 * -- which the game uses for scrolling water and the title screen -- splits a
 * frame into as many as 160 of them. Emitting every layer's geometry for every
 * band is what overran the command buffers. In practice HDMA rewrites ONE
 * layer's scroll, so each other layer is identical across the whole run and
 * can be drawn as a single band covering it.
 *
 * Merging is only sound when the inputs that layer's geometry and its batch
 * state depend on are equal across the run, so the test is deliberately
 * conservative: any active window whose registers differ, or an object window
 * (whose regions depend on which objects cross the band), blocks the merge.
 * ------------------------------------------------------------------------ */
enum {
    PPU_GPU3DS_LAYER_BAND_OBJ = MODE1_GBA_BG_COUNT,
    PPU_GPU3DS_LAYER_BAND_BACKDROP,
    PPU_GPU3DS_LAYER_BAND_COUNT
};

static bool bands_share_regions(const PpuGpu3DSFrameView* frame,
                                const PpuGpu3DSBand* left,
                                const PpuGpu3DSBand* right) {
    const uint16_t dispcnt = frame->dispcntPerLine[left->firstLine];
    if ((dispcnt & (MODE1_DISP_WIN0_ON | MODE1_DISP_WIN1_ON |
                    MODE1_DISP_OBJWIN_ON)) == 0)
        return true;
    /* Object-window regions depend on which objects cross the band. */
    if ((dispcnt & (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON)) ==
        (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON))
        return false;
    const uint8_t* a = frame->ioPerLine + (size_t)left->ioRow * MODE1_IO_MEM_SIZE;
    const uint8_t* b = frame->ioPerLine + (size_t)right->ioRow * MODE1_IO_MEM_SIZE;
    static const unsigned windowRegisters[] = {
        MODE1_IO_WIN0H, MODE1_IO_WIN1H, MODE1_IO_WIN0V,
        MODE1_IO_WIN1V, MODE1_IO_WININ, MODE1_IO_WINOUT
    };
    for (unsigned i = 0; i < sizeof(windowRegisters) / sizeof(*windowRegisters);
         ++i) {
        if (read16(a, windowRegisters[i]) != read16(b, windowRegisters[i]))
            return false;
    }
    return window_vertical_active(dispcnt, MODE1_DISP_WIN0_ON,
                                  read16(a, MODE1_IO_WIN0V),
                                  left->firstLine) ==
                   window_vertical_active(dispcnt, MODE1_DISP_WIN0_ON,
                                          read16(b, MODE1_IO_WIN0V),
                                          right->firstLine) &&
           window_vertical_active(dispcnt, MODE1_DISP_WIN1_ON,
                                  read16(a, MODE1_IO_WIN1V),
                                  left->firstLine) ==
                   window_vertical_active(dispcnt, MODE1_DISP_WIN1_ON,
                                          read16(b, MODE1_IO_WIN1V),
                                          right->firstLine);
}

/* Only what this particular layer depends on; the state common to every layer
 * has already been established by mark_shared_bands. */
static bool bands_share_layer(const PpuGpu3DSFrameView* frame, unsigned layer,
                              const PpuGpu3DSBand* left,
                              const PpuGpu3DSBand* right) {
    const uint8_t* a = frame->ioPerLine + (size_t)left->ioRow * MODE1_IO_MEM_SIZE;
    const uint8_t* b = frame->ioPerLine + (size_t)right->ioRow * MODE1_IO_MEM_SIZE;
    if (layer < MODE1_GBA_BG_COUNT) {
        if (read16(a, MODE1_IO_BG0CNT + layer * 2u) !=
                    read16(b, MODE1_IO_BG0CNT + layer * 2u) ||
            read16(a, MODE1_IO_BG0HOFS + layer * 4u) !=
                    read16(b, MODE1_IO_BG0HOFS + layer * 4u) ||
            read16(a, MODE1_IO_BG0VOFS + layer * 4u) !=
                    read16(b, MODE1_IO_BG0VOFS + layer * 4u))
            return false;
        /* An affine BG2 is placed from the per-line reference point, so bands
         * normally cannot merge across it. The exception is the one the game
         * actually uses: an identity matrix whose reference advances one
         * source line per scanline is a plainly scrolled layer, and every band
         * of it resolves to the same origin -- which is what lets a whole
         * frame of them collapse into a single batch. */
        if (frame->affine && layer == 2) {
            int32_t leftBase = 0, rightBase = 0;
            const bool scrolled = affine_is_scrolled_layer(a) &&
                                  affine_is_scrolled_layer(b) &&
                                  affine_scroll_base(frame, left->firstLine,
                                                     &leftBase) &&
                                  affine_scroll_base(frame, right->firstLine,
                                                     &rightBase);
            if (scrolled) {
                if (leftBase != rightBase ||
                    frame->affineRefX[left->firstLine] !=
                            frame->affineRefX[right->firstLine])
                    return false;
            } else if (frame->affineRefX[left->firstLine] !=
                               frame->affineRefX[right->firstLine] ||
                       frame->affineRefY[left->firstLine] !=
                               frame->affineRefY[right->firstLine]) {
                return false;
            }
        }
    }
    return true;
}

/* Whether each band shares the state every layer depends on -- display
 * control, mosaic, blending and the window regions -- with the band before it.
 * Six layers merge over the same band list, so this is computed once instead of
 * re-reading the same registers for each of them. */
static void mark_shared_bands(const PpuGpu3DSFrameView* frame,
                              const PpuGpu3DSBand* bands, size_t bandCount,
                              bool* sharedWithPrevious) {
    if (bandCount != 0) sharedWithPrevious[0] = false;
    for (size_t index = 1; index < bandCount; ++index) {
        const PpuGpu3DSBand* left = &bands[index - 1];
        const PpuGpu3DSBand* right = &bands[index];
        const uint8_t* a =
                frame->ioPerLine + (size_t)left->ioRow * MODE1_IO_MEM_SIZE;
        const uint8_t* b =
                frame->ioPerLine + (size_t)right->ioRow * MODE1_IO_MEM_SIZE;
        static const unsigned sharedRegisters[] = {
            MODE1_IO_MOSAIC, MODE1_IO_BLDCNT, MODE1_IO_BLDALPHA, MODE1_IO_BLDY
        };
        bool shared = frame->dispcntPerLine[left->firstLine] ==
                      frame->dispcntPerLine[right->firstLine];
        for (unsigned i = 0;
             shared && i < sizeof(sharedRegisters) / sizeof(*sharedRegisters);
             ++i) {
            shared = read16(a, sharedRegisters[i]) == read16(b, sharedRegisters[i]);
        }
        sharedWithPrevious[index] =
                shared && bands_share_regions(frame, left, right);
    }
}

static size_t merge_layer_bands(const PpuGpu3DSFrameView* frame, unsigned layer,
                                const PpuGpu3DSBand* bands, size_t bandCount,
                                const bool* sharedWithPrevious,
                                PpuGpu3DSBand* out) {
    size_t count = 0;
    for (size_t index = 0; index < bandCount; ++index) {
        if (count != 0 && sharedWithPrevious[index] &&
            out[count - 1].firstLine + out[count - 1].lineCount ==
                    bands[index].firstLine &&
            bands_share_layer(frame, layer, &bands[index - 1], &bands[index])) {
            out[count - 1].lineCount += bands[index].lineCount;
            continue;
        }
        out[count++] = bands[index];
    }
    return count;
}

static bool build_scene(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                        uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd,
                        const PpuGpu3DSOamSet* oam, const PpuGpu3DSBand* bands,
                        size_t bandCount, bool mapSpaceFits,
                        size_t* vertexCursor, size_t* indexCursor,
                        size_t* batchCursor) {
    PpuGpu3DSRegion regions[PPU_GPU3DS_ATLAS_SIDE * 2u];
    /* Function-local so these lists stay off the 3DS thread stack. */
    static PpuGpu3DSBand layerBands[PPU_GPU3DS_LAYER_BAND_COUNT]
                                   [MODE1_GBA_HEIGHT];
    size_t layerBandCount[PPU_GPU3DS_LAYER_BAND_COUNT];
    PROFILE_BEGIN(PPU_GPU3DS_PHASE_MERGE);
    static bool sharedWithPrevious[MODE1_GBA_HEIGHT];
    mark_shared_bands(frame, bands, bandCount, sharedWithPrevious);
    /* A layer that is off for the whole frame emits nothing, so its bands are
     * never walked; merging them is pure cost. */
    uint16_t enabledAnywhere = 0;
    for (size_t index = 0; index < bandCount; ++index)
        enabledAnywhere |= frame->dispcntPerLine[bands[index].firstLine];
    for (unsigned layer = 0; layer < PPU_GPU3DS_LAYER_BAND_COUNT; ++layer) {
        if (layer < MODE1_GBA_BG_COUNT &&
            (enabledAnywhere & (MODE1_DISP_BG0_ON << layer)) == 0) {
            layerBandCount[layer] = 0;
            continue;
        }
        if (layer == PPU_GPU3DS_LAYER_BAND_OBJ &&
            (enabledAnywhere & MODE1_DISP_OBJ_ON) == 0) {
            layerBandCount[layer] = 0;
            continue;
        }
        layerBandCount[layer] =
                merge_layer_bands(frame, layer, bands, bandCount,
                                  sharedWithPrevious, layerBands[layer]);
    }
    PROFILE_END(PPU_GPU3DS_PHASE_MERGE);
    const bool emit = true;
    const PpuGpu3DSBand* objBands = layerBands[PPU_GPU3DS_LAYER_BAND_OBJ];
    const size_t objBandCount = layerBandCount[PPU_GPU3DS_LAYER_BAND_OBJ];

    PpuGpu3DSBgMap maps[MODE1_GBA_BG_COUNT] = { { 0 } };
    PROFILE_BEGIN(PPU_GPU3DS_PHASE_MAPS);
    if (mapSpaceFits) {
        const size_t dynamicVertex =
                (size_t)MODE1_GBA_BG_COUNT * PPU_GPU3DS_MAP_SLICE_VERTICES;
        const size_t dynamicIndex =
                (size_t)MODE1_GBA_BG_COUNT * PPU_GPU3DS_MAP_SLICE_INDICES;
        for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
            const bool affineLayer = frame->affine && bg == 2u;
            if (affineLayer
                        ? !build_affine_map(frame, cache, atlas, cmd,
                                            layerBands[bg], layerBandCount[bg],
                                            &maps[bg], vertexCursor, indexCursor)
                        : !build_bg_map(frame, cache, atlas, cmd, bg,
                                        layerBands[bg], layerBandCount[bg],
                                        &maps[bg], vertexCursor, indexCursor))
                return false;
        }
        /* Per-band geometry starts past every slice, so it can never overwrite
         * a layer whose map geometry was kept from an earlier frame. */
        *vertexCursor = dynamicVertex;
        *indexCursor = dynamicIndex;
        cmd->dynamicFirstVertex = (uint32_t)dynamicVertex;
        cmd->dynamicFirstIndex = (uint32_t)dynamicIndex;
    }
    PROFILE_END(PPU_GPU3DS_PHASE_MAPS);

    PROFILE_BEGIN(PPU_GPU3DS_PHASE_OBJWIN);
    for (size_t bandIndex = 0; bandIndex < objBandCount; ++bandIndex) {
        if (build_abandoned(cmd)) return false;
        const PpuGpu3DSBand* band = &objBands[bandIndex];
        const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
        if ((dispcnt & (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON)) !=
            (MODE1_DISP_OBJ_ON | MODE1_DISP_OBJWIN_ON))
            continue;
        for (int entry = (int)oam->count - 1; entry >= 0; --entry) {
            const PpuGpu3DSObj* obj = &oam->entries[entry];
            if (obj->mode == 2u &&
                !build_obj(frame, cache, atlas, cmd, band, obj, NULL, 0, true,
                           emit, vertexCursor, indexCursor, batchCursor))
                return false;
        }
    }

    PROFILE_END(PPU_GPU3DS_PHASE_OBJWIN);

    {
        const PpuGpu3DSBand* backdropBands =
                layerBands[PPU_GPU3DS_LAYER_BAND_BACKDROP];
        const size_t count = layerBandCount[PPU_GPU3DS_LAYER_BAND_BACKDROP];
        for (size_t bandIndex = 0; bandIndex < count; ++bandIndex) {
            if (build_abandoned(cmd)) return false;
            const PpuGpu3DSBand* band = &backdropBands[bandIndex];
            const uint8_t* io =
                    frame->ioPerLine + (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
            if (!build_backdrop_target2(frame, cmd, band, io, emit,
                                        vertexCursor, indexCursor, batchCursor))
                return false;
        }
    }

    PROFILE_BEGIN(PPU_GPU3DS_PHASE_SCENE);
    /* Priority-major: a pixel still sees its layers in the same order, but each
     * layer now walks its own merged bands instead of every split band. */
    for (int priority = 3; priority >= 0; --priority) {
        for (int bg = MODE1_GBA_BG_COUNT - 1; bg >= 0; --bg) {
            if (frame->affine && bg == 3) continue;
            const PpuGpu3DSBand* bgBands = layerBands[bg];
            const size_t count = layerBandCount[bg];
            for (size_t bandIndex = 0; bandIndex < count; ++bandIndex) {
                if (build_abandoned(cmd)) return false;
                const PpuGpu3DSBand* band = &bgBands[bandIndex];
                const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
                const uint8_t* io = frame->ioPerLine +
                                    (size_t)band->ioRow * MODE1_IO_MEM_SIZE;
                const uint16_t bgcnt =
                        read16(io, MODE1_IO_BG0CNT + (unsigned)bg * 2u);
                if ((dispcnt & (MODE1_DISP_BG0_ON << bg)) == 0 ||
                    (bgcnt & 3u) != (unsigned)priority)
                    continue;
                PROFILE_BEGIN(PPU_GPU3DS_PHASE_REGIONS);
                const size_t regionCount =
                        build_regions(frame, oam, band, regions);
                PROFILE_END(PPU_GPU3DS_PHASE_REGIONS);
                PROFILE_BEGIN(PPU_GPU3DS_PHASE_BG);
                const bool built =
                        frame->affine && bg == 2
                                ? build_affine_bg(frame, cache, atlas, cmd,
                                                  band, &maps[bg], regions,
                                                  regionCount, emit,
                                                  vertexCursor, indexCursor,
                                                  batchCursor)
                                : build_text_bg(frame, cache, atlas, cmd, band,
                                                (unsigned)bg, &maps[bg],
                                                regions, regionCount, emit,
                                                vertexCursor, indexCursor,
                                                batchCursor);
                PROFILE_END(PPU_GPU3DS_PHASE_BG);
                if (!built) return false;
            }
        }
        PROFILE_BEGIN(PPU_GPU3DS_PHASE_OBJ);
        for (size_t bandIndex = 0; bandIndex < objBandCount; ++bandIndex) {
            if (build_abandoned(cmd)) return false;
            const PpuGpu3DSBand* band = &objBands[bandIndex];
            const uint16_t dispcnt = frame->dispcntPerLine[band->firstLine];
            if ((dispcnt & MODE1_DISP_OBJ_ON) == 0) continue;
            const uint8_t* list = oam->byPriority[priority & 3];
            const unsigned listCount = oam->priorityCount[priority & 3];
            if (listCount == 0) continue;
            const size_t regionCount = build_regions(frame, oam, band, regions);
            for (unsigned slot = 0; slot < listCount; ++slot) {
                const PpuGpu3DSObj* obj = &oam->entries[list[slot]];
                if (!build_obj(frame, cache, atlas, cmd, band, obj, regions,
                               regionCount, false, emit, vertexCursor,
                               indexCursor, batchCursor))
                    return false;
            }
        }
        PROFILE_END(PPU_GPU3DS_PHASE_OBJ);
    }
    PROFILE_END(PPU_GPU3DS_PHASE_SCENE);
    return true;
}

bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame,
                             PpuGpu3DSCache* cache, uint16_t* atlas,
                             PpuGpu3DSCommandBuffer* cmd) {
    if (!frame || !cache || !atlas || !cmd || !cmd->vertices || !cmd->indices ||
        !cmd->batches || !frame->memory.vram || !frame->memory.bg_palette ||
        !frame->ioPerLine || !frame->dispcntPerLine ||
        (frame->affine && (!frame->affineRefX || !frame->affineRefY)) ||
        frame->width == 0 || frame->width > PPU_GPU3DS_ATLAS_SIDE ||
        frame->height == 0 || frame->height > MODE1_GBA_HEIGHT) {
        if (cmd) cmd->failReason = PPU_GPU3DS_BUILD_ARGUMENTS;
        return false;
    }
    cmd->failReason = PPU_GPU3DS_BUILD_OK;

    PpuGpu3DSBand sourceBands[MODE1_GBA_HEIGHT];
    PpuGpu3DSBand bands[MODE1_GBA_HEIGHT];
    PROFILE_BEGIN(PPU_GPU3DS_PHASE_BANDS);
    const size_t sourceBandCount = PpuGpu3DS_BuildBands(frame, sourceBands);
    const bool forcedBlank =
            (frame->frameDispcnt & MODE1_DISP_FORCED_BLANK) != 0;
    if (sourceBandCount == 0 ||
        !frame_features_supported(frame, sourceBands, sourceBandCount,
                                  forcedBlank)) {
        cmd->failReason = PPU_GPU3DS_BUILD_UNSUPPORTED;
        return false;
    }
    const size_t bandCount =
            split_window_bands(frame, sourceBands, sourceBandCount, bands);
    PROFILE_END(PPU_GPU3DS_PHASE_BANDS);
    /* Function-local so the 6 KB set never lands on the 3DS thread stack. */
    static PpuGpu3DSOamSet oam;
    build_oam_set(frame, &oam);

    const size_t startVertex = cmd->vertexCount;
    const size_t startIndex = cmd->indexCount;
    const size_t startBatch = cmd->batchCount;
    /* One pass emits straight into the buffers and records any write that did
     * not fit; a frame that overruns is discarded whole, so the caller sees
     * the counts it came in with. */
    cmd->overflow = false;
    cmd->bandCount = (uint16_t)bandCount;
    /* Buffers too small to hold the per-layer slices keep the per-band walk;
     * this is a property of the caller's buffers, not a global switch. */
    const bool mapSpaceFits =
            cmd->vertexCapacity >=
                    (size_t)MODE1_GBA_BG_COUNT * PPU_GPU3DS_MAP_SLICE_VERTICES &&
            cmd->indexCapacity >=
                    (size_t)MODE1_GBA_BG_COUNT * PPU_GPU3DS_MAP_SLICE_INDICES;
    cmd->mapLayerMask = 0;
    cmd->mapDirtyMask = 0;
    cmd->mapLargestQuads = 0;
    cmd->dynamicFirstVertex = 0;
    cmd->dynamicFirstIndex = 0;
    memset(cmd->mapSliceVertices, 0, sizeof(cmd->mapSliceVertices));
    memset(cmd->mapReject, 0, sizeof(cmd->mapReject));
    if (startBatch >= cmd->batchCapacity || startVertex > (size_t)UINT16_MAX) {
        cmd->failReason = PPU_GPU3DS_BUILD_CAPACITY;
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
    if (!forcedBlank &&
        !build_scene(frame, cache, atlas, cmd, &oam, bands, bandCount,
                     mapSpaceFits, &vertexCursor, &indexCursor, &batchCursor)) {
        cmd->requiredVertices = (uint32_t)vertexCursor;
        cmd->requiredBatches = (uint32_t)batchCursor;
        cmd->failReason = cmd->overflow      ? PPU_GPU3DS_BUILD_CAPACITY
                          : cache->exhausted ? PPU_GPU3DS_BUILD_ATLAS_FULL
                                             : PPU_GPU3DS_BUILD_GEOMETRY;
        return false;
    }
    cmd->requiredVertices = (uint32_t)vertexCursor;
    cmd->requiredBatches = (uint32_t)batchCursor;
    if (cmd->overflow) {
        cmd->failReason = PPU_GPU3DS_BUILD_CAPACITY;
        return false;
    }

    cmd->vertexCount = vertexCursor;
    cmd->indexCount = indexCursor;
    cmd->batchCount = batchCursor;
    return true;
}
