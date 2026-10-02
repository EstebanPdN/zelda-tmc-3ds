#include "port_second_screen_dungeonmap.h"

/*
 * Authentic dungeon-map rendering for the second screen, rebuilt from the
 * pause menu's own dungeon-map tab and decoded from ROM at runtime — no
 * baked pixels, same policy as the item icons in
 * port_second_screen_render.c.
 *
 * Sources in the decomp this mirrors:
 *   - Room shapes: DrawDungeonFeatures (src/common.c) — per-room 2bpp
 *     bitmaps at gMapData + DungeonLayout.mapDataOffset, one map pixel per
 *     16px game tile, placed by the room's RoomHeader.map_x/map_y inside a
 *     128x128 map-pixel floor. map_y's bits 11+ are the floor row
 *     (sub_0801DB94), so floors stack in y at 0x800-pixel strides.
 *   - Floor structure: gDungeonLayouts[dungeon][floor] room lists +
 *     gDungeonFloorMetadatas[dungeon].numFloors (src/common.c), display
 *     order top floor first — the same tables PauseMenu_Screen_5 pages
 *     through with the dpad.
 *   - Colors: BG palette bank 12, loaded for the map tab by palette group
 *     184 (gUnk_08128AD8[3] in src/data/figurineMenuData.c). Raw pixel 1 =
 *     wall, 3 -> 7 = doorways, 2 = floor fill recolored by room state
 *     (sub_0801DF78), and the current room's fill (index 8) pulses because
 *     PauseMenu_Screen_5 rotates bank 12's colors 8..15 every 8 ticks.
 *   - Markers: DrawDungeonMap + DrawDungeonMapActually (src/common.c,
 *     src/menu/pauseMenu.c) — DrawDirect frames 0x7d (player), 0x80
 *     (chest), 0x81 (boss) of the direct sprite sheet, decoded from
 *     gFrameObjLists + the OBJ tiles the map screen's gfx groups load.
 *
 * Reveal rules mirror the real map screen so the bottom screen never
 * cheats (DrawDungeonFeatures's `features` logic, exactly):
 *   - the room the player stands in always draws (pulsing fill),
 *   - visited rooms draw with the lit fill,
 *   - other rooms draw (dim fill) only when the dungeon MAP item is owned,
 *   - rooms flagged hidden (DungeonLayout.unk_2 & 1) never draw,
 *   - chest and boss markers draw only with the COMPASS, like the game.
 *
 * Dungeon item bits: include/save.h's comment ("4: compass, 2: big key,
 * 1: small key") is wrong — per HasDungeonMap / HasDungeonCompass /
 * HasDungeonBigKey (src/gameUtils.c) the real encoding of
 * gSave.dungeonItems[] is bit 0 (1) = dungeon map, bit 1 (2) = compass,
 * bit 2 (4) = big key; small keys live in gSave.dungeonKeys[] instead.
 * dungeonItemBits carries that byte raw, so map = &1, compass = &2 here.
 *
 * Known gaps vs the real screen, all on the "show stale, never show
 * hidden" side because the needed state isn't reachable through the
 * parameters (extend SecondScreenSnapshot if they ever matter):
 *   - chest markers stay after the chest is opened (the game clears them
 *     via per-room local flags), and the boss skull stays after the boss
 *     falls (CheckGlobalFlag(dungeon+1)) — stale info the player already
 *     had, never unearned reveals;
 *   - the dungeon-entrance marker (frame 0x7f) is skipped: its position
 *     is the save's player_status.dungeon_x/y, which isn't snapshotted;
 *   - visitedMask is the port's per-session automap for the CURRENT area,
 *     so rooms of a sibling area on the same floor (boss arenas) and
 *     rooms visited in previous sessions count as unvisited until the map
 *     item lights them — less than the game shows, never more.
 *
 * Threading contract matches port_second_screen_worldmap.h: ROM-constant
 * data only (gMapData, layout tables, the raw palette/gfx/frame accessors
 * appended to src/common.c and port/port_draw.c); every live value arrives
 * as a parameter. The canvas below makes Draw single-caller — fine, it
 * only ever runs on the second-screen render thread.
 */

#include "area.h"
#include "common.h"
#include "region.h"
#include "room.h"

#include "port_offset_remap.h"

#include <string.h>

/* src/common.c (tables live at the end of that file; only
 * gDungeonFloorMetadatas has a header declaration). */
extern const DungeonLayout* const* const gDungeonLayouts[];

/* ROM map-data blob (port_linked_stubs.c, filled by Port_LoadRom). */
extern u8 gMapData[];

/* Raw-data accessors: src/common.c (palette groups, gfx groups, room
 * headers) and port/port_draw.c (frame pieces, OAM size table). */
extern const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors);
extern const u8* Port_GetRawGfxSpanForVram(u32 group, u32 vramAddr, u32 numBytes);
extern const RoomHeader* Port_GetRoomHeaderSafe(u32 area, u32 room);
extern const u8* Port_GetDirectSpriteFrame(u32 spriteIndex, u32 frameIndex, u32* outMaxPieces);
extern const u8* Port_GetSpriteSizeTable(void);

/* gDungeonLayouts has entries for "no dungeon" + the six real dungeons. */
#define DUNGEON_COUNT 7

/* One floor of the in-game map is a 128x128 map-pixel image. */
#define MAP_PX 128

/* DrawDirect sheet holding the map markers (include/subtask.h's
 * DRAW_DIRECT_SPRITE_INDEX). */
#define DIRECT_SPRITE_INDEX (REGION_IS_EU ? 0x1fau : 0x1fbu)

/* Marker frames, from DrawDungeonMapActually's switch (src/menu/pauseMenu.c). */
#define FRAME_MARKER_PLAYER 0x7du
#define FRAME_MARKER_CHEST 0x80u
#define FRAME_MARKER_BOSS 0x81u

/* Palette groups covering the dungeon-map tab, most recently loaded first:
 * 184 arrives with the tab itself (gUnk_08128AD8[3]), 181 and 11 with the
 * pause menu (sub_080A4D34 -> LoadPaletteGroup(0xb5), LoadGfxGroups). */
static const u8 kMapScreenPaletteGroups[] = { 184, 181, 11 };

/* The room-shape colors sit in BG palette bank 12 (PauseMenu_Screen_5
 * animates exactly that bank), which group 184's second chain entry loads. */
#define MAP_BG_PALETTE_GROUP 184u
#define MAP_BG_PALETTE_BANK 12u

/* Gfx groups whose OBJ-VRAM loads are live on the dungeon-map tab, most
 * recently loaded first: 129 (sub_080A5C44), 92 (the tab's gfx group), 86
 * (sub_080A4D34's heart-gfx group — its OBJ spans are identical across the
 * 86/87/88 health variants), 16 + 23 (LoadGfxGroups). The item-icon groups
 * (24/25/27..29) are skipped: inventory-conditional, and markers don't
 * live in their 0x6011800 window. */
static const u8 kMapScreenObjGfxGroups[] = { 129, 92, 86, 16, 23 };

/* Marker sprites can overhang their room by a few map pixels; give the
 * compose canvas a margin so nothing clips before scaling. */
#define CANVAS_MARGIN 16
#define CANVAS_W (MAP_PX + 2 * CANVAS_MARGIN)

/* Single-caller compose buffer (second-screen render thread only): one
 * floor at native map-pixel scale, 0 = transparent. */
static uint32_t sCanvas[CANVAS_W * CANVAS_W];

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static const DungeonLayout* GetFloorList(uint8_t dungeonIdx, uint32_t floor) {
    if (dungeonIdx == 0 || dungeonIdx >= DUNGEON_COUNT) {
        return NULL;
    }
    if (floor >= gDungeonFloorMetadatas[dungeonIdx].numFloors) {
        return NULL;
    }
    return gDungeonLayouts[dungeonIdx][floor];
}

/* OBJ tile data (32 bytes, 4bpp) as the map screen's VRAM would hold it.
 * Walking most-recent-group-first reproduces "later loads overwrite".
 * Language-gated gfx entries resolve to NULL (see the accessor) and fall
 * through to the underlying group-23 tiles — markers aren't text, so they
 * live in the unconditional spans anyway. */
static const uint8_t* MapScreenObjTile(uint32_t tileNo) {
    uint32_t i;
    if (tileNo >= 1024) {
        return NULL;
    }
    for (i = 0; i < sizeof(kMapScreenObjGfxGroups); i++) {
        const uint8_t* p =
            Port_GetRawGfxSpanForVram(kMapScreenObjGfxGroups[i], 0x6010000u + tileNo * 32u, 32u);
        if (p != NULL) {
            return p;
        }
    }
    return NULL;
}

static const uint16_t* MapScreenObjPalette(uint32_t row) {
    uint32_t i;
    for (i = 0; i < sizeof(kMapScreenPaletteGroups); i++) {
        u32 numColors = 0;
        const uint8_t* p =
            Port_GetRawPaletteGroupBankData(kMapScreenPaletteGroups[i], 16u + (row & 15u), &numColors);
        if (p != NULL && numColors >= 16) {
            return (const uint16_t*)p;
        }
    }
    return NULL;
}

static void CanvasPlot(int32_t x, int32_t y, uint32_t rgba) {
    x += CANVAS_MARGIN;
    y += CANVAS_MARGIN;
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_W) {
        return;
    }
    sCanvas[(size_t)y * CANVAS_W + (size_t)x] = rgba;
}

/* One DrawDirect frame at map-pixel scale, anchored like the game anchors
 * its map markers (gOamCmd position = the marker's map pixel; pieces then
 * offset around it minus the OAM size-table anchor — RenderSpritePieces in
 * port/port_draw.c, for the flags the map screen uses: no affine, no flip
 * from the command, extra/palette override 0). */
static void DrawMarkerFrame(int32_t mapX, int32_t mapY, uint32_t frameIndex) {
    const uint8_t* sizeTab = Port_GetSpriteSizeTable();
    u32 maxPieces = 0;
    const uint8_t* fd = Port_GetDirectSpriteFrame(DIRECT_SPRITE_INDEX, frameIndex, &maxPieces);
    uint32_t count;
    uint32_t i;

    if (fd == NULL || sizeTab == NULL) {
        return;
    }
    count = fd[0];
    fd++;
    if (count > maxPieces) {
        count = maxPieces;
    }
    for (i = 0; i < count; i++, fd += 5) {
        int32_t xoff = (int8_t)fd[0];
        int32_t yoff = (int8_t)fd[1];
        uint32_t shapeInfo = fd[2];
        /* attr2 layout: tile number low 10 bits, palette row top 4 — the
         * frame data bakes the palette in (command extra is 0 here). */
        uint32_t attr2 = (uint32_t)fd[3] | ((uint32_t)fd[4] << 8);
        uint32_t tileNo = attr2 & 0x3FFu;
        uint32_t palRow = attr2 >> 12;
        /* Size/anchor entry: sub-table 0 (no flip flags on the command),
         * indexed by shape (bits 7-6) and size (bits 5-4). */
        const uint8_t* se = &sizeTab[(shapeInfo & 0xF0u) >> 2];
        int32_t px = mapX + xoff - (int32_t)se[0];
        int32_t py = mapY + yoff - (int32_t)se[1];
        int32_t wpx = se[2];
        int32_t hpx = se[3];
        int32_t tilesW = wpx / 8;
        int32_t tilesH = hpx / 8;
        int32_t hflip = (shapeInfo & 4u) != 0; /* piece-local flips */
        int32_t vflip = (shapeInfo & 8u) != 0;
        const uint16_t* pal = MapScreenObjPalette(palRow);
        int32_t tx, ty, sx, sy;

        if (pal == NULL || tilesW <= 0 || tilesH <= 0) {
            continue;
        }
        for (ty = 0; ty < tilesH; ty++) {
            for (tx = 0; tx < tilesW; tx++) {
                /* OBJ 1D mapping (displayControl bit 0x40 is set on every
                 * pause-menu screen): consecutive tiles, row-major. */
                const uint8_t* tile = MapScreenObjTile(tileNo + (uint32_t)(ty * tilesW + tx));
                if (tile == NULL) {
                    continue;
                }
                for (sy = 0; sy < 8; sy++) {
                    for (sx = 0; sx < 8; sx++) {
                        uint8_t packed = tile[sy * 4 + sx / 2];
                        uint8_t colorIndex = (sx & 1) ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0Fu);
                        int32_t ox, oy;
                        if (colorIndex == 0) {
                            continue; /* 0 == transparent */
                        }
                        ox = tx * 8 + sx;
                        oy = ty * 8 + sy;
                        CanvasPlot(px + (hflip ? wpx - 1 - ox : ox), py + (vflip ? hpx - 1 - oy : oy),
                                   Rgb555ToRgba8888(pal[colorIndex]));
                    }
                }
            }
        }
    }
}

/* One room's shape, DrawDungeonFeatures's inner loop: 2bpp bitmap, 4
 * pixels per byte MSB-first (sub_0801DF60), row stride (width+3)/4, raw
 * values recolored per sub_0801DF78 (0 stays clear, 1 stays 1, 2 becomes
 * the room-state color, 3 becomes 7). */
static void BlitRoomShape(const DungeonLayout* lyt, uint32_t features, const uint16_t* pal,
                          uint32_t pulseColorIdx) {
    const RoomHeader* hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
    const uint8_t* shape;
    uint32_t mapX, mapY, w, h, stride, x, y;

    if (hdr == NULL) {
        return;
    }
    shape = gMapData + Port_RemapMapOffset(lyt->mapDataOffset);
    mapX = hdr->map_x / 16u;
    mapY = (hdr->map_y & 0x7FFu) / 16u; /* in-floor position; bits 11+ are the floor row */
    w = hdr->pixel_width / 16u;
    h = hdr->pixel_height / 16u;
    stride = (w + 3u) / 4u;
    if (w == 0 || h == 0 || stride * h > 0x400u) { /* the game DMAs at most 0x400 bytes per room */
        return;
    }
    for (y = 0; y < h; y++) {
        const uint8_t* row = shape + y * stride;
        for (x = 0; x < w; x++) {
            uint32_t raw = (row[x >> 2] >> (2u * ((~x) & 3u))) & 3u;
            uint32_t colorIdx = raw;
            if (raw == 2u) {
                colorIdx = features;
            } else if (raw == 3u) {
                colorIdx = 7u;
            }
            if (colorIdx == 0) {
                continue;
            }
            if (colorIdx == 8u) {
                colorIdx = pulseColorIdx; /* the rotating current-room fill */
            }
            if (mapX + x >= MAP_PX || mapY + y >= MAP_PX) {
                continue;
            }
            CanvasPlot((int32_t)(mapX + x), (int32_t)(mapY + y), Rgb555ToRgba8888(pal[colorIdx & 15u]));
        }
    }
}

int Port_SecondScreenDungeonMap_GetInfo(uint8_t dungeonIdx, uint8_t area, uint8_t room,
                                        SecondScreenDungeonMapInfo* out) {
    uint32_t numFloors, f;
    int32_t found = -1;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (out == NULL || dungeonIdx == 0 || dungeonIdx >= DUNGEON_COUNT) {
        return 0;
    }
    numFloors = gDungeonFloorMetadatas[dungeonIdx].numFloors;
    if (numFloors == 0 || numFloors > 8) {
        return 0;
    }
    /* The floor lists are the game's own room->floor mapping; scan them in
     * display order (index 0 = top floor, same order PauseMenu_Screen_5
     * shows them). */
    for (f = 0; f < numFloors && found < 0; f++) {
        const DungeonLayout* lyt = gDungeonLayouts[dungeonIdx][f];
        for (; lyt != NULL && lyt->area != 0; lyt++) {
            if (lyt->area == area && lyt->room == room) {
                found = (int32_t)f;
                break;
            }
        }
    }
    if (found < 0) {
        /* Rooms outside the lists (side chambers) still carry their floor
         * in map_y's bits 11+ — the exact value sub_0801DB94 uses for the
         * player's own floor row. */
        const RoomHeader* hdr = Port_GetRoomHeaderSafe(area, room);
        if (hdr != NULL) {
            uint32_t byHeader = hdr->map_y >> 11;
            found = (int32_t)(byHeader < numFloors ? byHeader : numFloors - 1);
        } else {
            found = 0;
        }
    }
    out->floorCount = (uint8_t)numFloors;
    out->currentFloor = (int8_t)found;
    return 1;
}

int Port_SecondScreenDungeonMap_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                     uint8_t dungeonIdx, int32_t floor, uint8_t area, uint8_t room,
                                     uint64_t visitedMask, uint8_t dungeonItemBits,
                                     int32_t playerAreaX, int32_t playerAreaY, uint32_t tick) {
    const DungeonLayout* list;
    const DungeonLayout* lyt;
    const uint16_t* bgPal;
    u32 numColors = 0;
    uint32_t numFloors, f;
    int32_t minX = MAP_PX, minY = MAP_PX, maxX = -1, maxY = -1;
    uint32_t pulseColorIdx;
    int32_t bx0, by0, bw, bh, outW, outH, ox, oy, dx, dy;
    int hasMap = (dungeonItemBits & 1) != 0;     /* bit 0 = dungeon map (HasDungeonMap) */
    int hasCompass = (dungeonItemBits & 2) != 0; /* bit 1 = compass (HasDungeonCompass) */

    if (pixels == NULL || dstW <= 0 || dstH <= 0 || floor < 0) {
        return 0;
    }
    list = GetFloorList(dungeonIdx, (uint32_t)floor);
    if (list == NULL) {
        return 0;
    }
    bgPal = (const uint16_t*)Port_GetRawPaletteGroupBankData(MAP_BG_PALETTE_GROUP, MAP_BG_PALETTE_BANK,
                                                             &numColors);
    if (bgPal == NULL || numColors < 16) {
        return 0; /* ROM data not ready yet — schematic fallback */
    }

    /* Frame on the dungeon-wide room bounding box (every floor, hidden
     * rooms excluded). It's ROM-constant, so the view never shifts as
     * rooms reveal or floors switch — nothing about the save leaks
     * through the framing. */
    numFloors = gDungeonFloorMetadatas[dungeonIdx].numFloors;
    for (f = 0; f < numFloors; f++) {
        for (lyt = gDungeonLayouts[dungeonIdx][f]; lyt != NULL && lyt->area != 0; lyt++) {
            const RoomHeader* hdr;
            int32_t rx, ry, rw, rh;
            if (lyt->unk_2 & 1) {
                continue;
            }
            hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
            if (hdr == NULL) {
                continue;
            }
            rx = (int32_t)(hdr->map_x / 16u);
            ry = (int32_t)((hdr->map_y & 0x7FFu) / 16u);
            rw = (int32_t)(hdr->pixel_width / 16u);
            rh = (int32_t)(hdr->pixel_height / 16u);
            if (rx < minX) {
                minX = rx;
            }
            if (ry < minY) {
                minY = ry;
            }
            if (rx + rw - 1 > maxX) {
                maxX = rx + rw - 1;
            }
            if (ry + rh - 1 > maxY) {
                maxY = ry + rh - 1;
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        return 0;
    }

    memset(sCanvas, 0, sizeof(sCanvas));

    /* PauseMenu_Screen_5 rotates bank 12's colors 8..15 one step every 8
     * GBA ticks (full cycle ~1.07s). tick here runs at the panel's ~20Hz,
     * so x3 approximates the 60Hz cadence. */
    pulseColorIdx = 8u + (((tick * 3u) >> 3) & 7u);

    /* Rooms — DrawDungeonFeatures's reveal logic, verbatim: current room
     * always (pulsing), visited rooms always (per-session mask), the rest
     * only with the map item; hidden rooms never. */
    for (lyt = list; lyt->area != 0; lyt++) {
        uint32_t features;
        if (lyt->unk_2 & 1) {
            continue;
        }
        if (lyt->area == area && lyt->room == room) {
            features = 8;
        } else if (lyt->area == area && ((visitedMask >> (lyt->room & 63u)) & 1u)) {
            features = 3;
        } else if (hasMap) {
            features = 2;
        } else {
            continue;
        }
        BlitRoomShape(lyt, features, bgPal, pulseColorIdx);
    }

    /* Markers, compass-gated exactly like DrawDungeonMap. The game walks
     * the same floor list and only rooms with a tile-entity property list
     * yield markers. Painter's order = reverse OAM priority: boss and
     * chests first, player last on top. */
    if (hasCompass) {
        for (lyt = list; lyt->area != 0; lyt++) {
            const TileEntity* te = (const TileEntity*)GetRoomProperty(lyt->area, lyt->room, 3);
            const RoomHeader* hdr;
            uint32_t n;
            if (te == NULL) {
                continue;
            }
            hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
            if (hdr == NULL) {
                continue;
            }
            if (lyt->unk_2 & 2) {
                /* Boss room: marker at the room's center (the game also
                 * checks CheckGlobalFlag(dungeon+1) to drop it once the
                 * boss is beaten — not snapshotted, so it stays). */
                int32_t bx = (int32_t)((((uint32_t)hdr->pixel_width / 2u + hdr->map_x) / 16u) & 0x7Fu);
                int32_t by = (int32_t)((((uint32_t)hdr->pixel_height / 2u + hdr->map_y) / 16u) & 0x7Fu);
                DrawMarkerFrame(bx, by, FRAME_MARKER_BOSS);
            }
            for (n = 0; n < 256 && te[n].type != 0; n++) {
                uint32_t tilePos, cx, cy;
                if (te[n].type != SMALL_CHEST && te[n].type != BIG_CHEST) {
                    continue;
                }
                /* The game also drops a chest marker once its local flag
                 * is set (chest opened) — not snapshotted, so it stays. */
                tilePos = te[n].tilePos;
                if (te[n].type == SMALL_CHEST) {
                    cx = ((((tilePos << 4) & 0x3F0u) | 8u) + (hdr->map_x % 0x800u)) >> 4;
                    cy = ((((tilePos >> 2) & 0x3F0u) | 8u) + (hdr->map_y % 0x800u)) >> 4;
                } else {
                    /* Big chests store pixel coordinates: x in tilePos, y
                     * as the u16 at offset 6 (DrawDungeonMap's
                     * *(u16*)&tileEntity->_6). */
                    uint32_t bcy = (uint32_t)te[n]._6 | ((uint32_t)te[n]._7 << 8);
                    cx = ((hdr->map_x % 0x800u) + tilePos) >> 4;
                    cy = ((hdr->map_y % 0x800u) + bcy) >> 4;
                }
                DrawMarkerFrame((int32_t)cx, (int32_t)cy, FRAME_MARKER_CHEST);
            }
        }
    }

    /* Player marker, only on the player's own floor (map_y bits 11+ =
     * floor row, sub_0801DB94), at the game's own map-pixel position
     * (DrawDungeonMap's (x >> 4) & 0x7f). Blink cadence borrowed from the
     * game's map-tab player icon (sub_080A6378 hides it every other 32
     * GBA ticks): ~0.4s on/off at the panel's ~20Hz tick. */
    if (playerAreaY >= 0 && (playerAreaY >> 11) == floor && (tick & 8u) == 0) {
        DrawMarkerFrame((playerAreaX >> 4) & 0x7F, (playerAreaY >> 4) & 0x7F, FRAME_MARKER_PLAYER);
    }

    /* Fit + center the framed region into the destination rect. Integer
     * upscale when it fits (the normal case) for the GBA's chunky look;
     * plain nearest-neighbor decimation for pathologically small rects.
     * A 2-map-pixel margin keeps marker overhang inside the frame. */
    bx0 = minX - 2;
    by0 = minY - 2;
    bw = (maxX + 3) - bx0;
    bh = (maxY + 3) - by0;
    if (dstW / bw >= 1 && dstH / bh >= 1) {
        int32_t s = dstW / bw < dstH / bh ? dstW / bw : dstH / bh;
        outW = bw * s;
        outH = bh * s;
    } else if ((int64_t)dstW * bh <= (int64_t)dstH * bw) {
        outW = dstW;
        outH = (int32_t)((int64_t)dstW * bh / bw);
    } else {
        outH = dstH;
        outW = (int32_t)((int64_t)dstH * bw / bh);
    }
    if (outW < 1) {
        outW = 1;
    }
    if (outH < 1) {
        outH = 1;
    }
    ox = dstX + (dstW - outW) / 2;
    oy = dstY + (dstH - outH) / 2;

    for (dy = 0; dy < outH; dy++) {
        int32_t destY = oy + dy;
        int32_t srcY = by0 + (int32_t)((int64_t)dy * bh / outH) + CANVAS_MARGIN;
        uint32_t* row;
        if (destY < 0 || destY >= bufH || srcY < 0 || srcY >= CANVAS_W) {
            continue;
        }
        row = pixels + (size_t)destY * (size_t)stride;
        for (dx = 0; dx < outW; dx++) {
            int32_t destX = ox + dx;
            int32_t srcX = bx0 + (int32_t)((int64_t)dx * bw / outW) + CANVAS_MARGIN;
            uint32_t c;
            if (destX < 0 || destX >= bufW || srcX < 0 || srcX >= CANVAS_W) {
                continue;
            }
            c = sCanvas[(size_t)srcY * CANVAS_W + (size_t)srcX];
            if (c != 0) {
                row[destX] = c;
            }
        }
    }
    return 1;
}
