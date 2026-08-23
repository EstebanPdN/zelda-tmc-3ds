#include "port_region_data.h"

#include <stddef.h>
#include <stdint.h>

#include "port_rom.h"
#include "port_types.h"
#include "region.h"

/* Compiled stubs use the USA baserom layout.  Keep this registry intentionally
 * exact: each EU offset below was verified against the corresponding symbol in
 * the retail EU map/entity data. */
extern const u8 gUnk_080D9328[];
extern const u8 gUnk_080DD7E0[];
extern const u8 gUnk_080DD840[];
extern const u8 gUnk_080EAE60[];
extern const u8 gUnk_080EB9F4[];
extern const u8 gUnk_080F58A8[];
extern const u8 gUnk_080F5B3C[];
extern const u8 gUnk_080F78A0[];
extern const u8 gUnk_080F9BF8[];
extern const u8 gUnk_080F09A0[];
extern const u8 gUnk_080FEAC8[];
extern const u8 gUnk_080FEE58[];

#define REGION_DATA_UNAVAILABLE UINT32_MAX

typedef struct {
    const void* usaData;
    u32 euOffset;
    u32 size;
} RegionDataEntry;

static const RegionDataEntry sRegionDataEntries[] = {
    { gUnk_080D9328, 0x000D8A84, 0x10 },
    { gUnk_080DD7E0, 0x000DCF1C, 0x40 },
    { gUnk_080DD840, 0x000DCF7C, 0x40 },
    { gUnk_080EAE60, 0x000EA53C, 0x50 },
    { gUnk_080EB9F4, 0x000EB0C0, 0x70 },
    { gUnk_080F58A8, REGION_DATA_UNAVAILABLE, 0 },
    { gUnk_080F5B3C, REGION_DATA_UNAVAILABLE, 0 },
    { gUnk_080F78A0, 0x000F6E5C, 0x20 },
    { gUnk_080F9BF8, 0x000F9144, 0x40 },
    { gUnk_080F09A0, 0x000EFFD4, 0x60 },
    { gUnk_080FEAC8, 0x000FE00C, 0x120 },
    { gUnk_080FEE58, 0x000FE39C, 0x20 },
};

static int Port_IsRomDataPointer(const void* data) {
    uintptr_t address;
    uintptr_t rom;

    if (data == NULL || gRomData == NULL) {
        return 0;
    }
    address = (uintptr_t)data;
    rom = (uintptr_t)gRomData;
    return address >= rom && address - rom < gRomSize;
}

const void* Port_ResolveRegionData(const void* data) {
    size_t i;

    if (data == NULL || Port_IsRomDataPointer(data) || !REGION_IS_EU) {
        return data;
    }

    for (i = 0; i < sizeof(sRegionDataEntries) / sizeof(sRegionDataEntries[0]); ++i) {
        const RegionDataEntry* entry = &sRegionDataEntries[i];
        if (data != entry->usaData) {
            continue;
        }
        if (entry->euOffset == REGION_DATA_UNAVAILABLE || gRomData == NULL || entry->euOffset > gRomSize ||
            entry->size > gRomSize - entry->euOffset) {
            return NULL;
        }
        return gRomData + entry->euOffset;
    }

    return data;
}
