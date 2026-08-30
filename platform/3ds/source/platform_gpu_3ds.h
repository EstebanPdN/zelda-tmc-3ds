#ifndef PLATFORM_GPU_3DS_H
#define PLATFORM_GPU_3DS_H

#include <stdbool.h>
#include <stdint.h>

#include "port_3ds_full_view_policy.h"

typedef struct PlatformGpu3DSUploadLayout {
    unsigned topPitch;
    unsigned topRows;
    unsigned bottomPitch;
    unsigned bottomRows;
} PlatformGpu3DSUploadLayout;

/*
 * Upload surface geometry, in RGBA8 pixels.
 *
 * The full 512x256 surface is 524288 bytes for each screen, but only 240x160 of
 * the top and 320x240 of the bottom is ever visible. The oversize surface costs
 * on every bottom change: PlatformGpu3DS_EndBottom cleans the span and then
 * issues a *synchronous* C3D_SyncDisplayTransfer, so the main thread blocks on
 * GSP -- which retires that queue on core 1 with the app's 20% quota -- for
 * 491520 bytes when 307200 would do.
 *
 * `compact` is passed by the caller, not derived here, and EVERY caller must
 * pass the same value: platform_gpu_3ds.c sizes the allocation and the transfer
 * from this, while port_ppu_3ds.c hands the pitch to the painter and to
 * VirtuaPPU. Disagreement means the painter strides differently from the
 * transfer and the screen is garbage.
 *
 * 272 rather than 266 for the top: the widescreen capacity is 266 and a display
 * transfer wants an 8-aligned width. 320 is the bottom screen exactly.
 */
static inline PlatformGpu3DSUploadLayout PlatformGpu3DS_GetUploadLayout(bool compact) {
    if (compact) {
        return (PlatformGpu3DSUploadLayout){ 272u, 160u, 320u, 240u };
    }
    return (PlatformGpu3DSUploadLayout){ 512u, 256u, 512u, 256u };
}

typedef struct PlatformGpu3DSStats {
    uint64_t frames;
    uint64_t frameBeginFailures;
    uint64_t topTransfers;
    uint64_t sharpBilinearFrames;
    uint64_t sharpBilinearFallbacks;
    uint64_t bottomTransfers;
    uint64_t bottomTransferTicks;
    uint64_t bottomTransferMaxTicks;
    uint64_t bottomTargetDraws;
    uint64_t bottomTargetReuseSkips;
    uint64_t boundedFlushBytes;
    uint32_t linearHeapBytes;
    uint32_t c2dFlushBytes;
    uint32_t topUploadPitch;
    uint32_t topUploadBytes;
    uint32_t bottomUploadPitch;
    uint32_t bottomUploadBytes;
    uintptr_t c2dFlushAddress;
    uintptr_t topUploadAddress;
    uintptr_t bottomUploadAddress[2];
    uint32_t sharpBilinearTargetBytes;
    bool sharpBilinearAvailable;
    float drawingTime;
    float processingTime;
} PlatformGpu3DSStats;

bool PlatformGpu3DS_Init(bool old3dsProfile);
uint32_t* PlatformGpu3DS_TopBuffer(void);
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index);
void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width, unsigned height,
                             unsigned validSourceWidth, unsigned validSourceHeight,
                             Port3DSFullViewMode mode, int cropX, int cropY);
bool PlatformGpu3DS_BeginCustomTop(void);
/* Repaint the black borders around the game image on the next few frames. */
void PlatformGpu3DS_InvalidateTopBorder(void);
void PlatformGpu3DS_DrawTopTexture(void* texture, unsigned width);
bool PlatformGpu3DS_QueueRgba5551Readback(void* texture, uint16_t* pixels);
/* Returns true only when a Citro3D frame was active and submitted. */
bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed);
void PlatformGpu3DS_ShowDumpSavedOverlay(void);
void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats);
void PlatformGpu3DS_InvalidateBottomTarget(void);
void PlatformGpu3DS_Shutdown(void);

#endif
