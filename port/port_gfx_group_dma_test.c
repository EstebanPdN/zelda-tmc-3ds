#include <stdio.h>
#include <string.h>

#include "port_gfx_group_dma.h"

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
