/*
 * tools/ppu_affine_test.c — unit test for the affine BG2 reference latch.
 *
 * virtuappu_mode1_affine_precompute() reproduces the GBA's per-scanline
 * BG2X/BG2Y "internal reference" behaviour (#132): reload from I/O whenever a
 * line's reference differs from the previous line's (a CPU/DMA write, e.g. the
 * Deepwood barrel's per-scanline HBlank DMA), otherwise advance by pb/pd.
 *
 * The end-to-end parity gate's `title` scene exercises the STATIC affine path
 * (constant reference). This test covers the per-scanline-HDMA path that a
 * static title screen can't, with closed-form expectations independent of the
 * implementation. It is the safety net for the Phase 3 mode1/mode2 unification.
 *
 * Build:
 *   gcc -O2 -fopenmp -I port/ppu/include -DMODE1_GBA_WIDTH=240 \
 *       tools/ppu_affine_test.c port/ppu/src/mode1.c port/ppu/src/virtuappu.c \
 *       -o /tmp/ppu_affine_test -lm
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "cpu/mode1.h"

#define H 160

static int failures = 0;

static void check(const char* name, int cond) {
    if (!cond) {
        printf("  FAIL: %s\n", name);
        failures++;
    } else
        printf("  ok:   %s\n", name);
}

/* Independent reference implementation of the latch (kept deliberately separate
 * from the production code so this is a real cross-check, not a tautology). */
static void reference(int n, int32_t ix, int32_t iy, const int32_t* rx, const int32_t* ry, const int16_t* pb,
                      const int16_t* pd, int32_t* ox, int32_t* oy) {
    int32_t ax = ix, ay = iy, px = ix, py = iy;
    for (int i = 0; i < n; ++i) {
        if (rx[i] != px)
            ax = rx[i];
        if (ry[i] != py)
            ay = ry[i];
        px = rx[i];
        py = ry[i];
        ox[i] = ax;
        oy[i] = ay;
        ax += pb[i];
        ay += pd[i];
    }
}

int main(void) {
    int32_t rx[H], ry[H], ox[H], oy[H], ex[H], ey[H];
    int16_t pb[H], pd[H];

    /* Case 1 — STATIC affine (title-screen sword): reference never changes, so
     * the internal point is pure accumulation out[L] = init + L*pb. */
    {
        const int32_t init = 0x12345;
        const int16_t step = 64;
        for (int i = 0; i < H; ++i) {
            rx[i] = init;
            ry[i] = init;
            pb[i] = step;
            pd[i] = -step;
        }
        virtuappu_mode1_affine_precompute(H, init, init, rx, ry, pb, pd, false, false, ox, oy);
        int ok = 1;
        for (int i = 0; i < H; ++i)
            if (ox[i] != init + (int32_t)i * step || oy[i] != init - (int32_t)i * step)
                ok = 0;
        check("static: pure pb/pd accumulation, no reload", ok);
    }

    /* Case 2 — per-scanline HDMA (rolling barrel): reference rewritten every
     * line, so each line's output is exactly that line's reference and the pb/pd
     * advance is discarded (this is the #132 "double-count" bug class). */
    {
        const int32_t init = 0;
        int ok = 1;
        for (int i = 0; i < H; ++i) {
            rx[i] = 0x1000 + i * 0x37;
            ry[i] = 0x9000 - i * 0x51; /* all distinct */
            pb[i] = 999;
            pd[i] = -777; /* must be ignored */
        }
        virtuappu_mode1_affine_precompute(H, init, init, rx, ry, pb, pd, false, false, ox, oy);
        for (int i = 0; i < H; ++i)
            if (ox[i] != rx[i] || oy[i] != ry[i])
                ok = 0;
        check("hdma: per-line reload wins, pb/pd advance discarded", ok);
    }

    /* Case 3 — MIXED (reload on some lines, accumulate on others) vs the
     * independent reference. */
    {
        const int32_t init = -0x4000;
        int32_t cur_x = 0x100, cur_y = 0x200;
        for (int i = 0; i < H; ++i) {
            if (i % 5 == 0) {
                cur_x += 0x800;
                cur_y -= 0x400;
            } /* a write this line */
            rx[i] = cur_x;
            ry[i] = cur_y;
            pb[i] = (int16_t)(i * 3 - 80);
            pd[i] = (int16_t)(50 - i);
        }
        reference(H, init, init, rx, ry, pb, pd, ex, ey);
        virtuappu_mode1_affine_precompute(H, init, init, rx, ry, pb, pd, false, false, ox, oy);
        int ok = 1;
        for (int i = 0; i < H; ++i)
            if (ox[i] != ex[i] || oy[i] != ey[i])
                ok = 0;
        check("mixed: matches independent reference", ok);
    }

    /* Case 4 — negative / sign-extended references accumulate correctly. */
    {
        const int32_t init = (int32_t)0xF8000000; /* large negative (28-bit sign) */
        for (int i = 0; i < H; ++i) {
            rx[i] = init;
            ry[i] = init;
            pb[i] = -1000;
            pd[i] = 1000;
        }
        virtuappu_mode1_affine_precompute(H, init, init, rx, ry, pb, pd, false, false, ox, oy);
        check("negative init: out[10] = init + 10*pb", ox[10] == init + 10 * -1000 && oy[10] == init + 10 * 1000);
    }

    /* Case 5 — CONSTANT-value HDMA (#28): the DMA rewrites the SAME BG2X/BG2Y
     * every line. Hardware reloads the latch on every write EVENT, pinning the
     * layer to the written reference on each line; value-diff detection alone
     * sees no change and drifts by pb/pd. The strobe restores the pin. */
    {
        const int32_t init = 0x4000;
        for (int i = 0; i < H; ++i) {
            rx[i] = init;
            ry[i] = init;
            pb[i] = 64;
            pd[i] = -64;
        }
        virtuappu_mode1_affine_precompute(H, init, init, rx, ry, pb, pd, true, true, ox, oy);
        int ok = 1;
        for (int i = 0; i < H; ++i)
            if (ox[i] != init || oy[i] != init)
                ok = 0;
        check("idempotent hdma strobe: layer pinned to written reference", ok);
    }

    if (failures) {
        printf("ppu_affine_test: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("ppu_affine_test: all passed\n");
    return 0;
}
