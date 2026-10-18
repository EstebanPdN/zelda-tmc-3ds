/**
 * @file lilypadSmall.c
 * @ingroup Objects
 *
 * @brief Lilypad Small object
 */
#include "object.h"
#include "asm.h"
#include "area.h"
#include "room.h"
#include "roomid.h"
#include "player.h"

#ifdef PC_PORT
#include <stdio.h>
#include <stdlib.h>
#endif

extern void* gLilypadRails[];

static const u16 sMinishWoodsLilypadRail0[] = {
    0x18, 0xa0, 0x80, 0x00, 0x40, 0x80, 0x08, 0xa0, 0x80, 0x10, 0x40, 0x80, 0xfe, 0x04, 0x00,
};

static const u16 sMinishWoodsLilypadRail1[] = {
    0x08, 0xa0, 0x80, 0x10, 0x40, 0x80, 0x18, 0xa0, 0x80, 0x00, 0x40, 0x80, 0xfe, 0x04, 0x00,
};

static const u16* const sMinishWoodsLilypadRails[] = {
    sMinishWoodsLilypadRail0,
    sMinishWoodsLilypadRail1,
};

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[8];
    /*0x70*/ u16 unk_70;
} LilypadSmallEntity;

static void sub_08097B24(LilypadSmallEntity* this);
static bool32 CheckMovePlayer(LilypadSmallEntity* this);

void LilypadSmall(LilypadSmallEntity* this) {
    u32 rand;
    u16* psVar4;

    if (super->action == 0) {
        super->action = 1;
        super->timer = 90;
        rand = Random();
        super->subtimer = rand;
        super->frameIndex = (rand >> 0x10) & 3;
        super->spriteSettings.draw = TRUE;
        super->spritePriority.b0 = 7;
        if (gRoomControls.area == AREA_MINISH_WOODS && gRoomControls.room == ROOM_MINISH_WOODS_MAIN &&
            super->type2 >= 0x12 && super->type2 <= 0x13) {
            super->child = (Entity*)sMinishWoodsLilypadRails[super->type2 - 0x12];
        } else if (super->type2 >= 0x80 && (super->type2 & 7) < 3) {
            super->child = (Entity*)gLilypadRails[super->type2 & 7];
        } else {
            super->child = GetCurrentRoomProperty(super->type2);
        }
        UpdateRailMovement(super, (u16**)&super->child, &this->unk_70);
    }
    SyncPlayerToPlatform(super, CheckMovePlayer(this));
    sub_08097B24(this);
    psVar4 = &this->unk_70;
    if (--*psVar4 == 0) {
        UpdateRailMovement(super, (u16**)&super->child, psVar4);
    }
}

static bool32 CheckMovePlayer(LilypadSmallEntity* this) {
    if (!(gPlayerState.flags & PL_MINISH)) {
        return FALSE;
    } else if (EntityInRectRadius(super, &gPlayerEntity.base, 8, 8) == 0) {
        return FALSE;
    } else if (!PlayerCanBeMoved()) {
        return FALSE;
    } else {
        gPlayerState.field_0x14 = 1;
        if (gPlayerEntity.base.z.HALF.HI != 0) {
            return FALSE;
        } else {
            return TRUE;
        }
    }
}

static void sub_08097B24(LilypadSmallEntity* this) {
    static const u16 gUnk_08123318[] = {
        0x100, 0x101, 0x102, 0x101, 0x100, 0xff, 0xfe, 0xff,
    };
    u32 temp;
    u32 temp2;
    const u16* temp3;

    if (--super->timer == 0) {
        super->timer = 90;
        super->frameIndex = (super->frameIndex + 1) & 3;
    }
    temp3 = gUnk_08123318;
    temp2 = ++super->subtimer;

    temp = temp3[(temp2 >> 5) & 7];
    SetAffineInfo(super, temp, temp, 0);
}
