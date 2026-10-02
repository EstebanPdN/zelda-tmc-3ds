#ifndef TMC_PLATFORM_3DS_H
#define TMC_PLATFORM_3DS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Platform3DSRuntimeStats {
    uint32_t kernelVersion;
    uint32_t firmVersion;
    uint32_t systemCoreVersion;
    uint32_t applicationMemoryType;
    int32_t mainThreadPriority;
    uint32_t applicationMemoryFree;
    uint32_t systemMemoryFree;
    uint32_t baseMemoryFree;
    uint32_t linearMemoryFree;
    uintptr_t currentStackPointer;
    uintptr_t stackRegionBase;
    uintptr_t stackRegionEnd;
    uint64_t logicFrames;
    uint64_t presentedFrames;
    uint64_t turboLogicFrames;
    uint64_t turboSkippedPresentations;
    uint64_t engineWorkTicks;
    uint64_t engineWorkLastTicks;
    uint64_t engineWorkMaxTicks;
    uint64_t vblankWaitTicks;
    uint64_t vblankWaitLastTicks;
    uint64_t vblankWaitMaxTicks;
    uint64_t aptChecks;
    uint32_t keyMaskHeld;
    uint32_t keyMaskDown;
    int16_t circleX;
    int16_t circleY;
    int16_t cstickX;
    int16_t cstickY;
    bool turboHeld;
    bool bottomWorkerRunning;
    bool bottomWorkerBusy;
    bool speedupRequested;
    bool aptCloseRequested;
} Platform3DSRuntimeStats;

typedef struct Platform3DSCaptureStats {
    uint32_t topFormat;
    uint32_t topStride;
    uint32_t bottomFormat;
    uint32_t bottomStride;
    uintptr_t topAddress;
    uintptr_t bottomAddress;
} Platform3DSCaptureStats;

int Platform3DS_Init(void);
void Platform3DS_Shutdown(void);
bool Platform3DS_IsRunning(void);
bool Platform3DS_IsNew3DS(void);
bool Platform3DS_CanUseCore1(void);
unsigned Platform3DS_Core1TimeLimit(void);
void Platform3DS_ShowSplash(void);
uint16_t Platform3DS_ReadKeyInput(void);
uint16_t Platform3DS_ReadKeyDownInput(void);
uint32_t Platform3DS_KeysHeld(void);
void Platform3DS_ReadCircle(float* x, float* y);
uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height);
uint64_t Platform3DS_Milliseconds(void);
uint64_t Platform3DS_SystemTick(void);
uint64_t Platform3DS_TicksPerSecond(void);
int Platform3DS_IsNativeAddress(uintptr_t value);
int Platform3DS_IsActiveStackAddress(uintptr_t value);
bool Platform3DS_SubmitBottomWorker(void);
bool Platform3DS_TryFinishBottomWorker(void);
void Platform3DS_ShutdownBottomWorker(void);
bool Platform3DS_BeginFrameBoundary(void);
void Platform3DS_EndFrameBoundary(void);
void Platform3DS_PumpWithoutVBlank(void);
bool Platform3DS_TurboHeld(void);
unsigned Platform3DS_TurboMultiplier(void);
void Platform3DS_SetTurboMultiplier(unsigned multiplier);
void Platform3DS_GetRuntimeStats(Platform3DSRuntimeStats* stats);
void Platform3DS_WaitForVBlank(void);
void Platform3DS_ShowFatal(const char* title, const char* message);
void Platform3DS_Debug(const char* message);
bool Platform3DS_SaveDisplayedScreens(const char* topPath, const char* bottomPath);
bool Platform3DS_SaveDisplayedScreensDetailed(const char* topPath, const char* bottomPath,
                                              const char* topRawPath, const char* bottomRawPath,
                                              Platform3DSCaptureStats* stats);

#ifdef __cplusplus
}
#endif

#endif
