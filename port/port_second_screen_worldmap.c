#include "port_second_screen_worldmap.h"

#include "port_asset_loader.h" /* Port_IsRoomHeaderPtrReadable */
#include "port_rom.h"          /* gRomData/gRomSize, Port_ReadU16/U32 */
#include "area.h"              /* AreaHeader/gAreaMetadata, RoomHeader/gAreaRoomHeaders, Transition */
#include "kinstone.h"          /* KinstoneWorldEvent/WorldEvent tables + GetWorldEvents() */
#include "game.h"              /* OverworldLocation/gOverworldLocations — the map screen's zoom grid */
#include "subtask.h"           /* GetOverworldLocation, gUnk_08128E94 (screen 6's per-region geometry) */
#include "region.h"            /* REGION_IS_EU/JP — ROM-derived, not save state */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hyrule world-map image for the second screen, decoded at runtime from the
 * same ROM data the game's own map screen loads — never shipped as baked
 * pixels, same policy as port_second_screen_render.c's item icons.
 *
 * What "the game's own map screen" concretely is: subtaskMapHint.c and the
 * pause menu's Map tab both run pause-menu screen 4, whose row in
 * gUnk_08128A38 selects gUnk_08128AD8[2] = { paletteGroup 185, gfxGroup 94,
 * dispcnt bits, bg1Control 0x1C03, bg2Control 0x1D0B } (src/data/
 * figurineMenuData.c, applied by sub_080A4DB8 in src/menu/figurineMenu.c).
 * Reading those control words plus the setup path gives the full recipe:
 *
 *   BG1 (map artwork):  4bpp tiles at charBase 0 (0x06000000, gfx group 94's
 *                       16 KB entry) + the 32x20 tilemap group 94 copies into
 *                       gBG1Buffer (0x02021F30). BGVOFS -4 (sub_080A6290).
 *   BG2 (frame/chrome): tiles at charBase 2 (0x06008000, from gfx group 86 —
 *                       sub_080A4D34's LoadGfxGroup(0x56 + health state); the
 *                       three variants differ only in the BG3 map below, we
 *                       use the full-health group) + group 94's gBG2Buffer
 *                       tilemap (0x020344B0). BGVOFS -4.
 *   BG3 (backdrop):     same charBase 2 tiles + the tilemap group 86 copies
 *                       into gBG3Buffer (0x02001A40). No scroll.
 *   Palettes:           the game reaches this screen through LoadGfxGroups()
 *                       (palette groups 11, 12), sub_080A4D34 (group 0xB5 =
 *                       181) and sub_080A4DB8 (group 185); later loads win,
 *                       exactly like the shared gPaletteBuffer. Backdrop
 *                       color = entry 81, per Subtask_MapHint_0's
 *                       SetColor(0, gPaletteBuffer[81]).
 *
 * Layers are composed back-to-front (BG3, BG2, BG1 — all priority 3, lower
 * BG number wins on the GBA) into one 240x160 RGBA8888 replica of the map
 * screen, so the game's own marker math lands on this image with no extra
 * transform.
 *
 * Threading: built lazily, ONCE, by whoever calls GetImage first — by design
 * that is only the second-screen render thread. The image is written into a
 * private static buffer and only then published through a single aligned
 * pointer-sized store (sPublishedImage); it is immutable afterwards. Today
 * builder and consumer are the same thread, so no cross-thread ordering is
 * exercised at all; if another reader thread is ever added it observes
 * either NULL or a pointer to a buffer whose writes precede the store in
 * program order — add a release/acquire pair then. ROM readiness is probed
 * per call and every failure path returns "not ready" instead of crashing,
 * so calling early (before Port_LoadRom resolved gGfxGroups/gPaletteGroups)
 * just keeps the caller on its schematic fallback for another frame.
 *
 * This module never reads live engine state (gSave/gArea/gRoomControls/
 * gPaletteBuffer/VRAM) — only ROM-constant data via the port's resolved
 * tables; anything save- or room-dependent arrives as caller parameters.
 */

/* Resolved ROM pointer tables (port/port_rom.c). Declared with the defining
 * types: entries point at raw little-endian GfxItem (12-byte) / PaletteGroup
 * (4-byte) records inside gRomData. */
extern const void* gGfxGroups[];
extern const void* gPaletteGroups[];
extern const u8* gGlobalGfxAndPalettes;

/* Windcrest warp targets — the compiled table the fast-travel screen indexes
 * by windcrest bit (src/menu/kinstoneMenu.c; used by sub_080A6EE0 in
 * src/subtask/subtaskFastTravel.c). */
extern const Transition gUnk_08128024[];

/* port/port_bios.c — BIOS LZ77 into a caller-owned buffer (the engine's
 * LZ77UnCompWram/Vram always resolve their destination into live GBA
 * regions, which an off-screen decode must not touch). */
extern void Port_LZ77DecompressToBuffer(const void* src, void* dst, size_t dstCap);

/* Raw-data accessors (src/common.c, port/port_draw.c) — the same read-only
 * faces port_second_screen_dungeonmap.c redraws its markers through. */
extern const u8* Port_GetRawGfxSpanForVram(u32 group, u32 vramAddr, u32 numBytes);
extern const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors);
extern const u8* Port_GetDirectSpriteFrame(u32 spriteIndex, u32 frameIndex, u32* outMaxPieces);
extern const u8* Port_GetSpriteSizeTable(void);

#define WORLDMAP_IMAGE_W 240 /* GBA LCD; menu screens always render the native canvas */
#define WORLDMAP_IMAGE_H 160

/* gfx groups (see file comment for the derivation) */
#define WORLDMAP_GFX_GROUP 94u /* map tiles + BG1/BG2 tilemaps (pause screen 4) */
#define CHROME_GFX_GROUP 86u   /* menu chrome tiles + BG3 tilemap (full-health variant) */

/* GfxItem destinations that identify the entries we need within those
 * groups. VRAM addresses are fixed by the BG control words on every region;
 * the EWRAM ones are the USA link addresses of the tilemap staging buffers
 * (include/vram.h) — EU/JP retail links may place those buffers elsewhere,
 * so EWRAM entries are ALSO matched positionally (nth EWRAM-destined record
 * of the group) as a region-agnostic fallback. */
#define DEST_MAP_TILES 0x06000000u    /* BG charBase 0 (bg1Control 0x1C03) */
#define DEST_CHROME_TILES 0x06008000u /* BG charBase 2 (bg2Control 0x1D0B, bg3 0x1E0B) */
#define DEST_BG1_TILEMAP 0x02021F30u  /* gBG1Buffer — group 94's 1st EWRAM entry */
#define DEST_BG2_TILEMAP 0x020344B0u  /* gBG2Buffer — group 94's 2nd EWRAM entry */
#define DEST_BG3_TILEMAP 0x02001A40u  /* gBG3Buffer — group 86's 1st EWRAM entry */

/* Palette groups applied on the way into the map screen, in load order. */
static const u8 sWorldMapPaletteGroups[] = { 11, 12, 181, 185 };

/* sub_080A6290 sets bg1/bg2 BGVOFS to -4: the artwork sits 4 px lower on
 * screen than in its tilemap. BG3 is not scrolled. */
#define MAP_LAYER_YSHIFT 4

#define GFX_GROUP_WALK_MAX 32 /* sanity cap, real groups have <8 records */
#define PALETTE_GROUP_WALK_MAX 16
#define LZ77_DECODED_CAP 0x20000u /* 128 KB — far above any BG tile/map blob */

static uint32_t sImagePixels[WORLDMAP_IMAGE_W * WORLDMAP_IMAGE_H];
static const uint32_t* volatile sPublishedImage = NULL;

typedef struct {
    int32_t x, y;
} WorldMapPin;
static WorldMapPin sWindcrestPins[8];
static int sWindcrestPinsReady = 0;

/* One fetched gfx-group entry: `data` points either into gRomData or at an
 * owned decompression buffer (owned != NULL, free after composing). */
typedef struct {
    const u8* data;
    u32 len;
    u8* owned;
} GfxBlob;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static int RangeInRom(const u8* p, u32 len) {
    if (gRomData == NULL || p == NULL) {
        return 0;
    }
    return p >= gRomData && len <= gRomSize && p + len <= gRomData + gRomSize;
}

/* Hand back one gfx-group record's payload. Mirrors LoadGfxGroup's walk
 * (src/common.c:513): 12-byte records { u32 offset|ctrl<<24, u32 dest,
 * s32 size }, chained while bit 31 of the first word is set; ctrl 0xD ends
 * the group, ctrl 0x7 means always-load, other values are language-
 * conditional. Records are selected by exact dest address, or — when
 * ewramOrdinal >= 0 — as the nth record targeting EWRAM (0x02xxxxxx),
 * whatever its exact address; wantDest is ignored then. size < 0 marks BIOS
 * LZ77 data (decoded into an owned buffer); size >= 0 is returned as a
 * direct pointer into ROM. Returns 0 when not found/ready. */
static int FetchGfxGroupEntry(u32 group, u32 wantDest, int ewramOrdinal, int allowAnyCtrl, GfxBlob* out) {
    const u8* rec;
    int i;
    int ewramSeen = 0;

    out->data = NULL;
    out->len = 0;
    out->owned = NULL;

    if (group >= 133u) { /* GFX_GROUPS_COUNT_MAX (port_rom.c) */
        return 0;
    }
    rec = (const u8*)gGfxGroups[group];
    if (rec == NULL) {
        return 0;
    }

    for (i = 0; i < GFX_GROUP_WALK_MAX; i++, rec += 12) {
        u32 raw0, ctrl, dest;
        int match;
        if (!RangeInRom(rec, 12)) {
            return 0;
        }
        raw0 = Port_ReadU32(rec);
        ctrl = (raw0 >> 24) & 0xF;
        if (ctrl == 0xD) {
            return 0; /* explicit end-of-group record */
        }
        if (ctrl == 0x7 || allowAnyCtrl) {
            dest = Port_ReadU32(rec + 4);
            if (ewramOrdinal >= 0) {
                match = (dest >> 24) == 0x02u && ewramSeen++ == ewramOrdinal;
            } else {
                match = dest == wantDest;
            }
            if (match) {
                const u8* src = gGlobalGfxAndPalettes + (raw0 & 0xFFFFFF);
                s32 size = (s32)Port_ReadU32(rec + 8);
                if (size >= 0) {
                    if (!RangeInRom(src, (u32)size)) {
                        return 0;
                    }
                    out->data = src;
                    out->len = (u32)size;
                    return 1;
                }
                /* LZ77: real payload length lives in the stream's own header
                 * (byte 0 = type 0x10, bytes 1-3 = decompressed size). */
                {
                    u32 header, decodedLen;
                    if (!RangeInRom(src, 4)) {
                        return 0;
                    }
                    header = Port_ReadU32(src);
                    decodedLen = header >> 8;
                    if ((header & 0xF0u) != 0x10u || decodedLen == 0 || decodedLen > LZ77_DECODED_CAP) {
                        return 0;
                    }
                    out->owned = (u8*)calloc(1, decodedLen);
                    if (out->owned == NULL) {
                        return 0;
                    }
                    Port_LZ77DecompressToBuffer(src, out->owned, decodedLen);
                    out->data = out->owned;
                    out->len = decodedLen;
                    return 1;
                }
            }
        }
        if (((raw0 >> 24) & 0x80) == 0) {
            return 0; /* last record, dest never matched */
        }
    }
    return 0;
}

/* Selection ladder per blob: unconditional (ctrl 7) record at the exact USA
 * dest first; then the first language variant at that dest (this module
 * can't read the live save language, and the map-screen groups are ctrl 7
 * on every known region anyway); then — for the EWRAM tilemap copies whose
 * absolute staging address is a per-region link detail — the nth
 * EWRAM-destined record of the group. */
static int FetchGfxGroupEntryRobust(u32 group, u32 wantDest, int ewramOrdinal, GfxBlob* out) {
    if (FetchGfxGroupEntry(group, wantDest, -1, 0, out) || FetchGfxGroupEntry(group, wantDest, -1, 1, out)) {
        return 1;
    }
    if (ewramOrdinal >= 0) {
        return FetchGfxGroupEntry(group, 0, ewramOrdinal, 0, out) ||
               FetchGfxGroupEntry(group, 0, ewramOrdinal, 1, out);
    }
    return 0;
}

static void FreeGfxBlob(GfxBlob* blob) {
    free(blob->owned);
    blob->owned = NULL;
    blob->data = NULL;
    blob->len = 0;
}

/* Apply one palette group to a private BG palette (banks 0-15, 16 colors
 * each, RGB555). Mirrors LoadPaletteGroup's chain walk (src/common.c:361):
 * 4-byte records { u16 paletteId, u8 destBank, u8 count|more<<7 }, count 0
 * meaning 16; palette data at gGlobalGfxAndPalettes[paletteId * 32]. OBJ
 * banks (destBank >= 16) are skipped — this composite has no sprites. */
static void ApplyPaletteGroup(u32 group, uint16_t* bgPal) {
    const u8* rec;
    int i;

    if (group >= 208u) { /* PALETTE_GROUPS_COUNT_MAX (port_rom.c) */
        return;
    }
    rec = (const u8*)gPaletteGroups[group];
    if (rec == NULL) {
        return;
    }

    for (i = 0; i < PALETTE_GROUP_WALK_MAX; i++, rec += 4) {
        u32 pg, paletteId, destBank, numPalettes, p;
        if (!RangeInRom(rec, 4)) {
            return;
        }
        pg = Port_ReadU32(rec);
        paletteId = pg & 0xFFFF;
        destBank = (pg >> 16) & 0xFF;
        numPalettes = (pg >> 24) & 0xF;
        if (numPalettes == 0) {
            numPalettes = 16;
        }
        for (p = 0; p < numPalettes; p++) {
            const u8* src = gGlobalGfxAndPalettes + (paletteId + p) * 32u;
            u32 bank = destBank + p;
            u32 c;
            if (bank >= 16 || !RangeInRom(src, 32)) {
                continue;
            }
            for (c = 0; c < 16; c++) {
                bgPal[bank * 16u + c] = Port_ReadU16(src + c * 2u);
            }
        }
        if (((pg >> 24) & 0x80) == 0) {
            return;
        }
    }
}

/* Paint one text-mode BG layer over the image. yShift: screen row r shows
 * tilemap pixel row (r - yShift) & 255 — the GBA adds BGVOFS, and the menu
 * parks it at -4 for BG1/BG2. Tilemap entries are the standard text-BG
 * format (tile 0-9, hflip 10, vflip 11, palette 12-15) over a 32x32 map;
 * rows past the 32x20 staging copy read as entry 0, exactly what the
 * MemClear'd gBGxBuffer holds on console. Color 0 is transparent, tile ids
 * beyond the loaded tile blob are skipped (console would show stale VRAM
 * there; real map tilemaps never reference unloaded tiles). */
static void DrawBgLayerSized(uint32_t* dst, int32_t dstW, int32_t dstH, const u8* tilemap, u32 tilemapLen,
                             const u8* tiles, u32 tilesLen, const uint16_t* bgPal, int32_t yShift) {
    u32 tileCount = tilesLen / 32u;
    int32_t x, y;

    for (y = 0; y < dstH; y++) {
        int32_t mapY = (y - yShift) & 0xFF;
        u32 tileRow = ((u32)mapY >> 3) & 31u;
        int32_t rowInTile = mapY & 7;
        for (x = 0; x < dstW; x++) {
            u32 tileCol = ((u32)x >> 3) & 31u;
            u32 entryOff = (tileRow * 32u + tileCol) * 2u;
            u16 entry = (entryOff + 2u <= tilemapLen) ? Port_ReadU16(tilemap + entryOff) : 0;
            u32 tileId = entry & 0x3FFu;
            int32_t inX = x & 7;
            int32_t inY = rowInTile;
            u8 packed, colorIndex;

            if (tileId >= tileCount) {
                continue;
            }
            if (entry & 0x400u) {
                inX = 7 - inX;
            }
            if (entry & 0x800u) {
                inY = 7 - inY;
            }
            packed = tiles[tileId * 32u + (u32)inY * 4u + ((u32)inX >> 1)];
            colorIndex = (inX & 1) ? (u8)(packed >> 4) : (u8)(packed & 0xFu);
            if (colorIndex == 0) {
                continue;
            }
            dst[(size_t)y * (size_t)dstW + (size_t)x] =
                Rgb555ToRgba8888(bgPal[(((u32)entry >> 12) & 0xFu) * 16u + colorIndex]);
        }
    }
}

static void DrawBgLayer(uint32_t* dst, const u8* tilemap, u32 tilemapLen, const u8* tiles, u32 tilesLen,
                        const uint16_t* bgPal, int32_t yShift) {
    DrawBgLayerSized(dst, WORLDMAP_IMAGE_W, WORLDMAP_IMAGE_H, tilemap, tilemapLen, tiles, tilesLen, bgPal,
                     yShift);
}

static int BuildWorldMapImage(void) {
    uint16_t bgPal[16 * 16];
    GfxBlob mapTiles = { 0 }, chromeTiles = { 0 }, bg1Map = { 0 }, bg2Map = { 0 }, bg3Map = { 0 };
    uint32_t backdrop;
    size_t i;
    int ok = 0;

    /* ROM tables come up on the game thread during Port_LoadRom; until then
     * (or during a mid-session ROM reload) entries read as NULL and we
     * simply report not-ready. */
    if (gRomData == NULL || gRomSize == 0 || gGlobalGfxAndPalettes == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
        if (gPaletteGroups[sWorldMapPaletteGroups[i]] == NULL) {
            return 0;
        }
    }

    if (FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_MAP_TILES, -1, &mapTiles) &&
        FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_BG1_TILEMAP, 0, &bg1Map) &&
        FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_BG2_TILEMAP, 1, &bg2Map) &&
        FetchGfxGroupEntryRobust(CHROME_GFX_GROUP, DEST_CHROME_TILES, -1, &chromeTiles) &&
        FetchGfxGroupEntryRobust(CHROME_GFX_GROUP, DEST_BG3_TILEMAP, 0, &bg3Map)) {

        memset(bgPal, 0, sizeof(bgPal));
        for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
            ApplyPaletteGroup(sWorldMapPaletteGroups[i], bgPal);
        }

        /* Subtask_MapHint_0: SetColor(0, gPaletteBuffer[81]) — the screen's
         * backdrop is bank 5 color 1 of the freshly loaded group 185. */
        backdrop = Rgb555ToRgba8888(bgPal[81]);
        for (i = 0; i < (size_t)WORLDMAP_IMAGE_W * WORLDMAP_IMAGE_H; i++) {
            sImagePixels[i] = backdrop;
        }

        DrawBgLayer(sImagePixels, bg3Map.data, bg3Map.len, chromeTiles.data, chromeTiles.len, bgPal, 0);
        DrawBgLayer(sImagePixels, bg2Map.data, bg2Map.len, chromeTiles.data, chromeTiles.len, bgPal,
                    MAP_LAYER_YSHIFT);
        DrawBgLayer(sImagePixels, bg1Map.data, bg1Map.len, mapTiles.data, mapTiles.len, bgPal,
                    MAP_LAYER_YSHIFT);

        sPublishedImage = sImagePixels; /* publish only the finished image */
        ok = 1;
    }

    FreeGfxBlob(&mapTiles);
    FreeGfxBlob(&chromeTiles);
    FreeGfxBlob(&bg1Map);
    FreeGfxBlob(&bg2Map);
    FreeGfxBlob(&bg3Map);
    return ok;
}

const uint32_t* Port_SecondScreenWorldMap_GetImage(int32_t* outW, int32_t* outH) {
    const uint32_t* image = sPublishedImage;

    if (image == NULL && BuildWorldMapImage()) {
        image = sPublishedImage;
    }
    if (outW) {
        *outW = image ? WORLDMAP_IMAGE_W : 0;
    }
    if (outH) {
        *outH = image ? WORLDMAP_IMAGE_H : 0;
    }
    return image;
}

/* The game's own world→map-screen scaling, ported verbatim per marker type
 * (the two screens use slightly different Y math and we keep that):
 *   player dot, pause map (src/menu/pauseMenu.c sub_080A6378):
 *       x = worldX * 160 / 0xF90 + 0x28;   y = worldY * 128 / 0xC60 + 0xC
 *   windcrest flags, fast travel (src/subtask/subtaskFastTravel.c
 *   sub_080A6EE0):
 *       x = worldX * 160 / 0xF90 + 0x28;   y = worldY * 160 / 0xF90 + 0xC
 * 0xF90/0xC60 are the overworld's pixel extents (the ~249x198 16px-tile
 * grid of gOverworldLocations), 0x28/0xC the artwork margin on the 240x160
 * screen. Both screens draw over the same BG1 map (gfx groups 93 and 94
 * share their map-tile and tilemap entries), so both land on our image. */
static int32_t WorldToMapX(int32_t worldX) {
    return worldX * 160 / 0xF90 + 0x28;
}

static int32_t WorldToMapY(int32_t worldY) {
    return worldY * 128 / 0xC60 + 0xC;
}

static int32_t ClampMapX(int32_t x) {
    return x < 0 ? 0 : (x >= WORLDMAP_IMAGE_W ? WORLDMAP_IMAGE_W - 1 : x);
}

static int32_t ClampMapY(int32_t y) {
    return y < 0 ? 0 : (y >= WORLDMAP_IMAGE_H ? WORLDMAP_IMAGE_H - 1 : y);
}

int Port_SecondScreenWorldMap_LocatePlayer(uint8_t area, int32_t areaX, int32_t areaY, int32_t* outMapX,
                                           int32_t* outMapY) {
    /* Overworld test = CheckAreaOverworld (src/gameUtils.c): the compiled
     * gAreaMetadata row (src/data/areaMetadata.c, 153 rows) must carry
     * exactly AR_IS_OVERWORLD|AR_ALLOWS_WARP. Interiors/dungeons fail here
     * and the caller keeps its last outdoor fix — the same reason the
     * game's own marker freezes at the doorway (UpdatePlayerMapCoords only
     * stores overworld_map_x/y while AreaIsOverworld()). On the overworld,
     * entity/area coordinates ARE overworld-map coordinates: that function
     * copies gPlayerEntity.base.x.HALF_U.HI straight into overworld_map_x,
     * no per-area offset. */
    if (area >= 153 || gAreaMetadata[area].flags != (AR_IS_OVERWORLD | AR_ALLOWS_WARP)) {
        return 0;
    }
    if (outMapX) {
        *outMapX = ClampMapX(WorldToMapX(areaX));
    }
    if (outMapY) {
        *outMapY = ClampMapY(WorldToMapY(areaY));
    }
    return 1;
}

/* Windcrest pin positions are ROM constants: gUnk_08128024[bit] gives the
 * warp's area/room/room-local landing point, the area's RoomHeader adds the
 * room's world origin (sub_080A6EE0's exact recipe). Computed once, lazily,
 * because gAreaRoomHeaders is resolved from the loaded ROM — until it's
 * readable we report not-ready and the caller just retries. */
static int EnsureWindcrestPins(void) {
    int i;

    if (sWindcrestPinsReady) {
        return 1;
    }
    if (gRomData == NULL || gRomSize == 0) {
        return 0;
    }

    for (i = 0; i < 8; i++) {
        const Transition* warp = &gUnk_08128024[i];
        const RoomHeader* table;
        int32_t worldX, worldY;

        if (warp->area >= 0x90 || warp->room >= MAX_ROOMS) { /* AREA_COUNT (port_rom.c) */
            return 0;
        }
        table = gAreaRoomHeaders[warp->area];
        if (!Port_IsRoomHeaderPtrReadable(table)) {
            return 0;
        }
        worldX = (int32_t)warp->endX + table[warp->room].map_x;
        worldY = (int32_t)warp->endY + table[warp->room].map_y;
        sWindcrestPins[i].x = ClampMapX(WorldToMapX(worldX));
        sWindcrestPins[i].y = ClampMapY(worldY * 160 / 0xF90 + 0xC);
    }
    sWindcrestPinsReady = 1;
    return 1;
}

/* ReadBit (src/common.c): LSB-first within bytes — the exact indexing
 * CheckKinstoneFused/CheckFusionMapMarkerDisabled apply to these arrays. */
static u32 SaveBit(const uint8_t* arr, u32 bit) {
    return (arr[bit >> 3] >> (bit & 7u)) & 1u;
}

/* gUnk_08128F58 is a frameIndex-0-terminated list (9 rows today); the walk is
 * capped anyway, and at 16 the cap can never cut a row a u16 map_hints could
 * still select. */
#define MAP_HINT_ROW_MAX 16

int32_t Port_SecondScreenWorldMap_GetMapHints(uint32_t hintMask, SecondScreenMapMarker* out,
                                              int32_t maxMarkers) {
    /* The world map's own marker pass, ported: sub_080A6438 (src/menu/
     * pauseMenu.c) walks gUnk_08128F58 and stamps row i when bit i of
     * `gSave.map_hints & gGenericMenu.unk10.h[0]` is set — that second word
     * being sub_080A6F40()'s, cached when the screen opened. Positions are
     * the row's OWN pre-baked screen coordinates (unk1, unk2), not a
     * transform of its world coordinates, so nothing here has to agree with
     * WorldToMapX/Y — though it does, to within a pixel, which is what pins
     * that transform down. The row's frameIndex picks the glyph: the red
     * check 0x5B for most rows, 0x60..0x63 for the four that name a specific
     * errand.
     *
     * These, and not kinstone fusions, are what the world map shows. The
     * fusion pass belongs to pause screen 6 — see GetRegionMarkers.
     *
     * The mask is the caller's business precisely because deciding it needs
     * live save state (gSave.map_hints plus sub_080A6F40's local-flag and
     * inventory reads); the game thread publishes it and this module stays
     * ROM-only. The screen positions are already in the composite's own
     * pixel space, since the composite replicates the whole 240x160 screen
     * including the BGVOFS the artwork sits at. */
    const struct_gUnk_08128F58* row;
    int32_t count = 0;
    u32 i;

    if (out == NULL || maxMarkers <= 0 || hintMask == 0) {
        return 0;
    }
    if (gRomData == NULL || gRomSize == 0) {
        return 0; /* markers annotate the ROM-decoded map; report not-ready with it */
    }
    for (row = gUnk_08128F58, i = 0;
         i < MAP_HINT_ROW_MAX && row->frameIndex != 0 && count < maxMarkers; i++, row++) {
        if (((hintMask >> i) & 1u) == 0) {
            continue;
        }
        out[count].x = ClampMapX(row->unk1);
        out[count].y = ClampMapY(row->unk2);
        out[count].frame = row->frameIndex;
        count++;
    }
    return count;
}

int Port_SecondScreenWorldMap_GetWindcrestPin(int32_t windcrestId, int32_t* outMapX, int32_t* outMapY) {
    /* windcrestId is the bit index within gSave.windcrests' upper byte:
     * 0 = WINDCREST_MT_CRENEL (bit 24) ... 7 = WINDCREST_MINISH_WOODS
     * (bit 31), the same index the fast-travel cursor uses. */
    if (windcrestId < 0 || windcrestId >= 8 || !EnsureWindcrestPins()) {
        return 0;
    }
    if (outMapX) {
        *outMapX = sWindcrestPins[windcrestId].x;
    }
    if (outMapY) {
        *outMapY = sWindcrestPins[windcrestId].y;
    }
    return 1;
}

/* --- Regional zoom ------------------------------------------------------
 *
 * The map screen's zoom grid is gOverworldLocations (src/data/areaMetadata.c):
 * 17 rects over the overworld's 16px tile grid, one per WindcrestID, and the
 * id the whole map screen family indexes by (gMenu.field_0x3). The world map
 * draws its cursor brackets on them — frame gMenu.field_0x3 * 3 + 0x26 at
 * gUnk_08128DE8[id].unk6/unk7 — and pressing A opens pause screen 6, the
 * enlarged map of that one region.
 *
 * Screen 6's artwork recipe comes out of sub_080A67C4 (src/menu/
 * pauseMenuScreen6.c) plus the screen's row in gUnk_08128AD8 (screen 6 ->
 * { paletteGroup 185, gfxGroup 129, dispcnt, bg1Control 0x1C0A, bg2Control
 * 0x1D03 }, src/data/figurineMenuData.c):
 *
 *   LoadPaletteGroup(region + 0xBA)  -> palette group 186 + region, applied
 *                                       after the 11 / 12 / 181 / 185 ladder
 *                                       the pause menu already ran.
 *   LoadGfxGroup(region + 0x5F)      -> gfx group 95 + region: 32 KB of 4bpp
 *                                       tiles to 0x06000000 (bg2Control
 *                                       0x1D03 = charBase 0) plus a 32x32
 *                                       tilemap into gBG2Buffer. BG1 is the
 *                                       menu chrome here, not the map, so
 *                                       only BG2 is decoded.
 *
 * The map layer is bigger than the screen and screen 6 scrolls it (gMenu
 * .field_0xa, capped by gUnk_08128E94[region].unk2), so the artwork's own
 * rect inside that 32x32 tilemap has to be recovered rather than screenshot:
 *
 *   origin: sub_080A66D0 places a marker at screen x = localX + unk7 and
 *           y = localY + unk3 - scroll, so the artwork's own (0, 0) sits at
 *           tilemap (unk7, unk3).
 *   size:   sub_080A69E0 scales region-local world pixels by 100 / 0x23A,
 *           so the artwork is (worldSpan * 100 / 0x23A) on each axis — the
 *           span being the region rect minus that function's own origin
 *           fixups (regions 4 and 7 start at area 9 / area 7 room 0's map_y
 *           instead of the rect top; region 15 starts 0x108 right of the
 *           rect edge, where row 12's overlapping reach ends).
 *
 * Decoding all 17 tilemaps and measuring where the artwork actually sits
 * agrees: nine regions land on the rect to the pixel and the rest sit within
 * a few pixels of edge decoration, including regions 4, 7 and 15, whose
 * sizes only come out right with those fixups applied. The scroll caps in
 * gUnk_08128E94 corroborate the scale independently (region 0: 210 px tall,
 * 160 visible, unk2 = 88 of scroll; region 7: 92 px, unk2 = 0 — no scroll).
 *
 * Regions whose gfx group has save-dependent variants (sub_080A67C4's
 * switch: region 9 after TATEKAKE_HOUSE, region 11 after KINSTONE_E, region
 * 14 while inside area 8) always decode the base variant — the alternates
 * need live flags, and the difference is a few tiles of scenery. */

#define WORLDMAP_REGION_COUNT 17       /* gOverworldLocations, one row per WindcrestID */
#define REGION_GFX_GROUP_BASE 95u      /* sub_080A67C4: LoadGfxGroup(region + 0x5F) */
#define REGION_PALETTE_GROUP_BASE 186u /* sub_080A67C4: LoadPaletteGroup(region + 0xBA) */
#define REGION_SCALE_NUM 100           /* sub_080A69E0's world -> enlarged-map scale */
#define REGION_SCALE_DEN 0x23A
#define REGION_CANVAS_W 256             /* the 32x32 text-BG tilemap group 95+ fills */
#define REGION_CANVAS_H 256
#define DEST_REGION_TILEMAP 0x020344B0u /* gBG2Buffer — the group's 2nd EWRAM entry */

static const uint32_t* volatile sRegionImage[WORLDMAP_REGION_COUNT];
static int32_t sRegionW[WORLDMAP_REGION_COUNT], sRegionH[WORLDMAP_REGION_COUNT];

/* The gOverworldLocations row for a WindcrestID, or NULL. Rows are keyed by
 * windcrestId (and today listed in that order); looked up by key so the two
 * never have to agree. */
static const OverworldLocation* RegionLocation(int32_t region) {
    const OverworldLocation* loc;

    if (region < 0 || region >= WORLDMAP_REGION_COUNT) {
        return NULL;
    }
    for (loc = gOverworldLocations; loc->minX != 0xFF; loc++) {
        if (loc->windcrestId == (u8)region) {
            return loc;
        }
    }
    return NULL;
}

/* Everything the enlarged map of one region needs, all of it sub_080A69E0's
 * and sub_080A66D0's own arithmetic: the overworld pixel the artwork's
 * (0, 0) stands for (including that function's per-region origin fixups),
 * the artwork's size at its 100 / 0x23A scale, and where it sits inside the
 * region's 256x256 tilemap. Returns 0 when the region id is unknown or the
 * fixups' room headers aren't readable yet. */
typedef struct {
    int32_t worldX, worldY; /* overworld pixel at artwork (0, 0) */
    int32_t artX, artY;     /* artwork origin within the 32x32 tilemap */
    int32_t artW, artH;     /* artwork size in enlarged-map pixels */
} RegionGeometry;

static int GetRegionGeometry(int32_t region, RegionGeometry* out) {
    const OverworldLocation* loc = RegionLocation(region);
    const struct_gUnk_08128E94* geom;

    if (loc == NULL) {
        return 0;
    }
    geom = &gUnk_08128E94[region];
    out->worldX = (int32_t)loc->minX * 0x10;
    out->worldY = (int32_t)loc->minY * 0x10;
    if (region == 4 || region == 7) {
        const RoomHeader* table = gAreaRoomHeaders[region == 4 ? 9 : 7];
        if (!Port_IsRoomHeaderPtrReadable(table)) {
            return 0;
        }
        out->worldY += table[0].map_y;
    } else if (region == 15) {
        out->worldX += 0x108;
    }
    out->artX = geom->unk7;
    out->artY = geom->unk3;
    out->artW = (((int32_t)loc->maxX + 1) * 0x10 - out->worldX) * REGION_SCALE_NUM / REGION_SCALE_DEN;
    out->artH = (((int32_t)loc->maxY + 1) * 0x10 - out->worldY) * REGION_SCALE_NUM / REGION_SCALE_DEN;
    return out->artW > 0 && out->artH > 0;
}

int Port_SecondScreenWorldMap_GetRegionAt(int32_t mapX, int32_t mapY, int32_t* outRegion, int32_t* outX0,
                                          int32_t* outY0, int32_t* outX1, int32_t* outY1) {
    /* GetOverworldLocation's own walk (src/menu/pauseMenuScreen6.c), just
     * run in world-map pixels instead of overworld tiles: first row whose
     * rect contains the point wins, which is what makes the one overlap in
     * the table (row 12 reaches into rows 14-16's column) resolve the way
     * the game resolves it. The rect is half-open — (x1, y1) is the first
     * pixel of the next tile, so neighbouring rects share an edge and tile
     * outlines tile the map without gaps or double-drawn columns. */
    const OverworldLocation* loc;

    for (loc = gOverworldLocations; loc->minX != 0xFF; loc++) {
        int32_t x0 = WorldToMapX((int32_t)loc->minX * 16);
        int32_t x1 = WorldToMapX(((int32_t)loc->maxX + 1) * 16);
        int32_t y0 = WorldToMapY((int32_t)loc->minY * 16);
        int32_t y1 = WorldToMapY(((int32_t)loc->maxY + 1) * 16);

        if (mapX < x0 || mapX >= x1 || mapY < y0 || mapY >= y1) {
            continue;
        }
        if (outRegion) {
            *outRegion = loc->windcrestId;
        }
        if (outX0) {
            *outX0 = x0;
        }
        if (outY0) {
            *outY0 = y0;
        }
        if (outX1) {
            *outX1 = x1;
        }
        if (outY1) {
            *outY1 = y1;
        }
        return 1;
    }
    return 0;
}

/* Decode one region's enlarged map into an owned buffer and publish it.
 * Same discipline as the world map image: built by whoever asks first, the
 * pointer only stored once the pixels are complete, immutable afterwards,
 * and every not-ready path leaves the slot NULL so the caller just retries. */
static int BuildRegionImage(int32_t region) {
    RegionGeometry g;
    GfxBlob tiles = { 0 }, tilemap = { 0 };
    uint16_t bgPal[16 * 16];
    uint32_t* canvas;
    uint32_t* art = NULL;
    int32_t x, y;
    size_t i;

    if (gRomData == NULL || gRomSize == 0 || gGlobalGfxAndPalettes == NULL) {
        return 0;
    }
    if (!GetRegionGeometry(region, &g)) {
        return 0;
    }
    if (g.artX < 0 || g.artY < 0 || g.artX + g.artW > REGION_CANVAS_W || g.artY + g.artH > REGION_CANVAS_H) {
        return 0;
    }
    for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
        if (gPaletteGroups[sWorldMapPaletteGroups[i]] == NULL) {
            return 0;
        }
    }
    if (gPaletteGroups[REGION_PALETTE_GROUP_BASE + (u32)region] == NULL) {
        return 0;
    }

    if (FetchGfxGroupEntryRobust(REGION_GFX_GROUP_BASE + (u32)region, DEST_MAP_TILES, -1, &tiles) &&
        FetchGfxGroupEntryRobust(REGION_GFX_GROUP_BASE + (u32)region, DEST_REGION_TILEMAP, 1, &tilemap)) {
        /* One frame's scratch: 256 KB is far too much for the stack a render
         * thread runs on, and the finished crop is a fraction of it. */
        canvas = (uint32_t*)calloc((size_t)REGION_CANVAS_W * REGION_CANVAS_H, sizeof(uint32_t));
        if (canvas != NULL) {
            memset(bgPal, 0, sizeof(bgPal));
            for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
                ApplyPaletteGroup(sWorldMapPaletteGroups[i], bgPal);
            }
            ApplyPaletteGroup(REGION_PALETTE_GROUP_BASE + (u32)region, bgPal);

            DrawBgLayerSized(canvas, REGION_CANVAS_W, REGION_CANVAS_H, tilemap.data, tilemap.len, tiles.data,
                             tiles.len, bgPal, 0);

            art = (uint32_t*)calloc((size_t)g.artW * g.artH, sizeof(uint32_t));
            if (art != NULL) {
                for (y = 0; y < g.artH; y++) {
                    for (x = 0; x < g.artW; x++) {
                        art[(size_t)y * g.artW + x] =
                            canvas[(size_t)(g.artY + y) * REGION_CANVAS_W + (size_t)(g.artX + x)];
                    }
                }
            }
            free(canvas);
        }
    }

    FreeGfxBlob(&tiles);
    FreeGfxBlob(&tilemap);
    if (art == NULL) {
        return 0;
    }
    sRegionW[region] = g.artW;
    sRegionH[region] = g.artH;
    sRegionImage[region] = art; /* publish only the finished image */
    return 1;
}

static const uint32_t* RegionImage(int32_t region, int32_t* outW, int32_t* outH) {
    const uint32_t* image;

    if (region < 0 || region >= WORLDMAP_REGION_COUNT) {
        return NULL;
    }
    image = sRegionImage[region];
    if (image == NULL) {
        if (!BuildRegionImage(region)) {
            return NULL;
        }
        image = sRegionImage[region];
    }
    *outW = sRegionW[region];
    *outH = sRegionH[region];
    return image;
}

/* The region artwork's own pixel size, so a caller can fit its rect to the
 * art's aspect instead of stretching the art to the rect. Builds the image on
 * demand exactly like DrawRegion, so asking is never a reason for the first
 * draw to miss. */
int Port_SecondScreenWorldMap_GetRegionSize(int32_t region, int32_t* outW, int32_t* outH) {
    int32_t srcW = 0, srcH = 0;
    if (RegionImage(region, &srcW, &srcH) == NULL || srcW <= 0 || srcH <= 0) {
        return 0;
    }
    if (outW != NULL) *outW = srcW;
    if (outH != NULL) *outH = srcH;
    return 1;
}

int Port_SecondScreenWorldMap_DrawRegion(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                         int32_t region) {
    int32_t srcW = 0, srcH = 0, x, y;
    const uint32_t* art;

    if (pixels == NULL || bufW <= 0 || bufH <= 0 || stride <= 0 || dstW <= 0 || dstH <= 0) {
        return 0;
    }
    art = RegionImage(region, &srcW, &srcH);
    if (art == NULL || srcW <= 0 || srcH <= 0) {
        return 0;
    }
    for (y = 0; y < dstH; y++) {
        int32_t destY = dstY + y;
        int32_t sy = y * srcH / dstH;
        if (destY < 0 || destY >= bufH) {
            continue;
        }
        for (x = 0; x < dstW; x++) {
            int32_t destX = dstX + x;
            uint32_t c;
            if (destX < 0 || destX >= bufW) {
                continue;
            }
            /* Color 0 is the BG's transparency, and on screen 6 the backdrop
             * behind it is whatever the pause menu left in palette entry 0 —
             * not something this module can speak for. Leave those pixels to
             * the caller's own background instead of punching holes in it. */
            c = art[(size_t)sy * srcW + (size_t)(x * srcW / dstW)];
            if ((c >> 24) == 0) {
                continue;
            }
            pixels[(size_t)destY * (size_t)stride + (size_t)destX] = c;
        }
    }
    return 1;
}

/* sub_080A69E0 verbatim (src/menu/pauseMenuScreen6.c), minus the -1 returns
 * the callers express as "not in this region": an overworld pixel position
 * must resolve to a region row, that row must be the one being drawn, and
 * the result is that region's enlarged-map pixel, rescaled from the
 * artwork's own size to the destination rect. The function's per-region
 * origin fixups live in GetRegionGeometry, so marker positions and the drawn
 * artwork can never drift apart. Returns 0 when the point belongs elsewhere
 * or the geometry isn't readable yet. */
static int LocalizeInRegion(int32_t region, int32_t worldX, int32_t worldY, int32_t dstW, int32_t dstH,
                            int32_t* outX, int32_t* outY) {
    const OverworldLocation* here;
    RegionGeometry g;
    int32_t lx, ly;

    if ((worldX | worldY) == 0) {
        return 0;
    }
    here = GetOverworldLocation((u32)worldX, (u32)worldY);
    if (here == NULL || here->windcrestId != (u8)region || !GetRegionGeometry(region, &g)) {
        return 0;
    }
    lx = (worldX - g.worldX) * REGION_SCALE_NUM / REGION_SCALE_DEN;
    ly = (worldY - g.worldY) * REGION_SCALE_NUM / REGION_SCALE_DEN;
    if (lx < 0 || ly < 0) {
        return 0; /* the origin fixups can push a point off its own artwork */
    }
    lx = lx * dstW / g.artW;
    ly = ly * dstH / g.artH;
    *outX = lx >= dstW ? dstW - 1 : lx;
    *outY = ly >= dstH ? dstH - 1 : ly;
    return 1;
}

int Port_SecondScreenWorldMap_LocateInRegion(int32_t region, uint8_t area, int32_t areaX, int32_t areaY,
                                             int32_t dstW, int32_t dstH, int32_t* outX, int32_t* outY) {
    /* The player marker is just another point pushed through sub_080A69E0,
     * with the overworld test LocatePlayer makes in front of it. */
    if (outX == NULL || outY == NULL || dstW <= 0 || dstH <= 0) {
        return 0;
    }
    if (area >= 153 || gAreaMetadata[area].flags != (AR_IS_OVERWORLD | AR_ALLOWS_WARP)) {
        return 0; /* CheckAreaOverworld, as in LocatePlayer */
    }
    return LocalizeInRegion(region, areaX, areaY, dstW, dstH, outX, outY);
}


int32_t Port_SecondScreenWorldMap_GetRegionMarkers(int32_t region, uint32_t hintMask,
                                                   const uint8_t* fusedKinstones,
                                                   const uint8_t* fusionUnmarked, int32_t dstW,
                                                   int32_t dstH, SecondScreenMapMarker* out,
                                                   int32_t maxMarkers) {
    /* sub_080A68D4's marker pass, in its own order: the map hints again — in
     * their REGION frames (unk3, 0x6D..0x73), from their world coordinates
     * (unk4, unk6) rather than the world map's baked screen ones — then one
     * marker per active kinstone fusion.
     *
     * Fusions show exactly when CheckKinstoneFused && !CheckFusionMapMarker-
     * Disabled, the pair of bitfields UpdateVisibleFusionMapMarkers
     * maintains, and each carries its own glyph: frame
     * gKinstoneWorldEvents[id].mapMarkerIcon + 100, nine icons across the 91
     * fusions. The id's location is ROM-constant — gKinstoneWorldEvents[id]
     * .worldEventId -> gWorldEvents[..]._c/._e, the event's overworld pixel
     * position, which sub_080A698C hands straight to sub_080A69E0. Table
     * variants are per-region twins selected like src/kinstone.c does;
     * REGION_IS_* is ROM identity, not live save state.
     *
     * Everything is dropped that sub_080A69E0 drops: a (0, 0) position, one
     * outside the overworld's region grid, and — the part that makes this
     * the region map's pass and not the world map's — one belonging to a
     * different region than the one on screen. Fusions that share a world
     * event (ids 37/38/41/42/43/47 all sit on the Hyrule Town well) are
     * left stacked exactly as the game stacks them: their glyphs differ, so
     * collapsing them would hide real information. */
    const KinstoneWorldEvent* kinstoneEvents = gKinstoneWorldEvents;
    const WorldEvent* worldEvents;
    const struct_gUnk_08128F58* row;
    int32_t count = 0;
    u32 i;

    if (out == NULL || maxMarkers <= 0 || dstW <= 0 || dstH <= 0) {
        return 0;
    }
    if (gRomData == NULL || gRomSize == 0) {
        return 0;
    }

    for (row = gUnk_08128F58, i = 0;
         i < MAP_HINT_ROW_MAX && row->frameIndex != 0 && count < maxMarkers; i++, row++) {
        int32_t lx, ly;
        if (((hintMask >> i) & 1u) == 0) {
            continue;
        }
        if (LocalizeInRegion(region, row->unk4, row->unk6, dstW, dstH, &lx, &ly)) {
            out[count].x = lx;
            out[count].y = ly;
            out[count].frame = row->unk3;
            count++;
        }
    }

    if (fusedKinstones == NULL || fusionUnmarked == NULL) {
        return count;
    }
    if (REGION_IS_EU) {
        kinstoneEvents = gKinstoneWorldEvents_eu;
    } else if (REGION_IS_JP) {
        kinstoneEvents = gKinstoneWorldEvents_jp;
    }
    worldEvents = GetWorldEvents();
    for (i = 10; i <= 100 && count < maxMarkers; i++) {
        const WorldEvent* event;
        int32_t lx, ly;

        if (!SaveBit(fusedKinstones, i) || SaveBit(fusionUnmarked, i)) {
            continue;
        }
        event = &worldEvents[kinstoneEvents[i].worldEventId];
        if (!LocalizeInRegion(region, event->_c, event->_e, dstW, dstH, &lx, &ly)) {
            continue;
        }
        out[count].x = lx;
        out[count].y = ly;
        out[count].frame = (uint8_t)(kinstoneEvents[i].mapMarkerIcon + 100u);
        count++;
    }
    return count;
}

/* --- Marker glyphs -------------------------------------------------------
 *
 * Both map screens stamp their markers as DrawDirect frames of the overlay's
 * direct sprite sheet, and no single glyph stands in for all of them: the
 * world map draws each hint row's frameIndex (0x5B, 0x60..0x63), the
 * enlarged region draws the hint rows' unk3 frames (0x6D..0x73) plus one of
 * nine fusion icons (0x64..0x6C). Every one is a single 16x16 OAM piece
 * anchored on the marker position, and the map screens' gOamCmd._8 never
 * reaches it (the pieces set shapeInfo bit 0, which clears the command's
 * palette bits, and neither screen passes a tile offset), so the frame data
 * alone names the tile and the palette row.
 *
 * Which tiles those numbers mean is per screen, because both screens load
 * marker art to the same OBJ span at 0x06014000:
 *   world map  — gfx group 94's 4864-byte record, tiles 512..663;
 *   region map — gfx group 95 + region's 2560-byte record, tiles 512..591,
 *                one blob every region's group points at.
 * Both are unconditional (ctrl 7) records, so there is no language variant
 * to pick: the language-gated span the map screen also loads (gfx group
 * 129 -> 0x06010800, tiles 64..127) carries none of these tiles. A frame
 * past the region blob's 80 tiles falls back to group 94, which is what the
 * console shows — screen 6 is only reachable from screen 4, whose art is
 * still in that VRAM underneath.
 *
 * OBJ palettes come from the pause menu's own group ladder on both screens,
 * most recent load first: the per-region palette groups (186 + region) write
 * BG banks only. */
#define DIRECT_SPRITE_INDEX (REGION_IS_EU ? 0x1fau : 0x1fbu)
#define MARKER_PX 16
#define MARKER_CACHE_MAX 24

/* Decoded glyphs, built on demand and immutable once complete (0 =
 * transparent). Same single-threaded discipline as the map images: the
 * second-screen render thread is the only caller, a slot's `ready` flag is
 * set last, and the table is simply emptied rather than evicted when a view
 * asks for more distinct glyphs than fit — the working set is one screen's
 * markers, well under the cap. */
typedef struct {
    uint32_t px[MARKER_PX * MARKER_PX];
    int32_t sheet; /* SECOND_SCREEN_WORLDMAP_NO_REGION or a region id */
    uint32_t frame;
    int ready;
} MarkerGlyph;

static MarkerGlyph sMarkerCache[MARKER_CACHE_MAX];
static int sMarkerCacheCount;

static const uint16_t* MarkerObjPalette(u32 row) {
    int i;
    for (i = (int)sizeof(sWorldMapPaletteGroups) - 1; i >= 0; i--) {
        u32 numColors = 0;
        const u8* p =
            Port_GetRawPaletteGroupBankData(sWorldMapPaletteGroups[i], 16u + (row & 15u), &numColors);
        if (p != NULL && numColors >= 16) {
            return (const uint16_t*)p;
        }
    }
    return NULL;
}

/* One 32-byte 4bpp tile of a marker sheet, or NULL. See the block comment
 * above for why the region's own group is asked first and group 94 second. */
static const u8* MarkerTile(int32_t region, u32 tileNo) {
    u32 vram = 0x6010000u + tileNo * 32u;
    const u8* tile = NULL;

    if (region >= 0 && region < WORLDMAP_REGION_COUNT) {
        tile = Port_GetRawGfxSpanForVram(REGION_GFX_GROUP_BASE + (u32)region, vram, 32u);
    }
    return tile != NULL ? tile : Port_GetRawGfxSpanForVram(WORLDMAP_GFX_GROUP, vram, 32u);
}

/* Decode one frame into `slot` (0 = transparent). Piece walk mirrors
 * port_second_screen_dungeonmap.c's DrawMarkerFrame — OAM 1D mapping,
 * size-table anchors, piece-local flips — with the canvas origin at the
 * frame's OAM anchor + (8, 8) so a 16x16 marker fills the buffer exactly. */
static int DecodeMarkerGlyph(MarkerGlyph* slot, u32 frame, int32_t region) {
    const u8* sizeTab = Port_GetSpriteSizeTable();
    u32 maxPieces = 0;
    const u8* fd = Port_GetDirectSpriteFrame(DIRECT_SPRITE_INDEX, frame, &maxPieces);
    u32 count, i;
    int drewAny = 0;

    if (fd == NULL || sizeTab == NULL) {
        return 0;
    }
    memset(slot->px, 0, sizeof(slot->px));
    count = fd[0];
    fd++;
    if (count > maxPieces) {
        count = maxPieces;
    }
    for (i = 0; i < count; i++, fd += 5) {
        int32_t xoff = (int8_t)fd[0];
        int32_t yoff = (int8_t)fd[1];
        u32 shapeInfo = fd[2];
        u32 attr2 = (u32)fd[3] | ((u32)fd[4] << 8);
        u32 tileNo = attr2 & 0x3FFu;
        u32 palRow = attr2 >> 12;
        const u8* se = &sizeTab[(shapeInfo & 0xF0u) >> 2];
        int32_t px = MARKER_PX / 2 + xoff - (int32_t)se[0];
        int32_t py = MARKER_PX / 2 + yoff - (int32_t)se[1];
        int32_t wpx = se[2];
        int32_t hpx = se[3];
        int32_t hflip = (shapeInfo & 4u) != 0;
        int32_t vflip = (shapeInfo & 8u) != 0;
        const uint16_t* pal = MarkerObjPalette(palRow);
        int32_t tx, ty, sx, sy;

        if (pal == NULL) {
            return 0; /* palette groups not resolved yet — retry next frame */
        }
        for (ty = 0; ty < hpx / 8; ty++) {
            for (tx = 0; tx < wpx / 8; tx++) {
                const u8* tile = MarkerTile(region, tileNo + (u32)(ty * (wpx / 8) + tx));
                if (tile == NULL) {
                    return 0;
                }
                for (sy = 0; sy < 8; sy++) {
                    for (sx = 0; sx < 8; sx++) {
                        u8 packed = tile[sy * 4 + sx / 2];
                        u8 colorIndex = (sx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0xFu);
                        int32_t ox, oy, cx, cy;
                        if (colorIndex == 0) {
                            continue;
                        }
                        ox = tx * 8 + sx;
                        oy = ty * 8 + sy;
                        cx = px + (hflip ? wpx - 1 - ox : ox);
                        cy = py + (vflip ? hpx - 1 - oy : oy);
                        if (cx < 0 || cx >= MARKER_PX || cy < 0 || cy >= MARKER_PX) {
                            continue;
                        }
                        slot->px[(size_t)cy * MARKER_PX + (size_t)cx] = Rgb555ToRgba8888(pal[colorIndex]);
                        drewAny = 1;
                    }
                }
            }
        }
    }
    return drewAny;
}

static const uint32_t* MarkerGlyphArt(u32 frame, int32_t region) {
    MarkerGlyph* slot;
    int i;

    for (i = 0; i < sMarkerCacheCount; i++) {
        if (sMarkerCache[i].ready && sMarkerCache[i].frame == frame && sMarkerCache[i].sheet == region) {
            return sMarkerCache[i].px;
        }
    }
    if (sMarkerCacheCount >= MARKER_CACHE_MAX) {
        sMarkerCacheCount = 0; /* a whole new view's worth of glyphs; start over */
    }
    slot = &sMarkerCache[sMarkerCacheCount];
    slot->ready = 0;
    if (!DecodeMarkerGlyph(slot, frame, region)) {
        return NULL;
    }
    slot->frame = frame;
    slot->sheet = region;
    slot->ready = 1;
    sMarkerCacheCount++;
    return slot->px;
}

/* One nearest-neighbor stamp of a decoded 16x16 glyph, top-left at (x, y).
 * `flat` non-zero paints every opaque source pixel that one colour instead
 * of the art's own — which is how the outline pass below draws the glyph's
 * silhouette. Transparent source pixels (0) are skipped either way, so 0 is
 * free to mean "use the art's colours". */
static void StampMarkerArt(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                           const uint32_t* art, int32_t x, int32_t y, int32_t scale, uint32_t flat) {
    int32_t sx, sy, dx, dy;

    for (sy = 0; sy < MARKER_PX; sy++) {
        for (sx = 0; sx < MARKER_PX; sx++) {
            uint32_t c = art[(size_t)sy * MARKER_PX + (size_t)sx];
            if (c == 0) {
                continue;
            }
            if (flat != 0) {
                c = flat;
            }
            for (dy = 0; dy < scale; dy++) {
                int32_t destY = y + sy * scale + dy;
                if (destY < 0 || destY >= bufH) {
                    continue;
                }
                for (dx = 0; dx < scale; dx++) {
                    int32_t destX = x + sx * scale + dx;
                    if (destX < 0 || destX >= bufW) {
                        continue;
                    }
                    pixels[(size_t)destY * (size_t)stride + (size_t)destX] = c;
                }
            }
        }
    }
}

int Port_SecondScreenWorldMap_DrawMarker(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t x, int32_t y, int32_t scale, uint32_t frame,
                                         int32_t region, uint32_t outline) {
    const uint32_t* art;

    if (pixels == NULL || bufW <= 0 || bufH <= 0 || stride <= 0) {
        return 0;
    }
    art = MarkerGlyphArt(frame, region);
    if (art == NULL) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    if (outline != 0) {
        /* The glyph sits on dense decoded terrain, and the game only ever
         * stamped it at 1:1 over its own low-contrast map; blown up on the
         * panel a bare stamp sinks into the art underneath. Ring it with its
         * own silhouette in the outline colour — eight offsets of one art
         * pixel, the same trick the sibling port's pins use — which lifts the
         * marker off the map without introducing any shape of its own to
         * read as clutter. */
        int32_t ox, oy;
        for (oy = -1; oy <= 1; oy++) {
            for (ox = -1; ox <= 1; ox++) {
                if (ox == 0 && oy == 0) {
                    continue;
                }
                StampMarkerArt(pixels, bufW, bufH, stride, art, x + ox * scale, y + oy * scale, scale,
                               outline);
            }
        }
    }
    StampMarkerArt(pixels, bufW, bufH, stride, art, x, y, scale, 0);
    return 1;
}
