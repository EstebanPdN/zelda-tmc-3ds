#ifndef PORT_RNG_H
#define PORT_RNG_H

#include <stdint.h>

/*
 * Single source of truth for the Minish Cap gameplay PRNG (ARM asm @ 0x08000E50):
 *   state = ROR(state * 3, 13);  return state >> 1;
 * seeded with PORT_RNG_SEED at boot (src/main.c). Both the live Random()
 * (port_linked_stubs.c) and the golden-vector regression test use these so the
 * algorithm cannot silently drift. Gameplay depends on bit-exact results.
 */
#define PORT_RNG_SEED 0x1234567u

/* Advance the 32-bit state: new = ror(state * 3, 13). */
static inline uint32_t Port_Rng_Advance(uint32_t state) {
    state = state * 3u;
    return (state >> 13) | (state << 19);
}

/* The value Random() hands back for a given (already-advanced) state. */
static inline uint32_t Port_Rng_Output(uint32_t state) {
    return state >> 1;
}

#endif /* PORT_RNG_H */
