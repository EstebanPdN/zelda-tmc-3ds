/**
 * @file lightableSwitch.c
 * @ingroup Objects
 *
 * @brief Lightable Switch object
 */
#include "asm.h"
#include "entity.h"
#include "flags.h"
#include "hitbox.h"
#include "object.h"
#include "physics.h"
#include "room.h"
#include "sound.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

#ifndef PC_PORT
typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[12];
    /*0x74*/ u16 unk_74;
    /*0x76*/ u8 unused2[14];
    /*0x84*/ u16 flag1;
    /*0x86*/ u16 flag2;
} LightableSwitchEntity;
#else
typedef GenericEntity LightableSwitchEntity;
#endif

#ifdef PC_PORT
#define LS_UNK74(this) (GE_FIELD(&((this)->base), field_0x74)->HWORD)
#define LS_FLAG1(this) (GE_FIELD(&((this)->base), cutsceneBeh)->HWORD)
#define LS_FLAG2(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define LS_UNK74(this) ((this)->unk_74)
#define LS_FLAG1(this) ((this)->flag1)
#define LS_FLAG2(this) ((this)->flag2)
#endif

static void sub_0809EB30(LightableSwitchEntity* this);
static void sub_0809EAD8(LightableSwitchEntity* this);
static void sub_0809EABC(LightableSwitchEntity* this);
static void LightableSwitch_Type0(LightableSwitchEntity* this);
static void LightableSwitch_Type1(LightableSwitchEntity* this);
static void LightableSwitch_Type0_Init(LightableSwitchEntity* this);
static void LightableSwitch_Type0_Action1(LightableSwitchEntity* this);
static void LightableSwitch_Type1_Action3(LightableSwitchEntity* this);
static void LightableSwitch_Type1_Action2(LightableSwitchEntity* this);
static void LightableSwitch_Type1_Action1(LightableSwitchEntity* this);
static void LightableSwitch_Type1_Init(LightableSwitchEntity* this);

void LightableSwitch(LightableSwitchEntity* this) {
    static void (*const LightableSwitch_Types[])(LightableSwitchEntity*) = {
        LightableSwitch_Type0,
        LightableSwitch_Type1,
    };
    LightableSwitch_Types[super->type](this);
    sub_0809EB30(this);
}

void LightableSwitch_Type0(LightableSwitchEntity* this) {
    static void (*const LightableSwitch_Type0_Actions[])(LightableSwitchEntity*) = {
        LightableSwitch_Type0_Init,
        LightableSwitch_Type0_Action1,
    };
    LightableSwitch_Type0_Actions[super->action](this);
}

void LightableSwitch_Type0_Init(LightableSwitchEntity* this) {
    super->action = 1;
    super->spriteSettings.draw = 1;
    COLLISION_ON(super);
    super->frameIndex = 0;
    super->collisionFlags = 7;
    super->hurtType = 0x48;
    super->hitType = 0x28;
    super->collisionMask = 10;
    super->hitbox = (Hitbox*)&gHitbox_0;
    sub_0809EAD8(this);
    UpdateSpriteForCollisionLayer(super);
    sub_0809EABC(this);
}

void LightableSwitch_Type0_Action1(LightableSwitchEntity* this) {

    if ((super->contactFlags & CONTACT_NOW) != 0) {
        if (CheckFlags(LS_FLAG2(this)) != 0) {
            ClearFlag(LS_FLAG2(this));
        } else {
            SetFlag(LS_FLAG2(this));
        }
        EnqueueSFX(SFX_110);
    }
    sub_0809EABC(this);
}

static void sub_0809EABC(LightableSwitchEntity* this) {
    bool32 anySet = 0;

    if (CheckFlags(LS_FLAG2(this))) {
        anySet = 1;
    }
    if (super->frameIndex != anySet) {
        super->frameIndex = anySet;
    }
}

static void sub_0809EAD8(LightableSwitchEntity* this) {
    u16 railTimer;

    if (super->type2 != 0) {

        super->child = GetCurrentRoomProperty(super->type2);
        railTimer = LS_UNK74(this);
        UpdateRailMovement(super, (u16**)&super->child, &railTimer);
        LS_UNK74(this) = railTimer;

    } else {
        SetTile(SPECIAL_TILE_80, COORD_TO_TILE(super), super->collisionLayer);
    }
}

static void sub_0809EB30(LightableSwitchEntity* this) {
    u16 railTimer;

    if (super->type2 != 0) {
        if ((super->direction & 0x80) == 0) {
            LinearMoveUpdate(super);
        }
        railTimer = LS_UNK74(this);
        if (!--railTimer) {
            UpdateRailMovement(super, (u16**)&super->child, &railTimer);
        }
        LS_UNK74(this) = railTimer;
    }
}

void LightableSwitch_Type1(LightableSwitchEntity* this) {
    static void (*const LightableSwitch_Type1_Actions[])(LightableSwitchEntity*) = {
        LightableSwitch_Type1_Init,
        LightableSwitch_Type1_Action1,
        LightableSwitch_Type1_Action2,
        LightableSwitch_Type1_Action3,
    };
    LightableSwitch_Type1_Actions[super->action](this);
}

void LightableSwitch_Type1_Init(LightableSwitchEntity* this) {
    super->action = 1;
    super->spriteSettings.draw = 1;
    COLLISION_ON(super);
    super->frameIndex = 3;
    super->collisionFlags = 7;
    super->hurtType = 0x48;
    super->hitType = 0x28;
    super->collisionMask = 10;
    super->hitbox = (Hitbox*)&gHitbox_0;
    sub_0809EAD8(this);
    UpdateSpriteForCollisionLayer(super);
    if (CheckFlags(LS_FLAG1(this))) {
        super->action = 3;
        super->frameIndex = 2;
    }
}

void LightableSwitch_Type1_Action1(LightableSwitchEntity* this) {
    if ((super->contactFlags & CONTACT_NOW) != 0) {
        super->action = 2;
        super->timer = 16;
        super->frameIndex = 2;
        SetFlag(LS_FLAG2(this));
        EnqueueSFX(SFX_110);
    }
}

void LightableSwitch_Type1_Action2(LightableSwitchEntity* this) {

    if (CheckFlags(LS_FLAG1(this))) {
        super->action = 3;

    } else {
        if (--super->timer == 0) {
            super->action = 1;
            super->frameIndex = 3;
            ClearFlag(LS_FLAG2(this));
            EnqueueSFX(SFX_110);
        }
    }
}

void LightableSwitch_Type1_Action3(LightableSwitchEntity* this) {
}
