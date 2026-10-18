/**
 * @file pushableRock.c
 * @ingroup Objects
 *
 * @brief Pushable Rock object
 */
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "flags.h"
#include "room.h"
#include "physics.h"
#include "tiles.h"
#include "port_generic_entity.h"

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unk_68[8];
    /*0x70*/ u16 tileIndex;
    /*0x72*/ u8 collisionData;
    /*0x73*/ u8 unk_73;
    /*0x74*/ u16 tilePos;
    /*0x76*/ u8 unk_76[0x10];
    /*0x86*/ u16 pushedFlag;
} PushableRockEntity;

/* Issue #89: PushableRockEntity.pushedFlag aliases GenericEntity.field_0x86
 * on GBA, but on PC the Entity base grew from 0x68 to 0x90 bytes and the
 * GenericEntity tail union (cutsceneBeh + field_0x86 vs. an 8-byte
 * scriptContext pointer) shifts field_0x86 to a different offset than
 * the byte-counted `unk_*` filler in this struct produces. The result
 * was pushedFlag reading 0 → SetFlag(0) being a no-op → the rock's
 * "already pushed" state never persisting. Read/write through GE_FIELD
 * so we hit the real GenericEntity.field_0x86 on either platform. */
#define PUSHED_FLAG(this) (GE_FIELD(&(this)->base, field_0x86)->HWORD)

extern void (*const PushableRock_Actions[])(PushableRockEntity*);
extern const u16 PushableRock_Speeds[];

void sub_0808A644(PushableRockEntity*);

void PushableRock(PushableRockEntity* this) {
    PushableRock_Actions[super->action](this);
}

void PushableRock_Init(PushableRockEntity* this) {
    super->spriteSettings.draw = 1;
    super->action++;
    super->spriteRendering.b3 = 2;
    super->spritePriority.b0 = 4;
    super->spritePriority.b1 = 3;
    sub_0808A644(this);
}

void PushableRock_Action1(PushableRockEntity* this) {
    u32 tileType;
    u32 tmp;

    tileType = GetTileTypeAtTilePos(this->tilePos, super->collisionLayer);
    switch (tileType) {
        case SPECIAL_TILE_28:
        case SPECIAL_TILE_29:
        case SPECIAL_TILE_30:
        case SPECIAL_TILE_31:
            tmp = (tileType - 0x1c);
            super->animationState = tmp * 2;
            super->direction = tmp * 8;
            if ((u8)(tmp * 2) == 2) {
                super->spriteSettings.flipX = 1;
            } else {
                super->spriteSettings.flipX = 0;
            }
            SetTile(this->tileIndex, this->tilePos, super->collisionLayer);
            super->action = 2;
            InitializeAnimation(super, (super->animationState >> 1) + 1);
            EnqueueSFX(SFX_10F);
            break;
        default:
            if (super->animIndex) {
                InitializeAnimation(super, 0);
            }
            break;
    }
}

void PushableRock_Action2(PushableRockEntity* this) {
    sub_0800445C(super);
    super->speed = PushableRock_Speeds[super->frame & 0xf];
    LinearMoveUpdate(super);
    GetNextFrame(super);
    if (super->frame & ANIM_DONE) {
        super->action = 0;
    }
}

void PushableRock_Action3(PushableRockEntity* this) {
    if ((super->frame & ANIM_DONE) == 0) {
        GetNextFrame(super);
    } else {
        super->spritePriority.b0 = 7;
    }
}

void sub_0808A644(PushableRockEntity* this) {
    u32 actTile;
    this->tilePos = COORD_TO_TILE(super);
    this->tileIndex = GetTileIndex(this->tilePos, super->collisionLayer);
    this->collisionData = GetCollisionDataAtTilePos(this->tilePos, super->collisionLayer);
    actTile = GetActTileAtTilePos(this->tilePos, super->collisionLayer);
    if ((actTile == ACT_TILE_25) || (actTile == ACT_TILE_240)) {
        SetTile(SPECIAL_TILE_21, this->tilePos, super->collisionLayer);
        super->action = 3;
        if (!CheckFlags(PUSHED_FLAG(this))) {
            SetFlag(PUSHED_FLAG(this));
            SoundReq(SFX_TASK_COMPLETE);
            InitializeAnimation(super, 5);
        } else {
            InitializeAnimation(super, 6);
        }
    } else {
        SetTile(SPECIAL_TILE_27, this->tilePos, super->collisionLayer);
    }
}

void (*const PushableRock_Actions[])(PushableRockEntity*) = {
    PushableRock_Init,
    PushableRock_Action1,
    PushableRock_Action2,
    PushableRock_Action3,
};
const u16 PushableRock_Speeds[] = {
    0,
    256,
    96,
    64,
};
