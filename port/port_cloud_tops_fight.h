#ifndef PORT_CLOUD_TOPS_FIGHT_H
#define PORT_CLOUD_TOPS_FIGHT_H

#include "room.h"

/*
 * Cloud Tops Bottom enters its two piranha fights through a callback, not a
 * room-property pointer.  The original callback therefore references the
 * compiled USA EntityData list directly.  A fat build must copy and patch
 * that list before loading it: its Cloud and EntitySpawnManager both carry a
 * LocalFlags1 ordinal which differs in EU/JP.
 */
#define PORT_CLOUD_TOPS_FIGHT_ENTITY_COUNT 4u

void Port_CloudTopsPrepareTopFightEntities(EntityData* destination, const EntityData* usaTemplate);
void Port_CloudTopsPrepareBottomFightEntities(EntityData* destination, const EntityData* usaTemplate);

#endif /* PORT_CLOUD_TOPS_FIGHT_H */
