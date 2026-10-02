/*
 * port_roll_attack_macro.c — one-button start-of-roll attack.
 *
 * Sequence (frame-perfect inside the port loop):
 *   1. Player holds a direction and presses the roll-attack bind (default D).
 *   2. Inject R + direction until PL_ROLLING is set.
 *   3. Wait until the roll animation reaches the item-use window
 *      (gPlayerEntity.base.frame & 0x40 — same gate PlayerRollUpdate uses
 *      before calling UpdateActiveItems).
 *   4. Inject a fresh B press with the best owned sword equipped virtually.
 *
 * If the animation window is missed, fall back to alternating B pulses for a
 * few frames while still rolling (covers edge timing / late detection).
 */

#include "port_roll_attack_macro.h"
#include "port_runtime_config.h"

#include "gba/io_reg.h"
#include "global.h"
#include "item.h"
#include "player.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    MACRO_IDLE = 0,
    MACRO_ROLL,
    MACRO_WAIT_ITEM_FRAME,
    MACRO_ATTACK,
    MACRO_ATTACK_PULSE,
} MacroPhase;

static MacroPhase sPhase = MACRO_IDLE;
static uint16_t sDirMask = 0;
static int sWaitFrames = 0;
static int sPulseFrames = 0;
static bool sSwordOverride = false;
static bool sBPulseHigh = false;
static bool sBFired = false;
static u8 sAttackStatusBaseline = 0;
static u32 sFlagsBaseline = 0;

static const uint8_t kSwordPriority[] = {
    ITEM_FOURSWORD,
    ITEM_BLUE_SWORD,
    ITEM_RED_SWORD,
    ITEM_GREEN_SWORD,
    ITEM_SMITH_SWORD,
};

static bool RollMacroDebug(void) {
    static int sCached = -1;
    if (sCached < 0) {
        const char* e = getenv("TMC_ROLL_MACRO_DEBUG");
        sCached = (e && e[0] == '1') ? 1 : 0;
    }
    return sCached != 0;
}

static uint8_t BestOwnedSword(void) {
    for (size_t i = 0; i < sizeof(kSwordPriority) / sizeof(kSwordPriority[0]); i++) {
        if (GetInventoryValue(kSwordPriority[i]) != 0) {
            return kSwordPriority[i];
        }
    }
    return 0;
}

static uint16_t ResolveDirectionMask(void) {
    uint16_t mask = 0;

    if (Port_Config_InputPressed(PORT_INPUT_UP)) {
        mask |= DPAD_UP;
    } else if (Port_Config_InputPressed(PORT_INPUT_DOWN)) {
        mask |= DPAD_DOWN;
    }
    if (Port_Config_InputPressed(PORT_INPUT_LEFT)) {
        mask |= DPAD_LEFT;
    } else if (Port_Config_InputPressed(PORT_INPUT_RIGHT)) {
        mask |= DPAD_RIGHT;
    }
    if (mask != 0) {
        return mask;
    }

    float sx = 0.f, sy = 0.f;
    if (!Port_Config_GetLeftStick(&sx, &sy)) {
        return 0;
    }

    const float dz = Port_Config_GetAnalogDeadzone();
    if ((sx * sx + sy * sy) < dz * dz) {
        return 0;
    }

    if (fabsf(sx) >= fabsf(sy)) {
        return (sx < 0.f) ? DPAD_LEFT : DPAD_RIGHT;
    }
    return (sy < 0.f) ? DPAD_UP : DPAD_DOWN;
}

static bool CanStartMacro(void) {
    if (!Port_Config_GetRollAttackMacroEnabled()) {
        return false;
    }
    if (sPhase != MACRO_IDLE) {
        return false;
    }
    if (gPlayerState.controlMode != CONTROL_ENABLED) {
        return false;
    }
    if (gPlayerState.flags & (PL_ROLLING | PL_CAPTURED | PL_DISABLE_ITEMS | PL_BUSY)) {
        return false;
    }
    if ((gPlayerState.skills & SKILL_ROLL_ATTACK) == 0) {
        return false;
    }
    if (BestOwnedSword() == 0) {
        return false;
    }
    if (ResolveDirectionMask() == 0) {
        return false;
    }
    return true;
}

static bool RollItemFrameOpen(void) {
    return gPlayerEntity.base.action == PLAYER_ROLL && (gPlayerEntity.base.frame & 0x40) != 0;
}

static void ReleaseMacroButtons(uint16_t* keyinput) {
    *keyinput |= (uint16_t)(R_BUTTON | B_BUTTON | DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT);
}

static void InjectDirection(uint16_t* keyinput) {
    if (sDirMask != 0) {
        *keyinput &= ~sDirMask;
    }
}

static void FinishMacro(void) {
    sPhase = MACRO_IDLE;
    sDirMask = 0;
    sSwordOverride = false;
    sBPulseHigh = false;
    sBFired = false;
    sWaitFrames = 0;
    sPulseFrames = 0;
}

static void InjectBPress(uint16_t* keyinput) {
    *keyinput &= ~B_BUTTON;
    sSwordOverride = true;
    sBFired = true;
}

/* Only treat a roll attack as success after *this* macro fired B.
 * lastSwordMove stays SWORD_MOVE_ROLL across attacks, so it cannot be
 * used alone or the second press exits during WAIT without sending B. */
static bool MacroAttackLanded(void) {
    if (!sBFired) {
        return false;
    }
    if ((gPlayerState.attack_status & 0xf) != 0 &&
        gPlayerState.attack_status != sAttackStatusBaseline) {
        return true;
    }
    if ((gPlayerState.flags & PL_SWORD_THRUST) && !(sFlagsBaseline & PL_SWORD_THRUST)) {
        return true;
    }
    return false;
}

void Port_RollAttackMacro_Tick(uint16_t* keyinput) {
    if (!keyinput) {
        return;
    }

    if (!Port_Config_GetRollAttackMacroEnabled()) {
        if (sPhase != MACRO_IDLE) {
            FinishMacro();
        }
        return;
    }

    if (sPhase == MACRO_IDLE) {
        if (Port_Config_InputEdgePressed(PORT_INPUT_ROLL_ATTACK) && CanStartMacro()) {
            sDirMask = ResolveDirectionMask();
            sPhase = MACRO_ROLL;
            sWaitFrames = 0;
            sPulseFrames = 0;
            sSwordOverride = false;
            sBPulseHigh = false;
            sBFired = false;
            sAttackStatusBaseline = gPlayerState.attack_status;
            sFlagsBaseline = gPlayerState.flags;
            if (RollMacroDebug()) {
                fprintf(stderr, "[roll-macro] start dir=0x%04x\n", sDirMask);
            }
        } else {
            return;
        }
    }

    /* While the macro runs, mask physical R/B/d-pad so we own the sequence. */
    ReleaseMacroButtons(keyinput);
    InjectDirection(keyinput);

    if (!(gPlayerState.flags & PL_ROLLING) && sPhase != MACRO_ROLL && !MacroAttackLanded()) {
        if (RollMacroDebug()) {
            fprintf(stderr, "[roll-macro] abort — roll ended before attack\n");
        }
        FinishMacro();
        return;
    }

    switch (sPhase) {
        case MACRO_ROLL:
            if (!(gPlayerState.flags & PL_ROLLING)) {
                *keyinput &= ~R_BUTTON;
            } else {
                sPhase = MACRO_WAIT_ITEM_FRAME;
                sWaitFrames = 0;
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] PL_ROLLING — waiting for frame&0x40\n");
                }
            }
            if (++sWaitFrames > 15) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] timeout waiting for roll\n");
                }
                FinishMacro();
            }
            break;

        case MACRO_WAIT_ITEM_FRAME:
            /* Keep direction held; leave B released so the next press is a new edge. */
            if (RollItemFrameOpen()) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] frame window open — firing B\n");
                }
                InjectBPress(keyinput);
                sPhase = MACRO_ATTACK;
                sWaitFrames = 0;
                break;
            }
            if (++sWaitFrames > 20) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] window miss — pulse fallback\n");
                }
                sPhase = MACRO_ATTACK_PULSE;
                sPulseFrames = 0;
                sBPulseHigh = true;
            }
            break;

        case MACRO_ATTACK:
            if (MacroAttackLanded()) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] success\n");
                }
                FinishMacro();
                break;
            }
            /* Hold sword override briefly in case the item spawns next frame. */
            if (++sWaitFrames <= 2) {
                sSwordOverride = true;
            } else {
                sPhase = MACRO_ATTACK_PULSE;
                sPulseFrames = 0;
                sBPulseHigh = false;
            }
            break;

        case MACRO_ATTACK_PULSE:
            if (MacroAttackLanded()) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] success (pulse)\n");
                }
                FinishMacro();
                break;
            }
            if (!(gPlayerState.flags & PL_ROLLING) || ++sPulseFrames > 16) {
                if (RollMacroDebug()) {
                    fprintf(stderr, "[roll-macro] pulse gave up\n");
                }
                FinishMacro();
                break;
            }
            if (sBPulseHigh) {
                InjectBPress(keyinput);
            } else {
                sSwordOverride = false;
            }
            sBPulseHigh = !sBPulseHigh;
            break;

        default:
            FinishMacro();
            break;
    }
}

bool Port_RollAttackMacro_IsBHeld(void) {
    return sSwordOverride;
}

uint8_t Port_RollAttackMacro_GetEffectiveBItem(uint8_t saved) {
    if (!sSwordOverride) {
        return saved;
    }
    uint8_t sword = BestOwnedSword();
    return sword ? sword : saved;
}
