/**
 * @file button.c
 * @ingroup Objects
 *
 * @brief Button object
 */
#include "tiles.h"
#include "area.h"
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "flags.h"
#include "effects.h"
#include "room.h"
#include "roomid.h"
#include "player.h"
#include "map.h"
#ifdef PC_PORT
#include <stdio.h>
#include "port/port_generic_entity.h"
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[8];
    /*0x70*/ u16 unk_70;
    /*0x72*/ u16 unk_72;
    /*0x74*/ u16 tilePos;
    /*0x76*/ u8 unused2[14];
    /*0x84*/ u16 unk_84;
    /*0x86*/ u16 flag;
} ButtonEntity;

PORT_STATIC_ASSERT_SIZE(ButtonEntity, 0x88, 0xB0, "ButtonEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(ButtonEntity, unk_70, 0x70, 0x98, "ButtonEntity unk_70 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(ButtonEntity, tilePos, 0x74, 0x9C, "ButtonEntity tilePos offset incorrect");
PORT_STATIC_ASSERT_OFFSET(ButtonEntity, unk_84, 0x84, 0xAC, "ButtonEntity unk_84 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(ButtonEntity, flag, 0x86, 0xAE, "ButtonEntity flag offset incorrect");

#ifdef PC_PORT
#define BTN_UNK70(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.LO)
#define BTN_UNK72(this) (GE_FIELD(&((this)->base), field_0x70)->HALF_U.HI)
#define BTN_TILEPOS(this) (GE_FIELD(&((this)->base), field_0x74)->HWORD)
#define BTN_UNK84(this) (GE_FIELD(&((this)->base), cutsceneBeh)->HWORD)
#define BTN_FLAG(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define BTN_UNK70(this) ((this)->unk_70)
#define BTN_UNK72(this) ((this)->unk_72)
#define BTN_TILEPOS(this) ((this)->tilePos)
#define BTN_UNK84(this) ((this)->unk_84)
#define BTN_FLAG(this) ((this)->flag)
#endif

void Button_Init(ButtonEntity* this);
void Button_Action1(ButtonEntity* this);
void Button_Action2(ButtonEntity* this);
void Button_Action3(ButtonEntity* this);
void Button_Action4(ButtonEntity* this);
void Button_Action5(ButtonEntity* this);

static bool32 Button_IsTileTypePressable(u32 tileType) {
    switch (tileType) {
        case TILE_TYPE_119:
        case TILE_TYPE_120:
        case TILE_TYPE_121:
        case TILE_TYPE_122:
        case SPECIAL_TILE_53:
            return TRUE;
        default:
            return FALSE;
    }
}

void Button(ButtonEntity* this) {
    static void (*const Button_Actions[])(ButtonEntity*) = {
        Button_Init, Button_Action1, Button_Action2, Button_Action3, Button_Action4, Button_Action5,
    };
    Button_Actions[super->action](this);
}

extern u32 sub_08081E3C(ButtonEntity*);

void Button_Init(ButtonEntity* this) {
    u32 tileType;

    COLLISION_OFF(super);
    super->updatePriority = PRIO_NO_BLOCK;
    super->y.HALF.HI++;
    if (BTN_UNK84(this) != 0) {
        super->collisionLayer = BTN_UNK84(this);
    }
    BTN_TILEPOS(this) = (((super->x.HALF.HI - gRoomControls.origin_x) >> 4) & 0x3F) |
                        ((((super->y.HALF.HI - gRoomControls.origin_y) >> 4) & 0x3F) << 6);
    tileType = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
#ifdef PC_PORT
    if (!Button_IsTileTypePressable(tileType)) {
        if (super->collisionLayer != COL_LAYER_BOTTOM &&
            Button_IsTileTypePressable(GetTileTypeAtTilePos(BTN_TILEPOS(this), COL_LAYER_BOTTOM))) {
            super->collisionLayer = COL_LAYER_BOTTOM;
            tileType = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
        } else if (super->collisionLayer != COL_LAYER_TOP &&
                   Button_IsTileTypePressable(GetTileTypeAtTilePos(BTN_TILEPOS(this), COL_LAYER_TOP))) {
            super->collisionLayer = COL_LAYER_TOP;
            tileType = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
        }
    }
#endif
    BTN_UNK72(this) = tileType;
#ifdef PC_PORT
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
        fprintf(stderr, "[BUTTON7] Init flag=0x%04X type=%u tilePos=0x%03X layer=%u tileType=0x%X x=%d y=%d\n",
                BTN_FLAG(this), super->type, BTN_TILEPOS(this), super->collisionLayer, BTN_UNK72(this),
                super->x.HALF.HI, super->y.HALF.HI);
    }
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
        fprintf(stderr, "[POTBRIDGE] Button_Init flag=0x%04X type=%u tilePos=0x%03X layer=%u tileType=0x%X x=%d y=%d\n",
                BTN_FLAG(this), super->type, BTN_TILEPOS(this), super->collisionLayer, BTN_UNK72(this),
                super->x.HALF.HI, super->y.HALF.HI);
    }
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_TORCHES) {
        fprintf(stderr, "[TORCH] Button_Init flag=0x%04X type=%u tilePos=0x%03X layer=%u tileType=0x%X\n",
                BTN_FLAG(this), super->type, BTN_TILEPOS(this), super->collisionLayer, BTN_UNK72(this));
    }
#endif
    if (super->type == 0 && CheckFlags(BTN_FLAG(this))) {
        super->action = 5;
        SetTileType(TILE_TYPE_122, BTN_TILEPOS(this), super->collisionLayer);
    } else {
        if (sub_08081E3C(this)) {
            super->action = 2;
        } else {
            super->action = 1;
        }
    }
}

void Button_Action1(ButtonEntity* this) {
#ifdef PC_PORT
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
        static u8 sButton7LastPressed;
        u8 pressed = sub_08081E3C(this);
        if (pressed != sButton7LastPressed) {
            fprintf(stderr, "[BUTTON7] Action1 pressed=%u tileType=0x%X layer=%u flag=0x%04X\n", pressed,
                    GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer), super->collisionLayer,
                    BTN_FLAG(this));
            sButton7LastPressed = pressed;
        }
        if (pressed) {
            super->action = 2;
            BTN_UNK72(this) = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
            return;
        }
    }
#endif
    if (sub_08081E3C(this)) {
        super->action = 2;
        BTN_UNK72(this) = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
    }
}

u32 sub_08081CB0(ButtonEntity*);
void sub_08081FF8(Entity*);

void Button_Action2(ButtonEntity* this) {
    if (sub_08081CB0(this)) {
#ifdef PC_PORT
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
            fprintf(stderr, "[BUTTON7] Action2 trigger flag=0x%04X tilePos=0x%03X layer=%u tileType=0x%X child=%p\n",
                    BTN_FLAG(this), BTN_TILEPOS(this), super->collisionLayer,
                    GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer), (void*)super->child);
        }
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
            fprintf(stderr, "[POTBRIDGE] Button_Trigger flag=0x%04X tilePos=0x%03X layer=%u tileType=0x%X child=%p\n",
                    BTN_FLAG(this), BTN_TILEPOS(this), super->collisionLayer,
                    GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer), (void*)super->child);
        }
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_TORCHES) {
            fprintf(stderr, "[TORCH] Button_Action2 trigger flag=0x%04X tilePos=0x%03X layer=%u tileType=0x%X\n",
                    BTN_FLAG(this), BTN_TILEPOS(this), super->collisionLayer,
                    GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer));
        }
#endif
        super->subAction = 0;
        super->timer = 10;
        RequestPriorityDuration(super, 10);
        sub_08081FF8(super);
        if (super->type == 1) {
            super->action = 3;
        } else {
            super->action = 5;
        }
    }
}

u32 sub_08081F7C(ButtonEntity*, u32 tileType);
u32 sub_08081D28(ButtonEntity*);
void sub_08081E6C(ButtonEntity*);

void Button_Action3(ButtonEntity* this) {
    if (!sub_08081F7C(this, TILE_TYPE_120))
        return;
    if (!sub_08081D28(this)) {
        super->action = 4;
        super->subtimer = 1;
        if ((gPlayerState.heldObject == 2) || (!(gPlayerState.field_0x35 & 0x80))) {
            super->timer = 24;
        } else {
            super->timer = 8;
        }
    } else {
        sub_08081E6C(this);
    }
}

void Button_Action4(ButtonEntity* this) {
    if (super->timer != 0) {
        super->timer--;
        if (super->subtimer != 0) {
            super->subtimer = 0;
            SetTile(SPECIAL_TILE_53, BTN_TILEPOS(this), super->collisionLayer);
        }
        if (sub_08081CB0(this)) {
            super->action = 3;
            super->timer = 0;
        }
    } else {
        super->action = 2;
        ClearFlag(BTN_FLAG(this));
        SetTileType(TILE_TYPE_119, BTN_TILEPOS(this), super->collisionLayer);
        SoundReq(SFX_BUTTON_PRESS);
    }
}

void Button_Action5(ButtonEntity* this) {
    if (sub_08081F7C(this, TILE_TYPE_122)) {
        sub_08081E6C(this);
    }
}

Entity* sub_08081D74(ButtonEntity*);

bool32 sub_08081CB0(ButtonEntity* this) {
    u16 tileType;
    if (sub_08081D74(this)) {
        BTN_UNK70(this) = 0xFFFF;
        if (GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer) == SPECIAL_TILE_53) {
            sub_0807B7D8(0x78, BTN_TILEPOS(this), super->collisionLayer);
        }
#ifdef PC_PORT
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
            fprintf(stderr, "[BUTTON7] sub_08081CB0 entity-on-button child=%p\n", (void*)super->child);
        }
#endif
        return TRUE;
    } else {
        tileType = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
        if (tileType != 0x77 && tileType != 0x79 && tileType != SPECIAL_TILE_53) {
            BTN_UNK70(this) = GetTileIndex(BTN_TILEPOS(this), super->collisionLayer);
#ifdef PC_PORT
            if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
                fprintf(stderr, "[BUTTON7] sub_08081CB0 tile-changed tileType=0x%X index=0x%X\n", tileType,
                        BTN_UNK70(this));
            }
#endif
            return TRUE;
        }
    }
    return FALSE;
}

bool32 sub_08081D28(ButtonEntity* this) {
    if (sub_08081D74(this)) {
        BTN_UNK70(this) = 0xFFFF;
        return TRUE;
    } else {
        if (BTN_UNK70(this) == 0xFFFF) {
            return FALSE;
        }
        if (GetTileIndex(BTN_TILEPOS(this), super->collisionLayer) != BTN_UNK70(this)) {
            return FALSE;
        }
    }
    return TRUE;
}

extern Entity* gPlayerClones[3];
u32 sub_08081E0C(Entity*);

Entity* sub_08081D74(ButtonEntity* this) {
    Entity* ent;
    if (GetCollisionDataAtTilePos(BTN_TILEPOS(this), super->collisionLayer) == COLLISION_DATA_15) {
        return NULL;
    }
    ent = 0;
    if (sub_08081E0C(super)) {
        if ((gPlayerState.flags & PL_CAPTURED) == 0 && (gPlayerState.flags & PL_MINISH) == 0) {
            ent = &gPlayerEntity.base;
        }
    } else {
        if (gPlayerState.flags & PL_CLONING) {
            /* Same NULL-clone hazard as interrupts.c sub_080171F0 (#67):
             * Red sword has 1 clone, Blue has 2, only Four Sword fills all
             * three slots. EntityInRectRadius reads its second arg's flags
             * byte at offset 0x40, so a NULL clone slot SIGSEGVs on x86-64
             * (silently read garbage from BIOS region on GBA). */
            if (gPlayerClones[0] != NULL && EntityInRectRadius(super, gPlayerClones[0], 5, 6)) {
                ent = gPlayerClones[0];
            } else if (gPlayerClones[1] != NULL && EntityInRectRadius(super, gPlayerClones[1], 5, 6)) {
                ent = gPlayerClones[1];
            } else if (gPlayerClones[2] != NULL && EntityInRectRadius(super, gPlayerClones[2], 5, 6)) {
                ent = gPlayerClones[2];
            }
        }
    }
    super->child = ent;
    return ent;
}

u32 sub_08081E0C(Entity* this) {
    Entity* tmp = &gPlayerEntity.base;
    if (tmp->z.HALF.HI != 0 || !PlayerCanBeMoved()) {
        return 0;
    } else {
        return EntityInRectRadius(this, tmp, 5, 6);
    }
}

u32 sub_08081E3C(ButtonEntity* this) {
    static const u16 gUnk_0811EE50[] = {
        0x77, 0x78, 0x79, 0x7a, 0, 0,
    };
    const u16* tmp1;
    s32 tmp2;
    tmp2 = GetTileTypeAtTilePos(BTN_TILEPOS(this), super->collisionLayer);
    tmp1 = gUnk_0811EE50;
    do {
        if (*tmp1 == tmp2)
            return 1;
    } while (*++tmp1);
    return 0;
}

u32 sub_08081F00(u32*, u32*);

extern u16 MAY_ALIAS gMapDataTopSpecial[0x4000];

extern u16 MAY_ALIAS gMapDataBottomSpecial[];

void sub_08081E6C(ButtonEntity* this) {
    u32 tileType;
    MapLayer* mapLayer;
    u16* tmp2;
    u16* subTiles;
    u16* tmp3;
    u32 tilePos = BTN_TILEPOS(this);
    u32 layer = super->collisionLayer;
    u32 specialTile = GetTileTypeAtTilePos(tilePos, layer);

    if (specialTile < 0x4000)
        return;
    mapLayer = GetLayerByIndex(layer);
    tileType = (super->type == 0 ? TILE_TYPE_122 : TILE_TYPE_120);
    subTiles = mapLayer->subTiles;
    subTiles = subTiles + (mapLayer->tileIndices[tileType] << 2);
    tmp2 = (layer == 2 ? gMapDataTopSpecial : gMapDataBottomSpecial);
    tmp2 += (((0x3f & tilePos) << 1) + ((0xfc0 & tilePos) << 2));
    if (sub_08081F00((u32*)tmp2, (u32*)subTiles))
        return;
    SetTileType(tileType, tilePos, layer);
    SetTile(specialTile, tilePos, layer);
}

// Are the two tiles already set to the correct one
bool32 sub_08081F00(u32* screenblock, u32* tileList) {
    if (screenblock[0] != tileList[0])
        return FALSE;
    if (screenblock[0x40] != tileList[1])
        return FALSE;
    return TRUE;
}

void sub_08081F24(Entity* this) {
    Entity* fx = CreateFx(this, FX_DASH, 0x40);
    if (fx) {
        fx->updatePriority = PRIO_NO_BLOCK;
        fx->x.HALF.HI += 7;
        fx->y.HALF.HI += 5;
    }
    fx = CreateFx(this, FX_DASH, 0x40);
    if (fx) {
        fx->updatePriority = PRIO_NO_BLOCK;
        fx->x.HALF.HI -= 7;
        fx->y.HALF.HI += 5;
    }
}

bool32 sub_08081F7C(ButtonEntity* this, u32 tileType) {
    u16 tmp;
    if (super->timer == 0)
        return TRUE;
    if (--super->timer > 6) {
        if (super->child != NULL)
            super->child->spriteOffsetY = -4;
    } else {
        if (super->timer == 6) {
            SetFlag(BTN_FLAG(this));
#ifdef PC_PORT
            if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
                fprintf(stderr, "[BUTTON7] SetFlag flag=0x%04X tileType=0x%X child=%p\n", BTN_FLAG(this), tileType,
                        (void*)super->child);
            }
            if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
                fprintf(stderr, "[POTBRIDGE] Button_SetFlag flag=0x%04X tileType=0x%X child=%p\n", BTN_FLAG(this),
                        tileType, (void*)super->child);
            }
            if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_TORCHES) {
                fprintf(stderr, "[TORCH] Button_SetFlag flag=0x%04X tilePos=0x%03X layer=%u tileType=0x%X\n",
                        BTN_FLAG(this), BTN_TILEPOS(this), super->collisionLayer, tileType);
            }
#endif
            SetTileType(tileType, BTN_TILEPOS(this), super->collisionLayer);
            sub_08081F24(super);
            SoundReq(SFX_BUTTON_PRESS);
            if (BTN_UNK70(this) != 0xFFFF)
                SetTile(BTN_UNK70(this), BTN_TILEPOS(this), super->collisionLayer);
            return FALSE;
        }
    }
    return TRUE;
}

void sub_08081FF8(Entity* this) {
    u32 direction;
    u32 i;
    if (this->child != &gPlayerEntity.base)
        return;
    direction = GetFacingDirection(this->child, this);
    sub_080044AE(this->child, 0x200, direction);
    for (i = 0; i < 3; i++) {
#ifdef PC_PORT
        /* #130 followup (bugreport_20260601_010030): clones spawned during the
         * Four Sword charge are inserted into gPlayerClones[] (player.c:3792)
         * before PlayerClone assigns super->hitbox — playerClone.c only does
         * that once gPlayerState.chargeState.action reaches 5. During that
         * window a clone sits here with hitbox == NULL; sub_080044AE ->
         * CalculateEntityTileCollisions dereferences it, which reads harmless
         * BIOS-region garbage on the GBA but SIGSEGVs at address 0 on PC.
         * Skip clones that are not yet pushable. */
        if (gPlayerClones[i] != NULL && gPlayerClones[i]->hitbox != NULL) {
#else
        if (gPlayerClones[i]) {
#endif
            sub_080044AE(gPlayerClones[i], 0x200, direction);
        }
    }
}
