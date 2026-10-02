#ifndef PORT_CLOUD_TOPS_FIGHT_H
#define PORT_CLOUD_TOPS_FIGHT_H

#include "room.h"
#include "save.h"

/*
 * Cloud Tops Bottom enters its two piranha fights through a callback, not a
 * room-property pointer.  The original callback therefore references the
 * compiled USA EntityData list directly.  A fat build must copy and patch
 * that list before loading it: its Cloud and EntitySpawnManager both carry a
 * LocalFlags1 ordinal which differs in EU/JP.
 */
#define PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT 4u
#define PORT_CLOUD_TOPS_GOLDEN_KINSTONE 0x66u

void Port_CloudTopsPrepareTopFightEntities(EntityData* destination, const EntityData* usaTemplate);
void Port_CloudTopsPrepareBottomFightEntities(EntityData* destination, const EntityData* usaTemplate);

/* Detect the exact EU vanilla dead-end produced by v1.2-E7 and earlier.  The
 * legacy v1.2-E9 repair inserted the missing Kinstone directly into the bag;
 * its permanent backup is required before that insertion may be undone. */
bool32 Port_CloudTopsBackupProvesLegacyInventoryRepair(const SaveFile* save, const SaveFile* backup);
bool32 Port_CloudTopsMayNeedLegacyInventoryRepair(const SaveFile* save);
bool32 Port_CloudTopsNeedsFightReplay(const SaveFile* save, bool32 legacyInventoryRepair);

/* Restore retail behavior by reopening the second piranha fight and its
 * falling reward.  When legacyInventoryRepair is true, exactly the one piece
 * proven to have been inserted by the old repair is removed first. */
bool32 Port_RepairCloudTopsFight(SaveFile* save, bool32 legacyInventoryRepair);

#endif /* PORT_CLOUD_TOPS_FIGHT_H */
