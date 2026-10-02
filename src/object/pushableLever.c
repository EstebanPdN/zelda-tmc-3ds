/**
 * @file pushableLever.c
 * @ingroup Objects
 *
 * @brief Pushable Lever object
 */
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "flags.h"
#include "room.h"
#include "player.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unk_68[8];
    /*0x70*/ u16 tilePos;
    /*0x72*/ u16 tileIndex;
    /*0x74*/ u16 timer;
    /*0x76*/ u8 unk_76[16];
    /*0x86*/ u16 pushedFlag;
} PushableLeverEntity;

/* Issue #75 — same root cause as #89 (PushableRock). PushableLeverEntity's
 * pushedFlag aliases GenericEntity.field_0x86 on GBA, but on PC the Entity
 * base grew from 0x68 to 0x90 bytes and the byte-counted unk_* filler in
 * this struct misses the resulting tail-union offset. Result on PC was
 * pushedFlag reading 0 → SetFlag(0) being a no-op → the local flag the
 * sunbeam manager waits on never got set → Temple of Droplets sunbeam
 * never activated when the player pushed the lever. Route through
 * GE_FIELD so we hit the real GenericEntity.field_0x86 on either
 * platform. */
#ifdef PC_PORT
#define PUSHED_FLAG(this) (GE_FIELD(&(this)->base, field_0x86)->HWORD)
#else
#define PUSHED_FLAG(this) ((this)->pushedFlag)
#endif

enum PushableLeverAction {
    INIT,
    IDLE,
    PUSHING,
};

extern void (*const PushableLever_Actions[])(PushableLeverEntity*);
extern const u16 PushableLever_Tiles[];
extern const u8 PushableLever_InitialOffsets[];
extern const u8 PushableLever_PushedOffsets[];

void PushableLever_SetIdle(PushableLeverEntity*);
bool32 PushableLever_ShouldStartPushing(PushableLeverEntity* this);
void PushableLever_SetTiles(PushableLeverEntity*);
void PushableLever_CalculateSpriteOffsets(PushableLeverEntity*);

void PushableLever(PushableLeverEntity* this) {
    PushableLever_Actions[super->action](this);
}

void PushableLever_Init(PushableLeverEntity* this) {
    super->spritePriority.b0 = 5;
    PushableLever_SetIdle(this);
}

void PushableLever_Idle(PushableLeverEntity* this) {
    if (PushableLever_ShouldStartPushing(this)) {
        super->action = PUSHING;
        super->spriteOffsetX = 0;
        super->spriteOffsetY = 0;
        SetTile(this->tileIndex, this->tilePos, super->collisionLayer);
        EnqueueSFX(SFX_16A);
        RequestPriorityDuration(super, 30);
        if (PlayerCanBeMoved()) {
            gPlayerState.pushedObject = 0x90;
            gPlayerState.queued_action = PLAYER_PUSH;
            gPlayerState.flags |= PL_BUSY;
            gPlayerEntity.base.x.HALF.LO = 0;
            gPlayerEntity.base.y.HALF.LO = 0;
            gPlayerEntity.base.direction = gPlayerEntity.base.animationState << 2;
        }
    }
}

void PushableLever_Pushing(PushableLeverEntity* this) {
    GetNextFrame(super);
    if ((super->frame & ANIM_DONE) != 0) {
        if (super->type2 == 0) {
            SetFlag(PUSHED_FLAG(this));
        } else {
            ClearFlag(PUSHED_FLAG(this));
        }
        PushableLever_SetIdle(this);
    }
}

void PushableLever_SetIdle(PushableLeverEntity* this) {
    super->action = IDLE;
    this->timer = 60;
    PushableLever_SetTiles(this);
}

void PushableLever_SetTiles(PushableLeverEntity* this) {
    if (CheckFlags(PUSHED_FLAG(this)) == FALSE) {
        super->type2 = 0;
        this->tilePos = COORD_TO_TILE_OFFSET(super, 0, 0x10);
        this->tileIndex = GetTileIndex(this->tilePos, super->collisionLayer);
        SetTile(SPECIAL_TILE_83, this->tilePos, super->collisionLayer);
        InitializeAnimation(super, 1);
    } else {
        super->type2 = 1;
        this->tilePos = COORD_TO_TILE_OFFSET(super, 0x10, 0);
        this->tileIndex = GetTileIndex(this->tilePos, super->collisionLayer);
        SetTile(SPECIAL_TILE_82, this->tilePos, super->collisionLayer);
        InitializeAnimation(super, 0);
    }
}

bool32 PushableLever_ShouldStartPushing(PushableLeverEntity* this) {
    if (GetTileIndex(this->tilePos, super->collisionLayer) == SPECIAL_TILE_84) {
        if (--this->timer == 0) {
            return TRUE;
        }
        PushableLever_CalculateSpriteOffsets(this);
        SetTile(PushableLever_Tiles[super->type2], this->tilePos, super->collisionLayer);
    } else {
        this->timer = 60;
        super->spriteOffsetX = 0;
        super->spriteOffsetY = 0;
    }
    return FALSE;
}

void PushableLever_CalculateSpriteOffsets(PushableLeverEntity* this) {
    const u8* offsets;
    if (super->type2 == 0) {
        offsets = PushableLever_InitialOffsets;
    } else {
        offsets = PushableLever_PushedOffsets;
    }
    offsets = &offsets[(this->timer / 4) & 6];
    super->spriteOffsetX = offsets[0];
    super->spriteOffsetY = offsets[1];
    if ((this->timer & 3) == 0) {
        EnqueueSFX(SFX_PLACE_OBJ);
    }
}

void (*const PushableLever_Actions[])(PushableLeverEntity*) = {
    PushableLever_Init,
    PushableLever_Idle,
    PushableLever_Pushing,
};
const u16 PushableLever_Tiles[] = {
    SPECIAL_TILE_83,
    SPECIAL_TILE_82,
};
const u8 PushableLever_InitialOffsets[] = {
    0, 0, 0, 0, 255, 0, 0, 0,
};
const u8 PushableLever_PushedOffsets[] = {
    0, 0, 0, 0, 0, 255, 0, 0,
};
