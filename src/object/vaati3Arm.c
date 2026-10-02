/**
 * @file vaati3Arm.c
 * @ingroup Objects
 *
 * @brief Vaati3 Arm object
 */
#include "entity.h"
#include "physics.h"

void sub_080A0640(Entity*);
void Vaati3Arm_Init(Entity*);
void Vaati3Arm_Action1(Entity*);
void Vaati3Arm_Action2(Entity*);

void Vaati3Arm(Entity* this) {
    static void (*const Vaati3Arm_Actions[])(Entity*) = {
        Vaati3Arm_Init,
        Vaati3Arm_Action1,
        Vaati3Arm_Action2,
    };
    Vaati3Arm_Actions[this->action](this);
}

void Vaati3Arm_Init(Entity* this) {
    if (this->type != 2) {
        this->action = 1;
        this->spritePriority.b0 = 6;
        sub_080A0640(this);
        InitializeAnimation(this, 1);
    } else {
        this->action = 2;
        this->y.HALF.HI++;
        this->z.HALF.HI = 0;
        this->spriteOffsetY--;
        InitializeAnimation(this, 3);
    }
}

void Vaati3Arm_Action1(Entity* this) {
    if (this->parent == NULL) {
        this->action = 2;
        InitializeAnimation(this, 2);
    } else {
        if (this->parent->next == NULL) {
            DeleteThisEntity();
#ifdef PC_PORT
            return;
#endif
        }
        sub_080A0640(this);
        GetNextFrame(this);
    }
}

void Vaati3Arm_Action2(Entity* this) {
    GetNextFrame(this);
    if (this->frame & ANIM_DONE) {
        DeleteThisEntity();
    }
}

void sub_080A0640(Entity* this) {
    if (this->type == 0) {
#ifdef PC_PORT
        /* #136 family — Vaati's death sequence (vaatiArm.c:1159) sets
           entities[4]->myHeap = NULL while keeping entities[4] alive as this
           type-0 arm's parent, so the heap[4] read below would deref
           ((Entity**)NULL)[4]. On GBA that read BIOS bytes; on PC it SIGSEGVs.
           Skip positioning this frame until the heap is valid again. */
        if (this->parent == NULL || this->parent->myHeap == NULL) {
            return;
        }
#endif
        PositionRelative(*(((Entity**)this->parent->myHeap) + 4), this, 0, Q_16_16(8.0));
    } else {
        CopyPosition(this->parent, this);
    }
    this->z.HALF.HI = 0;
}
