#ifndef PORT_ROLL_ATTACK_MACRO_H
#define PORT_ROLL_ATTACK_MACRO_H

/*
 * One-button roll attack (start-of-roll timing).
 *
 * Bound to PORT_INPUT_ROLL_ATTACK (default: keyboard D). While holding a
 * direction, a press injects R + direction, then B with the best owned sword
 * — regardless of what is equipped in A/B slots.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Called once per frame from Port_UpdateInput after real inputs are read.
 * May override KEYINPUT bits for R / B / direction during the sequence. */
void Port_RollAttackMacro_Tick(uint16_t* keyinput);

/* True on frames where the macro is injecting a sword press through B. */
bool Port_RollAttackMacro_IsBHeld(void);

/* Returns the best owned sword while IsBHeld(), else `saved`. */
uint8_t Port_RollAttackMacro_GetEffectiveBItem(uint8_t saved);

#ifdef __cplusplus
}
#endif

#endif
