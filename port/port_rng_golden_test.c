/*
 * port/port_rng_golden_test.c — guards the gameplay PRNG against silent drift.
 *
 * Recomputes the first outputs from the boot seed using the SAME shared
 * Port_Rng_* primitives the live Random() uses, and asserts them against a
 * vector captured from the verified ARM algorithm. If anyone edits the
 * multiply/rotate constants in port_rng.h, this fails the build.
 */
#include <stdint.h>
#include <stdio.h>
#include "port_rng.h"

/* Captured from seed 0x1234567 (ror(state*3,13); output = state>>1). */
static const uint32_t kGoldenOutputs[8] = {
    0x40D40DA7u, 0x47AA13E1u, 0x5D1AB7F1u, 0x3EA4BA81u,
    0x7C19DF71u, 0x729BA26Cu, 0x3A2EBE97u, 0x5E297461u,
};

int main(void) {
    uint32_t state = PORT_RNG_SEED;
    for (int i = 0; i < 8; ++i) {
        state = Port_Rng_Advance(state);
        uint32_t out = Port_Rng_Output(state);
        if (out != kGoldenOutputs[i]) {
            fprintf(stderr,
                    "FAIL: PRNG drift at index %d: got 0x%08X expected 0x%08X\n",
                    i, out, kGoldenOutputs[i]);
            return 1;
        }
    }
    fprintf(stderr, "RNG GOLDEN VECTOR OK\n");
    return 0;
}
