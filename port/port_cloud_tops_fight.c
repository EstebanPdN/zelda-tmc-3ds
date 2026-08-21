#include "port_cloud_tops_fight.h"

#include "flags.h"

#include <string.h>

static u16 Port_CloudTopsRemapLocalFlag(u16 baselineFlag) {
#if defined(PC_PORT) && defined(MULTI_REGION)
    return (u16)Port_RemapBaselineLocalFlag(FLAG_BANK_1, baselineFlag);
#else
    return baselineFlag;
#endif
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
