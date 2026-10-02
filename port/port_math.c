/*
 * port_math.c — Math & direction functions for the PC port.
 *
 * Implements CalcDistance, CalculateDirectionFromOffsets, CalculateDirectionTo.
 * Ported from ARM/Thumb ASM (code_08003FC4.s, code_080043E8.s, intr.s).
 */

#include "global.h"
#include <math.h>

/* Declared in syscall.h */
extern u16 Sqrt(u32 num);

/* Tangent-ratio lookup table (64 entries, from common.c) */
extern const u16 gUnk_080C93E0[];

/* Direction lookup table for CalculateDirectionTo (arm_CalcCollisionDirection).
 * Originally at gUnk_0800464E in code_080043E8.s:
 *   8 rows × 8 bytes = 64 bytes (only first 5 per row are meaningful). */
static const u8 sDirectionLUT[64] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, /* base 0:  dx>=0, dy>=0, |dx|<|dy| */
    0x08, 0x07, 0x06, 0x05, 0x04, 0x00, 0x00, 0x00, /* base 8:  dx>=0, dy>=0, |dx|>=|dy| */
    0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x00, 0x00, 0x00, /* base 16: dx>=0, dy<0,  |dx|<|dy| */
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x00, 0x00, /* base 24: dx>=0, dy<0,  |dx|>=|dy| */
    0x00, 0x1F, 0x1E, 0x1D, 0x1C, 0x00, 0x00, 0x00, /* base 32: dx<0,  dy>=0, |dx|<|dy| */
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x00, 0x00, 0x00, /* base 40: dx<0,  dy>=0, |dx|>=|dy| */
    0x10, 0x11, 0x12, 0x13, 0x14, 0x00, 0x00, 0x00, /* base 48: dx<0,  dy<0,  |dx|<|dy| */
    0x18, 0x17, 0x16, 0x15, 0x14, 0x00, 0x00, 0x00, /* base 56: dx<0,  dy<0,  |dx|>=|dy| */
};

/* ------------------------------------------------------------------ */
/* CalcDistance — sqrt((x² + y²) << 8)                                 */
/*   Ported from code_08003FC4.s @ 0x080041EC                         */
/* ------------------------------------------------------------------ */
u32 CalcDistance(s32 x, s32 y) {
    u32 sum = (u32)((x * x) + (y * y));
    return (u32)Sqrt(sum << 8);
}

/* ------------------------------------------------------------------ */
/* CalculateDirectionFromOffsets — 8-bit direction (0-255) from offsets */
/*   Ported from code_080043E8.s @ 0x080045DA                         */
/*   Direction convention: 0=north, 0x40=east, 0x80=south, 0xC0=west  */
/* ------------------------------------------------------------------ */
u32 CalculateDirectionFromOffsets(s32 x, s32 y) {
    u32 fine = 0x40; /* default fine angle when x == 0 */

    if (x != 0) {
        /* Tangent ratio: |y * 256 / x| */
        s32 ratio = (y * 256) / x;
        u32 abs_ratio = (u32)(ratio < 0 ? -ratio : ratio);

        /* Determine search range in the 64-entry table.
         * The ASM uses byte offsets (e.g., 0x20 = 16 u16 entries).
         * start_idx and end_idx are u16 indices. */
        u32 start_idx, end_idx;

        if (abs_ratio < 0x6E) {
            fine = 0; /* reset for first range */
            start_idx = 0;
            end_idx = 16;
        } else if (abs_ratio < 0x106) {
            start_idx = 16;
            end_idx = 32;
        } else if (abs_ratio < 0x280) {
            start_idx = 32;
            end_idx = 48;
        } else {
            start_idx = 48;
            end_idx = 63; /* 0x7E / 2 = 63 */
        }

        /* Linear search through pairs of thresholds */
        for (u32 i = start_idx; i < end_idx; i++) {
            if (abs_ratio >= gUnk_080C93E0[i] && abs_ratio < gUnk_080C93E0[i + 1]) {
                fine = i + 1;
                break;
            }
        }
    }

    /* Quadrant adjustment based on signs of x and y */
    if (x >= 0) {
        if (y >= 0)
            return 0x40 + fine;
        else
            return 0x40 - fine;
    } else {
        if (y < 0)
            return 0xC0 + fine;
        else
            return 0xC0 - fine;
    }
}

/* ------------------------------------------------------------------ */
/* CalculateDirectionTo — 5-bit direction (0-31) between two points    */
/*   Ported from intr.s arm_CalcCollisionDirection @ 0x080B237C        */
/*   r0=x1, r1=y1, r2=x2, ip(r12)=y2                                 */
/* ------------------------------------------------------------------ */
u32 CalculateDirectionTo(s32 x1, s32 y1, s32 x2, s32 y2) {
    s32 dx = x2 - x1;
    s32 dy = y1 - y2; /* note: y1 - y2 (flipped for screen coords) */

    u32 base = 0;
    s32 adx = dx;
    if (dx < 0) {
        base = 0x20;
        adx = -dx;
    }

    s32 ady = dy;
    if (dy < 0) {
        ady = -dy;
        base += 0x10;
    }

    /* Octant: compare |dx| vs |dy| */
    s32 smaller, larger;
    if (adx >= ady) {
        smaller = ady;
        larger = adx;
        base += 8;
    } else {
        smaller = adx;
        larger = ady;
    }

    /* 4-step binary refinement within octant */
    s32 test_val = larger;
    s32 inc = larger * 2;
    u32 idx = 0;

    smaller *= 8;

    if (smaller >= test_val) {
        idx++;
        test_val += inc;
    }
    if (smaller >= test_val) {
        idx++;
        test_val += inc;
    }
    if (smaller >= test_val) {
        idx++;
        test_val += inc;
    }
    if (smaller >= test_val) {
        idx++;
    }

    return sDirectionLUT[base + idx];
}
