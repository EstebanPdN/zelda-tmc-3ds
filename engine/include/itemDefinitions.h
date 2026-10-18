#ifndef ITEMDEFINITIONS_H
#define ITEMDEFINITIONS_H

#include "global.h"

typedef struct {
    /*0x00*/ bool8 isOnlyActiveFirstFrame; /**< Is the behavior for this item only created on the first frame */
    /*0x01*/ u8 priority;
    /*0x02*/ u8 createFunc;
    /*0x03*/ u8 playerItemId; /**< Id for the corresponsing PlayerItem. */
    /*0x04*/ u16 frameIndex;
    /*0x06*/ u8 animPriority;
    /*0x07*/ bool8 isChangingAttackStatus;
    /*0x08*/ bool8 isUseableAsMinish;
    /*0x09*/ u8 pad[3];
} ItemDefinition;

static_assert(sizeof(ItemDefinition) == 0xc);

#endif // ITEMDEFINITIONS_H
