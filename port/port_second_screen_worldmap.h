#ifndef PORT_SECOND_SCREEN_WORLDMAP_H
#define PORT_SECOND_SCREEN_WORLDMAP_H

/*
 * Hyrule world-map artwork for the second screen, decoded at runtime from
 * the game's own map-screen graphics (ROM/asset layer) — never shipped as
 * baked pixels, same policy as port_second_screen_render.c's item icons.
 *
 * Render-thread safe by construction: the image is built once (lazily) into
 * a private buffer and only published when complete; after that it is
 * immutable. Implementations read only ROM/asset data (port_rom accessors,
 * static decomp tables) — never live engine state (gSave / gArea /
 * gPaletteBuffer); anything save- or room-dependent reaches the caller
 * through SecondScreenSnapshot instead.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the decoded world-map image as RGBA8888 (row-major, w x h), or
 * NULL while ROM/asset data isn't ready yet. Callers fall back to schematic
 * rendering and simply retry next frame — the call is cheap once decoded. */
const uint32_t* Port_SecondScreenWorldMap_GetImage(int32_t* outW, int32_t* outH);

/* Maps an area + area-local player position (pixels) to world-map image
 * pixel coordinates. Returns 1 on success, 0 when the area has no world-map
 * location (interiors, dungeons) — callers keep the last successful fix,
 * zelda3-android's frozen-doorway-marker behavior. */
int Port_SecondScreenWorldMap_LocatePlayer(uint8_t area, int32_t areaX, int32_t areaY,
                                           int32_t* outMapX, int32_t* outMapY);

/* World-map pixel position of a windcrest warp point (WindcrestID bit
 * index, matching gSave.windcrests' upper-byte flags — the caller decides
 * from the snapshot which ones are unlocked and how to draw them). Returns
 * 1 and fills the position when the id is a real windcrest, 0 past the end
 * or while map data isn't ready. Positions come from the same static
 * tables the fast-travel screen (src/subtask/subtaskFastTravel.c) uses. */
int Port_SecondScreenWorldMap_GetWindcrestPin(int32_t windcrestId, int32_t* outMapX, int32_t* outMapY);

/* One marker the game's map screens stamp: a position and the DrawDirect
 * frame that belongs on it. Positions are marker CENTERS — DrawMarker takes
 * a top-left, so center a stamp with x = marker.x - 8*scale (same for y). */
typedef struct {
    int32_t x, y;
    uint8_t frame;
} SecondScreenMapMarker;

/* `region` value naming the world map's own marker art rather than an
 * enlarged region's (the two screens load different tiles to the same VRAM). */
#define SECOND_SCREEN_WORLDMAP_NO_REGION (-1)

/* The world map's MAP HINTS — the red checks and errand glyphs pause screen
 * 4 shows (sub_080A6438), NOT kinstone fusions: the world map has no fusion
 * pass at all, those belong to the enlarged region map below. Writes up to
 * maxMarkers markers at the hint table's own pre-baked screen positions,
 * each carrying that row's world-map frame, and returns how many.
 *
 * hintMask is the game's own visibility word, bit i = row i of
 * gUnk_08128F58 is showing: `gSave.map_hints & sub_080A6F40()`, published
 * from the game thread as SecondScreenSnapshot.mapHints (the predicate reads
 * local flags and inventory, which this ROM-only module cannot see). Pass 0
 * and nothing is drawn. Returns 0 while map/table data isn't ready. */
int32_t Port_SecondScreenWorldMap_GetMapHints(uint32_t hintMask, SecondScreenMapMarker* out,
                                              int32_t maxMarkers);

/* The markers belonging INSIDE one enlarged region (pause screen 6,
 * sub_080A68D4), in that drawn region's own pixel space (0..dstW, 0..dstH as
 * passed to DrawRegion): the same map hints again in their region frames,
 * then one marker per active kinstone fusion carrying that fusion's own
 * glyph (gKinstoneWorldEvents[id].mapMarkerIcon + 100 — nine distinct
 * icons). Markers outside the region being drawn are dropped, exactly as
 * sub_080A69E0 drops them. Returns how many were written, 0 while data isn't
 * ready.
 *
 * Fusion visibility is the game's own rule applied to the save bits passed
 * in: fused && marker not retired, kinstone ids 10..100 (1..9 are the
 * golden-kinstone story fusions the game's pass also skips).
 * fusedKinstones/fusionUnmarked are the 13-byte snapshot arrays, passed
 * through verbatim from gSave.kinstones; either may be NULL to ask for
 * hints only.
 *
 * Known gap, on the stale-not-cheating side: the game refreshes
 * fusionUnmarked from each event's completion flag only when the pause
 * menu opens (UpdateVisibleFusionMapMarkers, src/common.c) — that flag
 * state isn't in these two arrays, so a fusion reward claimed since the
 * last pause keeps its marker until the game's own retire pass next runs.
 * Stale info the player already had, never an unearned reveal. */
int32_t Port_SecondScreenWorldMap_GetRegionMarkers(int32_t region, uint32_t hintMask,
                                                   const uint8_t* fusedKinstones,
                                                   const uint8_t* fusionUnmarked, int32_t dstW,
                                                   int32_t dstH, SecondScreenMapMarker* out,
                                                   int32_t maxMarkers);

/* Draws one marker glyph (decoded from ROM, the exact 16x16 DrawDirect frame
 * the map screen stamps) at (x, y) top-left, nearest-neighbor scaled: the
 * stamp covers 16*scale pixels a side. `frame` is a marker's own frame id;
 * `region` picks which screen's marker tiles to read —
 * SECOND_SCREEN_WORLDMAP_NO_REGION for the world map, else the region id
 * whose enlarged map is being drawn. `outline` non-zero first stamps the
 * glyph's silhouette in that colour, offset one art pixel in all eight
 * directions, so the marker separates from busy map art; pass 0 for a bare
 * stamp. Returns 1 if drawn, 0 while that glyph isn't decodable yet —
 * callers simply skip the marker that frame rather than substituting some
 * other glyph. */
int Port_SecondScreenWorldMap_DrawMarker(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t x, int32_t y, int32_t scale, uint32_t frame,
                                         int32_t region, uint32_t outline);

/* The map screen's own zoom grid: the game's map lets the player put the
 * cursor on a tile and open that tile's enlarged regional map. Resolves a
 * world-map pixel position to the tile under it — outRegion is the id to
 * hand DrawRegion, and (x0,y0)-(x1,y1) is the tile's rect in world-map
 * pixels so callers can outline it like the game's cursor brackets.
 * Returns 1 on a real tile, 0 off-grid or while data isn't ready. */
int Port_SecondScreenWorldMap_GetRegionAt(int32_t mapX, int32_t mapY, int32_t* outRegion,
                                          int32_t* outX0, int32_t* outY0, int32_t* outX1,
                                          int32_t* outY1);

/* Draws one region's enlarged map — the same artwork the game shows after
 * zooming into a tile — fitted into the destination rect, nearest-neighbor.
 * Returns 1 when drawn, 0 while that region's data isn't decodable (caller
 * stays on the world view). */
/* Pixel size of a region's enlarged artwork. Lets the caller letterbox its
 * rect to the art's aspect rather than stretching to fill. Returns 0 when the
 * region has no drawable art. */
int Port_SecondScreenWorldMap_GetRegionSize(int32_t region, int32_t* outW, int32_t* outH);

int Port_SecondScreenWorldMap_DrawRegion(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                         int32_t region);

/* Where the player marker sits inside a region's enlarged map, in that
 * drawn region's own pixel space (0..w, 0..h as passed to DrawRegion).
 * Returns 1 when the player is inside this region and the position is
 * known, 0 otherwise — callers just omit the marker then. `area`, `areaX`
 * and `areaY` are the same values LocatePlayer takes. */
int Port_SecondScreenWorldMap_LocateInRegion(int32_t region, uint8_t area, int32_t areaX, int32_t areaY,
                                             int32_t dstW, int32_t dstH, int32_t* outX, int32_t* outY);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_WORLDMAP_H */
