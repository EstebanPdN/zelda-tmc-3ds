#include "port_upscale.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * xBRZ-inspired pixel-art upscaler (clean-room).
 *
 * Compared to plain nearest-neighbor stretch, this:
 *   - Detects diagonal edges using YCbCr color distance (not strict equality),
 *     so JPEG-ish dithered pixel art still gets smoothed.
 *   - Blends two colors at detected diagonal cuts (50/50 mix), producing
 *     anti-aliased diagonals instead of the staircase you get with Scale2x.
 *   - Avoids smoothing near isolated pixels (no "bleed" inside texture
 *     details) by requiring edge symmetry.
 *
 * The 5x5 neighborhood:
 *
 *      .  .  .  .  .
 *      .  A  .  .  .
 *      .  Bl M  Br .
 *      .  .  D  .  .
 *      .  .  .  .  .
 *
 * For each output 2x2 sub-block, we examine the four diagonal "corner"
 * directions (NW, NE, SW, SE) and replace the corresponding sub-pixel with
 * a blend of the two cardinal neighbors that bracket the diagonal — but
 * only when:
 *   1. the two cardinal neighbors are similar to each other,
 *   2. they differ from the center M, and
 *   3. they don't form a stronger pattern with the opposite cardinal
 *      (which would mean we're inside a texture, not on an edge).
 */

/* Squared YCbCr distance threshold. Tune to taste:
 *   smaller → fewer pixels marked similar → less blending, sharper output.
 *   larger  → more aggressive blending, can blur pixel art.
 * 36^2 = 1296 picks up gentle dithering without smearing distinct colors. */
#define COLOR_THRESHOLD_SQ (36 * 36)

static inline int color_dist_sq(uint32_t a, uint32_t b) {
    int ar, ag, ab;
    int br, bg, bb;
    int dr, dg, db;
    int dy, dcb, dcr;

    if (a == b) {
        return 0;
    }
    /* SDL_PIXELFORMAT_ABGR8888: byte 0=R, 1=G, 2=B, 3=A. */
    ar = (int)(a & 0xFF);
    ag = (int)((a >> 8) & 0xFF);
    ab = (int)((a >> 16) & 0xFF);
    br = (int)(b & 0xFF);
    bg = (int)((b >> 8) & 0xFF);
    bb = (int)((b >> 16) & 0xFF);
    dr = ar - br;
    dg = ag - bg;
    db = ab - bb;
    /* Approximate BT.601 in fixed-point (×1000). */
    dy = (299 * dr + 587 * dg + 114 * db) / 1000;
    dcb = (-169 * dr - 331 * dg + 500 * db) / 1000;
    dcr = (500 * dr - 418 * dg - 81 * db) / 1000;
    /* Weight luma higher than chroma — humans notice luminance edges more. */
    return dy * dy * 4 + dcb * dcb + dcr * dcr;
}

static inline int sim(uint32_t a, uint32_t b) {
    return color_dist_sq(a, b) < COLOR_THRESHOLD_SQ;
}

/* 50/50 alpha blend of two ABGR8888 pixels. */
static inline uint32_t blend2(uint32_t a, uint32_t b) {
    return ((a >> 1) & 0x7F7F7F7Fu) + ((b >> 1) & 0x7F7F7F7Fu);
}

void Port_Upscale_xBRZ_2x_Pitch(const uint32_t* src, int srcW, int srcH,
                                int srcPitchPixels, uint32_t* dst) {
    int x;
    int y;
    int dstW = srcW * 2;
    for (y = 0; y < srcH; y++) {
        for (x = 0; x < srcW; x++) {
            uint32_t M;     /* center */
            uint32_t Lt;    /* left  */
            uint32_t R;     /* right */
            uint32_t U;     /* up    */
            uint32_t Dn;    /* down  */
            uint32_t TL;    /* output 2x2 sub-pixels */
            uint32_t TR;
            uint32_t BL;
            uint32_t BR;

            M  = src[(size_t)y * (size_t)srcPitchPixels + x];
            Lt = (x > 0)        ? src[(size_t)y * (size_t)srcPitchPixels + (x - 1)]   : M;
            R  = (x < srcW - 1) ? src[(size_t)y * (size_t)srcPitchPixels + (x + 1)]   : M;
            U  = (y > 0)        ? src[(size_t)(y - 1) * (size_t)srcPitchPixels + x]   : M;
            Dn = (y < srcH - 1) ? src[(size_t)(y + 1) * (size_t)srcPitchPixels + x]   : M;

            TL = TR = BL = BR = M;

            /* For each diagonal corner, blend two cardinal neighbors when:
             *   - they are similar to each other (forming a continuous edge),
             *   - they differ from the center (we are on an edge, not inside),
             *   - the orthogonal pair (in the same corner) does NOT form an
             *     equally strong edge (otherwise we're at an X-junction; both
             *     diagonals would compete and we'd over-smooth). */
            if (sim(U, Lt) && !sim(U, M) && !sim(Lt, R) && !sim(U, Dn)) {
                TL = blend2(U, Lt);
            }
            if (sim(U, R)  && !sim(U, M) && !sim(R, Lt) && !sim(U, Dn)) {
                TR = blend2(U, R);
            }
            if (sim(Dn, Lt) && !sim(Dn, M) && !sim(Lt, R) && !sim(Dn, U)) {
                BL = blend2(Dn, Lt);
            }
            if (sim(Dn, R)  && !sim(Dn, M) && !sim(R, Lt) && !sim(Dn, U)) {
                BR = blend2(Dn, R);
            }

            dst[(2 * y)     * dstW + (2 * x)]     = TL;
            dst[(2 * y)     * dstW + (2 * x + 1)] = TR;
            dst[(2 * y + 1) * dstW + (2 * x)]     = BL;
            dst[(2 * y + 1) * dstW + (2 * x + 1)] = BR;
        }
    }
}

void Port_Upscale_xBRZ_4x_Pitch(const uint32_t* src, int srcW, int srcH,
                                int srcPitchPixels,
                                uint32_t* scratch2x, uint32_t* dst) {
    Port_Upscale_xBRZ_2x_Pitch(src, srcW, srcH, srcPitchPixels, scratch2x);
    Port_Upscale_xBRZ_2x_Pitch(scratch2x, srcW * 2, srcH * 2, srcW * 2, dst);
}

void Port_Upscale_xBRZ_2x(const uint32_t* src, int srcW, int srcH, uint32_t* dst) {
    Port_Upscale_xBRZ_2x_Pitch(src, srcW, srcH, srcW, dst);
}

void Port_Upscale_xBRZ_4x(const uint32_t* src, int srcW, int srcH,
                          uint32_t* scratch2x, uint32_t* dst) {
    Port_Upscale_xBRZ_4x_Pitch(src, srcW, srcH, srcW, scratch2x, dst);
}
