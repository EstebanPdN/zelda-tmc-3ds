/**
 * @file rupee.c
 * @ingroup Objects
 *
 * @brief Rupee object
 */
#include "hitbox.h"
#include "object.h"
#include "asm.h"
#include "physics.h"

void sub_08086A6C(Entity*);
void Rupee_Init(Entity*);
void Rupee_Action_1(Entity*);
void Rupee_Action_2(Entity*);

// Main
void Rupee(Entity* this) {
    static void (*const Rupee_Actions[])(Entity*) = {
        Rupee_Init,
        Rupee_Action_1,
        Rupee_Action_2,
    };

    Rupee_Actions[this->action](this);
}

void Rupee_Init(Entity* this) {
    Entity* itemEntity;

    this->action = 1;
    this->spriteSettings.draw = 0;
    this->hitbox = (Hitbox*)&gUnk_080FD1A8;
    this->collisionFlags |= 16;
    itemEntity = CreateObject(GROUND_ITEM, this->type, 0);
    if (itemEntity != NULL) {
        itemEntity->timer = 10;
        itemEntity->parent = this;
        this->child = itemEntity;
        CopyPosition(this, itemEntity);
        sub_08086A6C(this);
    }
}

void Rupee_Action_1(Entity* this) {
    /* #97 pattern: Rupee_Init advanced action to 1 before CreateObject; the
     * spawn may have failed and left child == NULL. */
    if (this->child == NULL || this->child->next == NULL) {
        this->action = 2;
    } else {
        if (BounceUpdate(this, Q_8_8(40.0)) == BOUNCE_DONE_ALL) {
            this->action = 2;
        }
        ProcessMovement0(this);
        CopyPosition(this, this->child);
    }
}

void Rupee_Action_2(Entity* this) {
    this->child->parent = NULL;
    DeleteThisEntity();
}

void sub_08086A6C(Entity* this) {
    u32 uVar1;

    uVar1 = Random();
    this->zVelocity = Q_16_16(2.5);
    this->direction = DirectionNormalize(uVar1 >> 16);
    this->speed = uVar1 & 480;
}
