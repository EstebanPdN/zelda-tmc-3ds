/**
 * @file removableDust.c
 * @ingroup Projectiles
 *
 * @brief Removable Dust Projectile
 */
#include "enemy.h"
#include "sound.h"
#include "flags.h"
#include "common.h"
#include "entity.h"
#include "object.h"
#include "asm.h"
#include "player.h"
#include "physics.h"
#include "room.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ EntityData entityData;
    /*0x78*/ u8 unused[14];
    /*0x86*/ u16 unk_86;
} RemovableDustEntity;

PORT_STATIC_ASSERT_SIZE(RemovableDustEntity, 0x88, 0xB0, "RemovableDustEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(RemovableDustEntity, entityData, 0x68, 0x90,
                          "RemovableDustEntity entityData offset incorrect");
PORT_STATIC_ASSERT_OFFSET(RemovableDustEntity, unk_86, 0x86, 0xAE, "RemovableDustEntity flag offset incorrect");

#ifdef PC_PORT
#define DUST_FLAG(this) (GE_FIELD(&((this)->base), field_0x86)->HWORD)
#else
#define DUST_FLAG(this) ((this)->unk_86)
#endif

extern void (*const RemovableDust_Functions[])(RemovableDustEntity*);
extern const u16 gUnk_08129FD0[];
extern const u16 gUnk_08129FE4[];
extern const s8 gUnk_08129FF8[];
extern const u8 gUnk_0812A004[];

void sub_080AA494(RemovableDustEntity*);
void sub_080AA534(Entity*);
void RemovableDust_OnGrabbed(RemovableDustEntity*);
void sub_080AA544(RemovableDustEntity*);
void sub_080AA654(RemovableDustEntity*, u32);

void RemovableDust(RemovableDustEntity* this) {
    RemovableDust_Functions[GetNextFunction(super)](this);
}

void RemovableDust_OnTick(RemovableDustEntity* this) {
    if (super->action == 0) {
        super->action = 1;
        super->frameIndex = super->type;
        super->gustJarFlags = 1;
        super->speed = DUST_FLAG(this);
        if (super->type == 0) {
            sub_080AA494(this);
        } else {
            sub_080AA534(super);
        }
    }
}

void RemovableDust_OnCollision(RemovableDustEntity* this) {
    if (super->contactFlags == (CONTACT_NOW | 0x16)) {
        RemovableDust_OnGrabbed(this);
    }
}

void RemovableDust_OnGrabbed(RemovableDustEntity* this) {
    Entity* entity;

    if (super->type == 0) {
        sub_080AA544(this);
    }
    entity = CreateObject(DIRT_PARTICLE, 3, 0);
    if (entity != NULL) {
        CopyPosition(super, entity);
    }
    DeleteEntity(super);
}

void sub_080AA494(RemovableDustEntity* this) {
    u32 tileType;
    const u16* iterator;
    u32 index;

    index = 0;
    tileType = GetTileTypeAtEntity(super);
    iterator = gUnk_08129FD0;
    while (*iterator != 0) {
        if (*(iterator++) == tileType) {
            break;
        }
        index++;
    }
    if (CheckFlags((u16)super->speed) != 0) {
        if (index == 4) {
            sub_080AA654(this, TILE(super->x.HALF.HI, super->y.HALF.HI));
        }
        DeleteThisEntity();
    }
    super->type2 = index;
    super->spritePriority.b0 = 7;
    SetTile(SPECIAL_TILE_104, TILE(super->x.HALF.HI, super->y.HALF.HI), super->collisionLayer);
}

void sub_080AA534(Entity* this) {
    this->collisionLayer = 3;
    UpdateSpriteForCollisionLayer(this);
}

void sub_080AA544(RemovableDustEntity* this) {
    u8* pbVar1;
    s32 actTile;
    u32 uVar3;
    s32 iVar4;
    const u16* puVar5;
    u32 tilePos;
    const s8* tmp;

    if (super->type2 < 9) {
        tmp = gUnk_08129FF8;
        tilePos = TILE(super->x.HALF.HI, super->y.HALF.HI) + tmp[super->type2];
        uVar3 = 0;
        iVar4 = 0;
        do {
            actTile = GetActTileAtTilePos((tilePos - tmp[uVar3]) & 0xffff, super->collisionLayer);
            if (actTile == ACT_TILE_62) {
                iVar4++;
            }
            uVar3++;
        } while (uVar3 < 9);

        if (iVar4 == 8) {
            uVar3 = 0;
            puVar5 = gUnk_08129FD0;
            do {
                sub_0807B7D8((u32)*puVar5, tilePos - tmp[uVar3], super->collisionLayer);
                puVar5++;
                uVar3++;
            } while (uVar3 < 9);
            sub_080AA654(this, tilePos);
            SetFlag((u16)super->speed);
        } else {
            sub_0807B7D8(gUnk_08129FE4[super->type2], tilePos - tmp[super->type2], super->collisionLayer);
        }
    } else {
        RestorePrevTileEntity(TILE(super->x.HALF.HI, super->y.HALF.HI), super->collisionLayer);
        SetFlag((u16)super->speed);
    }
}

void sub_080AA654(RemovableDustEntity* this, u32 param) {
    EntityData* entityData;
    entityData = &this->entityData;

    MemCopy(&gUnk_0812A004, entityData, 0x10);

    entityData->xPos = ((u16)param & 0x3f) * 0x10 + 8;
    entityData->yPos = ((param & 0xfc0) >> 2) + 8;

    LoadRoomEntity(entityData);
}

void (*const RemovableDust_Functions[])(RemovableDustEntity*) = {
    RemovableDust_OnTick,
    RemovableDust_OnCollision,
    (void (*)(RemovableDustEntity*))DeleteEntity,
    (void (*)(RemovableDustEntity*))DeleteEntity,
    (void (*)(RemovableDustEntity*))DeleteEntity,
    RemovableDust_OnGrabbed,
};
const u16 gUnk_08129FD0[] = { 387, 1018, 1019, 1020, 1021, 1022, 1023, 1024, 1025, 0 };
const u16 gUnk_08129FE4[] = { 388, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 0 };
const s8 gUnk_08129FF8[] = { 65, 64, 63, 1, 0, -1, -63, -64, -65, 0, 0, 0 };
const u8 gUnk_0812A004[] = {
    9, 0, 3, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
