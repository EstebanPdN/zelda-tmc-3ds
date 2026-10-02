#ifndef PLATFORM_GPU_3DS_H
#define PLATFORM_GPU_3DS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PlatformGpu3DSStats {
    uint64_t frames;
    uint64_t frameBeginFailures;
    uint64_t topTransfers;
    uint64_t bottomTransfers;
    uint64_t boundedFlushBytes;
    uint32_t linearHeapBytes;
    uint32_t c2dFlushBytes;
    uintptr_t c2dFlushAddress;
    uintptr_t topUploadAddress;
    uintptr_t bottomUploadAddress[2];
    float drawingTime;
    float processingTime;
} PlatformGpu3DSStats;

bool PlatformGpu3DS_Init(void);
uint32_t* PlatformGpu3DS_TopBuffer(void);
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index);
void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width);
/* Returns true only when a Citro3D frame was active and submitted. */
bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed);
void PlatformGpu3DS_ShowDumpSavedOverlay(void);
void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats);
void PlatformGpu3DS_Shutdown(void);

#endif
