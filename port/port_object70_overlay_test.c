#include <stdio.h>
#include <setjmp.h>
#include <string.h>

#include "effects.h"
#include "object.h"
#include "player.h"

void Object70_Init(Entity* this);
void Object70_Action1(Entity* this);

PlayerEntity gPlayerEntity;
PlayerState gPlayerState;

static int sDeleteCount;
static int sSnapCount;
static int sFxCount;
static int sDeleteJumpEnabled;
static jmp_buf sDeleteJump;

void SnapToTile(Entity* entity) {
    (void)entity;
    sSnapCount++;
}

void DeleteEntity(Entity* entity) {
    (void)entity;
    sDeleteCount++;
}

void DeleteThisEntity(void) {
    sDeleteCount++;
    if (sDeleteJumpEnabled) {
        longjmp(sDeleteJump, 1);
    }
}

Entity* CreateFx(Entity* target, u32 type, u32 type2) {
    (void)target;
    (void)type;
    (void)type2;
    sFxCount++;
    return NULL;
}

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                  \
    do {                                                                                                     \
        unsigned got__ = (unsigned)(actual);                                                                 \
        unsigned want__ = (unsigned)(expected);                                                              \
        if (got__ != want__) {                                                                               \
            fprintf(stderr, "FAIL: %s: got %u expected %u\n", message, got__, want__);                    \
            sFailures++;                                                                                     \
        }                                                                                                    \
    } while (0)

int main(void) {
    Entity overlay;

    memset(&overlay, 0, sizeof(overlay));
    memset(&gPlayerEntity, 0, sizeof(gPlayerEntity));
    memset(&gPlayerState, 0, sizeof(gPlayerState));
    overlay.type = 1;
    /* Entity setup gives the overlay OBJ priority 2.  The 180410 dump proves
     * that this piece is emitted while Link's body is also (incorrectly)
     * priority 2.  Object70 must preserve the overlay priority and move only
     * the body to retail priority 3. */
    overlay.spriteOrientation.flipY = 2;
    gPlayerEntity.base.spritePriority.b0 = 2;

    Object70_Init(&overlay);
    CHECK_EQ(overlay.action, 1, "head overlay enters its update action");
    CHECK_EQ(overlay.spriteSettings.draw, 1, "head overlay is visible");
    CHECK_EQ(overlay.frameIndex, 12, "stairs use the retail head-overlay frame");
    CHECK_EQ(sSnapCount, 1, "stairs overlay snaps to the doorway tile");
    CHECK_EQ(gPlayerEntity.base.spriteOrientation.flipY, 3,
             "Link body moves behind the foreground BG while overlay stays visible");
    CHECK_EQ(overlay.spriteOrientation.flipY, 2, "head overlay keeps its foreground OBJ priority");
    CHECK_EQ(overlay.spritePriority.b0, 3, "head overlay sorts immediately above Link");

    gPlayerEntity.base.action = PLAYER_USEENTRANCE;
    Object70_Action1(&overlay);
    CHECK_EQ(sDeleteCount, 0, "overlay survives throughout PLAYER_USEENTRANCE");
    CHECK_EQ(gPlayerEntity.base.spriteOrientation.flipY, 3,
             "stairs update does not expose Link's full body over the foreground");

    gPlayerEntity.base.action = PLAYER_NORMAL;
    overlay.collisionLayer = 1;
    sDeleteJumpEnabled = 1;
    if (setjmp(sDeleteJump) == 0) {
        Object70_Action1(&overlay);
    }
    sDeleteJumpEnabled = 0;
    CHECK_EQ(sDeleteCount, 1, "overlay is removed after the entrance action");
    CHECK_EQ(gPlayerEntity.base.spriteOrientation.flipY, 2,
             "normal layer priority is restored after the transition");

    memset(&overlay, 0, sizeof(overlay));
    overlay.type = 0;
    overlay.spriteOrientation.flipY = 2;
    gPlayerEntity.base.action = PLAYER_NORMAL;
    gPlayerEntity.base.z.WORD = 0;
    gPlayerState.floor_type = SURFACE_SWAMP;
    gPlayerState.jump_status = 0;
    Object70_Init(&overlay);
    Object70_Action1(&overlay);
    CHECK_EQ(gPlayerEntity.base.spriteOrientation.flipY, 3,
             "swamp head overlay uses the same retail foreground split");
    CHECK_EQ(sDeleteCount, 1, "valid swamp state keeps the overlay alive");
    CHECK_EQ(sFxCount, 0, "valid swamp state does not create an exit splash");

    gPlayerState.floor_type = SURFACE_NORMAL;
    sDeleteJumpEnabled = 1;
    if (setjmp(sDeleteJump) == 0) {
        Object70_Action1(&overlay);
    }
    sDeleteJumpEnabled = 0;
    CHECK_EQ(gPlayerEntity.base.spriteOrientation.flipY, 2,
             "leaving swamp restores Link's normal priority");
    CHECK_EQ(sDeleteCount, 2, "leaving swamp removes the overlay");
    CHECK_EQ(sFxCount, 1, "leaving grounded swamp creates the retail splash");

    if (sFailures != 0) {
        fprintf(stderr, "port_object70_overlay_test: %d failure(s)\n", sFailures);
        return 1;
    }
    puts("port_object70_overlay_test: ALL PASS");
    return 0;
}
