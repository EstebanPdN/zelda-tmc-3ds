#include "platform_gpu_3ds.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    const PlatformGpu3DSUploadLayout oldLayout = PlatformGpu3DS_GetUploadLayout(true);
    assert(oldLayout.topPitch == 272u);
    assert(oldLayout.topRows == 160u);
    assert(oldLayout.bottomPitch == 320u);
    assert(oldLayout.bottomRows == 240u);
    assert(oldLayout.topPitch >= 266u && (oldLayout.topPitch & 7u) == 0u);
    assert(oldLayout.bottomPitch >= 320u && (oldLayout.bottomPitch & 7u) == 0u);
    assert(oldLayout.topPitch * oldLayout.topRows * sizeof(uint32_t) == 174080u);
    assert(oldLayout.bottomPitch * oldLayout.bottomRows * sizeof(uint32_t) == 307200u);

    const PlatformGpu3DSUploadLayout newLayout = PlatformGpu3DS_GetUploadLayout(false);
    assert(newLayout.topPitch == 512u);
    assert(newLayout.topRows == 256u);
    assert(newLayout.bottomPitch == 512u);
    assert(newLayout.bottomRows == 256u);

    puts("platform_gpu_layout_3ds_test: PASS");
    return 0;
}
