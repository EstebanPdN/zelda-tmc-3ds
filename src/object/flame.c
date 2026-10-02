/**
 * @file flame.c
 * @ingroup Objects
 *
 * @brief Flame object
 */
#include "entity.h"
#include "flags.h"
#include "physics.h"
#include "sound.h"
#include "asm.h"
#include "room.h"
#include "player.h"

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[30];
    /*0x86*/ u16 flag;
} FlameEntity;

extern void sub_0807AB44(Entity*, s32, s32);

void (*const Flame_Actions[])(FlameEntity*);

void Flame(FlameEntity* this) {
    Flame_Actions[super->action](this);
}

void Flame_Init(FlameEntity* this) {
    super->action = 1;
    super->spriteSettings.draw = TRUE;
    if (super->type2 != 0) {
        super->timer = super->type2;
    }
    InitializeAnimation(super, 0);
    switch (super->type) {
        case 1:
            super->y.HALF.HI -= 8;
            super->timer = 40;
            break;
        case 2:
            super->timer = 15;
            SnapToTile(super);
            break;
        case 3:
            CopyPosition(super->parent, super);
            break;
        case 4:
            if (!CheckFlags(this->flag)) {
                super->spriteSettings.draw = FALSE;
                super->subAction = 1;
                return;
            }
    }
    EnqueueSFX(SFX_124);
}

void Flame_Action1(FlameEntity* this) {
    u32 val;
    GetNextFrame(super);

    switch (super->type) {
        case 0:
        default:
            if (super->type2 == 0)
                return;
        case 1:
        case 2:
            if (super->timer-- != 0)
                return;
            if (super->type == 2) {
                sub_0807B7D8(((u16*)super->child)[3], COORD_TO_TILE(super), super->collisionLayer);
                sub_0807AB44(super, 0, 0x10);
                sub_0807AB44(super, 0, -0x10);
                sub_0807AB44(super, 0x10, 0);
                sub_0807AB44(super, -0x10, 0);
            }
            DeleteThisEntity();
            break;
        case 3:
#ifdef PC_PORT
            if (super->parent == NULL) { DeleteThisEntity(); return; } /* #136 family */
#endif
            if (super->parent->next == NULL) {
                DeleteThisEntity();
#ifdef PC_PORT
                return;
#endif
            }
            if (--super->timer == 0) {
                DeleteThisEntity();
#ifdef PC_PORT
                return;
#endif
            }

            CopyPosition(super->parent, super);
            break;
        case 4:
            val = CheckFlags(this->flag);
            if (super->subAction == 0) {
                if (val)
                    return;
                super->subAction = 1;
                super->spriteSettings.draw = 0;
            } else {
                if (!val)
                    return;
                super->subAction = 0;
                super->spriteSettings.draw = 1;
                InitializeAnimation(super, 0);
                EnqueueSFX(SFX_124);
            }
            break;
    }
}

void (*const Flame_Actions[])(FlameEntity*) = {
    Flame_Init,
    Flame_Action1,
};
