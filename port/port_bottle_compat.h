#ifndef PORT_BOTTLE_COMPAT_H
#define PORT_BOTTLE_COMPAT_H

#include "save.h"

/*
 * Builds before v1.2-E5 loaded the USA WorldEvent chest record for Smith's
 * Kinstone reward even while an EU ROM was active.  Those builds marked the
 * USA LocalFlags1 ordinal (0xB4); retail EU uses 0xB2 for the same bottle
 * chest, while 0xB4 belongs to a different Kinstone chest.
 *
 * The predicate is deliberately conservative.  It only selects a vanilla EU
 * save where Smith's fusion is complete, the stale bit is present, and the
 * EU event which legitimately owns 0xB4 has not happened.  Ambiguous saves
 * fail closed and no bottle inventory is ever removed.
 */
bool32 Port_SmithBottleFlagsNeedRepair(const SaveFile* save, bool32 randomizerActive);
bool32 Port_RepairSmithBottleFlags(SaveFile* save, bool32 randomizerActive);

/* Bottle rewards use the first unowned bottle inventory slot, regardless of
 * the bottle id stored in the chest.  Refuse the reward when all four slots
 * are already owned so the chest's completion flag is not consumed. */
bool32 Port_BottleRewardCanBeCollected(const SaveFile* save, u32 item);

#endif /* PORT_BOTTLE_COMPAT_H */
