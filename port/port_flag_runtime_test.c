#include <stdio.h>
#include <string.h>

#include "area.h"
#include "flag_remap_generated.h"
#include "flags.h"
#include "room.h"
#include "save.h"
#include "region.h"

Area gArea;
SaveFile gSave;
RoomVars gRoomVars;
int gActiveRegion = TMC_REGION_USA;

static int sFailures;

#define CHECK(condition, description)                                                  \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "FAIL: %s (line %d)\n", (description), __LINE__);         \
            ++sFailures;                                                               \
        }                                                                              \
    } while (0)

u32 ReadBit(void* data, u32 bit) {
    const u8* bytes = data;
    return (bytes[bit >> 3] >> (bit & 7)) & 1;
}

u32 CheckBits(void* data, u32 bit, u32 count) {
    u32 i;
    for (i = 0; i < count; ++i) {
        if (!ReadBit(data, bit + i)) {
            return FALSE;
        }
    }
    return TRUE;
}

bool32 CheckLocalFlagByBank(u32 offset, u32 flag) {
    return ReadBit(gSave.flags, offset + flag);
}

u32 WriteBit(void* data, u32 bit) {
    u8* bytes = data;
    bytes[bit >> 3] |= 1u << (bit & 7);
    return 1;
}

u32 ClearBit(void* data, u32 bit) {
    u8* bytes = data;
    bytes[bit >> 3] &= ~(1u << (bit & 7));
    return 0;
}

static void ResetFlags(u32 bank) {
    memset(gSave.flags, 0, sizeof(gSave.flags));
    gArea.localFlagOffset = bank;
}

int main(void) {
    u32 packedTwoBits = 10u | (1u << 10);

    gActiveRegion = TMC_REGION_EU;
    ResetFlags(FLAG_BANK_1);
    CHECK(Port_RemapBaselineLocalFlag(FLAG_BANK_1, KUMOUE_02_00) == 240,
          "Cloud Tops semantic flag maps 243 USA to 240 EU");
    SetLocalFlagB(KUMOUE_02_00);
    CHECK(ReadBit(gSave.flags, FLAG_BANK_1 + 240), "baseline helper writes the EU Cloud Tops bit");
    CHECK(!ReadBit(gSave.flags, FLAG_BANK_1 + KUMOUE_02_00), "baseline helper never writes USA bit 243 on EU");

    ResetFlags(FLAG_BANK_2);
    WriteBit(gSave.flags, FLAG_BANK_2 + SHOP00_BOMBBAG);
    CHECK(Port_RemapBaselineLocalFlag(FLAG_BANK_2, SHOP00_BOMBBAG) == PORT_FLAG_REMAP_INVALID,
          "USA-only bank-2 flag has no invented EU equivalent");
    CHECK(!CheckLocalFlagB(SHOP00_BOMBBAG), "invalid baseline check fails closed");
    ClearLocalFlagB(SHOP00_BOMBBAG);
    CHECK(ReadBit(gSave.flags, FLAG_BANK_2 + SHOP00_BOMBBAG), "invalid baseline clear is a no-op");
    memset(gSave.flags, 0, sizeof(gSave.flags));
    SetLocalFlagB(SHOP00_BOMBBAG);
    CHECK(!ReadBit(gSave.flags, FLAG_BANK_2 + SHOP00_BOMBBAG), "invalid baseline set is a no-op");

    ResetFlags(FLAG_BANK_1);
    CHECK(gFlagRemapEU[0][10] == 10 && gFlagRemapEU[0][11] == 246,
          "test range crosses a non-contiguous EU remap");
    WriteBit(gSave.flags, FLAG_BANK_1 + 10);
    WriteBit(gSave.flags, FLAG_BANK_1 + 246);
    CHECK(CheckLocalFlagsB(10, 2), "multi-bit helper accepts a fully set non-contiguous semantic range");
    CHECK(CheckFlagsB(packedTwoBits), "packed multi-bit helper uses semantic per-bit remapping");
    ClearBit(gSave.flags, FLAG_BANK_1 + 246);
    CHECK(!CheckLocalFlagsB(10, 2), "multi-bit helper rejects a partially set semantic range");

    gActiveRegion = TMC_REGION_USA;
    ResetFlags(FLAG_BANK_2);
    SetLocalFlagB(SHOP00_BOMBBAG);
    CHECK(ReadBit(gSave.flags, FLAG_BANK_2 + SHOP00_BOMBBAG), "USA retains its USA-only flag unchanged");

    if (sFailures != 0) {
        fprintf(stderr, "port_flag_runtime_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_flag_runtime_test: ALL PASS");
    return 0;
}
