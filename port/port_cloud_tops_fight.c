#include "port_cloud_tops_fight.h"

#include "flags.h"
#include "kinstone.h"
#include "region.h"

#include <string.h>

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

bool32 Port_CloudTopsHasLostGoldenKinstone(const SaveFile* save) {
    u32 i;

    if (save == NULL || !REGION_IS_EU || save->invalid || !save->initialized) {
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

    for (i = 0; i < 18; ++i) {
        if (save->kinstones.types[i] == PORT_CLOUD_TOPS_GOLDEN_KINSTONE && save->kinstones.amounts[i] != 0) {
            return FALSE;
        }
    }
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
