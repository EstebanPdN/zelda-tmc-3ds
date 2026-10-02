#ifndef PORT_CHARGE_BAR_H
#define PORT_CHARGE_BAR_H

#include <stdint.h>

/* DrawChargeBar's four animation frames are consecutive 0xc0-byte blocks
 * inside gGlobalGfxAndPalettes.  These are USA-baseline offsets: callers
 * must pass the selected value through Port_RemapGfxOffset before reading
 * an active-region blob. */
#define PORT_CHARGE_BAR_FRAME_COUNT 4u
#define PORT_CHARGE_BAR_FRAME_BYTES 0xc0u

static inline uint32_t Port_ChargeBarUsaGfxOffset(uint32_t frame) {
    static const uint32_t sUsaOffsets[PORT_CHARGE_BAR_FRAME_COUNT] = {
        0x21f20u,
        0x21fe0u,
        0x220a0u,
        0x22160u,
    };

    /* A corrupt action/animation byte must not index past the table.  Frame
     * zero is the native idle artwork and is the safest fallback. */
    if (frame >= PORT_CHARGE_BAR_FRAME_COUNT) {
        frame = 0;
    }
    return sUsaOffsets[frame];
}

#endif /* PORT_CHARGE_BAR_H */
