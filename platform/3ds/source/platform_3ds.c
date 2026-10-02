#include "platform_3ds.h"
#include "old3ds_frame_pacer.h"
#include "port_audio_3ds.h"
#include "port_second_screen_3ds.h"
#include "port_second_screen_sync_3ds.h"

#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sHeld;
static uint32_t sDown;
static circlePosition sCirclePosition;
static circlePosition sCStickPosition;
static bool sCStickHeld;
static bool sQuickDumpRequested;
static bool sQuickDumpComboWasHeld;
static bool sRunning;
static bool sIsNew3DS;
static unsigned sTurboMultiplier = 5;
static bool sCore1Available;
static unsigned sCore1TimeLimit;
static bool sBottomWorkerAttempted;
static bool sBottomWorkerRunning;
static bool sBottomWorkerBusy;
static bool sSpeedupRequested;
static unsigned sTurboPhase;
static uint64_t sLogicFrames;
static uint64_t sPresentedFrames;
static uint64_t sLogicLastTick;
static uint64_t sTurboLogicFrames;
static uint64_t sTurboSkippedPresentations;
static uint64_t sEngineWorkTicks;
static uint64_t sEngineWorkLastTicks;
static uint64_t sEngineWorkMaxTicks;
static uint64_t sVblankWaitTicks;
static uint64_t sVblankWaitLastTicks;
static uint64_t sVblankWaitMaxTicks;
static uint64_t sAptChecks;
static uint64_t sFrameBoundaryEndTick;
static Old3DSFramePacer sOld3DSFramePacer;
static uint64_t sOld3DSSkippedTickSleep;
static PrintConsole sGameplayLogSink;
static bool sGameplayDisplayActive;
static uintptr_t sStackRegionBase;
static uintptr_t sStackRegionEnd;
typedef struct {
    uintptr_t base;
    uintptr_t end;
} NativeMemoryRegion;
static NativeMemoryRegion sNativeMemoryRegions[8];
static unsigned sNativeMemoryRegionCount;
static Thread sBottomWorkerThread;
static LightEvent sBottomWorkerStart;
static LightEvent sBottomWorkerDone;
static aptHookCookie sAptHookCookie;
static bool sAptHookRegistered;
extern void Port_Audio_Shutdown(void);
static void PollInput(void);

void Platform3DS_MarkFrameDiscontinuity(Old3DSFramePacerDiscontinuity reason) {
    Old3DSFramePacer_MarkDiscontinuity(&sOld3DSFramePacer, reason);
}

static void OnAptEvent(APT_HookType hook, void* parameter) {
    (void)parameter;
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONSLEEP:
        case APTHOOK_ONWAKEUP:
            /* APT is the authority for HOME/lid discontinuities. The atomic
             * mark is consumed at the next logic boundary, so suspend and
             * restore callbacks before that boundary coalesce into one resync. */
            Platform3DS_MarkFrameDiscontinuity(OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
            break;
        default:
            break;
    }
}

static void RegisterAptHook(void) {
    if (sAptHookRegistered) return;
    aptHook(&sAptHookCookie, OnAptEvent, NULL);
    sAptHookRegistered = true;
}

int Platform3DS_Init(void) {
    Port_SecondScreen_3DS_SyncInit();
    sHeld = 0;
    sDown = 0;
    memset(&sCirclePosition, 0, sizeof(sCirclePosition));
    memset(&sCStickPosition, 0, sizeof(sCStickPosition));
    sCStickHeld = false;
    sQuickDumpRequested = false;
    sQuickDumpComboWasHeld = false;
    sCore1Available = false;
    sCore1TimeLimit = 0;
    sSpeedupRequested = false;
    sTurboPhase = 0;
    sLogicFrames = 0;
    sPresentedFrames = 0;
    sLogicLastTick = 0;
    sTurboLogicFrames = 0;
    sTurboSkippedPresentations = 0;
    sEngineWorkTicks = 0;
    sEngineWorkLastTicks = 0;
    sEngineWorkMaxTicks = 0;
    sVblankWaitTicks = 0;
    sVblankWaitLastTicks = 0;
    sVblankWaitMaxTicks = 0;
    sAptChecks = 0;
    sFrameBoundaryEndTick = 0;
    sOld3DSSkippedTickSleep = 0;
    sGameplayDisplayActive = false;
    sStackRegionBase = 0;
    sStackRegionEnd = 0;
    sNativeMemoryRegionCount = 0;
    memset(sNativeMemoryRegions, 0, sizeof(sNativeMemoryRegions));
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);
    cfguInit();
    romfsInit();
    consoleInit(GFX_BOTTOM, NULL);

    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    APT_CheckNew3DS(&sIsNew3DS);
    Old3DSFramePacer_Init(&sOld3DSFramePacer, SYSCLOCK_ARM11);
    RegisterAptHook();
    if (sIsNew3DS) {
        osSetSpeedupEnable(true);
        sSpeedupRequested = true;
    }

    static const u32 core1Candidates[] = { 80, 70, 50, 30 };
    for (size_t i = 0; i < sizeof(core1Candidates) / sizeof(core1Candidates[0]); ++i) {
        if (R_FAILED(APT_SetAppCpuTimeLimit(core1Candidates[i]))) continue;
        u32 actual = 0;
        if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&actual)) && actual > 0) {
            sCore1Available = true;
            sCore1TimeLimit = actual;
            break;
        }
    }

    sRunning = true;
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    hidCircleRead(&sCirclePosition);
    if (sIsNew3DS) hidCstickRead(&sCStickPosition);
    return 1;
}

void Platform3DS_Shutdown(void) {
    sRunning = false;
    if (sAptHookRegistered) {
        aptUnhook(&sAptHookCookie);
        sAptHookRegistered = false;
    }
    Platform3DS_ShutdownBottomWorker();
    extern void virtuappu_mode1_shutdown_workers(void);
    virtuappu_mode1_shutdown_workers();
    Port_Audio_Shutdown();
    romfsExit();
    cfguExit();
    gfxExit();
}

bool Platform3DS_IsRunning(void) { return sRunning; }
bool Platform3DS_IsNew3DS(void) { return sIsNew3DS; }
bool Platform3DS_CanUseCore1(void) { return sCore1Available; }
unsigned Platform3DS_Core1TimeLimit(void) { return sCore1TimeLimit; }
bool Platform3DS_TurboHeld(void) { return sIsNew3DS && sCStickHeld; }
unsigned Platform3DS_TurboMultiplier(void) { return sTurboMultiplier; }
void Platform3DS_SetTurboMultiplier(unsigned multiplier) {
    sTurboMultiplier = multiplier < 2 ? 2 : (multiplier > 5 ? 5 : multiplier);
}

void Platform3DS_ShowSplash(void) {
    FILE* file = fopen("romfs:/splash.rgb565", "rb");
    uint16_t* pixels = NULL;
    if (file) {
        pixels = (uint16_t*)malloc(400u * 240u * sizeof(uint16_t));
        if (!pixels || fread(pixels, sizeof(uint16_t), 400u * 240u, file) != 400u * 240u) {
            free(pixels);
            pixels = NULL;
        }
        fclose(file);
    }
    if (!pixels) return;

    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t* top = (uint16_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    if (top) {
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 400; ++x) {
                top[(239 - y) + x * 240] = pixels[y * 400 + x];
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        svcSleepThread(1200000000LL);
    }
    free(pixels);
}

static bool GameplayLogSinkPrint(void* console, int character) {
    (void)console;
    (void)character;
    return true;
}

void Platform3DS_EnterGameplayDisplay(void) {
    if (sGameplayDisplayActive) return;

    /* Keep the visible console throughout boot, then prevent stdout from ever
     * writing into the bottom framebuffer owned by Citro3D. stderr remains
     * available to an attached debugger through SVC instead of being painted
     * over the live map/touch UI. */
    fflush(stdout);
    fflush(stderr);
    sGameplayLogSink = *consoleGetDefault();
    sGameplayLogSink.frameBuffer = NULL;
    sGameplayLogSink.PrintChar = GameplayLogSinkPrint;
    sGameplayLogSink.consoleInitialised = true;
    consoleSelect(&sGameplayLogSink);
    consoleDebugInit(debugDevice_SVC);
    sGameplayDisplayActive = true;
}

static uint16_t MapKeysToGba(uint32_t keys) {
    enum {
        GBA_A = 1u << 0,
        GBA_B = 1u << 1,
        GBA_SELECT = 1u << 2,
        GBA_START = 1u << 3,
        GBA_RIGHT = 1u << 4,
        GBA_LEFT = 1u << 5,
        GBA_UP = 1u << 6,
        GBA_DOWN = 1u << 7,
        GBA_R = 1u << 8,
        GBA_L = 1u << 9,
    };
    const bool quickDumpCombo = (keys & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    uint16_t input = 0x03ff;
    if ((keys & KEY_A) && !quickDumpCombo) input &= ~GBA_A;
    if (keys & KEY_B) input &= ~GBA_B;
    if (keys & KEY_SELECT) input &= ~GBA_SELECT;
    if (keys & KEY_START) input &= ~GBA_START;
    if (keys & (KEY_DRIGHT | KEY_CPAD_RIGHT)) input &= ~GBA_RIGHT;
    if (keys & (KEY_DLEFT | KEY_CPAD_LEFT)) input &= ~GBA_LEFT;
    if (keys & (KEY_DUP | KEY_CPAD_UP)) input &= ~GBA_UP;
    if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) input &= ~GBA_DOWN;
    if ((keys & KEY_R) && !quickDumpCombo) input &= ~GBA_R;
    if ((keys & KEY_L) && !quickDumpCombo) input &= ~GBA_L;
    return input;
}

uint16_t Platform3DS_ReadKeyInput(void) { return MapKeysToGba(sHeld); }
uint16_t Platform3DS_ReadKeyDownInput(void) { return MapKeysToGba(sDown); }

uint32_t Platform3DS_KeysHeld(void) {
    return sHeld;
}

void Platform3DS_ReadCircle(float* x, float* y) {
    if (x) *x = sCirclePosition.dx / 156.0f;
    if (y) *y = -sCirclePosition.dy / 156.0f;
}

uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height) {
    return (uint16_t*)gfxGetFramebuffer(top ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, width, height);
}

uint64_t Platform3DS_Milliseconds(void) {
    return osGetTime();
}

uint64_t Platform3DS_SystemTick(void) {
    return svcGetSystemTick();
}

uint64_t Platform3DS_TicksPerSecond(void) {
    return SYSCLOCK_ARM11;
}

int Platform3DS_IsNativeAddress(uintptr_t value) {
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));

    if (sStackRegionEnd == 0) {
        MemInfo stackInfo;
        PageInfo pageInfo;
        if (R_FAILED(svcQueryMemory(&stackInfo, &pageInfo, (u32)currentSp)) ||
            (stackInfo.perm & MEMPERM_WRITE) == 0u) {
            return 0;
        }
        sStackRegionBase = stackInfo.base_addr;
        sStackRegionEnd = sStackRegionBase + stackInfo.size;
    }

    if (value >= sStackRegionBase && value < sStackRegionEnd) return 1;

    for (unsigned i = 0; i < sNativeMemoryRegionCount; ++i) {
        if (value >= sNativeMemoryRegions[i].base && value < sNativeMemoryRegions[i].end) return 1;
    }

    MemInfo info;
    PageInfo pageInfo;
    if (R_FAILED(svcQueryMemory(&info, &pageInfo, (u32)value))) return 0;
    if (info.base_addr == 0 || info.size == 0) return 0;
    if (value < info.base_addr || value >= info.base_addr + info.size) return 0;
    if ((info.perm & (MEMPERM_READ | MEMPERM_WRITE)) == 0u) return 0;

    if (sNativeMemoryRegionCount < sizeof(sNativeMemoryRegions) / sizeof(sNativeMemoryRegions[0])) {
        NativeMemoryRegion* region = &sNativeMemoryRegions[sNativeMemoryRegionCount++];
        region->base = info.base_addr;
        region->end = info.base_addr + info.size;
    }
    return 1;
}

int Platform3DS_IsActiveStackAddress(uintptr_t value) {
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));

    /* GBA ROM addresses may overlap the 3DS stack reservation numerically.
     * Only objects close to the current frame are unambiguously native stack
     * pointers; keeping this window small preserves normal GBA ROM mapping. */
    const uintptr_t window = 64u * 1024u;
    return value >= currentSp - window && value <= currentSp + window;
}

bool Platform3DS_BeginFrameBoundary(void) {
    const uint64_t now = svcGetSystemTick();
    if (sFrameBoundaryEndTick != 0) {
        sEngineWorkLastTicks = now - sFrameBoundaryEndTick;
        sEngineWorkTicks += sEngineWorkLastTicks;
        if (sEngineWorkLastTicks > sEngineWorkMaxTicks) sEngineWorkMaxTicks = sEngineWorkLastTicks;
    }

    ++sLogicFrames;
    if (sIsNew3DS && sLogicLastTick != 0) {
        /* The New 3DS does not use adaptive presentation skipping, but its
         * diagnostic cadence still needs the pacer's HOME/sleep/dump filter. */
        Old3DSFramePacer_RecordActiveInterval(&sOld3DSFramePacer, now - sLogicLastTick);
    }
    sLogicLastTick = now;
    if (!sIsNew3DS) {
        const bool present = Old3DSFramePacer_BeginTick(&sOld3DSFramePacer, now, &sOld3DSSkippedTickSleep);
        if (present) ++sPresentedFrames;
        return present;
    }

    if (!Platform3DS_TurboHeld()) {
        sTurboPhase = 0;
        ++sPresentedFrames;
        return true;
    }

    ++sTurboLogicFrames;
    if (++sTurboPhase >= Platform3DS_TurboMultiplier()) {
        sTurboPhase = 0;
        ++sPresentedFrames;
        return true;
    }
    ++sTurboSkippedPresentations;
    return false;
}

void Platform3DS_EndFrameBoundary(void) {
    sFrameBoundaryEndTick = svcGetSystemTick();
}

static bool PumpLifecycleAndAudio(void) {
    ++sAptChecks;
    if (!sRunning || !aptMainLoop()) {
        sRunning = false;
        return false;
    }
    Port_Audio_3DSPump();
    return true;
}

void Platform3DS_PumpWithoutVBlank(void) {
    if (!PumpLifecycleAndAudio()) return;
    if (sIsNew3DS) return;

    if (sOld3DSSkippedTickSleep != 0) {
        const uint64_t sleepNs = sOld3DSSkippedTickSleep * 1000000000ULL / SYSCLOCK_ARM11;
        sOld3DSSkippedTickSleep = 0;
        if (sleepNs != 0) svcSleepThread((int64_t)sleepNs);
    }

    PollInput();
}

void Platform3DS_GetRuntimeStats(Platform3DSRuntimeStats* stats) {
    if (!stats) return;
    s32 threadPriority = -1;
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));
    svcGetThreadPriority(&threadPriority, CUR_THREAD_HANDLE);
    if (sStackRegionEnd == 0) Platform3DS_IsNativeAddress(currentSp);
    *stats = (Platform3DSRuntimeStats){
        .kernelVersion = osGetKernelVersion(),
        .firmVersion = osGetFirmVersion(),
        .systemCoreVersion = osGetSystemCoreVersion(),
        .applicationMemoryType = osGetApplicationMemType(),
        .mainThreadPriority = threadPriority,
        .applicationMemoryFree = osGetMemRegionFree(MEMREGION_APPLICATION),
        .systemMemoryFree = osGetMemRegionFree(MEMREGION_SYSTEM),
        .baseMemoryFree = osGetMemRegionFree(MEMREGION_BASE),
        .linearMemoryFree = linearSpaceFree(),
        .currentStackPointer = currentSp,
        .stackRegionBase = sStackRegionBase,
        .stackRegionEnd = sStackRegionEnd,
        .logicFrames = sLogicFrames,
        .presentedFrames = sPresentedFrames,
        .logicElapsedTicks = sOld3DSFramePacer.activeElapsedTicks,
        .logicCadenceIntervals = sOld3DSFramePacer.activeIntervals,
        .turboLogicFrames = sTurboLogicFrames,
        .turboSkippedPresentations = sTurboSkippedPresentations,
        .old3dsSkippedPresentations = sOld3DSFramePacer.skippedPresentations,
        .old3dsPacingSleepTicks = sOld3DSFramePacer.requestedSleepTicks,
        .old3dsPacingResyncs = sOld3DSFramePacer.resyncs,
        .old3dsAptDiscontinuities = sOld3DSFramePacer.aptDiscontinuities,
        .old3dsDumpDiscontinuities = sOld3DSFramePacer.dumpDiscontinuities,
        .old3dsDebtClampEvents = sOld3DSFramePacer.debtClampEvents,
        .old3dsPresentationDebtTicks = sOld3DSFramePacer.presentationDebtTicks,
        .old3dsMaxConsecutiveSkips = sOld3DSFramePacer.maxConsecutiveSkipsSeen,
        .engineWorkTicks = sEngineWorkTicks,
        .engineWorkLastTicks = sEngineWorkLastTicks,
        .engineWorkMaxTicks = sEngineWorkMaxTicks,
        .vblankWaitTicks = sVblankWaitTicks,
        .vblankWaitLastTicks = sVblankWaitLastTicks,
        .vblankWaitMaxTicks = sVblankWaitMaxTicks,
        .aptChecks = sAptChecks,
        .keyMaskHeld = sHeld,
        .keyMaskDown = sDown,
        .circleX = sCirclePosition.dx,
        .circleY = sCirclePosition.dy,
        .cstickX = sCStickPosition.dx,
        .cstickY = sCStickPosition.dy,
        .turboHeld = Platform3DS_TurboHeld(),
        .bottomWorkerRunning = sBottomWorkerRunning,
        .bottomWorkerBusy = sBottomWorkerBusy,
        .speedupRequested = sSpeedupRequested,
        .adaptiveFrameskipEnabled = !sIsNew3DS,
        .gameplayDisplayActive = sGameplayDisplayActive,
        .aptCloseRequested = aptShouldClose(),
    };
}

static void BottomWorkerMain(void* argument) {
    (void)argument;
    for (;;) {
        LightEvent_Wait(&sBottomWorkerStart);
        if (!__atomic_load_n(&sBottomWorkerRunning, __ATOMIC_ACQUIRE)) break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        extern void Port_PPU_3DS_RenderBottomWorker(void);
        Port_PPU_3DS_RenderBottomWorker();
        LightEvent_Signal(&sBottomWorkerDone);
    }
}

static bool EnsureBottomWorker(void) {
    if (sBottomWorkerThread) return true;
    if (sBottomWorkerAttempted) return false;
    sBottomWorkerAttempted = true;

    LightEvent_Init(&sBottomWorkerStart, RESET_ONESHOT);
    LightEvent_Init(&sBottomWorkerDone, RESET_ONESHOT);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (priority < 0x3e) priority += 2;
    sBottomWorkerRunning = true;
    const int core = 0;
    sBottomWorkerThread = threadCreate(BottomWorkerMain, NULL, 64u * 1024u, priority, core, false);
    if (!sBottomWorkerThread) sBottomWorkerRunning = false;
    return sBottomWorkerThread != NULL;
}

bool Platform3DS_SubmitBottomWorker(void) {
    if (sBottomWorkerBusy || !EnsureBottomWorker()) return false;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    sBottomWorkerBusy = true;
    LightEvent_Signal(&sBottomWorkerStart);
    return true;
}

bool Platform3DS_TryFinishBottomWorker(void) {
    if (!sBottomWorkerBusy || !LightEvent_TryWait(&sBottomWorkerDone)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    sBottomWorkerBusy = false;
    return true;
}

void Platform3DS_ShutdownBottomWorker(void) {
    if (!sBottomWorkerThread) return;
    __atomic_store_n(&sBottomWorkerRunning, false, __ATOMIC_RELEASE);
    LightEvent_Signal(&sBottomWorkerStart);
    threadJoin(sBottomWorkerThread, 2000000000ULL);
    threadFree(sBottomWorkerThread);
    sBottomWorkerThread = NULL;
    sBottomWorkerRunning = false;
    sBottomWorkerBusy = false;
    sBottomWorkerAttempted = false;
}

static void PollInput(void) {
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    hidCircleRead(&sCirclePosition);
    if (sIsNew3DS) {
        hidCstickRead(&sCStickPosition);
        sCStickHeld = (sHeld & (KEY_CSTICK_UP | KEY_CSTICK_DOWN | KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT)) != 0u ||
                      abs((int)sCStickPosition.dx) > 24 || abs((int)sCStickPosition.dy) > 24;
    } else {
        memset(&sCStickPosition, 0, sizeof(sCStickPosition));
        sCStickHeld = false;
    }
    if (sDown & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        Port_SecondScreen_3DS_OnTap(touch.px, touch.py, 0);
    }
    const bool quickDumpCombo = (sHeld & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    if (quickDumpCombo && !sQuickDumpComboWasHeld) sQuickDumpRequested = true;
    sQuickDumpComboWasHeld = quickDumpCombo;
}

void Platform3DS_WaitForVBlank(void) {
    if (!PumpLifecycleAndAudio()) return;
    extern bool Port_PPU_3DS_UsesGpuPresenter(void);
    const uint64_t waitStart = svcGetSystemTick();
    if (Port_PPU_3DS_UsesGpuPresenter()) {
        gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    } else {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    sVblankWaitLastTicks = svcGetSystemTick() - waitStart;
    sVblankWaitTicks += sVblankWaitLastTicks;
    if (sVblankWaitLastTicks > sVblankWaitMaxTicks) sVblankWaitMaxTicks = sVblankWaitLastTicks;
    /* EndBottom only marks a bottom generation submitted.  Promote it after
     * the display boundary and before this tick's HID scan, so touch never
     * targets a CPU-painted buffer that has not reached a presentation. */
    Port_SecondScreen_3DS_PromoteSubmitted();
    if (sQuickDumpRequested) {
        extern void Port_PPU_3DS_WriteQuickDump(void);
        sQuickDumpRequested = false;
        Port_PPU_3DS_WriteQuickDump();
    }
    PollInput();
}

void Platform3DS_ShowFatal(const char* title, const char* message) {
    /* consoleInit(NULL) reuses libctru's current console.  Gameplay replaces
     * that current console with a framebuffer-less log sink, so explicitly
     * select the real bottom console returned by consoleInit before printing. */
    PrintConsole* fatalConsole = consoleInit(GFX_BOTTOM, NULL);
    consoleSelect(fatalConsole);
    consoleDebugInit(debugDevice_CONSOLE);
    consoleClear();
    printf("%s\n\n%s\n\nPress START to exit.\n", title ? title : "Error", message ? message : "");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

void Platform3DS_Debug(const char* message) {
    if (!message) return;
    svcOutputDebugString(message, strlen(message));
    FILE* file = fopen("tmc3ds.log", "ab");
    if (file) {
        fwrite(message, 1, strlen(message), file);
        fclose(file);
    }
}

static void ReadFramebufferPixel(const uint8_t* pixel, GSPGPU_FramebufferFormat format,
                                 uint8_t* red, uint8_t* green, uint8_t* blue) {
    switch (format) {
        case GSP_RGB565_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 5) & 63u) * 255u / 63u);
            *blue = (uint8_t)((color & 31u) * 255u / 31u);
            break;
        }
        case GSP_BGR8_OES:
            *blue = pixel[0];
            *green = pixel[1];
            *red = pixel[2];
            break;
        case GSP_RGBA8_OES:
            *red = pixel[0];
            *green = pixel[1];
            *blue = pixel[2];
            break;
        case GSP_RGB5_A1_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 6) & 31u) * 255u / 31u);
            *blue = (uint8_t)(((color >> 1) & 31u) * 255u / 31u);
            break;
        }
        case GSP_RGBA4_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 12) & 15u) * 17u);
            *green = (uint8_t)(((color >> 8) & 15u) * 17u);
            *blue = (uint8_t)(((color >> 4) & 15u) * 17u);
            break;
        }
        default:
            *red = *green = *blue = 0;
            break;
    }
}

static bool SaveCapturedFramebufferBmp(const char* path, const GSPGPU_CaptureInfoEntry* capture,
                                       int width, int height) {
    if (!path || !capture || !capture->framebuf0_vaddr) return false;
    const GSPGPU_FramebufferFormat format = (GSPGPU_FramebufferFormat)(capture->format & 7u);
    const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
    if (bytesPerPixel < 2 || bytesPerPixel > 4 || capture->framebuf_widthbytesize == 0) return false;

    const uint8_t* framebuffer = (const uint8_t*)capture->framebuf0_vaddr;
    GSPGPU_InvalidateDataCache(framebuffer, capture->framebuf_widthbytesize * (u32)width);

    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const int rowSize = (width * 3 + 3) & ~3;
    const uint32_t fileSize = 54u + (uint32_t)rowSize * (uint32_t)height;
    const uint8_t header[54] = {
        'B', 'M',
        (uint8_t)fileSize, (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)width, (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)height, (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0, 24, 0,
    };
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    uint8_t row[400 * 3];
    for (int y = height - 1; ok && y >= 0; --y) {
        memset(row, 0, (size_t)rowSize);
        for (int x = 0; x < width; ++x) {
            const uint8_t* pixel = framebuffer + (size_t)x * capture->framebuf_widthbytesize +
                                   (size_t)(height - 1 - y) * bytesPerPixel;
            uint8_t red, green, blue;
            ReadFramebufferPixel(pixel, format, &red, &green, &blue);
            row[x * 3 + 0] = blue;
            row[x * 3 + 1] = green;
            row[x * 3 + 2] = red;
        }
        ok = fwrite(row, 1, (size_t)rowSize, file) == (size_t)rowSize;
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool SaveCapturedFramebufferRaw(const char* path, const GSPGPU_CaptureInfoEntry* capture,
                                       int width) {
    if (!path) return true;
    if (!capture || !capture->framebuf0_vaddr || capture->framebuf_widthbytesize == 0) return false;
    const size_t size = (size_t)capture->framebuf_widthbytesize * (size_t)width;
    GSPGPU_InvalidateDataCache(capture->framebuf0_vaddr, size);
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(capture->framebuf0_vaddr, 1, size, file) == size;
    if (fclose(file) != 0) return false;
    return ok;
}

bool Platform3DS_SaveDisplayedScreensDetailed(const char* topPath, const char* bottomPath,
                                              const char* topRawPath, const char* bottomRawPath,
                                              Platform3DSCaptureStats* stats) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    const GSPGPU_CaptureInfoEntry* top = &capture.screencapture[GSP_SCREEN_TOP];
    const GSPGPU_CaptureInfoEntry* bottom = &capture.screencapture[GSP_SCREEN_BOTTOM];
    if (stats) {
        *stats = (Platform3DSCaptureStats){
            .topFormat = top->format & 7u,
            .topStride = top->framebuf_widthbytesize,
            .bottomFormat = bottom->format & 7u,
            .bottomStride = bottom->framebuf_widthbytesize,
            .topAddress = (uintptr_t)top->framebuf0_vaddr,
            .bottomAddress = (uintptr_t)bottom->framebuf0_vaddr,
        };
    }
    const bool topOk = SaveCapturedFramebufferBmp(topPath, top, 400, 240);
    const bool bottomOk = SaveCapturedFramebufferBmp(bottomPath, bottom, 320, 240);
    const bool topRawOk = SaveCapturedFramebufferRaw(topRawPath, top, 400);
    const bool bottomRawOk = SaveCapturedFramebufferRaw(bottomRawPath, bottom, 320);
    return topOk && bottomOk && topRawOk && bottomRawOk;
}

bool Platform3DS_SaveDisplayedScreens(const char* topPath, const char* bottomPath) {
    return Platform3DS_SaveDisplayedScreensDetailed(topPath, bottomPath, NULL, NULL, NULL);
}
