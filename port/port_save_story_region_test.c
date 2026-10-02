#include <stdio.h>
#include <string.h>

#include "flag_remap_generated.h"
#include "flags.h"
#include "port_fusion_marker.h"
#include "port_rom.h"
#include "port_story_guard.h"
#include "region.h"

int gActiveRegion = TMC_REGION_USA;
static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned long long got__ = (unsigned long long)(actual);                                                 \
        unsigned long long want__ = (unsigned long long)(expected);                                              \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%llX expected 0x%llX\n", message, got__, want__);                  \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

#define CHECK(condition, message)                                  \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr, "FAIL: %s\n", message);              \
            sFailures++;                                           \
        }                                                          \
    } while (0)

static void CheckMarkerReward(u32 condition, u32 expectedBank, u32 expectedFlag, const char* message) {
    PortFusionMarkerReward reward = Port_FusionMarkerRewardForCondition(condition);
    CHECK(reward.valid, message);
    CHECK_EQ(reward.bank, expectedBank, message);
    CHECK_EQ(reward.flag, expectedFlag, message);
}

int main(void) {
    u8 rom[16 + (PORT_FUSER_ENTITY_RECORD_LIMIT + 2) * PORT_FUSER_ENTITY_RECORD_SIZE];
    u8 fuserRecord[PORT_FUSER_FUSION_RECORD_BYTES] = { 2, 100, 10, 30, 60, 15, 0 };
    u64 packed;
    u32 i;

    CHECK(Port_ShouldRunZeldaIntro(false, false), "unfinished opening escort still starts normally");
    CHECK(!Port_ShouldRunZeldaIntro(true, false), "completed opening escort never starts twice");
    CHECK(!Port_ShouldRunZeldaIntro(false, true), "TABIDACHI suppresses impossible Zelda-intro replay");
    CHECK(!Port_ShouldRunZeldaIntro(true, true), "fully progressed save keeps Zelda intro suppressed");

    CHECK_EQ(SOUGEN_01_ZELDA, 109u, "fat binary uses the USA baseline Zelda flag ordinal");
    CHECK_EQ(gFlagRemapEU[0][SOUGEN_01_ZELDA], 107u,
             "EU maps baseline SOUGEN_01_ZELDA to its native ordinal");

    for (i = CND_5; i <= CND_10; ++i) {
        CHECK_EQ(Port_SelectFusionMarkerCondition(CND_0, i), i,
                 "EU/JP CND_0 marker inherits only a USA special completion condition");
    }
    CHECK_EQ(Port_SelectFusionMarkerCondition(CND_1, CND_1), CND_1,
             "ordinary regional marker condition remains region-native");
    CheckMarkerReward(CND_5, LOCAL_BANK_3, SORA_10_H00, "Crenel beanstalk retires on its heart piece");
    CheckMarkerReward(CND_6, LOCAL_BANK_3, SORA_11_H00, "Lake Hylia beanstalk retires on its heart piece");
    CheckMarkerReward(CND_7, LOCAL_BANK_3, SORA_12_T00, "Wind Ruins beanstalk retires on its big chest");
    CheckMarkerReward(CND_8, LOCAL_BANK_3, SORA_13_H00, "Eastern Hills beanstalk retires on its heart piece");
    CheckMarkerReward(CND_9, LOCAL_BANK_3, SORA_14_T00, "Western Woods beanstalk retires on its chest");
    CheckMarkerReward(CND_10, LOCAL_BANK_4, KS_B15, "Gina grave marker retires on its reward flag");
    CHECK(!Port_FusionMarkerRewardForCondition(CND_4).valid,
          "ordinary inventory marker is not reinterpreted as a regional reward");

    memset(rom, 0, sizeof(rom));
    rom[16 + 6] = 0x49; /* WindTribespeople visitor */
    rom[16 + 7] = 0;
    rom[16 + 8] = 0;
    rom[16 + 9] = 0x30;
    rom[16 + 10] = 0x08;
    rom[16 + 11] = 0x02;
    packed = Port_FindEntityFuserDataFromRom(rom, sizeof(rom), 16, 0x49, 0, 0);
    CHECK_EQ((u32)packed, 0x30u, "bounded entity table finds Wind visitor fuser id");
    CHECK_EQ((u32)(packed >> 32), 0x0208u, "bounded entity table finds Wind visitor fusion text");
    CHECK_EQ(Port_FindEntityFuserDataFromRom(rom, sizeof(rom), 16, 0x4E, 6, 0), 0u,
             "bounded entity table stops at its terminator");

    rom[16 + 9] = PORT_FUSER_TABLE_COUNT;
    CHECK_EQ(Port_FindEntityFuserDataFromRom(rom, sizeof(rom), 16, 0x49, 0, 0), 0u,
             "entity table rejects fuser ids outside the 120-entry pointer table");
    CHECK_EQ(Port_FindEntityFuserDataFromRom(rom, 16 + 11, 16, 0x49, 0, 0), 0u,
             "entity table rejects a truncated six-byte record");

    memset(rom, 0x7F, sizeof(rom));
    for (i = 1; i <= PORT_FUSER_ENTITY_RECORD_LIMIT; ++i) {
        rom[16 + i * 6] = 0x55;
    }
    CHECK_EQ(Port_FindEntityFuserDataFromRom(rom, sizeof(rom), 16, 0x49, 0, 0), 0u,
             "entity table without a terminator stops at the retail safety cap");

    CHECK(Port_IsFuserSaveStateValid(fuserRecord, 0, 0), "fresh one-off fuser state is valid");
    CHECK(Port_IsFuserSaveStateValid(fuserRecord, 1, 0), "cursor may rest on the list terminator");
    CHECK(!Port_IsFuserSaveStateValid(fuserRecord, 2, 0), "saved cursor beyond terminator is rejected");
    CHECK(!Port_IsFuserSaveStateValid(fuserRecord, 1, 0xF2),
          "JUST_FUSED cannot advance beyond the list terminator");
    CHECK(!Port_IsFuserSaveStateValid(fuserRecord, 0, 0x80), "unknown saved offer value is rejected");
    memset(fuserRecord + 5, 0xFF, PORT_FUSER_FUSION_MAX_OFFERS + 1);
    CHECK(!Port_IsFuserSaveStateValid(fuserRecord, 0, 0), "offer list missing its bounded terminator is rejected");

    if (sFailures != 0) return 1;
    puts("port_save_story_region_test: ALL PASS");
    return 0;
}
