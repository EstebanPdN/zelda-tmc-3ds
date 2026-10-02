/**
 * @file minishSizedEntrance.c
 * @ingroup Objects
 *
 * @brief MinishSizedEntrance object
 */
#include "game.h"
#include "object.h"
#include "asm.h"
#include "room.h"
#include "player.h"
#include "vram.h"
#ifdef PC_PORT
#include "manager/minishSizedEntranceManager.h"
#endif

void MinishSizedEntrance_Action1(Entity*);
void MinishSizedEntrance_Init(Entity*);

void MinishSizedEntrance(Entity* this) {
    static void (*const MinishSizedEntrance_Actions[])(Entity*) = {
        MinishSizedEntrance_Init,
        MinishSizedEntrance_Action1,
    };
    MinishSizedEntrance_Actions[this->action](this);
}

void MinishSizedEntrance_Init(Entity* this) {
    this->action = 1;
    this->spriteRendering.b3 = 3;
    this->spritePriority.b0 = 7;
    this->frameIndex = this->type2;
    if (AreaIsDungeon()) {
        this->frameIndex += 4;
        UnloadGFXSlots(this);
        LoadFixedGFX(this, 0x184);
    }
}

void MinishSizedEntrance_Action1(Entity* this) {
    static const u16 gUnk_0812225C[] = {
        0x400,
        0x100,
        0x800,
        0x200,
    };
    if (this->type == 1) {
        Entity* parent = this->parent;
        u32 mask = 1 << this->subtimer;
#ifdef PC_PORT
        /* #128 fix: on GBA, parent->zVelocity (Entity offset 0x20)
         * aliases the MinishSizedEntranceManager's field_0x20 — the
         * bitfield the manager uses to track which entrances it has
         * spawned. On PC the Manager struct widens (prev/next/parent/
         * child are 8-byte pointers), so Manager offset 0x20 is now
         * the parent field's low 4 bytes, not field_0x20. Reading
         * parent->zVelocity returns random pointer bits, the bit
         * check fails, and the minish door self-deletes immediately
         * — "doors disappear shortly after loading the area".
         *
         * Cast to the concrete manager type and read field_0x20
         * directly. */
        MinishSizedEntranceManager* mgr = (MinishSizedEntranceManager*)parent;
        if (!(mgr->field_0x20 & mask)) {
            DeleteThisEntity();
        }
#else
        if (!(parent->zVelocity & mask)) {
            DeleteThisEntity();
        }
#endif
    }
    if ((gPlayerState.flags & PL_MINISH) && EntityInRectRadius(this, &gPlayerEntity.base, 4, 4) &&
        (gPlayerEntity.base.z.HALF.HI == 0) &&
        (((u16)gPlayerState.playerInput.heldInput) & gUnk_0812225C[this->type2])) {
        DoExitTransition(GetCurrentRoomProperty(this->timer));
    }
}
