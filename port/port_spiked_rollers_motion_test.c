#include "entity.h"
#include "physics.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    Entity base;
    s16 anchorX;
    s16 anchorY;
    s16 travel;
    u8 fill[0x16];
    u16 configuredTravel;
    u16 configuredSpeed;
} TestSpikedRollersEntity;

extern void SpikedRollers_Init(TestSpikedRollersEntity*);
extern void SpikedRollers_Action1(TestSpikedRollersEntity*);

static int sAnimation;
static int sAnimationFrames;
static int sFailures;

bool32 ProcessMovement3(Entity* entity) {
    switch (entity->direction) {
        case 0:
            entity->y.HALF.HI--;
            break;
        case 8:
            entity->x.HALF.HI++;
            break;
        case 0x10:
            entity->y.HALF.HI++;
            break;
        case 0x18:
            entity->x.HALF.HI--;
            break;
        default:
            return FALSE;
    }
    return TRUE;
}

void InitializeAnimation(Entity* entity, u32 animation) {
    (void)entity;
    sAnimation = (int)animation;
}

void GetNextFrame(Entity* entity) {
    (void)entity;
    sAnimationFrames++;
}

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                             \
        }                                            \
    } while (0)

static void InitRoller(TestSpikedRollersEntity* roller, s16 travel) {
    memset(roller, 0, sizeof(*roller));
    roller->base.type = 6;
    roller->base.x.HALF.HI = 100;
    roller->base.y.HALF.HI = 50;
    roller->configuredTravel = (u16)travel;
    roller->configuredSpeed = 0x100;
    sAnimation = -1;
    sAnimationFrames = 0;
    SpikedRollers_Init(roller);
}

int main(void) {
    TestSpikedRollersEntity roller;
    int i;

    /* Palace/fortress room data commonly encodes a negative horizontal
     * excursion (for example paramC low halves 0xFEA0 and 0xFF7F). */
    InitRoller(&roller, -32);
    CHECK(roller.base.action == 1 && roller.base.speed == 0x100, "init copies action and configured speed");
    CHECK(roller.anchorX == 100 && roller.anchorY == 50 && roller.travel == -32,
          "init preserves the signed retail travel distance");
    CHECK(roller.base.direction == 8 && roller.base.y.HALF.HI == 106 && sAnimation == 6,
          "type-6 roller uses the retail horizontal orientation and sprite");

    SpikedRollers_Action1(&roller);
    CHECK(roller.base.direction == 0x18 && roller.base.x.HALF.HI == 100,
          "negative excursion turns west at the anchor without a one-frame jump");
    CHECK(roller.base.spriteSettings.flipX == 1, "first endpoint flips the horizontal sprite once");

    for (i = 0; i < 32; i++) SpikedRollers_Action1(&roller);
    CHECK(roller.base.x.HALF.HI == 68 && roller.base.direction == 0x18,
          "negative excursion reaches its exact retail endpoint");
    SpikedRollers_Action1(&roller);
    CHECK(roller.base.x.HALF.HI == 68 && roller.base.direction == 8,
          "negative excursion reverses cleanly without overshooting");
    CHECK(roller.base.spriteSettings.flipX == 0, "second endpoint restores sprite orientation");

    InitRoller(&roller, 32);
    for (i = 0; i < 32; i++) SpikedRollers_Action1(&roller);
    CHECK(roller.base.x.HALF.HI == 132 && roller.base.direction == 8,
          "positive excursion reaches its exact retail endpoint");
    SpikedRollers_Action1(&roller);
    CHECK(roller.base.x.HALF.HI == 132 && roller.base.direction == 0x18,
          "positive excursion reverses cleanly without overshooting");

    if (sFailures != 0) return 1;
    puts("port_spiked_rollers_motion_test: ALL PASS");
    return 0;
}
