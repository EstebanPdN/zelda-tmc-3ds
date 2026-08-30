/*
 * Upload layout contract.
 *
 * The previous version of this test asserted 512x256 for BOTH arguments, which
 * is what the stub it was written against returned. It passed for months while
 * the compact surface did not exist, so "platform_gpu_layout_3ds_test: PASS"
 * was never evidence of anything. Assert the two layouts are actually
 * different, and assert the properties a display transfer needs, so a stub
 * cannot satisfy this again.
 */
#include "platform_gpu_3ds.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                    \
            fputs("platform_gpu_layout_3ds_test: FAILED " #cond "\n", stderr);            \
            return 1;                                                                     \
        }                                                                                 \
    } while (0)

int main(void) {
    const PlatformGpu3DSUploadLayout full = PlatformGpu3DS_GetUploadLayout(false);
    const PlatformGpu3DSUploadLayout compact = PlatformGpu3DS_GetUploadLayout(true);

    /* Full surface matches the PICA textures and must not change. */
    CHECK(full.topPitch == 512u && full.topRows == 256u);
    CHECK(full.bottomPitch == 512u && full.bottomRows == 256u);

    /* Compact must cover everything that is drawn: 266 is the widescreen top
     * capacity, 160 the GBA height, and the bottom screen is 320x240. */
    CHECK(compact.topPitch >= 266u);
    CHECK(compact.topRows >= 160u);
    CHECK(compact.bottomPitch >= 320u);
    CHECK(compact.bottomRows >= 240u);

    /* A display transfer wants 8-aligned dimensions. */
    CHECK((compact.topPitch & 7u) == 0u);
    CHECK((compact.topRows & 7u) == 0u);
    CHECK((compact.bottomPitch & 7u) == 0u);
    CHECK((compact.bottomRows & 7u) == 0u);

    /* Must fit inside the textures it transfers into. */
    CHECK(compact.topPitch <= full.topPitch && compact.topRows <= full.topRows);
    CHECK(compact.bottomPitch <= full.bottomPitch && compact.bottomRows <= full.bottomRows);

    /* And it must actually be smaller, or it buys nothing. This is the
     * assertion a stub fails. */
    const uint64_t fullBottom =
        (uint64_t)full.bottomPitch * full.bottomRows * sizeof(uint32_t);
    const uint64_t compactBottom =
        (uint64_t)compact.bottomPitch * compact.bottomRows * sizeof(uint32_t);
    CHECK(compactBottom < fullBottom);

    const uint64_t fullTop = (uint64_t)full.topPitch * full.topRows * sizeof(uint32_t);
    const uint64_t compactTop = (uint64_t)compact.topPitch * compact.topRows * sizeof(uint32_t);
    CHECK(compactTop < fullTop);

    printf("platform_gpu_layout_3ds_test: PASS (bottom %llu -> %llu bytes, top %llu -> %llu)\n",
           (unsigned long long)fullBottom, (unsigned long long)compactBottom,
           (unsigned long long)fullTop, (unsigned long long)compactTop);
    return 0;
}
