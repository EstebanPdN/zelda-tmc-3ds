#include "port_region_data.h"

#include <stdint.h>
#include <stdio.h>

#include "port_types.h"
#include "region.h"

u8* gRomData;
u32 gRomSize;
int gActiveRegion = TMC_REGION_USA;

const u8 gUnk_080D8E50[1];
const u8 gUnk_080D9328[1];
const u8 gUnk_080DD750[0x41];
const u8 gUnk_080DD7E0[1];
const u8 gUnk_080DD840[1];
const u8 gUnk_080EAE60[1];
const u8 gUnk_080EB9F4[1];
const u8 gUnk_080F58A8[1];
const u8 gUnk_080F5B3C[1];
const u8 gUnk_080F78A0[1];
const u8 gUnk_080F9BF8[1];
const u8 gUnk_080F09A0[1];
const u8 gUnk_080FEAC8[1];
const u8 gUnk_080FEE58[1];

static u8 sRom[0x100000];
static const u8 sUnknownCompiledData[1];
static int sFailures;

#define CHECK(condition, description)                                                     \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            fprintf(stderr, "FAIL: %s (line %d)\n", (description), __LINE__);            \
            ++sFailures;                                                                  \
        }                                                                                 \
    } while (0)

int main(void) {
    gRomData = sRom;
    gRomSize = sizeof(sRom);

    gActiveRegion = TMC_REGION_USA;
    CHECK(Port_ResolveRegionData(gUnk_080DD7E0) == gUnk_080DD7E0, "USA keeps its compiled data");

    gActiveRegion = TMC_REGION_JP;
    CHECK(Port_ResolveRegionData(gUnk_080DD7E0) == gUnk_080DD7E0,
          "JP remains unchanged until offsets are independently verified");

    gActiveRegion = TMC_REGION_EU;
    CHECK(Port_ResolveRegionData(gUnk_080D8E50) == sRom + 0xD85AC,
          "EU resolves all six Goron wall-break pointer records");
    gRomSize = 0xD85AC + 95;
    CHECK(Port_ResolveRegionData(gUnk_080D8E50) == NULL, "truncated Goron table fails closed");
    gRomSize = sizeof(sRom);
    CHECK(Port_ResolveRegionData(gUnk_080D9328) == sRom + 0xD8A84, "EU resolves HAKA tile entities");
    CHECK(Port_ResolveRegionData(gUnk_080DD750 + 0x40) == sRom + 0xDCECC,
          "EU resolves both Cloud Tops golden-Kinstone managers");
    CHECK(Port_ResolveRegionData(gUnk_080DD7E0) == sRom + 0xDCF1C, "EU resolves top Cloud Tops fight");
    CHECK(Port_ResolveRegionData(gUnk_080DD840) == sRom + 0xDCF7C, "EU resolves bottom Cloud Tops fight");
    CHECK(Port_ResolveRegionData(gUnk_080EAE60) == sRom + 0xEA53C, "EU resolves first structural list");
    CHECK(Port_ResolveRegionData(gUnk_080EB9F4) == sRom + 0xEB0C0, "EU resolves second structural list");
    CHECK(Port_ResolveRegionData(gUnk_080F78A0) == sRom + 0xF6E5C, "EU resolves divergent entity type");
    CHECK(Port_ResolveRegionData(gUnk_080F9BF8) == sRom + 0xF9144, "EU resolves Ezlo hint flag");
    CHECK(Port_ResolveRegionData(gUnk_080F09A0) == sRom + 0xEFFD4, "EU resolves Castle Garden flags");
    CHECK(Port_ResolveRegionData(gUnk_080FEAC8) == sRom + 0xFE00C, "EU resolves world-event chest flags");
    CHECK(Port_ResolveRegionData(gUnk_080FEE58) == sRom + 0xFE39C, "EU resolves pushable grave flag");
    CHECK(Port_ResolveRegionData(gUnk_080F58A8) == NULL, "EU rejects first USA-only list");
    CHECK(Port_ResolveRegionData(gUnk_080F5B3C) == NULL, "EU rejects second USA-only list");
    CHECK(Port_ResolveRegionData(sRom + 0x1234) == sRom + 0x1234, "ROM-native data is never translated");
    CHECK(Port_ResolveRegionData(sUnknownCompiledData) == sUnknownCompiledData,
          "unknown compiled data is not guessed");
    CHECK(Port_ResolveRegionData(NULL) == NULL, "NULL remains NULL");

    /* The first FallingItemManager must wait on EU F0 and mark EU F1 after
     * pickup; the second must use F2/F3.  These exact bytes are what let an
     * E4/E5/E6 save with the cloud already cleared recover on room re-entry. */
    sRom[0xDCECC + 0x1c] = 0xf1;
    sRom[0xDCECC + 0x1e] = 0xf0;
    sRom[0xDCECC + 0x2c] = 0xf3;
    sRom[0xDCECC + 0x2e] = 0xf2;
    {
        const u8* rewards = Port_ResolveRegionData(gUnk_080DD750 + 0x40);
        CHECK(rewards[0x1c] == 0xf1 && rewards[0x1e] == 0xf0,
              "top golden Kinstone uses EU reward/trigger flags F1/F0");
        CHECK(rewards[0x2c] == 0xf3 && rewards[0x2e] == 0xf2,
              "bottom golden Kinstone uses EU reward/trigger flags F3/F2");
    }

    gRomSize = 0xDCF1C + 0x3F;
    CHECK(Port_ResolveRegionData(gUnk_080DD7E0) == NULL, "partially truncated regional list fails closed");

    if (sFailures != 0) {
        fprintf(stderr, "port_region_data_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_region_data_test: ALL PASS");
    return 0;
}
