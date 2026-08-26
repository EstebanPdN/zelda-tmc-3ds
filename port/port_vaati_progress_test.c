#include <stdio.h>
#include <string.h>

#include "flags.h"
#include "port_vaati_progress.h"
#include "region.h"

int gActiveRegion = TMC_REGION_USA;
static int sFailures;

#define CHECK(condition, message)                   \
    do {                                            \
        if (!(condition)) {                         \
            fprintf(stderr, "FAIL: %s\\n", message); \
            ++sFailures;                            \
        }                                           \
    } while (0)

u32 ReadBit(void* data, u32 bit) {
    const u8* bytes = data;
    return (bytes[bit >> 3] >> (bit & 7)) & 1u;
}

u32 WriteBit(void* data, u32 bit) {
    u8* bytes = data;
    bytes[bit >> 3] |= 1u << (bit & 7);
    return 1;
}

static void SetBank10Flag(SaveFile* save, u32 flag) {
    WriteBit(save->flags, FLAG_BANK_10 + flag);
}

static bool32 HasFlag(const SaveFile* save, u32 flag) {
    return ReadBit((void*)save->flags, FLAG_BANK_10 + flag);
}

static void BuildReportedState(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    SetBank10Flag(save, LV6_GUFUU1_GISHIKI);
    SetBank10Flag(save, LV6_GUFUU2_DEAD);
}

static void CheckReportedState(int region, const char* name) {
    SaveFile save;

    gActiveRegion = region;
    BuildReportedState(&save);
    CHECK(Port_VaatiProgressNeedsRepair(&save), name);
    CHECK(Port_RepairVaatiProgress(&save), name);
    CHECK(HasFlag(&save, LV6_GUFUU1_DEMO), "repair restores only the missing Vaati 1 intro flag");
    CHECK(HasFlag(&save, LV6_GUFUU1_GISHIKI), "repair preserves the Vaati approach flag");
    CHECK(HasFlag(&save, LV6_GUFUU2_DEAD), "repair preserves the Vaati 2 completion flag");
    CHECK(!Port_RepairVaatiProgress(&save), "repair is one-shot after its prerequisite is restored");
}

int main(void) {
    SaveFile save;

    CHECK(LV6_GUFUU1_GISHIKI == 0x77u, "Vaati approach ordinal remains 0x77");
    CHECK(LV6_GUFUU1_DEMO == 0x78u, "Vaati 1 intro ordinal remains 0x78");
    CHECK(LV6_GUFUU2_DEAD == 0x7bu, "Vaati 2 completion ordinal remains 0x7B");
    CheckReportedState(TMC_REGION_USA, "USA reported Vaati state is repaired");
    CheckReportedState(TMC_REGION_EU, "European Vaati state uses the same proven repair");

    BuildReportedState(&save);
    gActiveRegion = TMC_REGION_USA;
    save.saw_staffroll = 1;
    CHECK(!Port_VaatiProgressNeedsRepair(&save), "completed saves are never modified");

    BuildReportedState(&save);
    SetBank10Flag(&save, LV6_GUFUU1_DEMO);
    CHECK(!Port_VaatiProgressNeedsRepair(&save), "valid in-progress Vaati state is never modified");

    BuildReportedState(&save);
    gActiveRegion = TMC_REGION_JP;
    CHECK(!Port_VaatiProgressNeedsRepair(&save), "unproven JP state is intentionally left untouched");

    if (sFailures != 0) return 1;
    puts("port_vaati_progress_test: ALL PASS");
    return 0;
}
