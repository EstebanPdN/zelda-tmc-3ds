#ifndef ARM11_FAST_MEM_H
#define ARM11_FAST_MEM_H

#include <stddef.h>

/* devkitARM's newlib memcpy/memset are generic word loops with no prefetching.
 * An Old 3DS has 32 KiB of L1, 32-byte cache lines and NO L2, so every miss
 * stalls directly on FCRAM for 100-200 cycles and a plain loop eats that
 * latency line by line. These copy a cache line per iteration with LDM/STM and
 * prefetch several lines ahead so the loads overlap the stalls.
 *
 * Worth it only for runs of at least a few cache lines; small copies are left
 * to the library, whose call overhead is lower than the setup here. */
void Arm11FastMemcpy(void* dst, const void* src, size_t bytes);
void Arm11FastMemset(void* dst, int value, size_t bytes);

#endif
