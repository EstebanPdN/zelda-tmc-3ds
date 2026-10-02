#include "port_cloud_tops_fight.h"

#include "flags.h"
#include "kinstone.h"
#include "region.h"

#include <string.h>

/* Retail leaves the final four SaveFile bytes unused.  Keep a durable receipt
 * there so collecting the replayed reward cannot resemble E9 on a later boot. */
#define PORT_CLOUD_TOPS_REPLAY_MARKER_OFFSET 0x50

static const u8 sCloudTopsReplayMarker[4] = { 'C', 'T', 'R', '2' };

static bool32 Port_CloudTopsFightWasReplayed(const SaveFile* save) {
    return memcmp(&save->filler4ac[PORT_CLOUD_TOPS_REPLAY_MARKER_OFFSET], sCloudTopsReplayMarker,
                  sizeof(sCloudTopsReplayMarker)) == 0;
}

static void Port_CloudTopsMarkFightReplayed(SaveFile* save) {
    memcpy(&save->filler4ac[PORT_CLOUD_TOPS_REPLAY_MARKER_OFFSET], sCloudTopsReplayMarker,
           sizeof(sCloudTopsReplayMarker));
}

static u16 Port_CloudTopsRemapLocalFlag(u16 baselineFlag) {
#if defined(PC_PORT) && defined(MULTI_REGION)
    return (u16)Port_RemapBaselineLocalFlag(FLAG_BANK_1, baselineFlag);
#else
    return baselineFlag;
#endif
}

static bool32 Port_CloudTopsSaveFlagIsSet(const SaveFile* save, u16 baselineFlag) {
    u16 nativeFlag = Port_CloudTopsRemapLocalFlag(baselineFlag);
    return ReadBit((void*)save->flags, FLAG_BANK_1 + nativeFlag);
}

static bool32 Port_CloudTopsFusionIsDone(const SaveFile* save, KinstoneId fusion) {
    return ReadBit((void*)save->kinstones.fusedKinstones, fusion);
}

static u32 Port_CloudTopsGoldenKinstoneCount(const SaveFile* save) {
    u32 i;
    u32 count = 0;

    for (i = 0; i < 18; ++i) {
        if (save->kinstones.types[i] == PORT_CLOUD_TOPS_GOLDEN_KINSTONE) {
            count += save->kinstones.amounts[i];
        }
    }
    return count;
}

static bool32 Port_CloudTopsMatchesBrokenProgress(const SaveFile* save, u32 goldenKinstoneCount) {

    if (save == NULL || !REGION_IS_EU || save->invalid || !save->initialized ||
        Port_CloudTopsFightWasReplayed(save)) {
        return FALSE;
    }
    if (ReadBit((void*)save->flags, FLAG_BANK_0 + KUMOTATSUMAKI)) {
        return FALSE;
    }

    if (!Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_AWASE_01) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_AWASE_02) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_AWASE_03) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_AWASE_04) ||
        Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_AWASE_05) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_00) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_01) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_02) ||
        !Port_CloudTopsSaveFlagIsSet(save, KUMOUE_02_03)) {
        return FALSE;
    }

    if (!Port_CloudTopsFusionIsDone(save, KINSTONE_MYSTERIOUS_CLOUD_TOP_RIGHT) ||
        !Port_CloudTopsFusionIsDone(save, KINSTONE_MYSTERIOUS_CLOUD_BOTTOM_LEFT) ||
        !Port_CloudTopsFusionIsDone(save, KINSTONE_MYSTERIOUS_CLOUD_TOP_LEFT) ||
        !Port_CloudTopsFusionIsDone(save, KINSTONE_MYSTERIOUS_CLOUD_MIDDLE) ||
        Port_CloudTopsFusionIsDone(save, KINSTONE_MYSTERIOUS_CLOUD_BOTTOM_RIGHT)) {
        return FALSE;
    }

    return Port_CloudTopsGoldenKinstoneCount(save) == goldenKinstoneCount;
}

bool32 Port_CloudTopsBackupProvesLegacyInventoryRepair(const SaveFile* save, const SaveFile* backup) {
    if (!Port_CloudTopsMatchesBrokenProgress(save, 1) || !Port_CloudTopsMatchesBrokenProgress(backup, 0)) {
        return FALSE;
    }

    /* The permanent backup exists only because the legacy repair selected
     * this dead-end before inserting 0x66.  Allow unrelated side activities
     * since that first launch, but bind the evidence to the same named save
     * and main-story stage before removing the injected piece. */
    return save->global_progress == backup->global_progress &&
           memcmp(save->name, backup->name, sizeof(save->name)) == 0;
}

bool32 Port_CloudTopsMayNeedLegacyInventoryRepair(const SaveFile* save) {
    return Port_CloudTopsMatchesBrokenProgress(save, 1);
}

bool32 Port_CloudTopsNeedsFightReplay(const SaveFile* save, bool32 legacyInventoryRepair) {
    return Port_CloudTopsMatchesBrokenProgress(save, 0) ||
           (legacyInventoryRepair && Port_CloudTopsMatchesBrokenProgress(save, 1));
}

static bool32 Port_CloudTopsRemoveOneGoldenKinstone(SaveFile* save) {
    u32 i;

    for (i = 0; i < 18; ++i) {
        if (save->kinstones.types[i] == PORT_CLOUD_TOPS_GOLDEN_KINSTONE && save->kinstones.amounts[i] != 0) {
            if (--save->kinstones.amounts[i] == 0) {
                save->kinstones.types[i] = KINSTONE_NONE;
            }
            return TRUE;
        }
    }
    return FALSE;
}

bool32 Port_RepairCloudTopsFight(SaveFile* save, bool32 legacyInventoryRepair) {
    SaveFile original;

    if (!Port_CloudTopsNeedsFightReplay(save, legacyInventoryRepair)) {
        return FALSE;
    }

    memcpy(&original, save, sizeof(original));
    if (legacyInventoryRepair &&
        (!Port_CloudTopsRemoveOneGoldenKinstone(save) || Port_CloudTopsGoldenKinstoneCount(save) != 0)) {
        memcpy(save, &original, sizeof(original));
        return FALSE;
    }

    /* Reopen the second fight and its independently persisted pickup.  On the
     * next room load the retail managers recreate both piranhas, the cloud,
     * the revealed whirlwind, and finally the falling 0x66 reward. */
    ClearBit(save->flags, FLAG_BANK_1 + Port_CloudTopsRemapLocalFlag(KUMOUE_02_02));
    ClearBit(save->flags, FLAG_BANK_1 + Port_CloudTopsRemapLocalFlag(KUMOUE_02_03));
    Port_CloudTopsMarkFightReplayed(save);
    return TRUE;
}

static void Port_CloudTopsPrepareFightEntities(EntityData* destination, const EntityData* usaTemplate, u16 cloudFlag,
                                                u16 spawnFlag) {
    memcpy(destination, usaTemplate, sizeof(EntityData) * PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT);

    cloudFlag = Port_CloudTopsRemapLocalFlag(cloudFlag);
    spawnFlag = Port_CloudTopsRemapLocalFlag(spawnFlag);

    /* Entry 1 is the disappearing Cloud.  Entry 2 waits on that same flag
     * before loading the visual-removal list.  They must use the identical
     * active-ROM ordinal or the cloud can disappear while the ROM-native
     * HiddenWhirlwind script keeps waiting forever. */
    destination[1].type2 = (destination[1].type2 & ~0xffu) | cloudFlag;
    destination[2].spritePtr = (destination[2].spritePtr & 0x0000ffffu) | ((u32)spawnFlag << 16);
}

void Port_CloudTopsPrepareTopFightEntities(EntityData* destination, const EntityData* usaTemplate) {
    Port_CloudTopsPrepareFightEntities(destination, usaTemplate, KUMOUE_02_00, KUMOUE_02_00);
}

void Port_CloudTopsPrepareBottomFightEntities(EntityData* destination, const EntityData* usaTemplate) {
    /* The second Cloud stores its own completion flag, but its accompanying
     * EntitySpawnManager waits for completion of the first fight. */
    Port_CloudTopsPrepareFightEntities(destination, usaTemplate, KUMOUE_02_02, KUMOUE_02_00);
}
