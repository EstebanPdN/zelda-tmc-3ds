/**
 * @file pot.c
 * @ingroup Objects
 *
 * @brief Pot object
 */
#include "entity.h"
#include "collision.h"
#include "flags.h"
#include "object/pot.h"
#include "hitbox.h"
#include "object.h"
#include "asm.h"
#include "effects.h"
#include "physics.h"
#include "object/itemOnGround.h"
#include "player.h"
#include "playeritem.h"
#include "room.h"
#include "script.h"
#include "sound.h"
#include "tiles.h"
#ifdef PC_PORT
#include <stdio.h>
#include "port/port_generic_entity.h"
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[8];
    /*0x70*/ u16 unk_70;
    /*0x72*/ u8 unused2[11];
    /*0x7d*/ u8 unk_7d;
    /*0x7e*/ u8 unused3[8];
    /*0x86*/ u16 flag;
} PotEntity;

PORT_STATIC_ASSERT_SIZE(PotEntity, 0x88, 0xB0, "PotEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(PotEntity, unk_70, 0x70, 0x98,
                        "PotEntity unk_70 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(PotEntity, unk_7d, 0x7d, 0xA5,
                        "PotEntity unk_7d offset incorrect");
PORT_STATIC_ASSERT_OFFSET(PotEntity, flag, 0x86, 0xAE,
                        "PotEntity flag offset incorrect");

#ifdef PC_PORT
#define POT_TILE_INDEX(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.LO)
#define POT_DROP_ARG(this) (GE_FIELD(&((this)->base), field_0x7c)->BYTES.byte1)
#define POT_FLAG(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define POT_TILE_INDEX(this) ((this)->unk_70)
#define POT_DROP_ARG(this) ((this)->unk_7d)
#define POT_FLAG(this) ((this)->flag)
#endif

void Pot_Action5(PotEntity*);
static void BreakPot(PotEntity*, Entity*);
void sub_08082608(PotEntity* this);
void Pot_Init(PotEntity* this);
void Pot_Action1(PotEntity* this);
void Pot_Action2(PotEntity* this);
void Pot_Action3(PotEntity* this);
void Pot_Action4(PotEntity* this);
void Pot_Action5(PotEntity* this);
void sub_08082510(PotEntity* this);
void nullsub_511(PotEntity* this);
void sub_08082588(PotEntity* this);
void sub_0808259C(PotEntity* this);
void sub_080825E8(PotEntity* this);
void sub_080825F0(PotEntity* this);
void sub_08082608(PotEntity* this);
void sub_08082818(PotEntity* this);
void nullsub_512(PotEntity* this);
void sub_080827F8(PotEntity* this);
void sub_08082778(PotEntity* this);
void sub_0808270C(PotEntity* this);
void sub_080826FC(PotEntity* this);
static bool32 Pot_HasJarContact(PotEntity* this);

extern void RegisterCarryEntity(Entity*);
extern u32 CheckOnLayerTransition(Entity*);

void Pot(PotEntity* this) {
    static void (*const Pot_Actions[])(PotEntity*) = {
        Pot_Init, Pot_Action1, Pot_Action2, Pot_Action3, Pot_Action4, Pot_Action5,
    };
    Pot_Actions[super->action](this);
    super->contactFlags = 0;
}

static bool32 Pot_HasJarContact(PotEntity* this) {
    if ((super->contactFlags & CONTACT_NOW) && ((super->contactFlags & 0x7F) == 0x13)) {
        return TRUE;
    }

#ifdef PC_PORT
    if ((gPlayerState.gustJarState & 0xF) != PL_JAR_SUCK) {
        return FALSE;
    }

    {
        LinkedList* list = &gEntityLists[2];
        Entity* it = list->first;

        for (; it != (Entity*)list; it = it->next) {
            if (it->kind != PLAYER_ITEM || it->id != PLAYER_ITEM_GUST) {
                continue;
            }
            if ((it->flags & ENT_COLLIDE) == 0 || it->hitbox == NULL) {
                continue;
            }
            if (IsColliding(super, it)) {
                super->contactedEntity = it;
                super->contactFlags = CONTACT_NOW | 0x13;
                return TRUE;
            }
        }
    }
#endif

    return FALSE;
}

void Pot_Init(PotEntity* this) {
    if (super->type2 == 1 && CheckFlags(POT_FLAG(this))) {
        DeleteThisEntity();
    }

    super->action = 1;
    super->hitbox = (Hitbox*)&gHitbox_18;
    super->speed = 0x80;
    super->y.HALF.HI += 3;
    super->carryFlags = 0;
    COLLISION_ON(super);
    super->health = 1;
    super->collisionFlags = 7;
    super->hitType = 0x6E;
    super->collisionMask = 0x84;
    super->gustJarFlags = 0x12;
    if (super->collisionLayer == 0) {
        ResolveCollisionLayer(super);
    }

    POT_TILE_INDEX(this) = GetTileIndex(COORD_TO_TILE(super), super->collisionLayer);
    if (POT_TILE_INDEX(this) == SPECIAL_TILE_0) {
        DeleteThisEntity();
    }

    SetTile(SPECIAL_TILE_0, COORD_TO_TILE(super), super->collisionLayer);
    InitializeAnimation(super, 5);
}

void Pot_Action1(PotEntity* this) {
    u32 tileType;
    u32 var0 = super->contactFlags & 0x7F;

    if (var0 == 0 && Pot_HasJarContact(this)) {
        var0 = 0x13;
    }
#ifdef PC_PORT
    if (super->contactFlags != 0) {
        fprintf(stderr,
                "[POT] contact=0x%02X type=%u type2=%u tile=0x%X act=0x%X base=0x%X pos=0x%03X layer=%u\n",
                super->contactFlags, super->type, super->type2, GetTileTypeAtEntity(super), GetActTileAtEntity(super),
                this->unk_70, COORD_TO_TILE(super), super->collisionLayer);
    }
#endif
    switch (var0) {
        case 0x13:
                super->action = 3;
                super->subAction = 0;
                break;
        case 0x1D:
            SetTile(POT_TILE_INDEX(this), COORD_TO_TILE(super), super->collisionLayer);
            super->action = 5;
            super->zVelocity = Q_16_16(2.625);
            super->spriteOffsetY = 0;
            super->spriteSettings.shadow = 1;
            super->spritePriority.b1 = 3;
            COLLISION_OFF(super);
            Pot_Action5(this);
            break;
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
        case 0xC:
        case 0xD:
        case 0x18:
        case 0x19:
        case 0x1A:
            tileType = GetTileTypeAtEntity(super);
            if (GetActTileAtEntity(super) == ACT_TILE_13) {
                CreateFx(super, FX_FALL_DOWN, 0);
            } else {
                gPlayerState.lastSwordMove = SWORD_MOVE_BREAK_POT;
                if (tileType == SPECIAL_TILE_5) {
                    SetTile(POT_TILE_INDEX(this), COORD_TO_TILE(super), super->collisionLayer);
                }
#ifdef PC_PORT
                fprintf(stderr, "[POT] sword-hit tile=0x%X restore=%u\n", tileType, tileType == SPECIAL_TILE_5);
#endif
            }
            BreakPot(this, NULL);
            break;
        default:
            tileType = GetTileTypeAtEntity(super);
            if (tileType != 0x4000) {
                switch (tileType) {
                    case SPECIAL_TILE_4:
                    case SPECIAL_TILE_3:
                    case SPECIAL_TILE_2:
                    case SPECIAL_TILE_1:
                        super->direction = (tileType - SPECIAL_TILE_1) * 8;
                        super->timer = 32;
                        super->action = 4;
                        if (gPlayerState.flags & PL_MINISH) {
                            super->speed >>= 1;
                            super->timer = 64;
                        }
                        SetTile((u16)this->unk_70, COORD_TO_TILE(super), super->collisionLayer);
                        EnqueueSFX(SFX_10F);
                        break;
                    case SPECIAL_TILE_103:
                        SetTile((u16)this->unk_70, COORD_TO_TILE(super), super->collisionLayer);
                        DeleteThisEntity();
                        break;
                    default:
                        if (GetActTileAtEntity(super) == ACT_TILE_13) {
                            CreateFx(super, FX_FALL_DOWN, 0);
                        } else if (tileType == SPECIAL_TILE_5) {
                            gPlayerState.lastSwordMove = SWORD_MOVE_BREAK_POT;
                            SetTile(POT_TILE_INDEX(this), COORD_TO_TILE(super), super->collisionLayer);
                        }
#ifdef PC_PORT
                        fprintf(stderr, "[POT] default-break tile=0x%X restore=%u\n", tileType,
                                tileType == SPECIAL_TILE_5);
#endif
                        BreakPot(this, NULL);
                        break;
                }
            } else {
                RegisterCarryEntity(super);
            }
            break;
    }
}

void Pot_Action2(PotEntity* this) {
    static void (*const subActionFuncs[])(PotEntity*) = {
        sub_08082510, nullsub_511, sub_08082588, sub_0808259C, sub_080825E8, sub_080825F0, sub_08082608,
    };
    subActionFuncs[super->subAction](this);
}

void sub_08082510(PotEntity* this) {
    COLLISION_ON(super);
    super->hitbox = (Hitbox*)&gUnk_080FD340;
    super->collisionFlags = 7;
    super->hitType = 1;
    super->collisionMask = gPlayerEntity.base.collisionMask;
    super->spriteOffsetY = 0;
    SetTile(POT_TILE_INDEX(this), COORD_TO_TILE(super), super->collisionLayer);
    super->subAction++;
}

void nullsub_511(PotEntity* this) {
}

void sub_08082588(PotEntity* this) {
    super->spritePriority.b1 = 3;
}

void sub_0808259C(PotEntity* this) {
    switch (GetTileHazardType(super)) {
        case 2:
            CreateFx(super, FX_WATER_SPLASH, 0);
            break;
        case 1:
            CreateFx(super, FX_FALL_DOWN, 0);
            break;
        case 3:
            CreateFx(super, FX_LAVA_SPLASH, 0);
            break;
        default:
            BreakPot(this, &gPlayerEntity.base);
            return;
    }

    DeleteThisEntity();
}

void sub_080825E8(PotEntity* this) {
    DeleteThisEntity();
}

void sub_080825F0(PotEntity* this) {
    if (gPlayerState.heldObject == 0) {
        sub_08082608(this);
    }
}

void sub_08082608(PotEntity* this) {
    super->action = 1;
    super->subAction = 0;
}

void Pot_Action4(PotEntity* this) {
    u32 tileType;

    sub_0800445C(super);
    if (super->timer-- != 0) {
        LinearMoveUpdate(super);
        CheckOnLayerTransition(super);
        return;
    }

    if (sub_0800442E(super)) {
        return;
    }

    super->action = 1;
    if (gPlayerState.flags & PL_MINISH) {
        super->speed <<= 1;
    }

    POT_TILE_INDEX(this) = GetTileIndex(COORD_TO_TILE(super), super->collisionLayer);
    tileType = GetTileTypeAtEntity(super);
    switch (tileType) {
        case 0x71:
        case 0x72:
            BreakPot(this, NULL);
            break;
        default:
            SetTile(SPECIAL_TILE_0, COORD_TO_TILE(super), super->collisionLayer);
            RegisterCarryEntity(super);
            break;
    }
}

void Pot_Action3(PotEntity* this) {
    static void (*const subActionFuncs[])(PotEntity*) = {
        sub_080826FC, sub_0808270C, sub_08082778, sub_080827F8, nullsub_512, sub_08082818,
    };
    subActionFuncs[super->subAction](this);
}

void sub_080826FC(PotEntity* this) {
    super->subAction = 1;
    super->gustJarTolerance = 48;
    super->timer = 0;
}

void sub_0808270C(PotEntity* this) {
    if ((gPlayerState.gustJarState & 0xF) != 0x1 || !Pot_HasJarContact(this)) {
        super->spriteOffsetX = 0;
        super->action = 1;
        SetTile(SPECIAL_TILE_0, COORD_TO_TILE(super), super->collisionLayer);
    } else {
        sub_0806F4E8(super);
    }
}

void sub_08082778(PotEntity* this) {
    if (super->timer == 0) {
        super->timer = 1;
        super->spriteOffsetX = 0;
        super->spriteOffsetY = -2;
        SetTile(POT_TILE_INDEX(this), COORD_TO_TILE(super), super->collisionLayer);
    }

    if ((gPlayerState.gustJarState & 0xF) != 0x1 || !Pot_HasJarContact(this)) {
        BreakPot(this, NULL);
    } else {
        sub_0806F3E4(super);
    }
}

void sub_080827F8(PotEntity* this) {
    if (gPlayerState.gustJarState == 0) {
        BreakPot(this, NULL);
    }
}

void nullsub_512(PotEntity* this) {
}

void sub_08082818(PotEntity* this) {
    BreakPot(this, NULL);
}

void Pot_Action5(PotEntity* this) {
    if (super->zVelocity < 0) {
        super->spriteSettings.flipY = 1;
    }

    if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
        BreakPot(this, NULL);
    }
}

static void BreakPot(PotEntity* this, Entity* parent) {
    u32 parameter = sub_0808288C(super, super->type, POT_DROP_ARG(this), super->type2);
    Entity* fxEntity = CreateFx(super, FX_POT_SHATTER, parameter);
    if (fxEntity) {
        fxEntity->parent = parent;
    }

    if (super->type2 == 1) {
        SetFlag(POT_FLAG(this));
    }

    DeleteThisEntity();
}

u32 sub_0808288C(Entity* this, u32 form, u32 arg2, u32 arg3) {
    ItemOnGroundEntity* entity;
    u32 result = 0;
    switch (form) {
        case 0xFF:
            result = 0;
            break;
        case 0:
            result = 0x80;
            break;
        default:
            entity = (ItemOnGroundEntity*)CreateObjectWithParent(this, GROUND_ITEM, form, arg2);
            if (entity != NULL) {
                if (arg3 == 2) {
                    entity->base.timer = 5;
                    entity->flag = POT_FLAG((PotEntity*)this); // This function is also used by flyingSkull.
                } else {
                    entity->base.timer = 0;
                }
            }
            break;
    }
    return result;
}
