#pragma once

#include "cpu/mode1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool PpuGpu3DS_ShouldUse(bool isNew3DS, bool initialized, bool disabled) {
    return !isNew3DS && initialized && !disabled;
}

enum {
    /* Each background's map-space geometry owns a fixed slice of the command
     * buffer, so a layer that has not changed keeps last frame's vertices
     * while its neighbours are rebuilt. */
    PPU_GPU3DS_MAP_MAX_QUADS = 1024,
    PPU_GPU3DS_MAP_SLICE_VERTICES = PPU_GPU3DS_MAP_MAX_QUADS * 4,
    PPU_GPU3DS_MAP_SLICE_INDICES = PPU_GPU3DS_MAP_MAX_QUADS * 6,
    PPU_GPU3DS_MAP_TILE_SNAPSHOT = 24576,
    PPU_GPU3DS_ATLAS_SIDE = 512,
    PPU_GPU3DS_TILE_SIDE = 8,
    PPU_GPU3DS_SLOT_COUNT = 4096,
    PPU_GPU3DS_ATLAS_PIXELS = PPU_GPU3DS_ATLAS_SIDE * PPU_GPU3DS_ATLAS_SIDE,
    /* Power of two, sized so the chains stay at roughly one entry per key. */
    PPU_GPU3DS_CACHE_BUCKETS = 8192,
    PPU_GPU3DS_CACHE_NIL = 0xffff
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
    /* The 64 source bytes used to live here. Every hash-bucket walk compares a
     * key and follows hashNext, so inlining the copy dragged ~80 bytes through
     * a 32 KiB L1 with no L2 behind it to read twelve. They now live in a
     * parallel array touched only when a key actually matches, which puts four
     * probe records in a 32-byte cache line instead of half of one. */
    uint32_t paletteGeneration;
    uint32_t lastUseFrame;
    /* Intrusive links: hashNext chains this slot inside its bucket, and the
     * lru pair orders every slot from most to least recently used so both
     * lookup and eviction stay O(1) instead of scanning all 4096 slots. */
    uint16_t hashNext;
    uint16_t lruPrev, lruNext;
    bool valid;
    bool dirty;
    /* Held by retained map geometry: eviction must not hand this slot to a
     * different tile while quads are still pointing at it. */
    bool pinned;
    /* Every texel is transparent, so a quad sampling it can be left out
     * entirely instead of being drawn and discarded by the alpha test. */
    bool transparent;
} PpuGpu3DSCacheEntry;

/* A background's map-space geometry, kept across frames while the tilemap,
 * the tiles it names and the palette all stay put. */
typedef struct PpuGpu3DSRetainedMap {
    uint32_t signature;
    uint32_t firstIndex;
    uint16_t rowLo, colLo, rows, cols;
    uint16_t bgcnt;
    uint16_t slotCount;
    uint16_t slots[PPU_GPU3DS_MAP_MAX_QUADS];
    /* Byte range of the character data these quads sample, plus a copy of it
     * taken when they were built. Comparing against the copy is exact and a
     * straight sequential compare -- cheaper than digesting the range, and far
     * cheaper than checking each tile against its scattered cache entry. */
    uint32_t tileFirst, tileLast;
    bool valid;
} PpuGpu3DSRetainedMap;

typedef struct PpuGpu3DSCache {
    PpuGpu3DSCacheEntry entries[PPU_GPU3DS_SLOT_COUNT];
    uint16_t buckets[PPU_GPU3DS_CACHE_BUCKETS];
    /* Slots decoded since the last upload, so the backend flushes only what
     * changed rather than walking every slot. */
    uint16_t dirtySlots[PPU_GPU3DS_SLOT_COUNT];
    uint16_t dirtyCount;
    uint16_t lruHead, lruTail;
    uint16_t bgPalette[MODE1_PALETTE_COLORS];
    uint16_t objPalette[MODE1_PALETTE_COLORS];
    uint32_t bgBankGeneration[16];
    uint32_t objBankGeneration[16];
    /* Packed 4bpp banks. Tiles decoded back to back almost always share a
     * bank, so the sixteen packs that begin a decode are done once per bank
     * per change instead of once per tile. Indexed [domain][bank]. */
    /* Ranges verified against VRAM already this frame. Retained layers share
     * character data heavily -- one layer's range is routinely a subset of
     * another's -- so the same bytes were compared two and three times over. */
    uint32_t verifiedFirst[MODE1_GBA_BG_COUNT];
    uint32_t verifiedLast[MODE1_GBA_BG_COUNT];
    uint32_t verifiedFrame;
    unsigned verifiedCount;
    /* Parallel to entries[]: see PpuGpu3DSCacheEntry. */
    uint8_t sources[PPU_GPU3DS_SLOT_COUNT][64];
    uint16_t bankLut[2][16][16];
    uint32_t bankLutGeneration[2][16];
    bool bankLutValid[2][16];
    uint32_t bg256Generation;
    uint32_t obj256Generation;
    uint32_t frame;
    uint64_t hits, decodes;
    /* Raised when every slot is already claimed by the frame being built. */
    bool exhausted;
    PpuGpu3DSRetainedMap retained[MODE1_GBA_BG_COUNT];
    uint32_t retainedVertices, retainedIndices;
    /* Snapshots of those ranges. A layer whose tiles outgrow this keeps
     * rebuilding every frame rather than risking a stale reuse. */
    uint8_t retainedTiles[MODE1_GBA_BG_COUNT][PPU_GPU3DS_MAP_TILE_SNAPSHOT];
    bool retainedValid;
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
    /* w was always 1.0. PICA supplies 1.0 for a missing fourth component, and
     * uOffset's w is 0, so dropping it changes nothing on screen and takes the
     * vertex from 24 to 20 bytes -- a sixth off every streaming store the
     * builder makes and off every byte flushed for geometry, which is worth
     * more than it sounds on a core with 32 KiB of L1 and no L2. */
    float x, y, z;
    /* UV as a fixed-point multiple of 1/PPU_GPU3DS_UV_SCALE in atlas-normalized
     * units. This is lossless rather than approximate: ATLAS_SIDE is 512, so a
     * texel centre is (2t+1)/1024, and scaling by 4096 makes that the exact
     * integer 8t+4. 1/4096 is 2^-12, so the shader's rescale is exact in float
     * as well, and packed geometry renders bit-for-bit like the float form.
     * Takes the vertex from 20 to 16 bytes -- four to a 32-byte cache line
     * instead of straddling, which is what actually matters with no L2. */
    int16_t u, v;
} PpuGpu3DSVertex;

enum { PPU_GPU3DS_UV_SCALE = 4096 };

static inline int16_t PpuGpu3DS_PackUV(float uv) {
    const float scaled = uv * (float)PPU_GPU3DS_UV_SCALE;
    return (int16_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

static inline float PpuGpu3DS_UnpackUV(int16_t uv) {
    return (float)uv * (1.0f / (float)PPU_GPU3DS_UV_SCALE);
}


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
    /* Added to every vertex position by the shader. Zero except for
     * background batches that share one map-space copy of the tiles. */
    float offsetX, offsetY;
} PpuGpu3DSBatch;

/* Why a frame could not be expressed as GPU commands, so the fallback rate
 * can be attributed instead of guessed. */
typedef enum PpuGpu3DSBuildReason {
    PPU_GPU3DS_BUILD_OK,
    PPU_GPU3DS_BUILD_ARGUMENTS,
    PPU_GPU3DS_BUILD_UNSUPPORTED,
    PPU_GPU3DS_BUILD_CAPACITY,
    PPU_GPU3DS_BUILD_ATLAS_FULL,
    PPU_GPU3DS_BUILD_GEOMETRY,
    PPU_GPU3DS_BUILD_REASON_COUNT
} PpuGpu3DSBuildReason;

typedef enum PpuGpu3DSMapReject {
    PPU_GPU3DS_MAP_REJECT_AFFINE,
    PPU_GPU3DS_MAP_REJECT_CONTROL,
    PPU_GPU3DS_MAP_REJECT_SCREEN_SPACE,
    PPU_GPU3DS_MAP_REJECT_DISABLED,
    PPU_GPU3DS_MAP_REJECT_TOO_LARGE,
    PPU_GPU3DS_MAP_REJECT_COVERAGE,
    PPU_GPU3DS_MAP_REJECT_COUNT
} PpuGpu3DSMapReject;

typedef struct PpuGpu3DSCommandBuffer {
    PpuGpu3DSVertex* vertices;
    uint16_t* indices;
    PpuGpu3DSBatch* batches;
    size_t vertexCount, vertexCapacity;
    size_t indexCount, indexCapacity;
    size_t batchCount, batchCapacity;
    /* Set when a write was dropped for want of room, so the single build pass
     * can run to completion and be discarded as a whole. */
    bool overflow;
    uint8_t failReason;
    /* Which backgrounds shared one map-space copy of their tiles, and why the
     * others could not. */
    uint8_t mapLayerMask;
    /* Layers whose slice was rewritten this frame, and how much of each slice
     * is live, so only what changed is flushed to the GPU. */
    uint8_t mapDirtyMask;
    uint32_t mapSliceVertices[4];
    uint32_t dynamicFirstVertex, dynamicFirstIndex;

    uint32_t mapLargestQuads;
    uint32_t mapReject[PPU_GPU3DS_MAP_REJECT_COUNT];
    uint16_t bandCount;
    /* What the frame asked for, which on overflow exceeds the capacities. */
    uint32_t requiredVertices, requiredBatches;
} PpuGpu3DSCommandBuffer;

/* Off makes every background walk the tilemap per band, for diffing. */
typedef enum PpuGpu3DSPhase {
    PPU_GPU3DS_PHASE_BANDS,
    PPU_GPU3DS_PHASE_MERGE,
    PPU_GPU3DS_PHASE_MAPS,
    /* Inside MAPS: the tilemap digest that decides whether a layer can keep
     * last frame's geometry. Walking the map is meant to be far cheaper than
     * re-emitting the quads, so this being the bulk of MAPS would mean the
     * reuse test costs more than the work it avoids. */
    PPU_GPU3DS_PHASE_MAPSIG,
    /* Inside MAPS: the tile-pixel snapshot compare that confirms a retained
     * layer's atlas contents are still current. This reads the layer's whole
     * char range out of VRAM and again out of the snapshot every frame, on the
     * path that is supposed to be the cheap one. */
    PPU_GPU3DS_PHASE_MAPRETAIN,
    PPU_GPU3DS_PHASE_SCENE,
    PPU_GPU3DS_PHASE_OBJWIN,
    PPU_GPU3DS_PHASE_REGIONS,
    PPU_GPU3DS_PHASE_BG,
    PPU_GPU3DS_PHASE_OBJ,
    PPU_GPU3DS_PHASE_COUNT
} PpuGpu3DSPhase;
#ifdef PPU_GPU3DS_PROFILE
extern double gPpuGpu3DSPhase[PPU_GPU3DS_PHASE_COUNT];
#endif

void PpuGpu3DS_SetMapSpaceEnabled(bool enabled);
void PpuGpu3DS_CacheInit(PpuGpu3DSCache* cache);
void PpuGpu3DS_CacheClearDirty(PpuGpu3DSCache* cache);
void PpuGpu3DS_CacheBeginFrame(PpuGpu3DSCache* cache, const uint16_t* bgPalette,
                               const uint16_t* objPalette, uint32_t frame);
bool PpuGpu3DS_CacheTile(PpuGpu3DSCache* cache, const uint8_t* vram, PpuGpu3DSTileKey key,
                         uint16_t* atlas, uint16_t* outSlot);
uint8_t PpuGpu3DS_MortonIndex(unsigned x, unsigned y);
uint16_t PpuGpu3DS_PackRgba5551(uint16_t gbaColor, bool opaque);
uint16_t PpuGpu3DS_PackAbgr8888(uint32_t abgr);
int32_t PpuGpu3DS_AffineSample(int32_t reference, int16_t coefficient,
                               int screenCoordinate);
int PpuGpu3DS_RemapBgX(const PpuGpu3DSFrameView* frame, unsigned bg,
                       unsigned line, int nativeX);
bool PpuGpu3DS_ShadowEntry(const PpuGpu3DSFrameView* frame, unsigned bg,
                           unsigned row, unsigned column, uint16_t* entry);
size_t PpuGpu3DS_BuildBands(const PpuGpu3DSFrameView* frame, PpuGpu3DSBand out[160]);
size_t PpuGpu3DS_WindowIntervals(unsigned left, unsigned right, unsigned width,
                                 PpuGpu3DSInterval out[2]);
void PpuGpu3DS_FillStaticIndices(uint16_t* indices, size_t capacity);
void PpuGpu3DS_CommandInit(PpuGpu3DSCommandBuffer* cmd, PpuGpu3DSVertex* vertices,
                           size_t vertexCapacity, uint16_t* indices, size_t indexCapacity,
                           PpuGpu3DSBatch* batches, size_t batchCapacity);
bool PpuGpu3DS_CommandReserve(PpuGpu3DSCommandBuffer* cmd, size_t vertices, size_t indices,
                              size_t batches);
bool PpuGpu3DS_BuildCommands(const PpuGpu3DSFrameView* frame, PpuGpu3DSCache* cache,
                             uint16_t* atlas, PpuGpu3DSCommandBuffer* cmd);
