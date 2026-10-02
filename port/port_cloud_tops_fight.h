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

/* Detect the exact EU vanilla dead-end produced by v1.2-E7 and earlier: all
 * four prerequisite clouds and both piranha rewards are marked complete, but
 * the fifth cloud is still waiting and its required golden Kinstone is absent.
 * The predicate is intentionally read-only and fails closed for every other
 * region/progression state. */
bool32 Port_CloudTopsHasLostGoldenKinstone(const SaveFile* save);

#endif /* PORT_CLOUD_TOPS_FIGHT_H */
