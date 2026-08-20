#pragma once

#include "port_ppu_gpu_3ds_model.h"


typedef struct PortPpuGpu3DSStats {
    uint64_t attemptedFrames, renderedFrames, fallbackFrames, disabledFrames;
    uint64_t tileHits, tileDecodes, atlasFlushBytes, atlasFlushCalls;
    uint64_t vertices, batches, parityChecks, parityFailures, differingPixels;
    /* Differences the blender's 8-bit constants cannot account for. */
    uint64_t structuralPixels;
    uint32_t firstDiffX, firstDiffY;
    uint64_t preflightTicks, drawTicks;
    /* Preflight split into command building and GPU cache maintenance. */
    uint64_t buildTicks, flushTicks;
    /* Indexed by PpuGpu3DSBuildReason, plus the high-water marks that say how
     * close a rendered frame came to the command-buffer limits. */
    uint64_t buildFailures[PPU_GPU3DS_BUILD_REASON_COUNT];
    uint32_t maxBands, maxVertices, maxBatches, maxRequiredVertices;
    uint64_t mapLayers, mapRejects[PPU_GPU3DS_MAP_REJECT_COUNT];
    uint32_t mapLargestQuads;
    /* The frame the quick dump captured, so a host replay is comparable. */
    uint64_t lastBuildTicks;
    uint32_t lastBands, lastVertices;
    uint8_t lastMapLayerMask;
    bool initialized, enabled, disabled;
} PortPpuGpu3DSStats;

bool PortPpuGpu3DS_Init(void);
void PortPpuGpu3DS_Shutdown(void);
bool PortPpuGpu3DS_Preflight(const PpuGpu3DSFrameView* frame);
bool PortPpuGpu3DS_DrawPrepared(void);
bool PortPpuGpu3DS_BindPresentShader(void);
void* PortPpuGpu3DS_OutputTexture(void);
void PortPpuGpu3DS_Disable(void);
bool PortPpuGpu3DS_IsDisabled(void);
void PortPpuGpu3DS_RecordDisabledFrame(void);
void PortPpuGpu3DS_GetStats(PortPpuGpu3DSStats* stats);
/* Directory the last quick dump wrote into, for parity artifacts. */
const char* Port_PPU_3DS_LastDumpDirectory(void);
/* Reads back the frame currently on screen, before anything perturbs it. */
void PortPpuGpu3DS_RequestFrameCapture(void);
bool PortPpuGpu3DS_FrameCaptureRequested(void);
bool PortPpuGpu3DS_QueueFrameCapture(void);
void PortPpuGpu3DS_WriteFrameCapture(const char* directory);

unsigned long long PortPpuGpu3DS_EmptyDrawsSkipped(void);
void PortPpuGpu3DS_RequestParityCheck(void);
bool PortPpuGpu3DS_ParityRequested(void);
void PortPpuGpu3DS_CaptureParityReference(const uint32_t* pixels, unsigned pitch,
                                          unsigned width, unsigned height);
bool PortPpuGpu3DS_QueueParityCopy(void);
void PortPpuGpu3DS_CancelParityCheck(void);
void PortPpuGpu3DS_DeferParityCheck(void);
bool PortPpuGpu3DS_ParityFinishedThisFrame(void);
void PortPpuGpu3DS_FinishParityCheck(void);
