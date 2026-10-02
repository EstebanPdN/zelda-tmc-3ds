#ifndef PORT_SECOND_SCREEN_DUNGEONMAP_H
#define PORT_SECOND_SCREEN_DUNGEONMAP_H

/*
 * Authentic dungeon-map rendering for the second screen: the same room
 * shapes, floor structure and reveal rules the game's own map screen
 * (src/subtask/subtaskLocalMapHint.c) shows, decoded at runtime from
 * ROM/asset data — no baked pixels, same policy as the item icons.
 *
 * Reveal rules mirror the real map so the second screen never cheats:
 * the dungeon map item gates the layout, the compass gates markers,
 * visited rooms come from the caller's session tracking.
 *
 * Same threading contract as port_second_screen_worldmap.h: ROM/static
 * decomp tables only; all live state (visited mask, dungeon items, player
 * position) arrives as parameters from the snapshot.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t floorCount;  /* 0 = no authentic map data for this dungeon */
    int8_t currentFloor; /* index into the drawable floors (display order,
                          * topmost floor = 0) of the floor `room` is on */
} SecondScreenDungeonMapInfo;

/* Fills `out` for the given dungeon/area/room. Returns 1 when authentic
 * map data exists, 0 otherwise (out->floorCount is zeroed then). */
int Port_SecondScreenDungeonMap_GetInfo(uint8_t dungeonIdx, uint8_t area, uint8_t room,
                                        SecondScreenDungeonMapInfo* out);

/* Draws one floor of the dungeon map into `pixels` (RGBA8888,
 * stride-pixels-per-row, bufW x bufH), fitted and centered inside the
 * destination rect. dungeonItemBits is gSave.dungeonItems[dungeonIdx]
 * (4: compass, 2: big key, 1: small key — plus whatever encodes the map
 * item). visitedMask bit n = room n entered this session. Player marker is
 * drawn only on the player's own floor; `tick` drives its blink. Returns 1
 * if authentic art was drawn, 0 for callers to fall back to the schematic
 * renderer. */
int Port_SecondScreenDungeonMap_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                     uint8_t dungeonIdx, int32_t floor, uint8_t area, uint8_t room,
                                     uint64_t visitedMask, uint8_t dungeonItemBits,
                                     int32_t playerAreaX, int32_t playerAreaY, uint32_t tick);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_DUNGEONMAP_H */
