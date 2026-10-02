#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "area.h"
#include "flags.h"
#include "player.h"
#include "port_vaati_progress.h"
#include "region.h"
#include "roomid.h"

int gActiveRegion = TMC_REGION_USA;
static int sFailures;

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                             \
        }                                            \
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

static bool32 HasBank10Flag(const SaveFile* save, u32 flag) {
    return ReadBit((void*)save->flags, FLAG_BANK_10 + flag);
}

static void BuildFinaleBase(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    save->global_progress = 9;
    SetBank10Flag(save, LV6_GUFUU1_GISHIKI);
}

static void CheckCheckpoint(const SaveFile* save, const char* message) {
    CHECK(save->saved_status.area_next == AREA_DARK_HYRULE_CASTLE &&
              save->saved_status.room_next == ROOM_DARK_HYRULE_CASTLE_3F_TRIPLE_DARKNUT &&
              save->saved_status.start_pos_x == 0xA8 && save->saved_status.start_pos_y == 0x78 &&
              save->saved_status.layer == 1 && save->saved_status.spawn_type == PL_SPAWN_DEFAULT,
          message);
}

static void CheckForwardRepair(int region, const char* name) {
    SaveFile save;

    gActiveRegion = region;
    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    CHECK(Port_VaatiProgressNeedsRepair(&save, FALSE), name);
    CHECK(Port_RepairVaatiProgress(&save, FALSE), name);
    CHECK(HasBank10Flag(&save, LV6_GUFUU1_DEMO), "completed Vaati 2 commits the Vaati 1 prerequisite");
    CHECK(HasBank10Flag(&save, LV6_GUFUU2_DEAD), "forward repair preserves Vaati 2 completion");
    CheckCheckpoint(&save, "forward repair resumes in the retail phase-3 handoff room");
    CHECK(!Port_RepairVaatiProgress(&save, FALSE), "forward repair is one-shot");
}

static void CheckReplayRepair(int region, const char* name) {
    SaveFile save;

    gActiveRegion = region;
    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU1_FIGURE);
    SetBank10Flag(&save, LV6_GUFUU2_FIGURE);
    CHECK(Port_VaatiProgressNeedsRepair(&save, FALSE), name);
    CHECK(Port_RepairVaatiProgress(&save, FALSE), name);
    CHECK(!HasBank10Flag(&save, LV6_GUFUU1_FIGURE) && !HasBank10Flag(&save, LV6_GUFUU2_FIGURE),
          "replay repair clears stale loader-only phase markers");
    CHECK(!HasBank10Flag(&save, LV6_GUFUU1_DEMO) && !HasBank10Flag(&save, LV6_GUFUU2_DEAD),
          "replay repair starts before Vaati 1");
    CheckCheckpoint(&save, "replay repair opens directly at the Vaati 1 checkpoint");
    CHECK(!Port_RepairVaatiProgress(&save, FALSE), "replay repair is one-shot");
}

static void CheckDumpFixture(void) {
    const char* path = getenv("TMC_VAATI_DUMP_SAVE");
    SaveFile save;
    FILE* file;

    if (path == NULL || path[0] == '\0') return;
    file = fopen(path, "rb");
    CHECK(file != NULL, "reported dump fixture opens");
    if (file == NULL) return;
    CHECK(fread(&save, 1, sizeof(save), file) == sizeof(save) && fgetc(file) == EOF,
          "reported dump fixture is one exact SaveFile");
    fclose(file);

    gActiveRegion = TMC_REGION_USA;
    CHECK(Port_VaatiProgressNeedsRepair(&save, FALSE), "reported E5 dump is recognized automatically");
    CHECK(Port_RepairVaatiProgress(&save, FALSE), "reported E5 dump is repaired automatically");
    CheckCheckpoint(&save, "reported E5 dump opens directly at Vaati 1");
    CHECK(!HasBank10Flag(&save, LV6_GUFUU2_FIGURE), "reported E5 dump loses its stale phase-2 marker");
}

int main(void) {
    SaveFile save;
    SaveFile backup;

    CHECK(LV6_GUFUU1_FIGURE == 0x75u, "Vaati 1 loader marker ordinal remains 0x75");
    CHECK(LV6_GUFUU2_FIGURE == 0x76u, "Vaati 2 loader marker ordinal remains 0x76");
    CHECK(LV6_GUFUU1_GISHIKI == 0x77u, "Vaati approach ordinal remains 0x77");
    CHECK(LV6_GUFUU1_DEMO == 0x78u, "Vaati 1 intro ordinal remains 0x78");
    CHECK(LV6_GUFUU2_DEAD == 0x7bu, "Vaati 2 completion ordinal remains 0x7B");
    CheckForwardRepair(TMC_REGION_USA, "USA phase-2 loop state advances to phase 3");
    CheckForwardRepair(TMC_REGION_EU, "European phase-2 loop state advances to phase 3");
    CheckReplayRepair(TMC_REGION_USA, "USA E5 stale phase marker replays Vaati 1");
    CheckReplayRepair(TMC_REGION_EU, "European E5 stale phase marker replays Vaati 1");

    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    WriteBit(save.flags, FLAG_BANK_1 + ENDING);
    CHECK(Port_VaatiProgressNeedsRepair(&save, FALSE),
          "an unrelated bank-1 bit cannot masquerade as the global ENDING flag");

    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    WriteBit(save.flags, FLAG_BANK_0 + ENDING);
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "real ending progress is never modified");

    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    save.saw_staffroll = 1;
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "completed saves are never modified");

    BuildFinaleBase(&backup);
    SetBank10Flag(&backup, LV6_GUFUU2_DEAD);
    save = backup;
    SetBank10Flag(&save, LV6_GUFUU1_DEMO);
    CHECK(Port_VaatiProgressBackupProvesLegacyRepair(&save, &backup),
          "the permanent backup still identifies the old E10 mutation");
    CHECK(!Port_VaatiProgressNeedsRepair(&save, TRUE),
          "the old E10 mutation is already the correct forward progression state");

    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    SetBank10Flag(&save, LV6_ZELDA_DISCURSE);
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "later Zelda progression makes the repair fail closed");

    BuildFinaleBase(&save);
    SetBank10Flag(&save, LV6_GUFUU2_DEAD);
    gActiveRegion = TMC_REGION_JP;
    CHECK(!Port_VaatiProgressNeedsRepair(&save, FALSE), "unproven JP state is intentionally left untouched");

    CHECK(!Port_VaatiRebornNeedsResumeDefeat(0, 0xff),
          "fresh Vaati ignores its uninitialized phase byte");
    CHECK(Port_VaatiRebornNeedsResumeDefeat(1, 3),
          "a running restored phase-3 Vaati resumes defeat");
    CHECK(!Port_VaatiRebornNeedsResumeDefeat(7, 3),
          "the defeat action is not restarted every frame");

    CheckDumpFixture();

    if (sFailures != 0) return 1;
    puts("port_vaati_progress_test: ALL PASS");
    return 0;
}
