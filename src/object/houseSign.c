/**
 * @file houseSign.c
 * @ingroup Objects
 *
 * @brief HouseSign object
 */
#include "object/houseSign.h"

#include "asm.h"
#ifdef PC_PORT
#include "manager/houseSignManager.h"
#endif

/*
This object is created by HouseSignManager.
It checks whether the 0x10 x 0x10 rect at field_0x80, field_0x82 is still on the screen.
If not, then it deletes itself and unsets the super->type2 bit in the managers field_0x20 bitfield.
*/
void HouseSign(HouseSignEntity* this) {
    if (super->action == 0) {
        super->action = 1;
    }
    if (CheckRectOnScreen(this->unk_80, this->unk_82, 0x10, 0x10) == 0) {
#ifdef PC_PORT
        /* #128 follow-up: same Manager-field-aliasing hazard as
         * MinishSizedEntrance. On GBA, parent->zVelocity (Entity
         * offset 0x20) is the same memory as HouseSignManager's
         * `bitfield` member (also offset 0x20 — just after Manager
         * base). On PC the widened Manager pushes `bitfield` to
         * offset 0x38; parent->zVelocity now writes into the low
         * 4 bytes of Manager's parent pointer instead.
         *
         * Symptom: when a sign scrolls off screen it deletes itself
         * but never clears its bit in the real manager bitfield, so
         * the manager's (bitfield & mask) == 0 check stays false
         * and the sign is never re-spawned when scrolled back into
         * view. Only a room re-entry (which resets the manager) lets
         * the signs reappear. Cast to the concrete manager type and
         * clear the bit on the right field. */
        HouseSignManager* mgr = (HouseSignManager*)super->parent;
        mgr->bitfield &= ~(1u << super->type2);
#else
        super->parent->zVelocity &= ~(1 << super->type2);
#endif
        DeleteThisEntity();
    }
}
