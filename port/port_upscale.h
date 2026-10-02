#ifndef PORT_UPSCALE_H
#define PORT_UPSCALE_H

/*
 * Pixel-art-aware software upscaler. xBRZ-inspired clean-room implementation:
 * detects edges via YCbCr color distance, blends two colors at diagonal cuts.
 * Operates on 32-bit ABGR pixels (matches virtuappu_frame_buffer layout).
 *
 * dst must point to a buffer of (srcW * 2) * (srcH * 2) uint32_t for 2x,
 * (srcW * 4) * (srcH * 4) for 4x.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Port_Upscale_xBRZ_2x_Pitch(const uint32_t* src, int srcW, int srcH,
                                int srcPitchPixels, uint32_t* dst);
void Port_Upscale_xBRZ_4x_Pitch(const uint32_t* src, int srcW, int srcH,
                                int srcPitchPixels,
                                uint32_t* scratch2x, uint32_t* dst);
void Port_Upscale_xBRZ_2x(const uint32_t* src, int srcW, int srcH, uint32_t* dst);
void Port_Upscale_xBRZ_4x(const uint32_t* src, int srcW, int srcH,
                          uint32_t* scratch2x, uint32_t* dst);

#ifdef __cplusplus
}
#endif

#endif
