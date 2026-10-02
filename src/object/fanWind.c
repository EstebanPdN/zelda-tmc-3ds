/**
 * @file fanWind.c
 * @ingroup Objects
 *
 * @brief Fan Wind object
 */
#include "collision.h"
#include "entity.h"
#include "physics.h"
#include "map.h"

void FanWind(Entity* this) {
    u8* collisionData;

    if (this->action == 0) {
        this->action = 1;
        this->timer = 16;
        this->direction = this->type << 3;
        if (this->collisionLayer == 2) {
            collisionData = gMapTop.collisionData;
        } else {
            collisionData = gMapBottom.collisionData;
        }
        this->child = (Entity*)collisionData;
        InitializeAnimation(this, this->type);
    }
    /* #97 pattern: fan wind effect can outlive the fan that spawned it. */
    if (this->parent == NULL) {
        DeleteThisEntity();
        return;
    }
    this->speed = this->parent->speed;
    LinearMoveUpdate(this);
    if (IsTileCollision((u8*)this->child, this->x.HALF.HI, this->y.HALF.HI, 9) != 0) {
        DeleteThisEntity();
    }
    if (this->speed < 0x41) {
        this->spriteSettings.draw ^= 1;

        if (--this->timer == 0) {
            DeleteThisEntity();
        }
    }
    GetNextFrame(this);
}
