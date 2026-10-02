/**
 * @file whirlwind.c
 * @ingroup Objects
 *
 * @brief Whirlwind object
 */
#include "area.h"
#include "script.h"
#include "hitbox.h"
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "flags.h"
#include "physics.h"
#include "player.h"
#include "color.h"

void Whirlwind_Init(Entity*);
void Whirlwind_Action1(Entity*);
void Whirlwind_Action2(Entity*);

void Whirlwind(Entity* this) {
    static void (*const Whirlwind_Actions[])(Entity*) = {
        Whirlwind_Init,
        Whirlwind_Action1,
        Whirlwind_Action2,
    };
    u32 tmp = this->health;
#ifdef PC_PORT
    /* GBA #82 fix: gArea.filler6 happens to be at EWRAM 0x020342F8 on
     * GBA — i.e. it aliases gUnk_020342F8, the bitmap
     * DelayedEntityLoadManager actually writes to. On the PC port, gArea
     * is a standalone global so gArea.filler6 is a different (always-zero)
     * buffer, every spawned Whirlwind sees the bit cleared, and they all
     * self-delete — visible as missing wind vortexes in Lon Lon Ranch
     * (and any other map that spawns whirlwinds via the delayed-entity
     * path). pinwheel.c and npc.c already do it the right way. */
    {
        extern u8 gUnk_020342F8[];
        if (((tmp & 0x7f) != 0) && (ReadBit((u32*)gUnk_020342F8, tmp - 1) == 0)) {
            DeleteThisEntity();
        }
    }
#else
    if (((tmp & 0x7f) != 0) && (ReadBit(gArea.filler6, tmp - 1) == 0)) {
        DeleteThisEntity();
    }
#endif
    Whirlwind_Actions[this->action](this);
}

void Whirlwind_Init(Entity* this) {
    this->action = 1;
    this->hitbox = (Hitbox*)&gUnk_080FD320;
    this->flags |= ENT_COLLIDE;
    this->hitType = 1;
    this->collisionFlags = 0x47;
    this->hurtType = 0x44;
    this->collisionMask = 0x80;
    this->spriteOrientation.flipY = 1;
    this->spriteRendering.b3 = 1;
    this->animationState = this->type;
    this->direction = this->type << 3;
    this->speed = 0x700;
    if (this->type2 == 1) {
        ChangeObjPalette(this, 0x161);
        InitializeAnimation(this, 1);
    } else {
        this->z.HALF.HI = -24;
        InitializeAnimation(this, 1);
    }
    if ((this->flags & ENT_SCRIPTED) != 0) {
        sub_0807DD64(this);
        ExecuteScriptForEntity(this, NULL);
    }
}

void Whirlwind_Action1(Entity* this) {
    if ((this->flags & ENT_SCRIPTED) != 0) {
        ExecuteScriptForEntity(this, NULL);
    }
    if (this->spriteSettings.draw != 0) {
        GetNextFrame(this);
        if ((((((gPlayerState.flags & PL_MINISH) == 0)) && (gPlayerState.killed == 0)) &&
             ((gPlayerEntity.base.action == PLAYER_JUMP || ((gPlayerEntity.base.flags & ENT_COLLIDE) != 0)))) &&
            ((gPlayerEntity.base.action != PLAYER_BOUNCE &&
              (sub_0800419C(this, &gPlayerEntity.base, 0xc, 0xc) != 0)))) {
            if (((gPlayerState.flags & PL_PARACHUTE) == 0) && (gPlayerEntity.base.action != PLAYER_JUMP)) {
                if ((this->type2 != 1) && (-0x10 < gPlayerEntity.base.z.HALF.HI)) {
                    return;
                }
            } else if (gPlayerEntity.base.z.HALF.HI == 0) {
                return;
            }
            CopyPosition(this, &gPlayerEntity.base);
            sub_08004542(this);
            sub_08004542(&gPlayerEntity.base);
            gPlayerEntity.base.collisionLayer = 1;
            gPlayerEntity.base.flags |= ENT_COLLIDE;
            gPlayerState.queued_action = PLAYER_PARACHUTE;
            gPlayerState.field_0x38 = this->type2;
            gPlayerState.field_0x39 = 0xff;
            this->spritePriority.b0 = gPlayerEntity.base.spritePriority.b0 - 1;
            this->action++;
            PutAwayItems();
            SoundReq(SFX_153);
            SoundReq(SFX_PLY_VO5);
        }
    }
}

void Whirlwind_Action2(Entity* this) {
    UpdateAnimationSingleFrame(this);
    if (sub_0800419C(this, &gPlayerEntity.base, 0xc, 0xc) == 0) {
        ResetCollisionLayer(this);
        this->spriteOrientation.flipY = 1;
        this->spriteRendering.b3 = 1;
        this->action--;
    }
}
