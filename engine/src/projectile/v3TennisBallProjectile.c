/**
 * @file v3TennisBallProjectile.c
 * @ingroup Projectiles
 *
 * @brief V3 Tennis Ball Projectile
 */
#include "collision.h"
#include "enemy.h"
#include "sound.h"
#include "effects.h"
#include "projectile.h"
#include "entity.h"
#include "physics.h"

extern void (*const V3TennisBallProjectile_Functions[])(Entity*);
extern void (*const V3TennisBallProjectile_Actions[])(Entity*);

void sub_080ACB90(Entity*);
bool32 sub_080ACB40(Entity* this);

void V3TennisBallProjectile(Entity* this) {
    V3TennisBallProjectile_Functions[GetNextFunction(this)](this);
}

void V3TennisBallProjectile_OnTick(Entity* this) {
    V3TennisBallProjectile_Actions[this->action](this);
}

void V3TennisBallProjectile_OnCollision(Entity* this) {
    switch (this->contactFlags & 0x7f) {
        case 0x1a:
        case 0xa:
        case 0xb:
        case 0xc:
        case 0x6: {
            this->action = 2;
            this->flags &= ~ENT_COLLIDE;
            this->speed += 0x80 * 2;
            this->child = this->contactedEntity;
            if (sub_080ACB40(this)) {
                this->direction = DirectionNorth;
            } else {
                this->direction = this->knockbackDirection;
            }

            SoundReq(SFX_ITEM_SWORD_CHARGE_FINISH);
            break;
        }
        default: {
            DeleteThisEntity();
            break;
        }
    }
}

void V3TennisBallProjectile_Init(Entity* this) {
    this->action = 1;
    this->direction = DirectionSouth;
    this->z.HALF.HI = -4;
    this->child = NULL;
    InitializeAnimation(this, 7);
    SoundReq(SFX_199);
}

void V3TennisBallProjectile_Action1(Entity* this) {
    ProcessMovement3(this);
    GetNextFrame(this);
    if (IsProjectileOffScreen(this)) {
        DeleteThisEntity();
    }
}

void V3TennisBallProjectile_Action2(Entity* this) {
    ProcessMovement3(this);
    if (this->collisions != COL_NONE) {
        DeleteThisEntity();
    }
    GetNextFrame(this);
    sub_080ACB90(this);
    if (IsProjectileOffScreen(this)) {
        DeleteThisEntity();
    }
}

bool32 sub_080ACB40(Entity* this) {
    Entity* r1_grandparent;
    Entity* child;
#ifndef PC_PORT
    Entity* tmp;
#endif
    /* #97 pattern: parent or grandparent may have died this frame. */
    if (this->parent == NULL || this->parent->parent == NULL) {
        return FALSE;
    }
    r1_grandparent = this->parent->parent;
    child = this->child;
#ifdef PC_PORT
    /* #140-class: heap[7..10] are the four eyes, which Vaati's defeat cleanup
     * (vaatiWrath.c:sub_08041BE8) DeleteEntity's and NULLs. A tennis ball
     * already in flight outlives them and still reaches OnCollision -> here,
     * so an eye slot (or the whole heap) can be NULL. The GBA read harmless
     * open-bus at the small address; PC SIGSEGVs on eye->child. Skip any eye
     * that's gone (a deleted eye can't be claiming a target). */
    Entity** heap = (Entity**)r1_grandparent->myHeap;
    int i;
    if (heap == NULL) {
        return TRUE;
    }
    for (i = 7; i <= 10; i++) {
        Entity* tmp = heap[i];
        if (tmp == NULL) {
            continue;
        }
        tmp = tmp->child;
        if (tmp == NULL) {
            continue;
        }
        if (tmp != this && child == tmp->child) {
            return FALSE;
        }
    }
    return TRUE;
#else
    tmp = ((Entity**)(r1_grandparent->myHeap))[7]->child;

    if (tmp != this && child == tmp->child) {
        return FALSE;
    }

    tmp = ((Entity**)(r1_grandparent->myHeap))[8]->child;

    if (tmp != this && child == tmp->child) {
        return FALSE;
    }

    tmp = ((Entity**)(r1_grandparent->myHeap))[9]->child;

    if (tmp != this && child == tmp->child) {
        return FALSE;
    }

    tmp = ((Entity**)(r1_grandparent->myHeap))[10]->child;

    if (tmp != this && child == tmp->child) {
        return FALSE;
    }

    return TRUE;
#endif
}

void sub_080ACB90(Entity* this) {
    Entity* parent = this->parent;
    if ((this->x.HALF.HI == parent->x.HALF.HI) && (this->y.HALF.HI <= parent->y.HALF.HI)) {
        parent->subtimer = 1;
        CreateFx(this, FX_REFLECT2, 0x40);
        EnqueueSFX(SFX_ITEM_GLOVES_KNOCKBACK);
        DeleteThisEntity();
    }
}

void (*const V3TennisBallProjectile_Functions[])(Entity*) = {
    V3TennisBallProjectile_OnTick, V3TennisBallProjectile_OnCollision, DeleteEntity, DeleteEntity, DeleteEntity,
    V3TennisBallProjectile_OnTick,
};
void (*const V3TennisBallProjectile_Actions[])(Entity*) = {
    V3TennisBallProjectile_Init,
    V3TennisBallProjectile_Action1,
    V3TennisBallProjectile_Action2,
};
