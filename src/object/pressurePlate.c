/**
 * @file pressurePlate.c
 * @ingroup Objects
 *
 * @brief Pressure Plate object
 */
#include "collision.h"
#include "hitbox.h"
#include "object.h"
#include "sound.h"
#include "flags.h"
#include "room.h"
#include "player.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

typedef struct {
    Entity base;
    /*0x68*/ union SplitHWord field_0x68;
    /*0x6a*/ union SplitHWord field_0x6a;
    /*0x6c*/ union SplitHWord field_0x6c;
    /*0x6e*/ union SplitHWord field_0x6e;
    /*0x70*/ u16 canToggle;
    /*0x72*/ u8 dir;
    /*0x73*/ u8 filler73[0x86 - 0x73];
    /*0x86*/ u16 flag;
} PressurePlateEntity;

PORT_STATIC_ASSERT_SIZE(PressurePlateEntity, 0x88, 0xB0, "PressurePlateEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(PressurePlateEntity, canToggle, 0x70, 0x98,
                        "PressurePlateEntity canToggle offset incorrect");
PORT_STATIC_ASSERT_OFFSET(PressurePlateEntity, dir, 0x72, 0x9A,
                        "PressurePlateEntity dir offset incorrect");
PORT_STATIC_ASSERT_OFFSET(PressurePlateEntity, flag, 0x86, 0xAE,
                        "PressurePlateEntity flag offset incorrect");

#ifdef PC_PORT
#define PLATE_CAN_TOGGLE(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.LO)
#define PLATE_DIR(this) (GE_FIELD(&((this)->base), field_0x70)->BYTES.byte2)
#define PLATE_FLAG(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define PLATE_CAN_TOGGLE(this) ((this)->canToggle)
#define PLATE_DIR(this) ((this)->dir)
#define PLATE_FLAG(this) ((this)->flag)
#endif

typedef void(PressurePlateAction)(PressurePlateEntity*);

PressurePlateAction PressurePlate_Init;
PressurePlateAction PressurePlate_Action1;
PressurePlateAction PressurePlate_Action2;

static u32 sub_08088938(PressurePlateEntity*);
static u32 get_standing_count(PressurePlateEntity*);

static const u8 sSpriteOffsets[];

void PressurePlate(PressurePlateEntity* this) {
    static PressurePlateAction* const PressurePlate_Actions[] = {
        PressurePlate_Init,
        PressurePlate_Action1,
        PressurePlate_Action2,
    };

    if (super->subtimer) {
        if (--super->subtimer == 0) {
            PLATE_DIR(this) = super->animationState;
            InitializeAnimation(super, super->animationState);
        }
    }

    PressurePlate_Actions[super->action](this);
}

void PressurePlate_Init(PressurePlateEntity* this) {
    super->action = 1;
    super->spriteSettings.draw = 1;
    super->spritePriority.b0 = 7;
    super->hitbox = (Hitbox*)&gUnk_080FD1D4;
    PLATE_DIR(this) = super->animationState;
}

void PressurePlate_Action1(PressurePlateEntity* this) {
    u8 weight = sub_08088938(this) + get_standing_count(this);
    if (super->type + 2 <= weight) {
        super->action = 2;
        super->subtimer = 0;
        super->animationState = 4;
        super->z.HALF.HI = 0;
        InitializeAnimation(super, 4);
        SetFlag(PLATE_FLAG(this));
        EnqueueSFX(SFX_PRESSURE_PLATE);
    } else {
        if (weight > super->animationState) {
            if (super->type + 1 == weight) {
                super->subtimer = 4;
                InitializeAnimation(super, weight + 1);
            } else {
                InitializeAnimation(super, weight);
            }
            EnqueueSFX(SFX_BUTTON_PRESS);
        } else if (weight < super->animationState) {
            InitializeAnimation(super, weight);
        }
        super->animationState = weight;
    }
}

void PressurePlate_Action2(PressurePlateEntity* this) {
    if (PLATE_CAN_TOGGLE(this)) {
        u8 weight = sub_08088938(this) + get_standing_count(this);
        if (super->type + 2 > weight) {
            super->action = 1;
            super->animationState = weight;
            ClearFlag(PLATE_FLAG(this));
            InitializeAnimation(super, weight);
        }
    }
}

static u32 sub_08088938(PressurePlateEntity* this) {
    u16 x, y;
    s32 num;
    u32 i;

    num = 0;
    x = super->x.HALF.HI - 8;
    y = super->y.HALF.HI - 8;
    for (i = 0; i < 8; ++i) {
        Entity* e = gRoomVars.puzzleEntities[i];
        if (e != NULL) {
            if ((u16)(e->x.HALF.HI - x) < 0x11 && ((u16)(e->y.HALF_U.HI - y) < 0x11)) {
                e->spriteOffsetY = sSpriteOffsets[PLATE_DIR(this)];
                num++;
            }
        }
    }
    return num;
}

static u32 get_standing_count(PressurePlateEntity* this) {
    u32 num;

    num = 0;
    if (IsCollidingPlayer(super) != 0) {
        gPlayerEntity.base.spriteOffsetY = sSpriteOffsets[PLATE_DIR(this)];
        num = 1;
    }
    if ((gPlayerState.flags & PL_CLONING) != 0) {
        /* NULL-clone hazard (same family as #67). Lower-tier swords leave
         * trailing gPlayerClones[] slots NULL and IsColliding derefs its
         * second arg's collisionLayer at the entry, SIGSEGV'ing on x86-64. */
        u32 i;
        for (i = 0; i < 3; ++i) {
            if (gPlayerClones[i] != NULL && IsColliding(super, gPlayerClones[i]) != 0) {
                gPlayerClones[i]->spriteOffsetY = sSpriteOffsets[PLATE_DIR(this)];
                num++;
            }
        }
    }
    return num;
}

static const u8 sSpriteOffsets[] = {
    -4, -3, -2, -1, 0,
};

static const Frame gSpriteAnimations_PressurePlate_0 = {
    .index = 0,
    .duration = 0xFF,
    .frameSettings = { .b = { .endOfAnimation = 1 } },
};

static const Frame gSpriteAnimations_PressurePlate_1 = {
    .index = 1,
    .duration = 0xFF,
    .frameSettings = { .b = { .endOfAnimation = 1 } },
};

static const Frame gSpriteAnimations_PressurePlate_2 = {
    .index = 2,
    .duration = 0xFF,
    .frameSettings = { .b = { .endOfAnimation = 1 } },
};

static const Frame gSpriteAnimations_PressurePlate_3 = {
    .index = 3,
    .duration = 0xFF,
    .frameSettings = { .b = { .endOfAnimation = 1 } },
};

static const Frame gSpriteAnimations_PressurePlate_4 = {
    .index = 4,
    .duration = 0xFF,
    .frameSettings = { .b = { .endOfAnimation = 1 } },
};

const Frame* const gSpriteAnimations_PressurePlate[] = {
    &gSpriteAnimations_PressurePlate_0, &gSpriteAnimations_PressurePlate_1, &gSpriteAnimations_PressurePlate_2,
    &gSpriteAnimations_PressurePlate_3, &gSpriteAnimations_PressurePlate_4,
};
