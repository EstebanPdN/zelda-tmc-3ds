/**
 * @file jailBars.c
 * @ingroup Objects
 *
 * @brief Jail Bars object
 */
#include "entity.h"
#include "flags.h"
#include "beanstalkSubtask.h"
#include "room.h"
#include "sound.h"
#include "tiles.h"

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[30];
    /*0x86*/ u16 flag;
} JailBarsEntity;

PORT_STATIC_ASSERT_SIZE(JailBarsEntity, 0x88, 0xB0, "JailBarsEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(JailBarsEntity, flag, 0x86, 0xAE,
                        "JailBarsEntity flag offset incorrect");

static void SetJailBarTiles(JailBarsEntity* this, u32);
void JailBars_Init(JailBarsEntity* this);
void JailBars_Action1(JailBarsEntity* this);
void JailBars_Action2(JailBarsEntity* this);
void JailBars_Action3(JailBarsEntity* this);

void JailBars(JailBarsEntity* this) {
    static void (*const JailBars_Actions[])(JailBarsEntity*) = {
        JailBars_Init,
        JailBars_Action1,
        JailBars_Action2,
        JailBars_Action3,
    };
    JailBars_Actions[super->action](this);
}

void JailBars_Init(JailBarsEntity* this) {
    if (CheckFlags(this->flag) == 0) {
        super->action = 1;
        SetJailBarTiles(this, 0);
    } else {
        super->action = 3;
        InitializeAnimation(super, 1);
        SetJailBarTiles(this, 1);
    }
    super->spriteSettings.draw = 3; // ???
    super->collisionLayer = 1;
    UpdateSpriteForCollisionLayer(super);
}

void JailBars_Action1(JailBarsEntity* this) {
    if (CheckFlags(this->flag) != 0) {
        super->action = 2;
        SetJailBarTiles(this, 1);
        SoundReq(SFX_10B);
    }
}

void JailBars_Action2(JailBarsEntity* this) {
    GetNextFrame(super);
#ifdef PC_PORT
    /* #149: the action-1 (closed) Init path never InitializeAnimation()s, so
     * super->animPtr is NULL here. The GBA UpdateAnimationVariableFrames had no
     * NULL-animPtr guard and advanced off the read-protected BIOS region, which
     * set ANIM_DONE and let the door reach action 3 (loads the open frame). The
     * PC animation system guards that NULL deref (port_animation.c), making
     * GetNextFrame a no-op — so the door stalls in action 2 forever: collision
     * opened (SetJailBarTiles in action 1) but the texture never did, and only
     * a room reload (which re-runs the open Init path) fixed it. Mirror the GBA
     * advance: with no animation to tick, the transition completes immediately. */
    if (super->animPtr == NULL) {
        super->frame |= ANIM_DONE;
    }
#endif
    if (super->frame & ANIM_DONE) {
        super->action = 3;
        InitializeAnimation(super, 1);
    }
}

void JailBars_Action3(JailBarsEntity* this) {
}

static void SetJailBarTiles(JailBarsEntity* this, u32 arg1) {
    static const s16 gUnk_08124960[] = {
        SPECIAL_TILE_35,
        -0x3,
        SPECIAL_TILE_35,
        -0x2,
        SPECIAL_TILE_35,
        -0x1,
        SPECIAL_TILE_35,
        0x0,
        SPECIAL_TILE_35,
        0x1,
        SPECIAL_TILE_35,
        0x2,
        -0x1,
    };
    static const s16 gUnk_0812497A[] = {
        SPECIAL_TILE_35,
        -0x3,
        SPECIAL_TILE_35,
        -0x2,
        SPECIAL_TILE_136,
        -0x1,
        SPECIAL_TILE_135,
        0x0,
        SPECIAL_TILE_35,
        0x1,
        SPECIAL_TILE_35,
        0x2,
        -0x1,
    };
    const s16* puVar1;

    puVar1 = gUnk_08124960;
    if (arg1 == 1) {
        puVar1 = gUnk_0812497A;
    }
    SetMultipleTiles((const TileData*)puVar1, COORD_TO_TILE(super), 1);
}
