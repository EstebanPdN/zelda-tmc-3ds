#include "flags.h"
#include "manager.h"
#include "object.h"
#include "port_cloud_tops_fight.h"
#include "region.h"
#include "kinstone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gActiveRegion;
static int sFailures;

u32 Port_RemapBaselineLocalFlag(u32 bankOffset, u32 ordinal) {
    if (bankOffset != FLAG_BANK_1 || ordinal < KUMOUE_02_AWASE_01 || ordinal > KUMOUE_02_03) {
        fprintf(stderr, "unexpected remap request: bank=%u ordinal=%u\n", bankOffset, ordinal);
        sFailures++;
        return ordinal;
    }
    if (gActiveRegion == TMC_REGION_USA) return ordinal;
    return ordinal - 3;
}

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

#define CHECK(condition, message)                  \
    do {                                           \
        if (!(condition)) {                        \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                           \
        }                                          \
    } while (0)

static const EntityData sUsaCloudTopsTopFight[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT] = {
    { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000a, 0, 0, 0x80000000 },
    { OBJECT, 0x0f, CLOUD, 0x00, KUMOUE_02_00, 0x208, 0x038, 0x00000000 },
    { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0c, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
    { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
};

static const EntityData sUsaCloudTopsBottomFight[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT] = {
    { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000b, 0, 0, 0x80080000 },
    { OBJECT, 0x0f, CLOUD, 0x00, 0x000008f5, 0x238, 0x2e8, 0x00000000 },
    { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0d, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
    { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
};

static void RunTopRegion(int region, u16 expectedFlag, const char* name) {
    EntityData patched[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT];

    gActiveRegion = region;
    Port_CloudTopsPrepareTopFightEntities(patched, sUsaCloudTopsTopFight);

    CHECK((u16)patched[1].type2 == expectedFlag, name);
    CHECK((u16)(patched[2].spritePtr >> 16) == expectedFlag, name);
    CHECK(patched[0].type2 == sUsaCloudTopsTopFight[0].type2, "fight manager remains retail-identical");
    CHECK(patched[1].xPos == 0x208 && patched[1].yPos == 0x038, "cloud coordinates remain retail-identical");
    CHECK(patched[2].type == 0x0c, "spawn manager still loads property 12");
    CHECK(patched[3].kind == 0xff, "entity-list sentinel remains intact");
    CHECK(memcmp(sUsaCloudTopsTopFight, (const EntityData[]) {
              { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000a, 0, 0, 0x80000000 },
              { OBJECT, 0x0f, CLOUD, 0x00, KUMOUE_02_00, 0x208, 0x038, 0x00000000 },
              { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0c, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
              { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
          }, sizeof(sUsaCloudTopsTopFight)) == 0,
          "USA template is never mutated");
}

static void RunBottomRegion(int region, u16 expectedCloudFlag, u16 expectedSpawnFlag, const char* name) {
    EntityData patched[PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT];

    gActiveRegion = region;
    Port_CloudTopsPrepareBottomFightEntities(patched, sUsaCloudTopsBottomFight);

    CHECK((patched[1].type2 & 0xffu) == expectedCloudFlag, name);
    CHECK((u16)(patched[2].spritePtr >> 16) == expectedSpawnFlag, name);
    CHECK(patched[0].type2 == sUsaCloudTopsBottomFight[0].type2, "bottom fight manager remains retail-identical");
    CHECK(patched[1].xPos == 0x238 && patched[1].yPos == 0x2e8, "bottom cloud coordinates remain retail-identical");
    CHECK(patched[2].type == 0x0d, "bottom spawn manager still loads property 13");
    CHECK(patched[3].kind == 0xff, "bottom entity-list sentinel remains intact");
    CHECK(memcmp(sUsaCloudTopsBottomFight, (const EntityData[]) {
              { MANAGER, 0x00, FIGHT_MANAGER, 0x00, 0x0000000b, 0, 0, 0x80080000 },
              { OBJECT, 0x0f, CLOUD, 0x00, 0x000008f5, 0x238, 0x2e8, 0x00000000 },
              { MANAGER, 0x1f, ENTITY_SPAWN_MANAGER, 0x0d, 0x00000000, 0, 0, KUMOUE_02_00 << 16 },
              { 0xff, 0x00, 0x00, 0x00, 0x00000000, 0, 0, 0x00000000 },
          }, sizeof(sUsaCloudTopsBottomFight)) == 0,
          "bottom USA template is never mutated");
}

static void SetEuCloudFlag(SaveFile* save, u16 baselineFlag) {
    WriteBit(save->flags, FLAG_BANK_1 + baselineFlag - 3);
}

static bool32 HasEuCloudFlag(const SaveFile* save, u16 baselineFlag) {
    return ReadBit((void*)save->flags, FLAG_BANK_1 + baselineFlag - 3);
}

static void BuildReportedLostRewardSave(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    SetEuCloudFlag(save, KUMOUE_02_AWASE_01);
    SetEuCloudFlag(save, KUMOUE_02_AWASE_02);
    SetEuCloudFlag(save, KUMOUE_02_AWASE_03);
    SetEuCloudFlag(save, KUMOUE_02_AWASE_04);
    SetEuCloudFlag(save, KUMOUE_02_00);
    SetEuCloudFlag(save, KUMOUE_02_01);
    SetEuCloudFlag(save, KUMOUE_02_02);
    SetEuCloudFlag(save, KUMOUE_02_03);
    WriteBit(save->kinstones.fusedKinstones, KINSTONE_MYSTERIOUS_CLOUD_TOP_RIGHT);
    WriteBit(save->kinstones.fusedKinstones, KINSTONE_MYSTERIOUS_CLOUD_BOTTOM_LEFT);
    WriteBit(save->kinstones.fusedKinstones, KINSTONE_MYSTERIOUS_CLOUD_TOP_LEFT);
    WriteBit(save->kinstones.fusedKinstones, KINSTONE_MYSTERIOUS_CLOUD_MIDDLE);
}

static void RunLostRewardFightReplay(void) {
    SaveFile save;
    SaveFile backup;
    SaveFile mismatchedBackup;

    gActiveRegion = TMC_REGION_EU;
    BuildReportedLostRewardSave(&save);
    memcpy(save.name, "CLOUD", 5);
    save.global_progress = 7;
    save.kinstones.fusedCount = 43;
    CHECK(Port_CloudTopsNeedsFightReplay(&save, FALSE),
          "exact pre-E9 EU dead-end is selected for a retail fight replay");
    CHECK(Port_RepairCloudTopsFight(&save, FALSE), "untreated dead-end reopens the second fight");
    CHECK(HasEuCloudFlag(&save, KUMOUE_02_00) && HasEuCloudFlag(&save, KUMOUE_02_01),
          "first fight and collected reward remain complete");
    CHECK(!HasEuCloudFlag(&save, KUMOUE_02_02) && !HasEuCloudFlag(&save, KUMOUE_02_03),
          "second fight and pickup are both reopened");
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE), "replayed state is not repaired twice");
    SetEuCloudFlag(&save, KUMOUE_02_02);
    SetEuCloudFlag(&save, KUMOUE_02_03);
    save.kinstones.types[0] = PORT_CLOUD_TOPS_GOLDEN_KINSTONE;
    save.kinstones.amounts[0] = 1;
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, TRUE),
          "collecting the real replayed reward cannot trigger another repair on relaunch");

    BuildReportedLostRewardSave(&backup);
    memcpy(backup.name, "CLOUD", 5);
    backup.global_progress = 7;
    backup.kinstones.fusedCount = 43;
    save = backup;
    save.kinstones.types[0] = PORT_CLOUD_TOPS_GOLDEN_KINSTONE;
    save.kinstones.amounts[0] = 1;
    CHECK(Port_CloudTopsMayNeedLegacyInventoryRepair(&save),
          "legacy inventory signature requests permanent-backup evidence");
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE),
          "an inventory piece is never removed without permanent-backup proof");
    CHECK(Port_CloudTopsBackupProvesLegacyInventoryRepair(&save, &backup),
          "the pre-E9 permanent backup proves the direct inventory insertion");
    CHECK(Port_CloudTopsNeedsFightReplay(&save, TRUE), "backup-proven E9 save is selected for replay");
    CHECK(Port_RepairCloudTopsFight(&save, TRUE), "backup-proven E9 insertion is replaced by the real fight");
    CHECK(save.kinstones.types[0] == KINSTONE_NONE && save.kinstones.amounts[0] == 0,
          "only the legacy inserted piece is removed");
    CHECK(!HasEuCloudFlag(&save, KUMOUE_02_02) && !HasEuCloudFlag(&save, KUMOUE_02_03),
          "legacy E9 save also reopens the second fight and pickup");

    save = backup;
    save.kinstones.types[0] = PORT_CLOUD_TOPS_GOLDEN_KINSTONE;
    save.kinstones.amounts[0] = 1;
    mismatchedBackup = backup;
    memcpy(mismatchedBackup.name, "OTHER", 5);
    CHECK(!Port_CloudTopsBackupProvesLegacyInventoryRepair(&save, &mismatchedBackup),
          "a backup from another slot cannot authorize inventory removal");
    CHECK(!Port_RepairCloudTopsFight(&save, FALSE), "unproven inventory remains untouched");

    mismatchedBackup = backup;
    mismatchedBackup.global_progress++;
    CHECK(!Port_CloudTopsBackupProvesLegacyInventoryRepair(&save, &mismatchedBackup),
          "a backup from another main-story stage cannot authorize inventory removal");

    save.kinstones.types[1] = 0x71;
    save.kinstones.amounts[1] = 2;
    WriteBit(save.kinstones.fusedKinstones, 40);
    save.kinstones.fusedCount++;
    CHECK(Port_CloudTopsBackupProvesLegacyInventoryRepair(&save, &backup),
          "unrelated inventory and side fusions since E9 do not strand the save");

    save = backup;
    save.kinstones.types[0] = PORT_CLOUD_TOPS_GOLDEN_KINSTONE;
    save.kinstones.amounts[0] = 2;
    CHECK(!Port_CloudTopsBackupProvesLegacyInventoryRepair(&save, &backup),
          "more than the one legacy insertion fails closed");

    BuildReportedLostRewardSave(&save);
    WriteBit(save.kinstones.fusedKinstones, KINSTONE_MYSTERIOUS_CLOUD_BOTTOM_RIGHT);
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE), "completed fifth fusion is never repaired");

    BuildReportedLostRewardSave(&save);
    SetEuCloudFlag(&save, KUMOUE_02_AWASE_05);
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE), "spinning fifth pinwheel is never repaired");

    BuildReportedLostRewardSave(&save);
    WriteBit(save.flags, FLAG_BANK_0 + KUMOTATSUMAKI);
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE), "completed tornado event is never repaired");

    BuildReportedLostRewardSave(&save);
    gActiveRegion = TMC_REGION_USA;
    CHECK(!Port_CloudTopsNeedsFightReplay(&save, FALSE), "USA saves are outside the proven EU repair signature");
}

static bool32 ReadFixture(const char* path, SaveFile* save) {
    FILE* file;

    if (path == NULL || path[0] == '\0') return FALSE;
    file = fopen(path, "rb");
    if (file == NULL) return FALSE;
    if (fread(save, 1, sizeof(*save), file) != sizeof(*save) || fgetc(file) != EOF) {
        fclose(file);
        return FALSE;
    }
    fclose(file);
    return TRUE;
}

static void RunSuppliedSaveFixtures(void) {
    const char* currentPath = getenv("TMC_CLOUD_TOPS_CURRENT_SAVE");
    const char* backupPath = getenv("TMC_CLOUD_TOPS_BACKUP_SAVE");
    SaveFile current;
    SaveFile backup;

    if (currentPath == NULL && backupPath == NULL) return;
    CHECK(ReadFixture(currentPath, &current), "supplied current Cloud Tops slot fixture opens");
    CHECK(ReadFixture(backupPath, &backup), "supplied pre-repair Cloud Tops slot fixture opens");
    if (sFailures != 0) return;

    gActiveRegion = TMC_REGION_EU;
    CHECK(Port_CloudTopsBackupProvesLegacyInventoryRepair(&current, &backup),
          "supplied reported save proves the v1.2-E9 direct inventory insertion");
    CHECK(Port_RepairCloudTopsFight(&current, TRUE),
          "supplied reported save is repaired by replaying the fight");
    CHECK(!HasEuCloudFlag(&current, KUMOUE_02_02) && !HasEuCloudFlag(&current, KUMOUE_02_03),
          "supplied reported save reopens the exact second fight and reward flags");
}

int main(void) {
    RunTopRegion(TMC_REGION_USA, KUMOUE_02_00, "top USA keeps retail ordinal 243");
    RunTopRegion(TMC_REGION_EU, 0xf0, "top EU uses the ROM-native ordinal 240");
    RunTopRegion(TMC_REGION_JP, 0xf0, "top JP uses the ROM-native ordinal 240");
    RunBottomRegion(TMC_REGION_USA, KUMOUE_02_02, KUMOUE_02_00,
                    "bottom USA keeps retail ordinals 245 and 243");
    RunBottomRegion(TMC_REGION_EU, 0xf2, 0xf0,
                    "bottom EU uses ROM-native ordinals 242 and 240");
    RunBottomRegion(TMC_REGION_JP, 0xf2, 0xf0,
                    "bottom JP uses ROM-native ordinals 242 and 240");
    RunLostRewardFightReplay();
    RunSuppliedSaveFixtures();

    if (sFailures != 0) return 1;
    puts("port_cloud_tops_fight_test: ALL PASS");
    return 0;
}
