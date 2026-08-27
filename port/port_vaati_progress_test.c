#include <stdio.h>
#include <string.h>

#include "area.h"
#include "flags.h"
#include "player.h"
#include "port_vaati_progress.h"
#include "region.h"
#include "roomid.h"

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

u32 ClearBit(void* data, u32 bit) {
    u8* bytes = data;
    bytes[bit >> 3] &= ~(1u << (bit & 7));
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
    save.saved_status.area_next = AREA_VAATI_2;
    CHECK(Port_VaatiProgressNeedsRepair(&save, FALSE), name);
    CHECK(Port_RepairVaatiProgress(&save, FALSE), name);
    CHECK(!HasFlag(&save, LV6_GUFUU1_DEMO), "repair leaves the Vaati 1 intro ready to replay");
    CHECK(HasFlag(&save, LV6_GUFUU1_GISHIKI), "repair preserves the Vaati approach flag");
    CHECK(!HasFlag(&save, LV6_GUFUU2_DEAD), "repair clears the premature Vaati 2 completion flag");
    CHECK(save.saved_status.area_next == AREA_DARK_HYRULE_CASTLE &&
              save.saved_status.room_next == ROOM_DARK_HYRULE_CASTLE_3F_TRIPLE_DARKNUT &&
              save.saved_status.start_pos_x == 0xA8 && save.saved_status.start_pos_y == 0x78 &&
              save.saved_status.layer == 1 && save.saved_status.spawn_type == PL_SPAWN_DEFAULT,
          "Vaati 2 saves return to the retail Vaati 1 checkpoint");
    CHECK(!Port_RepairVaatiProgress(&save, FALSE), "repair is one-shot after the premature bit is cleared");
}

int main(void) {
    SaveFile save;
    SaveFile backup;

    CHECK(LV6_GUFUU1_GISHIKI == 0x77u, "Vaati approach ordinal remains 0x77");
    CHECK(LV6_GUFUU1_DEMO == 0x78u, "Vaati 1 intro ordinal remains 0x78");
    CHECK(LV6_GUFUU2_DEAD == 0x7bu, "Vaati 2 completion ordinal remains 0x7B");
    CheckReportedState(TMC_REGION_USA, "USA reported Vaati state is repaired");
    CheckReportedState(TMC_REGION_EU, "European Vaati state uses the same proven repair");

    BuildReportedState(&save);
    gActiveRegion = TMC_REGION_USA;
    save.saw_staffroll = 1;
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "completed saves are never modified");

    BuildReportedState(&save);
    SetBank10Flag(&save, LV6_GUFUU1_DEMO);
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE),
          "Vaati 1 plus Vaati 2 flags are not changed without legacy-backup evidence");

    BuildReportedState(&backup);
    CHECK(Port_VaatiProgressBackupProvesLegacyRepair(&save, &backup),
          "the exact pre-E10 backup proves the legacy opposite repair");
    SetBank10Flag(&backup, LV6_KANE_START);
    CHECK(!Port_VaatiProgressBackupProvesLegacyRepair(&save, &backup),
          "a stale backup with different finale progress cannot authorize a repair");
    ClearBit(backup.flags, FLAG_BANK_10 + LV6_KANE_START);
    CHECK(Port_VaatiProgressNeedsRepair(&save, TRUE), "the proven legacy E10 state is repaired");
    CHECK(Port_RepairVaatiProgress(&save, TRUE), "the proven legacy E10 state can replay Vaati 1");
    CHECK(!HasFlag(&save, LV6_GUFUU1_DEMO) && !HasFlag(&save, LV6_GUFUU2_DEAD),
          "legacy repair reversal clears both mutually inconsistent phase bits");

    BuildReportedState(&save);
    save.saved_status.area_next = AREA_HYRULE_TOWN;
    save.saved_status.room_next = 3;
    CHECK(Port_RepairVaatiProgress(&save, FALSE), "corrupt flags are repaired outside the Vaati 2 arena");
    CHECK(save.saved_status.area_next == AREA_HYRULE_TOWN && save.saved_status.room_next == 3,
          "non-Vaati save locations remain untouched");

    BuildReportedState(&save);
    SetBank10Flag(&save, LV6_ZELDA_DISCURSE);
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "later Zelda progression makes the repair fail closed");

    BuildReportedState(&save);
    WriteBit(save.flags, FLAG_BANK_1 + ENDING);
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "ending progress makes the repair fail closed");

    BuildReportedState(&save);
    gActiveRegion = TMC_REGION_JP;
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "unproven JP state is intentionally left untouched");

    if (sFailures != 0) return 1;
    puts("port_vaati_progress_test: ALL PASS");
    return 0;
}
