/*
 * port/port_analog_movement.c — 360° left-stick movement bridge.
 *
 * The GBA has a 4-bit D-pad. The TMC engine reads those bits and snaps
 * the player to one of 8 directions via gUnk_08109202[]. But the
 * underlying `gPlayerState.direction` field is a 5-bit value (0..31, ~
 * 11.25° each), and the engine's movement / animation lookup tables
 * have valid entries for every one of those 32 slots — they're just
 * never reached because the 8 D-pad combinations map to the 8 cardinals
 * (values 0, 4, 8, 12, 16, 20, 24, 28).
 *
 * This file feeds the engine the intermediate directions by reading the
 * raw left-stick angle from SDL, snapping to one of the 32 slots, and
 * overwriting `direction` plus the INPUT_LEFT/RIGHT/UP/DOWN bits so the
 * "is moving" downstream checks (`heldInput & INPUT_ANY_DIRECTION`) all
 * fire correctly.
 *
 * Gated behind REBORN_FEAT_ANALOG_360_MOVEMENT so users who prefer the
 * stock 8-direction feel can keep it.
 */

#include "global.h"
#include "player.h"
#include "port_reborn.h"
#include "port_runtime_config.h"

#include <math.h>
#include <stdbool.h>

/* Stick magnitudes below the deadzone don't trigger movement — the stock
 * D-pad input remains authoritative. The threshold is user-configurable
 * (Port_Config_GetAnalogDeadzone, default 0.30 ~= 30%, range [0..0.95]) so a
 * worn or drifty stick can be tuned without rebuilding; ~30% lets most users
 * rest a thumb on the stick without phantom walking. */

/* C11 doesn't guarantee M_PI without a feature-test macro and we'd
 * rather not pull _GNU_SOURCE into a leaf file. */
static const float kPi = 3.14159265358979323846f;

bool Port_AnalogMovement_Apply(u32* heldInputOut, u32* directionOut) {
    if (!Port_Reborn_IsEnabled(REBORN_FEAT_ANALOG_360_MOVEMENT)) {
        return false;
    }

    float sx = 0.0f, sy = 0.0f;
    if (!Port_Config_GetLeftStick(&sx, &sy)) {
        return false;
    }
    const float magSq = sx * sx + sy * sy;
    float dz = Port_Config_GetAnalogDeadzone();
    if (dz < 0.0f) dz = 0.0f;
    if (dz > 0.95f) dz = 0.95f;
    if (magSq < dz * dz) {
        return false;
    }

    /* TMC's "direction 0 = up", clockwise (4 = upper-right, 8 = right,
     * 12 = lower-right, ...). SDL gives +x = right, +y = down. The
     * atan2 below gives 0 at the +y axis (down) increasing clockwise
     * via +x (right), matching the engine's quadrant scheme once we
     * subtract 8 (90°) to put 0 at the top of the screen. */
    float angle = atan2f(sx, -sy); /* 0 = up, +π/2 = right */
    if (angle < 0.0f) angle += 2.0f * kPi;
    const int dir32 = ((int)((angle * 32.0f) / (2.0f * kPi) + 0.5f)) & 31;

    /* Also set the cardinal direction bits whose component matches the
     * stick. The engine's downstream `heldInput & INPUT_ANY_DIRECTION`
     * checks just need *any* direction bit set; we set the dominant
     * axis (and the secondary when diagonal) so the existing logic
     * sees a sane "user is pressing a direction" state. */
    u32 held = *heldInputOut & ~INPUT_ANY_DIRECTION;
    if (sx >  0.35f) held |= INPUT_RIGHT;
    if (sx < -0.35f) held |= INPUT_LEFT;
    if (sy < -0.35f) held |= INPUT_UP;
    if (sy >  0.35f) held |= INPUT_DOWN;
    if ((held & INPUT_ANY_DIRECTION) == 0) {
        /* Magnitude was past the deadzone but neither axis component
         * was strong enough to assert a cardinal — happens at very
         * narrow off-axis tilts. Fall back to the dominant component
         * so the engine sees *some* direction held. */
        if (fabsf(sx) >= fabsf(sy)) {
            held |= (sx >= 0.0f) ? INPUT_RIGHT : INPUT_LEFT;
        } else {
            held |= (sy >= 0.0f) ? INPUT_DOWN : INPUT_UP;
        }
    }

    *heldInputOut = held;
    *directionOut = (u32)dir32;
    return true;
}
