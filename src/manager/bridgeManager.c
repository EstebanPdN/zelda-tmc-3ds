/**
 * @file bridgeManager.c
 * @ingroup Managers
 *
 * @brief Manages spawn/removal of bridges
 */
#include "manager/bridgeManager.h"
#include "flags.h"
#include "sound.h"
#include "player.h"
#include "room.h"
#include "map.h"
#include "asm.h"
#ifdef PC_PORT
#include <stdio.h>
#include "area.h"
#include "roomid.h"
#endif

void BridgeManager_Init(BridgeManager*);
void BridgeManager_Action1(BridgeManager*);
void BridgeManager_Action2(BridgeManager*);

static const u16 gUnk_08108024[] = { 0, -1, 1, 0, 0, 1, -1, 0 };
static const u16 gUnk_08108034[] = { 0, 1, -1, 0, 0, -1, 1, 0 };

void BridgeManager_Main(BridgeManager* this) {
    static void (*const BridgeManager_Actions[])(BridgeManager*) = {
        BridgeManager_Init,
        BridgeManager_Action1,
        BridgeManager_Action2,
    };
    BridgeManager_Actions[super->action](this);
}

void sub_08057CA4(BridgeManager*, u32, u32);

void BridgeManager_Init(BridgeManager* this) {
    u32 tmp;
    tmp = (super->type2 & 0x3) << 1;
    if (super->timer == 1) {
        this->unk_30 = 0x323;
    } else {
        this->unk_30 = tmp & 2 ? 0x37 : 0x36;
    }
    this->unk_28 = gUnk_08108024[tmp];
    this->unk_2a = gUnk_08108024[tmp + 1];
    this->unk_2c = gUnk_08108034[tmp];
    this->unk_2e = gUnk_08108034[tmp + 1];
    this->unk_32 = ((super->type2 >> 2) & 0xF) + 1;
    super->timer = 28;
    super->subtimer = 0;
    super->action = (super->type2 & 0x80) ? 2 : 1;
#ifdef PC_PORT
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
        fprintf(stderr, "[BRIDGE7] Init flag=0x%04X x=%d y=%d layer=%u len=%u type2=0x%02X action=%u check=%u\n",
                this->flags, this->x, this->y, this->unk_3c, this->unk_32, super->type2, super->action,
                CheckFlags(this->flags));
    }
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
        fprintf(stderr,
                "[POTBRIDGE] Bridge_Init flag=0x%04X x=%d y=%d layer=%u len=%u type2=0x%02X action=%u check=%u\n",
                this->flags, this->x, this->y, this->unk_3c, this->unk_32, super->type2, super->action,
                CheckFlags(this->flags));
    }
#endif
    if (super->action != 2 || !CheckFlags(this->flags))
        return;
    for (; this->unk_32; this->unk_32--) {
        sub_08057CA4(this, this->unk_28, this->unk_2a);
        sub_0807B7D8(this->unk_30, this->x | (this->y << 6), this->unk_3c);
    }
    DeleteManager(super);
}

void BridgeManager_Action1(BridgeManager* this) {
    if (--super->timer)
        return;
    super->timer = 8;
#ifdef PC_PORT
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_BUTTON) {
        fprintf(stderr, "[BRIDGE7] Tick flag=0x%04X check=%u progress=%u/%u tilePos=0x%03X layer=%u\n", this->flags,
                CheckFlags(this->flags), super->subtimer, this->unk_32, this->x | (this->y << 6), this->unk_3c);
    }
    if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
        fprintf(stderr,
                "[POTBRIDGE] Bridge_Tick flag=0x%04X check=%u progress=%u/%u tilePos=0x%03X layer=%u tile=0x%X\n",
                this->flags, CheckFlags(this->flags), super->subtimer, this->unk_32, this->x | (this->y << 6),
                this->unk_3c, GetTileTypeAtTilePos(this->x | (this->y << 6), this->unk_3c));
    }
#endif
    if (CheckFlags(this->flags)) {
        if (this->unk_32 == super->subtimer)
            return;
        sub_08057CA4(this, this->unk_28, this->unk_2a);
        sub_0807B7D8(this->unk_30, this->x | (this->y << 6), this->unk_3c);
        super->subtimer++;
#ifdef PC_PORT
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
            fprintf(stderr, "[POTBRIDGE] Bridge_Set step=%u tilePos=0x%03X layer=%u tile=0x%X setType=0x%X\n",
                    super->subtimer, this->x | (this->y << 6), this->unk_3c,
                    GetTileTypeAtTilePos(this->x | (this->y << 6), this->unk_3c), this->unk_30);
        }
#endif
        SoundReq(SFX_HEART_GET);
    } else {
        if (!super->subtimer)
            return;
        RestorePrevTileEntity(this->x | (this->y << 6), this->unk_3c);
        sub_08057CA4(this, this->unk_2c, this->unk_2e);
        super->subtimer--;
#ifdef PC_PORT
        if (gRoomControls.area == AREA_DEEPWOOD_SHRINE && gRoomControls.room == ROOM_DEEPWOOD_SHRINE_POT_BRIDGE) {
            fprintf(stderr, "[POTBRIDGE] Bridge_Clear step=%u tilePos=0x%03X layer=%u tile=0x%X\n", super->subtimer,
                    this->x | (this->y << 6), this->unk_3c,
                    GetTileTypeAtTilePos(this->x | (this->y << 6), this->unk_3c));
        }
#endif
        SoundReq(SFX_HEART_GET);
    }
}

void BridgeManager_Action2(BridgeManager* this) {
    if (super->subAction == 0) {
        if (!CheckFlags(this->flags))
            return;
        super->subAction++;
    } else {
        if (--super->timer)
            return;
        super->timer = 8;
        if (this->unk_32 != super->subtimer) {
            sub_08057CA4(this, this->unk_28, this->unk_2a);
            sub_0807B7D8(this->unk_30, this->x | (this->y << 6), this->unk_3c);
            super->subtimer++;
            SoundReq(SFX_HEART_GET);
        } else {
            if (this->unk_30 != 0x323) {
                SoundReq(SFX_SECRET);
            }
            DeleteManager(super);
        }
    }
}

void sub_08057CA4(BridgeManager* this, u32 xOffset, u32 yOffset) {
    this->x += xOffset;
    this->y += yOffset;
}
