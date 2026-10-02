/**
 * @file mazaalHand.c
 * @ingroup Enemies
 *
 * @brief Mazaal Hand enemy
 */
#include "entity.h"
#include "hitbox.h"

void sub_08035194(Entity*);

const Hitbox* const gUnk_080CEF34[] = {
    &gUnk_080FD394, &gUnk_080FD394, &gUnk_080FD394, &gUnk_080FD39C, &gUnk_080FD3A4,
    &gUnk_080FD3AC, &gUnk_080FD3AC, &gUnk_080FD3AC, &gUnk_080FD3AC,
};
const Hitbox* const gUnk_080CEF58[] = {
    &gUnk_080FD3B4, &gUnk_080FD3B4, &gUnk_080FD3B4, &gUnk_080FD3BC, &gUnk_080FD3A4,
    &gUnk_080FD3C4, &gUnk_080FD3C4, &gUnk_080FD3C4, &gUnk_080FD3C4,
};

void MazaalHand(Entity* this) {
    if (this->action == 0) {
        this->action = 1;
        this->spriteSettings.flipX = this->type;
        InitAnimationForceUpdate(this, 0);
    }
    sub_08035194(this);
}

void sub_08035194(Entity* this) {
    /* #91 / #97 pattern: tables have 9 entries; frameIndex may exceed that
     * during animation transitions. Clamp to keep the last valid hitbox. */
    u32 idx = this->frameIndex;
    if (idx >= 9) idx = 8;
    if (this->type == 0) {
        this->hitbox = (Hitbox*)gUnk_080CEF34[idx];
    } else {
        this->hitbox = (Hitbox*)gUnk_080CEF58[idx];
    }
}
