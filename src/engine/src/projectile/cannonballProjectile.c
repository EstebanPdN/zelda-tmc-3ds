/**
 * @file cannonballProjectile.c
 * @ingroup Projectiles
 *
 * @brief Cannonball Projectile
 */
#include "collision.h"
#include "enemy.h"
#include "effects.h"
#include "projectile.h"
#include "entity.h"
#include "physics.h"
#include "asm.h"
#include "manager/angryStatueManager.h"

extern void (*const CannonballProjectile_Functions[])(Entity*);
extern void (*const CannonballProjectile_Actions[])(Entity*);

bool32 sub_080AB5F4(Entity*);
bool32 sub_080AB634(Entity*);

void CannonballProjectile(Entity* this) {
    CannonballProjectile_Functions[GetNextFunction(this)](this);
}

void CannonballProjectile_OnTick(Entity* this) {
    CannonballProjectile_Actions[this->action](this);
}

void CannonballProjectile_OnCollision(Entity* this) {
    u32 tmp;

    if (this->iframes < -4) {
        this->action = 2;
        this->direction = this->knockbackDirection;
        tmp = (this->type ^ 2) << 3;
        if (this->direction - tmp + 1 < 3) {
            this->direction = tmp;
        }
        this->speed = 0x280;
    }
}

void CannonballProjectile_Init(Entity* this) {
    this->action = 1;
    this->direction = this->type << 3;
    this->z.HALF.HI = -4;
    InitializeAnimation(this, this->type);
}

void CannonballProjectile_Action1(Entity* this) {
    GetNextFrame(this);
    if (ProcessMovement3(this) == 0) {
        CreateFx(this, FX_DEATH, 0);
        DeleteThisEntity();
    }
    sub_080AB5F4(this);
}

void CannonballProjectile_Action2(Entity* this) {
    GetNextFrame(this);
    ProcessMovement3(this);
    if ((sub_080AB634(this) == 0) && (this->collisions != COL_NONE)) {
        CreateFx(this, FX_DEATH, 0);
        DeleteThisEntity();
    }
}

bool32 sub_080AB5F4(Entity* this) {
    switch (GetTileHazardType(this)) {
        case 1:
            CreatePitFallFx(this);
            return TRUE;
        case 2:
            CreateDrownFx(this);
            return TRUE;
        case 3:
            CreateLavaDrownFx(this);
            return TRUE;
    }
    return FALSE;
}

bool32 sub_080AB634(Entity* this) {
#ifdef PC_PORT
    /* #-port crash fix: on GBA `&this->parent->zVelocity` (Entity offset 0x20)
     * aliased the parent AngryStatueManager's `field_0x20[4]` array exactly,
     * because GBA Entity* slots are 4 bytes (Manager is 0x20, field_0x20[] at
     * +0x20 == zVelocity's offset). On x86-64 the pointer-widened Manager base
     * grows to 0x38, so `&parent->zVelocity` now lands 0x10 bytes BEFORE the
     * real field_0x20[] and is read as 8-byte slots — splicing unrelated
     * Manager fields into non-NULL wild pointers that pass the `!= NULL` test
     * and SIGSEGV inside IsColliding (which derefs `that->collisionLayer` /
     * `that->hitbox` before its host-pointer guard). Reachable in normal play:
     * deflecting a cannonball back to destroy the AngryStatues. Index the real
     * array, and NULL-guard the parent. */
    AngryStatueManager* parent = (AngryStatueManager*)this->parent;
    Entity** entities;
    u32 i;
    if (parent == NULL) {
        return FALSE;
    }
    entities = parent->field_0x20;
#else
    Entity** entities = ((Entity**)&this->parent->zVelocity);
    u32 i;
#endif
    for (i = 0; i <= 3; ++i) {
        if (entities[i] != NULL && (IsColliding(this, entities[i]) != 0)) {
            if (entities[i]->action < 3) {
                entities[i]->action = 3;
                entities[i]->timer = 30;
                entities[i]->spriteSettings.draw = 0;
                CreateFx(entities[i], FX_WHITE_ROCK, 0);
            }
            DeleteEntity(this);
            return TRUE;
        }
    }
    return FALSE;
}

void (*const CannonballProjectile_Functions[])(Entity*) = {
    CannonballProjectile_OnTick, CannonballProjectile_OnCollision, DeleteEntity, DeleteEntity, DeleteEntity,
};
void (*const CannonballProjectile_Actions[])(Entity*) = {
    CannonballProjectile_Init,
    CannonballProjectile_Action1,
    CannonballProjectile_Action2,
};
