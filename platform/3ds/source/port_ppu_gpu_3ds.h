#pragma once

#include "port_ppu_gpu_3ds_model.h"


typedef struct PortPpuGpu3DSStats {
    uint64_t attemptedFrames, renderedFrames, fallbackFrames, disabledFrames;
    uint64_t tileHits, tileDecodes, atlasFlushBytes;
    uint64_t vertices, batches, parityChecks, parityFailures, differingPixels;
    uint32_t firstDiffX, firstDiffY;
    uint64_t preflightTicks, drawTicks;
    bool initialized, enabled, disabled;
} PortPpuGpu3DSStats;

bool PortPpuGpu3DS_Init(void);
void PortPpuGpu3DS_Shutdown(void);
bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame);
bool PortPpuGpu3DS_DrawPrepared(void);
void* PortPpuGpu3DS_OutputTexture(void);
void PortPpuGpu3DS_Disable(void);
bool PortPpuGpu3DS_IsDisabled(void);
void PortPpuGpu3DS_RecordDisabledFrame(void);
void PortPpuGpu3DS_GetStats(PortPpuGpu3DSStats* stats);
void PortPpuGpu3DS_RequestParityCheck(void);
bool PortPpuGpu3DS_ParityRequested(void);
void PortPpuGpu3DS_CaptureParityReference(const uint32_t* pixels, unsigned pitch,
                                          unsigned width, unsigned height);
bool PortPpuGpu3DS_QueueParityCopy(void);
void PortPpuGpu3DS_CancelParityCheck(void);
bool PortPpuGpu3DS_ParityFinishedThisFrame(void);
void PortPpuGpu3DS_FinishParityCheck(void);
