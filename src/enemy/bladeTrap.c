/**
 * @file bladeTrap.c
 * @ingroup Enemies
 *
 * @brief Blade Trap enemy
 */
#include "object.h"
#include "sound.h"
#include "room.h"
#include "physics.h"

typedef struct {
    /*0x00*/ Entity base;
#if defined(PC_PORT) && (__SIZEOF_POINTER__ == 8)
    /*0x68*/ u8 unused1[12 + 4];
#else
    /*0x68*/ u8 unused1[12];
#endif
    /*0x74*/ u16 unk_74;
} BladeTrapEntity;

PORT_STATIC_ASSERT_OFFSET(BladeTrapEntity, unk_74, 0x74, 0xA0,
                          "BladeTrapEntity unk_74 offset incorrect");

#define DIR_NOT_MOVING_CHECK 0x80

void BladeTrap(BladeTrapEntity* this) {
    if (super->action == 0) {
        super->action = 1;
        super->child = GetCurrentRoomProperty(super->type);
        UpdateRailMovement(super, (u16**)&super->child, &this->unk_74);
    }
    if (!(super->direction & DIR_NOT_MOVING_CHECK)) {
        LinearMoveUpdate(super);
    }

    if (!(--this->unk_74)) {
        if (!(super->direction & DIR_NOT_MOVING_CHECK)) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
        UpdateRailMovement(super, (u16**)&super->child, &this->unk_74);
    }
}
