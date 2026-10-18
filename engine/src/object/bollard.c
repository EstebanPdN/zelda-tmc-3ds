/**
 * @file bollard.c
 * @ingroup Objects
 *
 * @brief Bollard object
 */
#include "asm.h"
#include "entity.h"
#include "flags.h"
#include "room.h"
#include "sound.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

typedef struct {
    Entity base;
    u8 filler[0x8];
    u16 tilePos;
    u16 tileIndex;
    u8 collisionData;
    u8 unk75;
    u8 filler2[0x10];
    u16 flags;
} BollardEntity;

PORT_STATIC_ASSERT_SIZE(BollardEntity, 0x88, 0xB0, "BollardEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(BollardEntity, tilePos, 0x70, 0x98, "BollardEntity tilePos offset incorrect");
PORT_STATIC_ASSERT_OFFSET(BollardEntity, tileIndex, 0x72, 0x9A, "BollardEntity tileIndex offset incorrect");
PORT_STATIC_ASSERT_OFFSET(BollardEntity, collisionData, 0x74, 0x9C, "BollardEntity collisionData offset incorrect");
PORT_STATIC_ASSERT_OFFSET(BollardEntity, flags, 0x86, 0xAE, "BollardEntity flags offset incorrect");

#ifdef PC_PORT
#define BOLLARD_TILEPOS(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.LO)
#define BOLLARD_TILEINDEX(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.HI)
#define BOLLARD_COLLISION(this) (GE_FIELD(&((this)->base), field_0x74)->HALF.LO)
#define BOLLARD_FLAGS(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define BOLLARD_TILEPOS(this) ((this)->tilePos)
#define BOLLARD_TILEINDEX(this) ((this)->tileIndex)
#define BOLLARD_COLLISION(this) ((this)->collisionData)
#define BOLLARD_FLAGS(this) ((this)->flags)
#endif

void Bollard_Init(BollardEntity*);
void Bollard_Action1(BollardEntity*);
void Bollard_Action2(BollardEntity*);
void Bollard_Action3(BollardEntity*);
void Bollard_Action4(BollardEntity*);
void sub_0808B41C(BollardEntity*);
void sub_0808B3AC(BollardEntity*);
void sub_0808B42C(BollardEntity*);

void Bollard(Entity* this) {
    static void (*const Bollard_Actions[])(BollardEntity*) = {
        Bollard_Init, Bollard_Action1, Bollard_Action2, Bollard_Action3, Bollard_Action4,
    };

    Bollard_Actions[this->action]((BollardEntity*)this);
}

void Bollard_Init(BollardEntity* this) {
    if (super->type2 == 0) {
        sub_0808B41C(this);
        sub_0808B3AC(this);
    } else {
        sub_0808B42C(this);
    }
}

void Bollard_Action1(BollardEntity* this) {
    if (super->type2 == 0) {
        if (CheckFlags(BOLLARD_FLAGS(this)) == 0) {
            return;
        }
    } else if (CheckFlags(BOLLARD_FLAGS(this)) != 0) {
        return;
    }
    super->action = 2;
    InitializeAnimation(super, 3);
    SetTile(BOLLARD_TILEINDEX(this), BOLLARD_TILEPOS(this), super->collisionLayer);
    EnqueueSFX(SFX_1A5);
}

void Bollard_Action2(BollardEntity* this) {
    GetNextFrame(super);
    if (super->frame & ANIM_DONE) {
        sub_0808B42C(this);
    }
}

void Bollard_Action3(BollardEntity* this) {
    if (super->type2 == 0) {
        if (CheckFlags(BOLLARD_FLAGS(this)) != 0) {
            return;
        }
    } else if (CheckFlags(BOLLARD_FLAGS(this)) == 0) {
        return;
    }
    super->action = 4;
    InitializeAnimation(super, 2);
    sub_0808B3AC(this);
    EnqueueSFX(SFX_1A5);
}

void Bollard_Action4(BollardEntity* this) {
    GetNextFrame(super);
    if (super->frame & ANIM_DONE) {
        sub_0808B41C(this);
    }
}

void sub_0808B3AC(BollardEntity* this) {
    super->spritePriority.b0 = 4;
    BOLLARD_TILEPOS(this) = COORD_TO_TILE(super);
    BOLLARD_TILEINDEX(this) = GetTileIndex(BOLLARD_TILEPOS(this), super->collisionLayer);
    BOLLARD_COLLISION(this) = GetCollisionDataAtTilePos(BOLLARD_TILEPOS(this), super->collisionLayer);
    SetTile(SPECIAL_TILE_11, BOLLARD_TILEPOS(this), super->collisionLayer);
}

void sub_0808B41C(BollardEntity* this) {
    super->action = 1;
    InitializeAnimation(super, 0);
}

void sub_0808B42C(BollardEntity* this) {
    super->action = 3;
    super->spritePriority.b0 = 7;
    InitializeAnimation(super, 1);
}
