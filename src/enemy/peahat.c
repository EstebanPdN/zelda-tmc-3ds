/**
 * @file peahat.c
 * @ingroup Enemies
 *
 * @brief Peahat enemy
 */
#include "enemy.h"
#include "sound.h"
#include "effects.h"
#include "physics.h"
#include "asm.h"
#include "room.h"

typedef struct {
    /*0x00*/ Entity base;
#if defined(PC_PORT) && (__SIZEOF_POINTER__ == 8)
    /*0x68*/ u8 unused1[24 + 8];
#else
    /*0x68*/ u8 unused1[24];
#endif
    /**
     * While flying around the Peahat will gradually change direction using this value.
     * Changes between 2 and -2 at
     * random intervals.
     */
    /*0x80*/ u8 directionDelta;
    /**
     * Controls if the Peahat will move up and down while flying.
     */
    /*0x81*/ bool8 moveUpAnDown;
    /**
     * The Peahat is currently flying.
     */
    /*0x82*/ bool8 flying;
    /**
     * The Peahat will continue flying around for this period before considering to charge again.
     */
    /*0x83*/ u8 flyTimer;
} PeahatEntity;

PORT_STATIC_ASSERT_OFFSET(PeahatEntity, directionDelta, 0x80, 0xB0,
                          "PeahatEntity directionDelta offset incorrect");

enum PeahatActions {
    PeahatActionInitialize,
    PeahatActionFly,
    PeahatActionChargeStart,
    PeahatActionChargeTarget,
    PeahatActionChargeEnd,
    PeahatActionStunned,
    PeahatActionRepairPropeller,
    PeahatActionRecover,
    PeahatActionHop,
    PeahatActionTakeoff,
};

extern void (*const Peahat_Functions[])(PeahatEntity*);
extern void (*const gPeahatPropellerFunctions[])(PeahatEntity*);
extern void (*const gPeahatActions[])(PeahatEntity*);
extern void (*const gPeahatOnGrabbedSubactions[])(PeahatEntity*);

extern const s8 gPeahatFlightHeights[];
extern const s8 gPeahatChargeDirectionOffsets[];

void Peahat_EndCharge(PeahatEntity* this);
void Peahat_UpdateDirection(PeahatEntity* this);

enum {
    PeahatForm_Torso,
    PeahatForm_Propeller,
};

enum {
    PeahatAnimation_Flying,
    PeahatAnimation_BrokenPropeller,
    PeahatAnimation_SlicedPropeller,
    PeahatAnimation_Propeller,
    PeahatAnimation_NewPropeller,
    PeahatAnimation_RepairPropeller,
};

void Peahat(PeahatEntity* this) {
    if (super->type == PeahatForm_Torso) {
        EnemyFunctionHandler(super, (EntityActionArray)Peahat_Functions);
        EnemySetFXOffset(super, 0, 1, -0x10);
    } else {
        gPeahatPropellerFunctions[super->action](this);
    }
}

void Peahat_OnTick(PeahatEntity* this) {
    gPeahatActions[super->action](this);
    if (this->moveUpAnDown)
        super->z.HALF.HI = gPeahatFlightHeights[(super->subtimer++ & 0x30) >> 4];
}

void Peahat_OnCollision(PeahatEntity* this) {
    if (this->flying) {
        if (super->contactFlags == (CONTACT_NOW | 0x14)) {
            Entity* entity = CreateEnemy(PEAHAT, PeahatForm_Propeller);
            if (entity != NULL) {
                CopyPosition(super, entity);
                entity->z.HALF.HI -= 8;
            }
            this->flying = FALSE;
            super->animationState = PeahatAnimation_SlicedPropeller;
            super->action = PeahatActionStunned;
            super->speed = 0x80;
            super->iframes = -30;
            this->moveUpAnDown = FALSE;
            InitializeAnimation(super, super->animationState);
        } else if (super->contactFlags == (CONTACT_NOW | 0x1b)) {
            super->animationState = PeahatAnimation_BrokenPropeller;
            super->action = PeahatActionStunned;
            super->speed = 0x80;
            super->iframes = -30;
            this->moveUpAnDown = FALSE;
            InitializeAnimation(super, super->animationState);
        } else if (super->contactFlags == CONTACT_NOW) {
            if (super->animationState == PeahatAnimation_Flying) {
                super->action = PeahatActionFly;
                super->timer = 30;
                super->speed = 0x80;
                super->direction = -1;
                this->flyTimer = 120;
                GetNextFrame(super);
            }
        }
    }

    if (super->confusedTime)
        EnemyCreateFX(super, FX_STARS);

    EnemyFunctionHandlerAfterCollision(super, Peahat_Functions);
}

void Peahat_OnGrabbed(PeahatEntity* this) {
    if (2 >= super->subAction && !sub_0806F520(super))
        return;

    gPeahatOnGrabbedSubactions[super->subAction](this);
}

void Peahat_OnGrabbed_Subaction0(PeahatEntity* this) {
    super->subAction = 1;
    super->gustJarTolerance = 60;
    if (super->animationState == PeahatAnimation_Flying) {
        super->animationState = PeahatAnimation_BrokenPropeller;
        super->action = PeahatActionStunned;
        super->hitType = 0x71;
        this->moveUpAnDown = FALSE;
        InitializeAnimation(super, super->animationState);
    }
}

void Peahat_OnGrabbed_Subaction1(PeahatEntity* this) {
    sub_0806F4E8(super);
}

void Peahat_OnGrabbed_Subaction2(PeahatEntity* this) {
    sub_0806F3E4(super);
}

void Peahat_OnGrabbed_Subaction3(PeahatEntity* this) {
    COLLISION_OFF(super);
}

void Peahat_OnGrabbed_Subaction4(PeahatEntity* this) {
}

void Peahat_OnGrabbed_Subaction5(PeahatEntity* this) {
    if (super->flags & ENT_COLLIDE) {
        COLLISION_ON(super);
        super->gustJarState &= 0xfb;
    } else {
        super->health = 0;
    }
}

void Peahat_Initialize(PeahatEntity* this) {
    sub_0804A720(super);
    super->action = PeahatActionFly;
    super->timer = 16;
    super->subtimer = Random();
    super->direction = Random() & 0x1f;
    super->gustJarFlags = 18;
    this->directionDelta = (Random() & 1) ? 2 : -2;
    this->moveUpAnDown = TRUE;
    this->flying = TRUE;
    super->animationState = PeahatAnimation_Flying;
    InitializeAnimation(super, super->animationState);
}

void Peahat_Fly(PeahatEntity* this) {
    if (this->flyTimer)
        this->flyTimer--;

    if (sub_08049FDC(super, 1)) {
        if (this->flyTimer == 0 && (super->subtimer & 0xf) == 0 && sub_08049F1C(super, gEnemyTarget, 0x30)) {
            super->action = PeahatActionChargeStart;
            super->subAction = Random() & 3;
            super->timer = 60;
            super->speed = 160;
        }
    }

    if (--super->timer == 0) {
        super->timer = 16;
        Peahat_UpdateDirection(this);
        if ((Random() & 3) == 0) {
            this->directionDelta = (Random() & 1) ? 2 : -2;
        }
    }

    ProcessMovement2(super);
    GetNextFrame(super);
}

void Peahat_ChargeStart(PeahatEntity* this) {
    if (sub_08049FDC(super, 1)) {
        if (--super->timer) {
            UpdateAnimationVariableFrames(super, 4 - ((super->timer >> 4) & 0x3));
            return;
        }
        super->action = PeahatActionChargeTarget;
        super->timer = 120;
        super->speed = 192;
        super->direction = (GetFacingDirection(super, gEnemyTarget) + gPeahatChargeDirectionOffsets[Random() & 1]) &
                           (0x3 | DirectionNorthWest);
    } else {
        Peahat_EndCharge(this);
    }

    UpdateAnimationVariableFrames(super, 4);
}

void Peahat_ChargeTarget(PeahatEntity* this) {
    if (sub_08049FDC(super, 1)) {
        if (--super->timer == 0) {
            Peahat_EndCharge(this);
        }
        if (super->timer > 60) {
            if (super->timer & 1)
                super->speed += 4;

            if ((gRoomTransition.frameCount & 3) == 0)
                sub_08004596(super, GetFacingDirection(super, gEnemyTarget));
        }
        ProcessMovement2(super);
    } else {
        Peahat_EndCharge(this);
    }
    UpdateAnimationVariableFrames(super, 4);
}

void Peahat_ChargeEnd(PeahatEntity* this) {
    if (--super->timer == 0) {
        super->action = PeahatActionFly;
        super->timer = 1;
        super->speed = 128;
        this->flyTimer = 120;
        GetNextFrame(super);
    } else {
        if (super->timer & 1)
            super->speed -= 8;

        ProcessMovement2(super);
        UpdateAnimationVariableFrames(super, 4);
    }
}

#define DIR_NONE 0xff

void Peahat_Stunned(PeahatEntity* this) {
    switch (super->animationState) {
        default:
            if (BounceUpdate(super, Q_8_8(24.0)) == BOUNCE_DONE_ALL) {
                super->action = PeahatActionRepairPropeller;
                super->timer = 240;
                super->subtimer = 10;
                super->hitType = 0x71;
            }

            if (super->direction == DIR_NONE)
                super->direction = super->knockbackDirection;

            ProcessMovement0(super);
            GetNextFrame(super);
            break;
        case PeahatAnimation_SlicedPropeller:
            GravityUpdate(super, Q_8_8(28.0));
            if (super->z.HALF.HI == 0) {
                super->action = PeahatActionRecover;
                super->timer = 150;
                super->subtimer = 10;
                super->hitType = 0x71;
            }
            break;
    }
}

void Peahat_RepairPropeller(PeahatEntity* this) {
    if ((super->subtimer != 0) && (--super->subtimer == 0)) {
        EnemyCreateFX(super, FX_STARS);
    }

    if (sub_0800442E(super) || (--super->timer == 0)) {
        super->action = PeahatActionTakeoff;
        super->zVelocity = Q_16_16(1.5);
        super->direction = Random() & 0x1f;
        EnemyDetachFX(super);
        super->animationState = PeahatAnimation_RepairPropeller;
        InitializeAnimation(super, super->animationState);
    }
}

void Peahat_Recover(PeahatEntity* this) {
    if ((super->subtimer != 0) && (--super->subtimer == 0)) {
        EnemyCreateFX(super, FX_STARS);
    }

    if (sub_0800442E(super) || (--super->timer == 0)) {
        super->action = PeahatActionHop;
        super->timer = 240;
        super->direction = Random() & 0x1f;
        EnemyDetachFX(super);
    }
}

void Peahat_Hop(PeahatEntity* this) {
    GetNextFrame(super);
    if (--super->timer == 0) {
        if (super->frame & ANIM_DONE) {
            super->action = PeahatActionTakeoff;
            super->zVelocity = Q_16_16(1.5);
            super->animationState = PeahatAnimation_NewPropeller;
            InitializeAnimation(super, super->animationState);
        } else {
            super->timer = 1;
        }
    }

    if (super->frame & 2) {
        super->frame &= ~2;
        super->direction = Random() & 0x1f;
    }

    if (super->frame & 1) {
        sub_0800442E(super);
    } else {
        ProcessMovement0(super);
    }
}

void Peahat_Takeoff(PeahatEntity* this) {
    GetNextFrame(super);
    if (super->frame & ANIM_DONE) {
        super->action = PeahatActionFly;
        super->hitType = 0x70;
        this->flying = TRUE;
        this->moveUpAnDown = TRUE;
        super->animationState = PeahatAnimation_Flying;
        InitializeAnimation(super, super->animationState);
    } else if (super->frame & 1) {
        sub_0800442E(super);
    } else {
        GravityUpdate(super, Q_8_8(28.0));
        ProcessMovement0(super);
    }
}

void PeahatPropeller_Initialize(PeahatEntity* this) {
    super->action = PeahatActionFly;
    super->timer = 240;
    super->subtimer = 40;
    super->spriteSettings.draw = 1;
    super->spriteRendering.b3 = 1;
    super->spriteOrientation.flipY = 1;
    super->spriteSettings.shadow = 0;
    super->speed = 0x20;
    super->direction = (Random() & 0x10) + 8;
    InitializeAnimation(super, PeahatAnimation_Propeller);
}

void PeahatPropeller_Fly(PeahatEntity* this) {
    GetNextFrame(super);
    if (--super->timer == 0) {
        DeleteEntity(super);
    } else {
        if (super->timer < 60)
            super->spriteSettings.draw ^= 1;

        super->z.WORD -= Q_16_16(0.75);
        LinearMoveUpdate(super);
        if (--super->subtimer == 0) {
            super->subtimer = 40;
            super->direction = (Random() & 0x10) + 8;
        }
    }
}

void Peahat_EndCharge(PeahatEntity* this) {
    super->action = PeahatActionChargeEnd;
    super->timer = 60;
}

void Peahat_UpdateDirection(PeahatEntity* this) {
    if (!sub_08049FA0(super) && (Random() & 3)) {
        super->direction = sub_08049EE4(super);
    } else {
        super->direction += this->directionDelta;
        super->direction &= (0x3 | DirectionNorthWest);
    }
}

// clang-format off
void (*const Peahat_Functions[])(PeahatEntity*) = {
    Peahat_OnTick,
    Peahat_OnCollision,
    (void (*)(PeahatEntity*))GenericKnockback,
    (void (*)(PeahatEntity*))GenericDeath,
    (void (*)(PeahatEntity*))GenericConfused,
    Peahat_OnGrabbed,
};

void (*const gPeahatPropellerFunctions[])(PeahatEntity*) = {
    PeahatPropeller_Initialize,
    PeahatPropeller_Fly,
};

void (*const gPeahatActions[])(PeahatEntity*) = {
    Peahat_Initialize,
    Peahat_Fly,
    Peahat_ChargeStart,
    Peahat_ChargeTarget,
    Peahat_ChargeEnd,
    Peahat_Stunned,
    Peahat_RepairPropeller,
    Peahat_Recover,
    Peahat_Hop,
    Peahat_Takeoff,
};

const s8 gPeahatFlightHeights[] = {
    -5, -6, -7, -6,
};

void (*const gPeahatOnGrabbedSubactions[])(PeahatEntity*) = {
    Peahat_OnGrabbed_Subaction0,
    Peahat_OnGrabbed_Subaction1,
    Peahat_OnGrabbed_Subaction2,
    Peahat_OnGrabbed_Subaction3,
    Peahat_OnGrabbed_Subaction4,
    Peahat_OnGrabbed_Subaction5,
};

#ifdef PC_PORT
const s8 gPeahatChargeDirectionOffsets[] = {
    4,
    -4,
};
#endif
// clang-format on
