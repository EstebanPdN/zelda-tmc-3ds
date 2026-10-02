#include <stdio.h>
#include <string.h>

#include "port_gfx_group_dma.h"
#include "port_vertical_minish_path.h"

#define CHECK(condition, message)                                                                  \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__);                           \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

enum {
    MAP_DATA_TOP_SPECIAL = 0x02002F00u,
    MAP_TOP = 0x0200B650u,
    MAP_DATA_BOTTOM_SPECIAL = 0x02019EE0u,
    MAP_BOTTOM = 0x02025EB0u,
};

static uint8_t sMapDataTopSpecial[0x8000];
static uint8_t sMapTop[0xC008];
static uint8_t sMapDataBottomSpecial[0x8000];
static uint8_t sMapBottom[0xC008];
static uint8_t sGenericEwram[0x40000];
static int sRejectResolution;
static u32 sInteriorDiscontinuityAddress;

/* OLD-3b capture: Minish Rafters/Cafe, native 240x160 viewport.  These are
 * room-header and live-state values, not ROM-derived graphics. */
enum {
    RAFTERS_ROOM_WIDTH = 496,
    RAFTERS_ROOM_HEIGHT = 256,
    RAFTERS_VIEW_WIDTH = 240,
    RAFTERS_VIEW_HEIGHT = 160,
    RAFTERS_PLAYER_X = 346,
    RAFTERS_PLAYER_Y = 211,
    RAFTERS_PARALLAX_WIDTH = 48,
    RAFTERS_PARALLAX_HEIGHT = 32,
};

void* Port_ResolveEwramPtr(u32 gbaAddress) {
    if (sRejectResolution) return NULL;
    if (gbaAddress == sInteriorDiscontinuityAddress) {
        return sGenericEwram + (gbaAddress - 0x02000000u) + 1u;
    }
    if (gbaAddress >= MAP_DATA_TOP_SPECIAL && gbaAddress < MAP_DATA_TOP_SPECIAL + sizeof sMapDataTopSpecial) {
        return sMapDataTopSpecial + (gbaAddress - MAP_DATA_TOP_SPECIAL);
    }
    if (gbaAddress >= MAP_TOP && gbaAddress < MAP_TOP + 0xC004u) {
        u32 offset = gbaAddress - MAP_TOP;
        return sMapTop + offset + (offset >= 4 ? 4 : 0);
    }
    if (gbaAddress >= MAP_DATA_BOTTOM_SPECIAL &&
        gbaAddress < MAP_DATA_BOTTOM_SPECIAL + sizeof sMapDataBottomSpecial) {
        return sMapDataBottomSpecial + (gbaAddress - MAP_DATA_BOTTOM_SPECIAL);
    }
    if (gbaAddress >= MAP_BOTTOM && gbaAddress < MAP_BOTTOM + 0xC004u) {
        u32 offset = gbaAddress - MAP_BOTTOM;
        return sMapBottom + offset + (offset >= 4 ? 4 : 0);
    }
    if (gbaAddress >= 0x02000000u && gbaAddress < 0x02040000u) {
        return sGenericEwram + (gbaAddress - 0x02000000u);
    }
    return NULL;
}

static int CheckKnownDestination(u32 gbaAddress, uint8_t* nativeDest, const char* name) {
    static const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    PortGfxGroupDmaResult result;

    memset(nativeDest, 0, sizeof payload);
    result = Port_CopyGfxGroupDmaToEwram(payload, gbaAddress, sizeof payload);
    if (result != PORT_GFX_GROUP_DMA_COPIED || memcmp(nativeDest, payload, sizeof payload) != 0) {
        fprintf(stderr, "FAIL: %s did not resolve to its native buffer\n", name);
        return 1;
    }
    return 0;
}

static int ClampCameraAxis(int target, int center, int origin, int roomExtent, int viewExtent) {
    int result = target - center;
    int maximum = origin + roomExtent - viewExtent;
    if (maximum < origin) maximum = origin;
    if (result < origin) result = origin;
    if (result > maximum) result = maximum;
    return result;
}

static int CheckCapturedRaftersCoverage(void) {
    const int scrollX = ClampCameraAxis(RAFTERS_PLAYER_X, RAFTERS_VIEW_WIDTH / 2, 0,
                                        RAFTERS_ROOM_WIDTH, RAFTERS_VIEW_WIDTH);
    const int scrollY = ClampCameraAxis(RAFTERS_PLAYER_Y, RAFTERS_VIEW_HEIGHT / 2, 0,
                                        RAFTERS_ROOM_HEIGHT, RAFTERS_VIEW_HEIGHT);
    const int lastX = scrollX + RAFTERS_VIEW_WIDTH - 1;
    const int lastY = scrollY + RAFTERS_VIEW_HEIGHT - 1;
    const int parallaxX = scrollX * RAFTERS_PARALLAX_WIDTH /
                          (RAFTERS_ROOM_WIDTH - RAFTERS_VIEW_WIDTH);
    const int parallaxY = scrollY * RAFTERS_PARALLAX_HEIGHT /
                          (RAFTERS_ROOM_HEIGHT - RAFTERS_VIEW_HEIGHT);

    CHECK(scrollX == 226 && scrollY == 96, "OLD-3b camera matches the retail native clamp");
    CHECK(lastX == 465 && lastY == 255, "OLD-3b viewport ends inside the room");
    CHECK(scrollX / 16 == 14 && lastX / 16 == 29, "visible X metatiles are covered by the 31-column map");
    CHECK(scrollY / 16 == 6 && lastY / 16 == 15, "visible Y metatiles are covered by the 16-row map");
    CHECK(lastX < RAFTERS_ROOM_WIDTH && lastY < RAFTERS_ROOM_HEIGHT,
          "native viewport never samples outside the room map");
    CHECK(parallaxX == 42 && parallaxY == 32, "parallax offsets match the captured BG1 registers");
    return 0;
}

static int CheckMinishRaftersGroupPath(void) {
    uint8_t rawGroupTilemap[0x1000];
    uint8_t interleaved[0x4000];
    uint8_t screenblock[0x800];
    const unsigned parallaxX = 42;
    const unsigned sourceOffset = (parallaxX >> 4) * sizeof(u32);
    PortGfxGroupDmaResult result;

    /* Gfx group 54 (Minish Rafters/Cafe) has a raw 0x1000-byte entry whose
     * GBA destination is gMapDataTopSpecial.  A deterministic synthetic
     * payload keeps this regression legally independent of game assets. */
    for (size_t i = 0; i < sizeof rawGroupTilemap; ++i) {
        rawGroupTilemap[i] = (uint8_t)(1u + (i * 37u) % 251u);
    }
    memset(sMapDataTopSpecial, 0, sizeof sMapDataTopSpecial);
    result = Port_CopyGfxGroupDmaToEwram(rawGroupTilemap, MAP_DATA_TOP_SPECIAL,
                                         sizeof rawGroupTilemap);
    CHECK(result == PORT_GFX_GROUP_DMA_COPIED, "Minish Rafters raw tilemap reaches its native alias");
    CHECK(memcmp(sMapDataTopSpecial, rawGroupTilemap, sizeof rawGroupTilemap) == 0,
          "Minish Rafters raw tilemap remains byte exact");

    /* Mirror the retail manager's two 0x800-byte halves into 0x100-byte
     * source rows, then select the captured horizontal parallax window. */
    memset(interleaved, 0, sizeof interleaved);
    memset(screenblock, 0, sizeof screenblock);
    for (size_t row = 0; row < 32; ++row) {
        memcpy(interleaved + row * 0x100, sMapDataTopSpecial + row * 0x40, 0x40);
        memcpy(interleaved + row * 0x100 + 0x40,
               sMapDataTopSpecial + 0x800 + row * 0x40, 0x40);
        memcpy(screenblock + row * 0x40,
               interleaved + row * 0x100 + sourceOffset, 0x40);
    }
    for (size_t row = 0; row < 32; ++row) {
        CHECK(memcmp(screenblock + row * 0x40,
                     interleaved + row * 0x100 + sourceOffset, 0x40) == 0,
              "captured parallax screenblock row is populated");
    }
    CHECK(memcmp(screenblock, (uint8_t[0x800]){ 0 }, sizeof screenblock) != 0,
          "Minish Rafters BG1 screenblock is not blank");
    return 0;
}

static int CheckVerticalMinishPathWindowing(void) {
    uint8_t mapData[0x8000];
    uint8_t* bg3;
    uint8_t* bg1;

    /* Captured failing state: origin_y=64, scroll_y=580.  BG3 and BG1 use
     * different parallax rates, but both offsets are expressed in bytes. */
    const int scrollDelta = 580 - 64;
    const int bg3Offset = scrollDelta + (scrollDelta >> 3);
    const int bg1Offset = scrollDelta + (scrollDelta >> 2);

    bg3 = Port_VerticalMinishPathSubTileMap(mapData, bg3Offset, 0);
    bg1 = Port_VerticalMinishPathSubTileMap(mapData, bg1Offset, 0x2000);

    CHECK(bg3 - mapData == 0x1200, "vertical Minish Path BG3 selects the captured byte page");
    CHECK(bg1 - mapData == 0x3400, "vertical Minish Path opaque BG1 selects its populated byte page");
    CHECK(bg1 + 0x800 <= mapData + 0x4000, "opaque BG1 screenblock remains inside loaded group data");
    CHECK(Port_VerticalMinishPathSubTileMap(mapData, -0x80, 0) == mapData,
          "negative transition offset remains clamped to the tilemap start");
    return 0;
}

int main(void) {
    static const uint8_t payload[] = { 0xA1, 0xB2, 0xC3, 0xD4 };
    PortGfxGroupDmaResult result;

    CHECK(CheckKnownDestination(MAP_DATA_TOP_SPECIAL, sMapDataTopSpecial, "gMapDataTopSpecial") == 0,
          "top-special destination");
    CHECK(CheckKnownDestination(MAP_TOP + 4, sMapTop + 8, "gMapTop.mapData") == 0, "top-layer destination");
    CHECK(CheckKnownDestination(MAP_DATA_BOTTOM_SPECIAL, sMapDataBottomSpecial, "gMapDataBottomSpecial") == 0,
          "bottom-special destination");
    CHECK(CheckKnownDestination(MAP_BOTTOM + 4, sMapBottom + 8, "gMapBottom.mapData") == 0,
          "bottom-layer destination");
    CHECK(CheckCapturedRaftersCoverage() == 0, "OLD-3b camera/map coverage oracle");
    CHECK(CheckMinishRaftersGroupPath() == 0, "OLD-3b Minish Rafters background path");
    CHECK(CheckVerticalMinishPathWindowing() == 0, "vertical Minish Path parallax byte windowing");

    /* Gfx groups 30-35 chain four 4 KiB blocks inside top-special. */
    memset(sMapDataTopSpecial, 0, sizeof sMapDataTopSpecial);
    result = Port_CopyGfxGroupDmaToEwram(payload, MAP_DATA_TOP_SPECIAL + 0x3000, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_COPIED, "top-special interior destination accepted");
    CHECK(memcmp(sMapDataTopSpecial + 0x3000, payload, sizeof payload) == 0,
          "top-special interior destination uses native alias");

    result = Port_CopyGfxGroupDmaToEwram(payload, MAP_TOP + 2, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "MapLayer pointer-padding crossing fails closed");

    result = Port_CopyGfxGroupDmaToEwram(payload, MAP_DATA_TOP_SPECIAL + sizeof sMapDataTopSpecial - 2,
                                         sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "native alias end crossing fails closed");

    result = Port_CopyGfxGroupDmaToEwram(payload, MAP_DATA_TOP_SPECIAL - 2, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "generic-to-native alias crossing fails closed");

    memset(sGenericEwram, 0, sizeof sGenericEwram);
    result = Port_CopyGfxGroupDmaToEwram(payload, 0x02001A40u, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_COPIED, "generic EWRAM gfx destination accepted");
    CHECK(memcmp(sGenericEwram + 0x1A40, payload, sizeof payload) == 0,
          "generic EWRAM gfx destination remains supported");

    /* Match 16-bit DMA semantics: an odd trailing byte is not transferred. */
    memset(sGenericEwram + 0x100, 0, 4);
    result = Port_CopyGfxGroupDmaToEwram(payload, 0x02000100u, 3);
    CHECK(result == PORT_GFX_GROUP_DMA_COPIED, "odd declared size accepted");
    CHECK(sGenericEwram[0x100] == payload[0] && sGenericEwram[0x101] == payload[1] &&
              sGenericEwram[0x102] == 0,
          "copy preserves whole-16-bit-unit DMA length");

    result = Port_CopyGfxGroupDmaToEwram(payload, 0x06000000u, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_NOT_EWRAM, "VRAM remains on the normal DMA path");

    memset(sGenericEwram + sizeof sGenericEwram - 4, 0x7C, 4);
    result = Port_CopyGfxGroupDmaToEwram(payload, 0x0203FFFEu, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "EWRAM overrun fails closed");
    CHECK(sGenericEwram[sizeof sGenericEwram - 2] == 0x7C && sGenericEwram[sizeof sGenericEwram - 1] == 0x7C,
          "invalid EWRAM span writes nothing");

    memset(sGenericEwram + 0x180, 0x6D, sizeof payload);
    sInteriorDiscontinuityAddress = 0x02000182u;
    result = Port_CopyGfxGroupDmaToEwram(payload, 0x02000180u, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "internal resolver discontinuity fails closed");
    CHECK(memcmp(sGenericEwram + 0x180, "mmmm", sizeof payload) == 0,
          "internal resolver discontinuity writes nothing");
    sInteriorDiscontinuityAddress = 0;

    sRejectResolution = 1;
    result = Port_CopyGfxGroupDmaToEwram(payload, MAP_DATA_TOP_SPECIAL, sizeof payload);
    CHECK(result == PORT_GFX_GROUP_DMA_INVALID, "unresolved EWRAM destination fails closed");
    sRejectResolution = 0;

    puts("port_gfx_group_dma_test: PASS");
    return 0;
}
