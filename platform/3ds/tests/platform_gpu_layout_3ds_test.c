#include "platform_gpu_3ds.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    const PlatformGpu3DSUploadLayout oldLayout = PlatformGpu3DS_GetUploadLayout(true);
    const PlatformGpu3DSUploadLayout newLayout = PlatformGpu3DS_GetUploadLayout(false);
    if (oldLayout.topPitch != 512u || oldLayout.topRows != 256u ||
        oldLayout.bottomPitch != 512u || oldLayout.bottomRows != 256u ||
        newLayout.topPitch != 512u || newLayout.topRows != 256u ||
        newLayout.bottomPitch != 512u || newLayout.bottomRows != 256u) {
        fputs("platform_gpu_layout_3ds_test: invalid texture transfer dimensions\n", stderr);
        return 1;
    }

    puts("platform_gpu_layout_3ds_test: PASS");
    return 0;
}
