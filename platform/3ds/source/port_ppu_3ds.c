#include "port_ppu.h"

#include "port_gba_mem.h"
#include "port_config.h"
#include "port_hdma.h"
#include "port_runtime_config.h"
#include "port_audio_3ds.h"
#include "port_save.h"
#include "port_second_screen.h"
#include "port_second_screen_3ds.h"
#include "port_second_screen_state.h"
#include "port_widescreen.h"
#include "platform_3ds.h"
#include "platform_gpu_3ds.h"
#include "port_ppu_gpu_3ds.h"

#include "virtuappu.h"
#include "cpu/mode1.h"
#include "main.h"
#include "map.h"
#include "menu.h"
#include "player.h"
#include "room.h"
#include "tileMap.h"
#include "vram.h"

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define GBA_NATIVE_W 240
#define GBA_H 160

static uint32_t* sBottomUploads[2];
static uint32_t* sTopUpload;
static unsigned sTopUploadPitch;
static unsigned sBottomUploadPitch;
static uint32_t sBottomTick;
static bool sBottomReady;
static bool sBottomTextureReady;
static bool sBottomTextureDirty;
static bool sBottomWorkerPending;
static int sBottomFrontBuffer;
static int sBottomWorkerBuffer;
static uint32_t sBottomWorkerTick;
static SecondScreenSnapshot sBottomWorkerSnapshot;
static bool sBottomSnapshotValid;
static volatile uint32_t sBottomWorkerGeneration;
static uint32_t sBottomBufferGeneration[2];
static uint8_t sBottomBufferInGame[2];
static uint64_t sBottomWorkerLastTicks;
static uint64_t sBottomPaintRequests;
static uint64_t sBottomPeriodicChecks;
static uint64_t sBottomPeriodicSkips;
static bool sGpuPresenterReady;
static bool sInitialized;
static bool sColorCorrection;
static uint32_t sFrameNumber;
static uint64_t sPerfLastFrameTick;
static uint64_t sPerfRenderTicks;
static uint64_t sPerfTopTicks;
static uint64_t sPerfBottomTicks;
static uint64_t sPerfTotalTicks;
static uint64_t sPerfSamples;
static uint64_t sPerfBottomSamples;
static uint64_t sPerfRenderMaxTicks;
static uint64_t sPerfTopMaxTicks;
/* Frequency buckets for the presentation span; thresholds are derived from the
 * tick rate once, in Port_PPU_Init. */
static uint64_t sPerfTopOver4ms;
static uint64_t sPerfTopOver16ms;
static uint64_t sPerfTopOver50ms;
static uint64_t sPerfTopOver4msTicks;
static uint64_t sPerfTopOver16msTicks;
static uint64_t sPerfTopOver50msTicks;
static uint64_t sPerfRenderOver4ms;
static uint64_t sPerfRenderOver16ms;
static uint64_t sPerfRenderOver50ms;
static uint64_t sFrameBeginTicks;
static uint64_t sFrameBeginMaxTicks;
static uint64_t sFrameBeginOver4ms;
static uint64_t sFrameBeginOver16ms;
static uint64_t sFrameBeginSamples;
static uint64_t sPreflightTicks;
static uint64_t sPreflightMaxTicks;
static uint64_t sPreflightOver4ms;
static uint64_t sPreflightOver16ms;
static uint64_t sPerfBottomMaxTicks;
static uint64_t sPerfTotalMaxTicks;
static uint64_t sPerfIntervalTicks;
static uint64_t sPerfIntervalLastTicks;
static uint64_t sPerfIntervalMinTicks;
static uint64_t sPerfIntervalMaxTicks;
static uint64_t sPerfIntervalSamples;
static uint64_t sPerfFramesOver16ms;
static uint64_t sPerfFramesOver33ms;
static volatile uint32_t sCurrentFpsX100;
static volatile uint32_t sAverageFpsX100;
static int sTopPresentWidth = GBA_NATIVE_W;
static uint8_t sGpuIoPerLine[MODE1_GBA_HEIGHT][MODE1_IO_MEM_SIZE];
static bool sGpuIoUniform = true;
/* Presentation split: submitting the PPU draws versus the presenter blit. */
static uint64_t sTopDrawTicks;
static uint64_t sTopBlitTicks;
static uint16_t sGpuDispcntPerLine[MODE1_GBA_HEIGHT];
static int32_t sGpuAffRefX[MODE1_GBA_HEIGHT], sGpuAffRefY[MODE1_GBA_HEIGHT];
static uint16_t
    sGpuWsShadow[MODE1_GBA_BG_COUNT * MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];
static bool sGpuPpuInitialized, sGpuPpuDisabled;

extern uint8_t virtuappu_mode1_obj_clip_mark[MODE1_GBA_OAM_COUNT];
extern int virtuappu_mode1_obj_clip_y;
extern int virtuappu_mode1_obj_clip_enable;

static int TopFrameWidth(void) {
    Port_Widescreen_SetWindowPixels(400, 240);
    if (Port_Widescreen_IsActive() && Port_Widescreen_ShadowsLive()) {
        int width = Port_Widescreen_EffectiveViewWidth();
        if (width > MODE1_GBA_WIDTH)
            width = MODE1_GBA_WIDTH;
        if (width > GBA_NATIVE_W)
            return width;
    }
    return GBA_NATIVE_W;
}

static void FillPreparedFrameView(PpuGpu3DSFrameView* view) {
    memset(view, 0, sizeof(*view));
    view->width = sTopPresentWidth;
    view->height = MODE1_GBA_HEIGHT;
    view->affine = virtuappu_registers.mode == 2;
    view->ioUniform = virtuappu_mode1_pre_line_callback == NULL;
    /* Remembered for the quick dump: by the time it runs the HDMA channels
     * have already been reset, so the callback no longer says what the frame
     * that was built actually saw. */
    sGpuIoUniform = view->ioUniform;
    virtuappu_mode1_get_bound_gba_memory(&view->memory);
    view->ioPerLine = &sGpuIoPerLine[0][0];
    view->dispcntPerLine = sGpuDispcntPerLine;
    view->affineRefX = sGpuAffRefX;
    view->affineRefY = sGpuAffRefY;
    view->wsCols = MODE1_WS_SHADOW_COLS;
    view->wsHudRightAnchor = virtuappu_mode1_ws_hud_right_anchor;
    view->wsHudRightNativeX = MODE1_WS_HUD_RIGHT_NATIVE_X;
    view->wsMsgShift = virtuappu_mode1_ws_msg_shift;
    view->wsMsgX0 = virtuappu_mode1_ws_msg_x0;
    view->wsMsgX1 = virtuappu_mode1_ws_msg_x1;
    view->wsMsgY0 = virtuappu_mode1_ws_msg_y0;
    view->wsMsgY1 = virtuappu_mode1_ws_msg_y1;
    view->objClipEnable = virtuappu_mode1_obj_clip_enable;
    view->objClipMark = virtuappu_mode1_obj_clip_mark;
    view->objClipY = virtuappu_mode1_obj_clip_y;
    bool anyShadow = false;
    for (unsigned bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
        view->wsShadowBaseTile[bg] = virtuappu_mode1_ws_shadow[bg]
                                                ? virtuappu_mode1_ws_shadow_base_tile[bg]
                                                : -1;
        if (virtuappu_mode1_ws_shadow[bg]) {
            memcpy(&sGpuWsShadow[bg * MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS],
                   virtuappu_mode1_ws_shadow[bg],
                   MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS * sizeof(uint16_t));
            anyShadow = true;
        }
    }
    view->wsShadow = anyShadow ? sGpuWsShadow : NULL;
    view->wsShadowHalfwords =
        anyShadow ? (int)(sizeof(sGpuWsShadow) / sizeof(sGpuWsShadow[0])) : 0;
}

#ifdef TMC_3DS_DIAGNOSTICS
static unsigned sDiagnosticFrames;
static uint64_t sDiagnosticStartMs;

static void DumpPpuSnapshot(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file)
        return;
    static const char magic[4] = { 'P', 'P', 'U', '1' };
    static const uint32_t sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
    fwrite(magic, 1, sizeof(magic), file);
    fwrite(sizes, sizeof(uint32_t), 5, file);
    fwrite(gIoMem, 1, 0x400u, file);
    fwrite(gVram, 1, 0x18000u, file);
    fwrite(gBgPltt, 1, 0x200u, file);
    fwrite(gObjPltt, 1, 0x200u, file);
    fwrite(gOamMem, 1, 0x400u, file);
    fclose(file);
}
#endif

static bool WriteBlob(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    const bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0)
        return false;
    return ok;
}

static bool WritePalettes(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fwrite(gBgPltt, 1, sizeof(gBgPltt), file) == sizeof(gBgPltt);
    ok = fwrite(gObjPltt, 1, sizeof(gObjPltt), file) == sizeof(gObjPltt) && ok;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static void MakeTimestamp(char* stamp, size_t stampSize) {
    time_t now = time(NULL);
    struct tm* tmNow = now > 0 ? localtime(&now) : NULL;
    if (tmNow) {
        strftime(stamp, stampSize, "%Y%m%d-%H%M%S", tmNow);
    } else {
        snprintf(stamp, stampSize, "unknown-time");
    }
}

static char sLastDumpDirectory[128];

const char* Port_PPU_3DS_LastDumpDirectory(void) {
    return sLastDumpDirectory;
}

static bool CreateDumpDirectory(char* out, size_t outSize) {
    if (!out || outSize == 0)
        return false;
    if (mkdir("dumps", 0777) != 0 && errno != EEXIST)
        return false;

    char stamp[32];
    MakeTimestamp(stamp, sizeof(stamp));
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (attempt == 0) {
            snprintf(out, outSize, "dumps/dump-%s", stamp);
        } else {
            snprintf(out, outSize, "dumps/dump-%s-%02d", stamp, attempt);
        }
        if (mkdir(out, 0777) == 0) {
            snprintf(sLastDumpDirectory, sizeof(sLastDumpDirectory), "%s", out);
            return true;
        }
        if (errno != EEXIST)
            break;
    }
    out[0] = 0;
    return false;
}

static double TicksToMilliseconds(uint64_t ticks) {
    return (double)ticks * 1000.0 / (double)Platform3DS_TicksPerSecond();
}

void Port_PPU_3DS_WriteQuickDump(void) {
    if (!sInitialized)
        return;
    /* Capture what is on screen now, before the parity path re-renders it. */
    if (sGpuPpuInitialized) PortPpuGpu3DS_RequestFrameCapture();
    if (sGpuPpuInitialized) PortPpuGpu3DS_RequestParityCheck();
    /* This synchronous SD capture and its confirmation overlay intentionally
     * pause gameplay. Mark it before any fallible I/O so the next cadence
     * boundary excludes the whole operation, including an early failure. */
    Platform3DS_MarkFrameDiscontinuity(OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    Port_Audio_3DSSetPaused(true);
    char dir[128];
    if (!CreateDumpDirectory(dir, sizeof(dir))) {
        Port_Audio_3DSSetPaused(false);
        return;
    }

    char topPath[192];
    char bottomPath[192];
    char topRawPath[192];
    char bottomRawPath[192];
    snprintf(topPath, sizeof(topPath), "%s/top-screen.bmp", dir);
    snprintf(bottomPath, sizeof(bottomPath), "%s/bottom-screen.bmp", dir);
    snprintf(topRawPath, sizeof(topRawPath), "%s/top-screen.raw", dir);
    snprintf(bottomRawPath, sizeof(bottomRawPath), "%s/bottom-screen.raw", dir);
    Platform3DSCaptureStats captureStats;
    memset(&captureStats, 0, sizeof(captureStats));
    const bool screensOk =
        Platform3DS_SaveDisplayedScreensDetailed(topPath, bottomPath, topRawPath, bottomRawPath, &captureStats);
    char path[192];
    snprintf(path, sizeof(path), "%s/ewram.bin", dir);
    WriteBlob(path, gEwram, sizeof(gEwram));
    snprintf(path, sizeof(path), "%s/iwram.bin", dir);
    WriteBlob(path, gIwram, sizeof(gIwram));
    snprintf(path, sizeof(path), "%s/vram.bin", dir);
    WriteBlob(path, gVram, sizeof(gVram));
    snprintf(path, sizeof(path), "%s/io-registers.bin", dir);
    WriteBlob(path, gIoMem, sizeof(gIoMem));
    snprintf(path, sizeof(path), "%s/palettes.bin", dir);
    WritePalettes(path);
    snprintf(path, sizeof(path), "%s/oam.bin", dir);
    WriteBlob(path, gOamMem, sizeof(gOamMem));
    /* The per-line register snapshot the GPU builder consumes. Without it a
     * host replay cannot reproduce an HDMA frame, which is exactly the case
     * whose cost matters. On a frame without HDMA only row 0 is populated, so
     * replicate it: the replay must see what the builder saw. */
    if (sGpuIoUniform) {
        for (unsigned line = 1; line < MODE1_GBA_HEIGHT; ++line) {
            memcpy(sGpuIoPerLine[line], sGpuIoPerLine[0],
                   sizeof(sGpuIoPerLine[0]));
            sGpuDispcntPerLine[line] = sGpuDispcntPerLine[0];
        }
    }
    snprintf(path, sizeof(path), "%s/io-per-line.bin", dir);
    WriteBlob(path, sGpuIoPerLine, sizeof(sGpuIoPerLine));
    snprintf(path, sizeof(path), "%s/dispcnt-per-line.bin", dir);
    WriteBlob(path, sGpuDispcntPerLine, sizeof(sGpuDispcntPerLine));
    snprintf(path, sizeof(path), "%s/affine-ref.bin", dir);
    {
        FILE* file = fopen(path, "wb");
        if (file) {
            fwrite(sGpuAffRefX, sizeof(sGpuAffRefX[0]), MODE1_GBA_HEIGHT, file);
            fwrite(sGpuAffRefY, sizeof(sGpuAffRefY[0]), MODE1_GBA_HEIGHT, file);
            fclose(file);
        }
    }
    snprintf(path, sizeof(path), "%s/main-state.bin", dir);
    WriteBlob(path, &gMain, sizeof(gMain));
    snprintf(path, sizeof(path), "%s/room-controls.bin", dir);
    WriteBlob(path, &gRoomControls, sizeof(gRoomControls));
    snprintf(path, sizeof(path), "%s/map-bottom-layer.bin", dir);
    WriteBlob(path, &gMapBottom, sizeof(gMapBottom));
    snprintf(path, sizeof(path), "%s/map-top-layer.bin", dir);
    WriteBlob(path, &gMapTop, sizeof(gMapTop));
    snprintf(path, sizeof(path), "%s/map-bottom-special.bin", dir);
    WriteBlob(path, gMapDataBottomSpecial, sizeof(gMapDataBottomSpecial));
    snprintf(path, sizeof(path), "%s/map-top-special.bin", dir);
    WriteBlob(path, gMapDataTopSpecial, sizeof(gMapDataTopSpecial));
    snprintf(path, sizeof(path), "%s/bg0-buffer.bin", dir);
    WriteBlob(path, gBG0Buffer, sizeof(gBG0Buffer));
    snprintf(path, sizeof(path), "%s/bg1-buffer.bin", dir);
    WriteBlob(path, gBG1Buffer, sizeof(gBG1Buffer));
    snprintf(path, sizeof(path), "%s/bg2-buffer.bin", dir);
    WriteBlob(path, gBG2Buffer, sizeof(gBG2Buffer));
    snprintf(path, sizeof(path), "%s/bg3-buffer.bin", dir);
    WriteBlob(path, gBG3Buffer, sizeof(gBG3Buffer));

    snprintf(path, sizeof(path), "%s/info.txt", dir);
    FILE* info = fopen(path, "wb");
    if (info) {
        const double sampleCount = sPerfSamples ? (double)sPerfSamples : 1.0;
        const double bottomSampleCount = sPerfBottomSamples ? (double)sPerfBottomSamples : 1.0;
        const double intervalSampleCount = sPerfIntervalSamples ? (double)sPerfIntervalSamples : 1.0;
        const double measuredFps =
            sPerfIntervalTicks != 0 && sPerfIntervalSamples != 0
                ? (double)Platform3DS_TicksPerSecond() *
                      (double)sPerfIntervalSamples / (double)sPerfIntervalTicks
                : 0.0;
        Platform3DSRuntimeStats runtimeStats;
        PlatformGpu3DSStats gpuStats;
        PortPpuGpu3DSStats ppuGpuStats;
        BottomFrameState3DSStats bottomFrameStats;
        PortAudio3DSStats audioStats;
        PortSaveStats saveStats;
        VirtuaPPUMode13DSStats workerStats;
        Platform3DS_GetRuntimeStats(&runtimeStats);
        PlatformGpu3DS_GetStats(&gpuStats);
        PortPpuGpu3DS_GetStats(&ppuGpuStats);
        Port_SecondScreen_3DS_GetFrameStats(&bottomFrameStats);
        Port_Audio_3DSGetStats(&audioStats);
        Port_Save_GetStats(&saveStats);
        virtuappu_mode1_get_3ds_stats(&workerStats);
        const uint64_t engineSamples = runtimeStats.logicFrames > 1 ? runtimeStats.logicFrames - 1u : 1u;
        const uint64_t vblankSamples = runtimeStats.presentedFrames ? runtimeStats.presentedFrames : 1u;
        const double logicElapsedSeconds =
            (double)runtimeStats.logicElapsedTicks / (double)Platform3DS_TicksPerSecond();
        const double measuredLogicRate = logicElapsedSeconds > 0.0 && runtimeStats.logicCadenceIntervals != 0
                                             ? (double)runtimeStats.logicCadenceIntervals / logicElapsedSeconds
                                             : 0.0;
        const uint64_t audioSamples = audioStats.buffersRendered ? audioStats.buffersRendered : 1u;
        const uint16_t dumpDispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
        const uint8_t dumpMode = dumpDispcnt & 7u;
        const uint32_t avoidedFlushBytes =
            gpuStats.linearHeapBytes > gpuStats.c2dFlushBytes ? gpuStats.linearHeapBytes - gpuStats.c2dFlushBytes : 0;
        const double loadIntervalTicks =
            sPerfIntervalLastTicks ? (double)sPerfIntervalLastTicks : (double)Platform3DS_TicksPerSecond() / 60.0;
        fprintf(info, "The Minish Cap 3DS v" TMC_PORT_VERSION " quick dump\n");
        fprintf(info, "\n[System]\n");
        fprintf(info, "Model: %s\n", Platform3DS_IsNew3DS() ? "New Nintendo 3DS" : "Old Nintendo 3DS");
        fprintf(info, "CPU profile requested: %s\n", runtimeStats.speedupRequested ? "804 MHz + L2" : "268 MHz");
        fprintf(info, "Runtime performance profile: %s\n",
                runtimeStats.adaptiveFrameskipEnabled ? "Old 3DS (59.7275 Hz GBA logic target + adaptive presentation skip)"
                                                       : "New 3DS (full presentation path)");
        fprintf(info, "Post-boot stdio target: %s\n",
                runtimeStats.gameplayDisplayActive ? "bottom framebuffer detached; stderr uses SVC"
                                                   : "visible boot console");
        fprintf(info, "Kernel version: 0x%08lX\n", (unsigned long)runtimeStats.kernelVersion);
        fprintf(info, "FIRM version: 0x%08lX\n", (unsigned long)runtimeStats.firmVersion);
        fprintf(info, "System core version: %lu\n", (unsigned long)runtimeStats.systemCoreVersion);
        fprintf(info, "Application memory type: %lu\n", (unsigned long)runtimeStats.applicationMemoryType);
        fprintf(info, "Main thread priority: %ld\n", (long)runtimeStats.mainThreadPriority);
        fprintf(info, "Core 1 time limit: %u%%\n", Platform3DS_Core1TimeLimit());
        fprintf(info, "PPU workers: %lu (core 1: %s, New 3DS core 2: %s)\n", (unsigned long)workerStats.workerCount,
                Platform3DS_CanUseCore1() ? "enabled" : "unavailable",
                Platform3DS_IsNew3DS() ? "enabled" : "unavailable");
        fprintf(info, "Application memory free: %lu bytes\n", (unsigned long)runtimeStats.applicationMemoryFree);
        fprintf(info, "System memory free: %lu bytes\n", (unsigned long)runtimeStats.systemMemoryFree);
        fprintf(info, "Base memory free: %lu bytes\n", (unsigned long)runtimeStats.baseMemoryFree);
        fprintf(info, "Linear memory free: %lu bytes\n", (unsigned long)runtimeStats.linearMemoryFree);
        fprintf(info, "Stack pointer / region: 0x%08lX / 0x%08lX-0x%08lX\n",
                (unsigned long)runtimeStats.currentStackPointer, (unsigned long)runtimeStats.stackRegionBase,
                (unsigned long)runtimeStats.stackRegionEnd);
        fprintf(info, "ROM loaded: %s, %lu bytes\n", gRomData ? "yes" : "no", (unsigned long)gRomSize);
        fprintf(info, "ROM region: %s\n",
                gRomRegion == ROM_REGION_EU    ? "EU"
                : gRomRegion == ROM_REGION_JP ? "JP"
                : gRomRegion == ROM_REGION_USA ? "USA"
                                              : "unknown");
        fprintf(info, "ROM buffer: 0x%08lX\n", (unsigned long)(uintptr_t)gRomData);

        fprintf(info, "\n[Lifecycle and input]\n");
        fprintf(info, "Application running: %s\n", Platform3DS_IsRunning() ? "yes" : "no");
        fprintf(info, "APT close requested: %s\n", runtimeStats.aptCloseRequested ? "yes" : "no");
        fprintf(info, "APT lifecycle checks: %llu\n", (unsigned long long)runtimeStats.aptChecks);
        fprintf(info, "Keys held/down: 0x%08lX / 0x%08lX\n", (unsigned long)runtimeStats.keyMaskHeld,
                (unsigned long)runtimeStats.keyMaskDown);
        fprintf(info, "Circle Pad: %d, %d\n", runtimeStats.circleX, runtimeStats.circleY);
        fprintf(info, "C-stick: %d, %d\n", runtimeStats.cstickX, runtimeStats.cstickY);
        fprintf(info, "Turbo: %s, multiplier x%u\n", runtimeStats.turboHeld ? "held" : "released",
                Platform3DS_TurboMultiplier());

        fprintf(info, "\n[Cadence]\n");
        fprintf(info, "Engine logic frames: %llu\n", (unsigned long long)runtimeStats.logicFrames);
        fprintf(info, "Presented frames: %llu\n", (unsigned long long)runtimeStats.presentedFrames);
        fprintf(info, "Turbo logic frames: %llu\n", (unsigned long long)runtimeStats.turboLogicFrames);
        fprintf(info, "Turbo-skipped presentations: %llu\n",
                (unsigned long long)runtimeStats.turboSkippedPresentations);
        fprintf(info, "Old 3DS adaptive-skipped presentations: %llu (maximum consecutive: %lu)\n",
                (unsigned long long)runtimeStats.old3dsSkippedPresentations,
                (unsigned long)runtimeStats.old3dsMaxConsecutiveSkips);
        fprintf(info, "Old 3DS pacing sleep: %.3f ms; resyncs: %llu; current debt: %.3f ms\n",
                TicksToMilliseconds(runtimeStats.old3dsPacingSleepTicks),
                (unsigned long long)runtimeStats.old3dsPacingResyncs,
                (double)runtimeStats.old3dsPresentationDebtTicks * 1000.0 / (double)Platform3DS_TicksPerSecond());
        fprintf(info, "Pacing discontinuities: APT %llu, quick dump %llu; debt clamps: %llu\n",
                (unsigned long long)runtimeStats.old3dsAptDiscontinuities,
                (unsigned long long)runtimeStats.old3dsDumpDiscontinuities,
                (unsigned long long)runtimeStats.old3dsDebtClampEvents);
        fprintf(info, "Top screenshot: 400x240 displayed framebuffer BMP\n");
        fprintf(info, "Bottom screenshot: 320x240 displayed framebuffer BMP\n");
        fprintf(info, "Raw physical framebuffers: top-screen.raw, bottom-screen.raw\n");
        fprintf(info, "Displayed framebuffer capture: %s\n", screensOk ? "OK" : "FAILED");
        fprintf(info, "Top capture format/stride/address: %lu / %lu / 0x%08lX\n", (unsigned long)captureStats.topFormat,
                (unsigned long)captureStats.topStride, (unsigned long)captureStats.topAddress);
        fprintf(info, "Bottom capture format/stride/address: %lu / %lu / 0x%08lX\n",
                (unsigned long)captureStats.bottomFormat, (unsigned long)captureStats.bottomStride,
                (unsigned long)captureStats.bottomAddress);
        fprintf(info, "Measured cadence: %.2f FPS\n", measuredFps);
        fprintf(info, "Measured engine logic cadence: %.2f ticks/s\n", measuredLogicRate);
        fprintf(info, "Frame interval: last %.3f ms, average %.3f ms, minimum %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfIntervalLastTicks),
                TicksToMilliseconds(sPerfIntervalTicks) / intervalSampleCount,
                TicksToMilliseconds(sPerfIntervalMinTicks == UINT64_MAX ? 0 : sPerfIntervalMinTicks),
                TicksToMilliseconds(sPerfIntervalMaxTicks));
        fprintf(info, "Frames over 1 / 2 GBA periods (16.743 / 33.485 ms): %llu / %llu\n",
                (unsigned long long)sPerfFramesOver16ms, (unsigned long long)sPerfFramesOver33ms);
        fprintf(info, "Engine work between frame boundaries: last %.3f ms, average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(runtimeStats.engineWorkLastTicks),
                TicksToMilliseconds(runtimeStats.engineWorkTicks) / (double)engineSamples,
                TicksToMilliseconds(runtimeStats.engineWorkMaxTicks));
        fprintf(info, "VBlank wait: last %.3f ms, average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(runtimeStats.vblankWaitLastTicks),
                TicksToMilliseconds(runtimeStats.vblankWaitTicks) / (double)vblankSamples,
                TicksToMilliseconds(runtimeStats.vblankWaitMaxTicks));
        {
            extern uint64_t Platform3DS_VblankWaitSamples(void);
            extern uint64_t Platform3DS_VblankWaitOverOnePeriod(void);
            extern uint64_t Platform3DS_VblankWaitOverTwoPeriods(void);
            const uint64_t waits = Platform3DS_VblankWaitSamples();
            const uint64_t over1 = Platform3DS_VblankWaitOverOnePeriod();
            const uint64_t over2 = Platform3DS_VblankWaitOverTwoPeriods();
            fprintf(info,
                    "  waits exceeding 1 / 2 GBA periods: %llu (%.2f%%) / %llu of %llu\n",
                    (unsigned long long)over1,
                    waits ? 100.0 * (double)over1 / (double)waits : 0.0,
                    (unsigned long long)over2, (unsigned long long)waits);
        }

        fprintf(info, "\n[Renderer]\n");
        fprintf(info, "PPU render: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfRenderTicks) / sampleCount, TicksToMilliseconds(sPerfRenderMaxTicks));
        fprintf(info,
                "Top presentation split: PPU submit %.3f ms, presenter blit %.3f ms per frame\n",
                TicksToMilliseconds(sTopDrawTicks) /
                        (double)(sFrameNumber ? sFrameNumber : 1),
                TicksToMilliseconds(sTopBlitTicks) /
                        (double)(sFrameNumber ? sFrameNumber : 1));
        fprintf(info, "Top presentation CPU work: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfTopTicks) / sampleCount, TicksToMilliseconds(sPerfTopMaxTicks));
        fprintf(info,
                "  presentation spans over 4/16/50 ms: %llu / %llu / %llu of %llu\n",
                (unsigned long long)sPerfTopOver4ms, (unsigned long long)sPerfTopOver16ms,
                (unsigned long long)sPerfTopOver50ms, (unsigned long long)sPerfSamples);
        fprintf(info,
                "  PPU render spans over 4/16/50 ms: %llu / %llu / %llu of %llu\n",
                (unsigned long long)sPerfRenderOver4ms, (unsigned long long)sPerfRenderOver16ms,
                (unsigned long long)sPerfRenderOver50ms, (unsigned long long)sPerfSamples);
        {
            const double n = (double)(sFrameBeginSamples ? sFrameBeginSamples : 1);
            fprintf(info,
                    "    C3D_FrameBegin wait: %.3f avg / %.3f max ms, over 4/16 ms: %llu / %llu\n",
                    TicksToMilliseconds(sFrameBeginTicks) / n,
                    TicksToMilliseconds(sFrameBeginMaxTicks),
                    (unsigned long long)sFrameBeginOver4ms,
                    (unsigned long long)sFrameBeginOver16ms);
            fprintf(info,
                    "    PPU preflight:       %.3f avg / %.3f max ms, over 4/16 ms: %llu / %llu of %llu\n",
                    TicksToMilliseconds(sPreflightTicks) / n,
                    TicksToMilliseconds(sPreflightMaxTicks),
                    (unsigned long long)sPreflightOver4ms,
                    (unsigned long long)sPreflightOver16ms,
                    (unsigned long long)sFrameBeginSamples);
        }
        fprintf(info, "Bottom-screen paint worker: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfBottomTicks) / bottomSampleCount, TicksToMilliseconds(sPerfBottomMaxTicks));
        fprintf(info, "Main-thread render/presentation CPU work: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfTotalTicks) / sampleCount, TicksToMilliseconds(sPerfTotalMaxTicks));
        fprintf(info, "PPU core 0: last %.3f ms, maximum %.3f ms, last lines %lu\n",
                TicksToMilliseconds(workerStats.mainLastTicks), TicksToMilliseconds(workerStats.mainMaxTicks),
                (unsigned long)workerStats.mainLastLines);
        fprintf(info, "PPU core 0 measured load in last frame interval: %.1f%%\n",
                (double)workerStats.mainLastTicks * 100.0 / loadIntervalTicks);
        for (int i = 0; i < 2; ++i) {
            fprintf(info, "PPU core %d: last %.3f ms, maximum %.3f ms, last lines %lu\n", i + 1,
                    TicksToMilliseconds(workerStats.workerLastTicks[i]),
                    TicksToMilliseconds(workerStats.workerMaxTicks[i]), (unsigned long)workerStats.workerLastLines[i]);
            fprintf(info, "PPU core %d measured load in last frame interval: %.1f%%\n", i + 1,
                    (double)workerStats.workerLastTicks[i] * 100.0 / loadIntervalTicks);
        }
        fprintf(info, "Old 3DS PPU paths last frame (direct/special-alpha/compact/fallback): %lu/%lu/%lu/%lu lines\n",
                (unsigned long)workerStats.oldPathLastLines[MODE1_OLD_PATH_DIRECT],
                (unsigned long)workerStats.oldPathLastLines[MODE1_OLD_PATH_FIELD_ALPHA],
                (unsigned long)workerStats.oldPathLastLines[MODE1_OLD_PATH_COMPACT],
                (unsigned long)workerStats.oldPathLastLines[MODE1_OLD_PATH_FALLBACK]);
        fprintf(info,
                "Old 3DS PPU paths cumulative (direct/special-alpha/compact/fallback): %llu/%llu/%llu/%llu lines\n",
                (unsigned long long)workerStats.oldPathTotalLines[MODE1_OLD_PATH_DIRECT],
                (unsigned long long)workerStats.oldPathTotalLines[MODE1_OLD_PATH_FIELD_ALPHA],
                (unsigned long long)workerStats.oldPathTotalLines[MODE1_OLD_PATH_COMPACT],
                (unsigned long long)workerStats.oldPathTotalLines[MODE1_OLD_PATH_FALLBACK]);
        fprintf(info, "PICA200 PPU config gpu_renderer/gpu_frame_sync: %s/%s\n",
                Port_Config_GpuRenderer() ? "on" : "off",
                Port_Config_GpuFrameSync() ? "on" : "off");
        fprintf(info, "PICA200 PPU initialized/enabled/disabled: %s/%s/%s\n",
                ppuGpuStats.initialized ? "yes" : "no",
                ppuGpuStats.enabled ? "yes" : "no",
                ppuGpuStats.disabled ? "yes" : "no");
        fprintf(info, "PICA200 PPU attempted/rendered/fallback/disabled frames: %llu/%llu/%llu/%llu\n",
                (unsigned long long)ppuGpuStats.attemptedFrames,
                (unsigned long long)ppuGpuStats.renderedFrames,
                (unsigned long long)ppuGpuStats.fallbackFrames,
                (unsigned long long)ppuGpuStats.disabledFrames);
        fprintf(info, "PICA200 PPU atlas flush calls: %llu\n",
                (unsigned long long)ppuGpuStats.atlasFlushCalls);
        fprintf(info, "PICA200 PPU tile hits/decodes/atlas flush bytes: %llu/%llu/%llu\n",
                (unsigned long long)ppuGpuStats.tileHits,
                (unsigned long long)ppuGpuStats.tileDecodes,
                (unsigned long long)ppuGpuStats.atlasFlushBytes);
        fprintf(info, "PICA200 PPU vertices/batches: %llu/%llu\n",
                (unsigned long long)ppuGpuStats.vertices,
                (unsigned long long)ppuGpuStats.batches);
        fprintf(info, "PICA200 PPU parity checks/failures/differing/structural pixels: %llu/%llu/%llu/%llu\n",
                (unsigned long long)ppuGpuStats.parityChecks,
                (unsigned long long)ppuGpuStats.parityFailures,
                (unsigned long long)ppuGpuStats.differingPixels,
                (unsigned long long)ppuGpuStats.structuralPixels);
        fprintf(info,
                "PICA200 PPU build failures (args/unsupported/capacity/atlas/geometry): "
                "%llu/%llu/%llu/%llu/%llu\n",
                (unsigned long long)ppuGpuStats.buildFailures[PPU_GPU3DS_BUILD_ARGUMENTS],
                (unsigned long long)ppuGpuStats.buildFailures[PPU_GPU3DS_BUILD_UNSUPPORTED],
                (unsigned long long)ppuGpuStats.buildFailures[PPU_GPU3DS_BUILD_CAPACITY],
                (unsigned long long)ppuGpuStats.buildFailures[PPU_GPU3DS_BUILD_ATLAS_FULL],
                (unsigned long long)ppuGpuStats.buildFailures[PPU_GPU3DS_BUILD_GEOMETRY]);
        fprintf(info,
                "PICA200 PPU peak bands/vertices/batches/wanted vertices: %lu/%lu/%lu/%lu\n",
                (unsigned long)ppuGpuStats.maxBands,
                (unsigned long)ppuGpuStats.maxVertices,
                (unsigned long)ppuGpuStats.maxBatches,
                (unsigned long)ppuGpuStats.maxRequiredVertices);
        fprintf(info, "PICA200 PPU parity first difference: %lu,%lu\n",
                (unsigned long)ppuGpuStats.firstDiffX,
                (unsigned long)ppuGpuStats.firstDiffY);
        fprintf(info,
                "PICA200 PPU map layers used: %llu (rebuilt %llu); rejects "
                "(affine/control/screen-space/off/too-large/coverage): "
                "%llu/%llu/%llu/%llu/%llu/%llu; largest window %lu quads\n",
                (unsigned long long)ppuGpuStats.mapLayers,
                (unsigned long long)ppuGpuStats.mapRebuilds,
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_AFFINE],
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_CONTROL],
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_SCREEN_SPACE],
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_DISABLED],
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_TOO_LARGE],
                (unsigned long long)ppuGpuStats.mapRejects[PPU_GPU3DS_MAP_REJECT_COVERAGE],
                (unsigned long)ppuGpuStats.mapLargestQuads);
        fprintf(info,
                "PICA200 PPU map rebuilds (new/tilemap/palette/tiles/window): "
                "%llu/%llu/%llu/%llu/%llu; palette refreshes %llu\n",
                (unsigned long long)ppuGpuStats.mapRebuildReason[PPU_GPU3DS_MAP_REBUILD_NEW],
                (unsigned long long)ppuGpuStats.mapRebuildReason[PPU_GPU3DS_MAP_REBUILD_TILEMAP],
                (unsigned long long)ppuGpuStats.mapRebuildReason[PPU_GPU3DS_MAP_REBUILD_PALETTE],
                (unsigned long long)ppuGpuStats.mapRebuildReason[PPU_GPU3DS_MAP_REBUILD_TILES],
                (unsigned long long)ppuGpuStats.mapRebuildReason[PPU_GPU3DS_MAP_REBUILD_WINDOW],
                (unsigned long long)ppuGpuStats.mapRefreshes);
        fprintf(info,
                "PICA200 PPU captured frame: %.3f ms build, %lu bands, %lu vertices, map mask 0x%02x\n",
                TicksToMilliseconds(ppuGpuStats.lastBuildTicks),
                (unsigned long)ppuGpuStats.lastBands,
                (unsigned long)ppuGpuStats.lastVertices,
                (unsigned)ppuGpuStats.lastMapLayerMask);
#ifdef PPU_GPU3DS_PROFILE
        {
            static const char* phaseNames[PPU_GPU3DS_PHASE_COUNT] = {
                "bands", "merge", "maps", "mapsig", "mapretain", "scene",
                "objwin", "regions", "bg", "obj", "decode"
            };
            const unsigned long long attempts =
                    ppuGpuStats.attemptedFrames ? ppuGpuStats.attemptedFrames : 1;
            fprintf(info, "PICA200 PPU build phases (ms/frame):");
            for (unsigned phase = 0; phase < PPU_GPU3DS_PHASE_COUNT; ++phase)
                fprintf(info, " %s %.4f", phaseNames[phase],
                        TicksToMilliseconds((uint64_t)gPpuGpu3DSPhase[phase]) /
                                (double)attempts);
            fprintf(info, "\n");
        }
#endif
        {
            /* How much of the audio budget a DSP offload could actually remove:
             * m4aSoundMain is the sequencer plus the software mix. */
            extern uint64_t Port_M4A_Backend_MixTicks(void);
            const double mixMs = TicksToMilliseconds(Port_M4A_Backend_MixTicks());
            const unsigned long long buffers = audioStats.buffersRendered
                                                       ? audioStats.buffersRendered
                                                       : 1ull;
            fprintf(info,
                    "M4A software mix: %.3f ms per rendered buffer (%.1f ms total)\n",
                    mixMs / (double)buffers, mixMs);
        }
        {
            extern unsigned long long NdspPsg_VoicesOffloaded(void);
            extern unsigned long long NdspPsg_VoicesRateDeclined(void);
            extern int NdspPsg_CurrentReverbLevel(void);
            extern unsigned long long NdspPcm_VoicesOffloaded(void);
            extern unsigned long long NdspPcm_VoicesDeclined(void);
            fprintf(info,
                    "CGB voices offloaded to DSP: %llu (rate-declined %llu, "
                    "reverb level %d)\n",
                    NdspPsg_VoicesOffloaded(), NdspPsg_VoicesRateDeclined(),
                    NdspPsg_CurrentReverbLevel());
            extern unsigned long long NdspPcm_VoicesUnsupported(void);
            fprintf(info,
                    "PCM voices offloaded to DSP: %llu (no-channel %llu, "
                    "unsupported type %llu)\n",
                    NdspPcm_VoicesOffloaded(), NdspPcm_VoicesDeclined(),
                    NdspPcm_VoicesUnsupported());
        }
        fprintf(info, "Cache clean path: %s\n", Platform3DS_CacheCleanPath());
        fprintf(info,
                "Lifecycle/audio pump: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(Platform3DS_PumpTicks()) /
                        (double)(sFrameNumber ? sFrameNumber : 1),
                TicksToMilliseconds(Platform3DS_PumpMaxTicks()));
        {
            extern uint64_t Platform3DS_AptTicks(void);
            extern uint64_t Platform3DS_AptMaxTicks(void);
            extern uint64_t Platform3DS_AudioPumpTicks(void);
            extern uint64_t Platform3DS_AudioPumpMaxTicks(void);
            const double frames = (double)(sFrameNumber ? sFrameNumber : 1);
            fprintf(info,
                    "  pump split: aptMainLoop %.3f/%.3f ms, audio pump %.3f/%.3f ms (avg/max)\n",
                    TicksToMilliseconds(Platform3DS_AptTicks()) / frames,
                    TicksToMilliseconds(Platform3DS_AptMaxTicks()),
                    TicksToMilliseconds(Platform3DS_AudioPumpTicks()) / frames,
                    TicksToMilliseconds(Platform3DS_AudioPumpMaxTicks()));
        }
        fprintf(info,
                "Promote/input after wait: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(Platform3DS_PostWaitTicks()) /
                        (double)(sFrameNumber ? sFrameNumber : 1),
                TicksToMilliseconds(Platform3DS_PostWaitMaxTicks()));
        fprintf(info, "PICA200 PPU build/flush CPU time: %.3f/%.3f ms\n",
                TicksToMilliseconds(ppuGpuStats.buildTicks),
                TicksToMilliseconds(ppuGpuStats.flushTicks));
        fprintf(info, "PICA200 PPU preflight/draw CPU time: %.3f/%.3f ms\n",
                TicksToMilliseconds(ppuGpuStats.preflightTicks),
                TicksToMilliseconds(ppuGpuStats.drawTicks));
        fprintf(info, "GPU frames / begin failures: %llu / %llu\n", (unsigned long long)gpuStats.frames,
                (unsigned long long)gpuStats.frameBeginFailures);
        fprintf(info, "GPU top/bottom transfers: %llu / %llu\n", (unsigned long long)gpuStats.topTransfers,
                (unsigned long long)gpuStats.bottomTransfers);
        fprintf(info,
                "  bottom transfer queue-append: %.3f ms average, %.3f ms maximum over %llu (NOT the DMA)\n",
                gpuStats.bottomTransfers
                        ? TicksToMilliseconds(gpuStats.bottomTransferTicks) /
                                  (double)gpuStats.bottomTransfers
                        : 0.0,
                TicksToMilliseconds(gpuStats.bottomTransferMaxTicks),
                (unsigned long long)gpuStats.bottomTransfers);
        fprintf(info, "GPU upload pitch/bytes top: %lu / %lu; bottom: %lu / %lu\n",
                (unsigned long)gpuStats.topUploadPitch, (unsigned long)gpuStats.topUploadBytes,
                (unsigned long)gpuStats.bottomUploadPitch, (unsigned long)gpuStats.bottomUploadBytes);
        fprintf(info, "Bottom target draws / unchanged Old 3DS reuses: %llu / %llu\n",
                (unsigned long long)gpuStats.bottomTargetDraws, (unsigned long long)gpuStats.bottomTargetReuseSkips);
        fprintf(info, "Citro3D drawing/processing time: %.3f / %.3f ms\n", gpuStats.drawingTime,
                gpuStats.processingTime);
        fprintf(info, "Linear heap full flush: disabled (%lu bytes avoided per frame)\n",
                (unsigned long)avoidedFlushBytes);
        fprintf(info, "Bounded C2D cache flush: %lu bytes per frame, %llu bytes total\n",
                (unsigned long)gpuStats.c2dFlushBytes, (unsigned long long)gpuStats.boundedFlushBytes);
        fprintf(info, "C2D flush address: 0x%08lX; upload buffers: 0x%08lX / 0x%08lX / 0x%08lX\n",
                (unsigned long)gpuStats.c2dFlushAddress, (unsigned long)gpuStats.topUploadAddress,
                (unsigned long)gpuStats.bottomUploadAddress[0], (unsigned long)gpuStats.bottomUploadAddress[1]);
        {
            /* Measured, not inferred: which phase of a paint actually costs
             * the 10-15 ms. */
            extern void Port_SecondScreen_PhaseTicks(unsigned long long*, unsigned long long*,
                                                     int, unsigned long long*);
            unsigned long long totals[4] = { 0, 0, 0, 0 };
            unsigned long long maxima[4] = { 0, 0, 0, 0 };
            unsigned long long paints = 0;
            Port_SecondScreen_PhaseTicks(totals, maxima, 4, &paints);
            static const char* kNames[4] = { "backdrop", "panel", "sidebar", "tabbar" };
            fprintf(info, "Bottom paint phases over %llu paints (avg / max ms):\n",
                    paints);
            for (int i = 0; i < 4; ++i) {
                fprintf(info, "  %-9s %.3f / %.3f\n", kNames[i],
                        paints ? TicksToMilliseconds(totals[i]) / (double)paints : 0.0,
                        TicksToMilliseconds(maxima[i]));
            }
        }
        fprintf(info, "Bottom paint scheduling: %llu paints requested; %llu periodic checks; %llu static skips\n",
                (unsigned long long)sBottomPaintRequests, (unsigned long long)sBottomPeriodicChecks,
                (unsigned long long)sBottomPeriodicSkips);
        fprintf(info, "Bottom generations requested/painted/submitted/visible: %lu/%lu/%lu/%lu\n",
                (unsigned long)bottomFrameStats.requested, (unsigned long)bottomFrameStats.painted,
                (unsigned long)bottomFrameStats.submitted, (unsigned long)bottomFrameStats.visible);
        fprintf(info, "Bottom pipeline events requested/painted/submitted/visible: %lu/%lu/%lu/%lu\n",
                (unsigned long)bottomFrameStats.requestCount, (unsigned long)bottomFrameStats.paintCount,
                (unsigned long)bottomFrameStats.submissionCount, (unsigned long)bottomFrameStats.visibilityCount);
        fprintf(info, "Bottom touch rejects (hidden generation/task mismatch): %lu/%lu\n",
                (unsigned long)bottomFrameStats.touchGenerationRejects,
                (unsigned long)bottomFrameStats.touchModeRejects);
        fprintf(info,
                "Bottom animation cadence: every %u presentations; live developer overlay every 30\n",
                Platform3DS_IsNew3DS() ? 3u : 6u);

        fprintf(info, "\n[Audio]\n");
        fprintf(info, "Initialized/playing: %s / %s\n", audioStats.initialized ? "yes" : "no",
                audioStats.channelPlaying ? "yes" : "no");
        fprintf(info, "Paused for quick dump: %s\n", audioStats.paused ? "yes" : "no");
        fprintf(info, "Sample rate: %lu Hz; buffer geometry: %lu x %lu frames\n", (unsigned long)audioStats.sampleRate,
                (unsigned long)audioStats.bufferCount, (unsigned long)audioStats.bufferFrames);
        fprintf(info, "Worker running/core/priority: %s / %ld / %ld\n", audioStats.audioThreadRunning ? "yes" : "no",
                (long)audioStats.threadCore, (long)audioStats.threadPriority);
        fprintf(info, "Pumps / rendered buffers / underrun observations: %llu / %llu / %llu\n",
                (unsigned long long)audioStats.pumps, (unsigned long long)audioStats.buffersRendered,
                (unsigned long long)audioStats.underrunObservations);
        fprintf(info, "Worker wakes / requeues / NDSP callback signals / queue recoveries: %llu / %llu / %llu / %llu\n",
                (unsigned long long)audioStats.workerWakeups, (unsigned long long)audioStats.workerRequeues,
                (unsigned long long)audioStats.callbackSignals, (unsigned long long)audioStats.queueRecoveries);
        fprintf(info, "Audio render: last %.3f ms, average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(audioStats.renderLastTicks),
                TicksToMilliseconds(audioStats.renderTicks) / (double)audioSamples,
                TicksToMilliseconds(audioStats.renderMaxTicks));
        fprintf(info, "Audio block deadline misses / multi-buffer recovery wakes: %llu / %llu\n",
                (unsigned long long)audioStats.renderDeadlineMisses, (unsigned long long)audioStats.multiBufferWakeups);
        fprintf(info, "Wave buffers free/queued/playing/done: %lu/%lu/%lu/%lu\n", (unsigned long)audioStats.freeBuffers,
                (unsigned long)audioStats.queuedBuffers, (unsigned long)audioStats.playingBuffers,
                (unsigned long)audioStats.doneBuffers);
        fprintf(info, "Maximum buffers per worker wake / maximum DONE backlog: %lu / %lu\n",
                (unsigned long)audioStats.maxBuffersPerWake, (unsigned long)audioStats.maxDoneBuffersObserved);
        fprintf(info, "Fallback renders / sample position: %llu / %lu\n",
                (unsigned long long)audioStats.fallbackRenders, (unsigned long)audioStats.samplePosition);
        fprintf(info, "NDSP frames / dropped frames: %lu / %lu\n", (unsigned long)audioStats.ndspFrames,
                (unsigned long)audioStats.ndspDroppedFrames);

        fprintf(info, "\n[Save storage]\n");
        fprintf(info, "Active path: %s\n", saveStats.activePath);
        fprintf(info, "Initialized/dirty/transaction depth: %s / %s / %ld\n", saveStats.initialized ? "yes" : "no",
                saveStats.dirty ? "yes" : "no", (long)saveStats.transactionDepth);
        fprintf(info, "Flush attempts / successes / failures: %llu / %llu / %llu\n",
                (unsigned long long)saveStats.flushAttempts, (unsigned long long)saveStats.flushSuccesses,
                (unsigned long long)saveStats.flushFailures);
        fprintf(info, "Interrupted recoveries / rollback restores / rollback failures: %llu / %llu / %llu\n",
                (unsigned long long)saveStats.interruptedRecoveries, (unsigned long long)saveStats.rollbackRestores,
                (unsigned long long)saveStats.rollbackFailures);
        fprintf(info, "Last persistence stage / errno: %s / %ld (%s)\n", Port_Save_StageName(saveStats.lastStage),
                (long)saveStats.lastErrno, saveStats.lastErrno ? strerror(saveStats.lastErrno) : "none");

        fprintf(info, "\n[Game and GBA PPU]\n");
        fprintf(info, "Task/state/substate/sleep: %u/%u/%u/%u\n", gMain.task, gMain.state, gMain.substate,
                gMain.sleepStatus);
        fprintf(info, "Subtask state: next/load=%u/%u, pause origin=%u, menu=%u/%u/%u\n", gUI.nextToLoad, gUI.state,
                gUI.pauseFadeIn, gMenu.menuType, gMenu.overlayType, gMenu.transitionTimer);
        fprintf(info, "Game ticks: %u; pause frames/count/interval: %u/%u/%u\n", gMain.ticks, gMain.pauseFrames,
                gMain.pauseCount, gMain.pauseInterval);
        fprintf(info, "DISPCNT: 0x%04X; display mode: %u\n", dumpDispcnt, dumpMode);
        fprintf(info, "VRAM/EWRAM/IWRAM bytes: %lu/%lu/%lu\n", (unsigned long)sizeof(gVram),
                (unsigned long)sizeof(gEwram), (unsigned long)sizeof(gIwram));
        fprintf(info, "Native pointer / MapLayer bytes: %lu/%lu\n", (unsigned long)sizeof(void*),
                (unsigned long)sizeof(MapLayer));
        fprintf(info,
                "MapLayer native offsets: map=0x%lX collision=0x%lX original=0x%lX "
                      "types=0x%lX indices=0x%lX subtiles=0x%lX act=0x%lX\n",
                (unsigned long)offsetof(MapLayer, mapData), (unsigned long)offsetof(MapLayer, collisionData),
                (unsigned long)offsetof(MapLayer, mapDataOriginal), (unsigned long)offsetof(MapLayer, tileTypes),
                (unsigned long)offsetof(MapLayer, tileIndices), (unsigned long)offsetof(MapLayer, subTiles),
                (unsigned long)offsetof(MapLayer, actTiles));
        fprintf(info, "Room area/id/origin/size/scroll: 0x%02X/0x%02X, %d,%d, %u,%u, %d,%d\n", gRoomControls.area,
                gRoomControls.room, gRoomControls.origin_x, gRoomControls.origin_y, gRoomControls.width,
                gRoomControls.height, gRoomControls.scroll_x, gRoomControls.scroll_y);
        fprintf(info, "Player position/z: %d,%d,%d; action/subaction: %u/%u; direction/animation: %u/%u\n",
                gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, gPlayerEntity.base.z.HALF.HI,
                gPlayerEntity.base.action, gPlayerEntity.base.subAction, gPlayerEntity.base.direction,
                gPlayerEntity.base.animationState);
        fprintf(info, "Player control/framestate/layer/draw/flags: %u/%u/%u/%u/0x%02X\n", gPlayerState.controlMode,
                gPlayerState.framestate, gPlayerEntity.base.collisionLayer, gPlayerEntity.base.spriteSettings.draw,
                gPlayerEntity.base.flags);
        fprintf(info, "Camera target/player pointers: 0x%08lX / 0x%08lX\n",
                (unsigned long)(uintptr_t)gRoomControls.camera_target, (unsigned long)(uintptr_t)&gPlayerEntity.base);
        fprintf(info, "Pending transition: out=%u destination=0x%02X/0x%02X position=%d,%d\n",
                gRoomTransition.transitioningOut, gRoomTransition.player_status.area_next,
                gRoomTransition.player_status.room_next, gRoomTransition.player_status.start_pos_x,
                gRoomTransition.player_status.start_pos_y);
        fprintf(info, "Map BG controls: bottom=0x%04X top=0x%04X\n",
                gMapBottom.bgSettings ? gMapBottom.bgSettings->control : 0,
                gMapTop.bgSettings ? gMapTop.bgSettings->control : 0);
        fprintf(info, "Top rendered width: %d pixels (capacity %d; widescreen %s)\n", sTopPresentWidth, MODE1_GBA_WIDTH,
                Port_Config_WidescreenEnabled() ? "enabled" : "disabled");

        fprintf(info, "\n[Files]\n");
        fprintf(info, "top-screen.bmp, bottom-screen.bmp, top-screen.raw, bottom-screen.raw\n");
        fprintf(info, "ewram.bin, iwram.bin, vram.bin, io-registers.bin, palettes.bin, oam.bin, main-state.bin\n");
        fprintf(info, "io-per-line.bin, dispcnt-per-line.bin, affine-ref.bin\n");
        fprintf(info, "room-controls.bin, map-bottom-layer.bin, map-top-layer.bin\n");
        fprintf(info, "map-bottom-special.bin, map-top-special.bin, bg0-buffer.bin, bg1-buffer.bin, "
                      "bg2-buffer.bin, bg3-buffer.bin\n");
        fprintf(info, "Trigger: L + R + A\n");
        fclose(info);
    }

    char message[192];
    snprintf(message, sizeof(message), "[tmc3ds] quick dump written to %s\n", dir);
    Platform3DS_Debug(message);
    PlatformGpu3DS_ShowDumpSavedOverlay();
    Port_Audio_3DSSetPaused(false);
}

void Port_PPU_3DS_RenderBottomWorker(void) {
    const uint64_t startTick = Platform3DS_SystemTick();
    sBottomWorkerGeneration = Port_SecondScreen_3DS_PaintInto(
        sBottomUploads[sBottomWorkerBuffer], 320, 240, sBottomUploadPitch, &sBottomWorkerSnapshot, sBottomWorkerTick);
    sBottomWorkerLastTicks = Platform3DS_SystemTick() - startTick;
}

static void RecordBottomWorkerTiming(void) {
    if (sFrameNumber < 120u)
        return;
    ++sPerfBottomSamples;
    sPerfBottomTicks += sBottomWorkerLastTicks;
    if (sBottomWorkerLastTicks > sPerfBottomMaxTicks)
        sPerfBottomMaxTicks = sBottomWorkerLastTicks;
}

void Port_PPU_Init(SDL_Window* window) {
    (void)window;
    const bool old3dsProfile = !Platform3DS_IsNew3DS();
    /* MUST match the expression in PlatformGpu3DS_Init exactly. */
    const PlatformGpu3DSUploadLayout uploadLayout =
            PlatformGpu3DS_GetUploadLayout(old3dsProfile && Port_Config_CompactUpload());
    sTopUploadPitch = uploadLayout.topPitch;
    sBottomUploadPitch = uploadLayout.bottomPitch;
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_mode1_set_old3ds_profile(old3dsProfile);
    sColorCorrection = Port_Config_GetColorCorrection();
    virtuappu_mode1_set_color_correction(sColorCorrection);
    Port_Widescreen_SetWindowPixels(400, 240);
    virtuappu_registers.frame_width = GBA_NATIVE_W;
    virtuappu_registers.frame_pitch = sTopUploadPitch;
    virtuappu_registers.mode = 1;
    Port_SecondScreen_Init();
    Port_SecondScreen_3DS_ResetFrameState();
    sBottomReady = false;
    sBottomTextureReady = false;
    sBottomTextureDirty = false;
    sBottomWorkerPending = false;
    sBottomFrontBuffer = 0;
    sBottomWorkerBuffer = 1;
    sBottomSnapshotValid = false;
    sBottomWorkerGeneration = 0;
    memset(sBottomBufferGeneration, 0, sizeof(sBottomBufferGeneration));
    memset(sBottomBufferInGame, 0, sizeof(sBottomBufferInGame));
    sBottomTick = 0;
    sBottomPaintRequests = 0;
    sBottomPeriodicChecks = 0;
    sBottomPeriodicSkips = 0;
    sFrameNumber = 0;
    sPerfLastFrameTick = 0;
    sPerfRenderTicks = 0;
    sPerfTopTicks = 0;
    sPerfBottomTicks = 0;
    sPerfTotalTicks = 0;
    sPerfSamples = 0;
    sPerfBottomSamples = 0;
    sPerfRenderMaxTicks = 0;
    sPerfTopMaxTicks = 0;
    sPerfTopOver4ms = 0;
    sPerfTopOver16ms = 0;
    sPerfTopOver50ms = 0;
    sPerfRenderOver4ms = 0;
    sPerfRenderOver16ms = 0;
    sPerfRenderOver50ms = 0;
    {
        const uint64_t hz = Platform3DS_TicksPerSecond();
        sPerfTopOver4msTicks = hz / 250u;  /* 4 ms */
        sPerfTopOver16msTicks = hz / 60u;  /* one frame period */
        sPerfTopOver50msTicks = hz / 20u;  /* 50 ms: unambiguously a stall */
    }
    sPerfBottomMaxTicks = 0;
    sPerfTotalMaxTicks = 0;
    sPerfIntervalTicks = 0;
    sPerfIntervalLastTicks = 0;
    sPerfIntervalMinTicks = UINT64_MAX;
    sPerfIntervalMaxTicks = 0;
    sPerfIntervalSamples = 0;
    sPerfFramesOver16ms = 0;
    sPerfFramesOver33ms = 0;
    sCurrentFpsX100 = 0;
    sAverageFpsX100 = 0;
    sGpuPresenterReady = PlatformGpu3DS_Init(old3dsProfile);
    sGpuPpuDisabled = false;
    sGpuPpuInitialized =
        old3dsProfile && sGpuPresenterReady && PortPpuGpu3DS_Init();
    sTopUpload = PlatformGpu3DS_TopBuffer();
    sBottomUploads[0] = PlatformGpu3DS_BottomBuffer(0);
    sBottomUploads[1] = PlatformGpu3DS_BottomBuffer(1);
    sInitialized = sGpuPresenterReady && sTopUpload && sBottomUploads[0] && sBottomUploads[1];
    virtuappu_mode1_set_output_buffer(sInitialized ? sTopUpload : NULL, sTopUploadPitch);
}

/* Frames skipped because the GPU had not finished the previous one. */
static unsigned long long sGpuBusyFrameDrops;

void Port_PPU_PresentFrame(void) {
    if (!sInitialized)
        return;
    PortPpuGpu3DS_FinishParityCheck();
    PortPpuGpu3DS_WriteFrameCapture(Port_PPU_3DS_LastDumpDirectory());
    /* Before this frame draws over it, the output texture still holds the
     * frame the screen is showing. */
    if (PortPpuGpu3DS_FrameCaptureRequested() && sGpuPpuInitialized &&
        !sGpuPpuDisabled && PlatformGpu3DS_BeginCustomTop())
        PortPpuGpu3DS_QueueFrameCapture();
    const bool parityCompletionFrame =
        PortPpuGpu3DS_ParityFinishedThisFrame();
    if (sGpuPpuInitialized && PortPpuGpu3DS_IsDisabled())
        sGpuPpuDisabled = true;
    bool parityFrame = false;
    ++sFrameNumber;
    const uint64_t frameStartTick = Platform3DS_SystemTick();

#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t frameStart = Platform3DS_Milliseconds();
#endif
    const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
    {
        /* One dump, once, about thirty seconds in, and only while the PICA200
         * path is actually rendering. The full statistics -- audio underruns,
         * render and presentation costs, build phases, cadence -- are only in a
         * dump, and every measurement so far predates the GPU path working. One
         * pause of roughly a second, then never again. */
        static bool measurementTaken = false;
        if (!measurementTaken && sFrameNumber == 1800u &&
            Port_Config_GpuRenderer() && sGpuPpuInitialized && !sGpuPpuDisabled) {
            measurementTaken = true;
            Platform3DS_RequestQuickDump();
        }
    }
    const uint8_t mode = (uint8_t)(dispcnt & 7);
    virtuappu_registers.mode = (mode == 1 || mode == 2) ? 2 : 1;
    sTopPresentWidth = TopFrameWidth();
    virtuappu_registers.frame_width = sTopPresentWidth;
    virtuappu_registers.frame_pitch = sTopUploadPitch;
    virtuappu_mode1_pre_line_callback = port_hdma_has_active_channels() ? port_hdma_step_line : NULL;
    virtuappu_mode1_bg2x_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x28, gIoMem + 0x2c) != 0;
    virtuappu_mode1_bg2y_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x2c, gIoMem + 0x30) != 0;

    Platform3DS_Heartbeat();
    Platform3DS_SetStage(1);
    PpuGpu3DSFrameView frameView;
    bool gpuReady = false;
    if (Port_Config_GpuRenderer() &&
        PpuGpu3DS_ShouldUse(Platform3DS_IsNew3DS(), sGpuPpuInitialized,
                            sGpuPpuDisabled)) {
        parityFrame = PortPpuGpu3DS_ParityRequested();
        if (parityFrame) {
            Platform3DS_MarkFrameDiscontinuity(
                OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
            virtuappu_render_frame();
            PortPpuGpu3DS_CaptureParityReference(
                sTopUpload, sTopUploadPitch, sTopPresentWidth, MODE1_GBA_HEIGHT);
            port_hdma_vblank_reset();
        }
        FillPreparedFrameView(&frameView);
        /* Building the per-line register snapshot walks the HBlank DMA
         * channels exactly as rendering does, so they have to be rewound first
         * or the next frame resumes past the end of the per-scanline table.
         * The software path already does this; without it here a channel that
         * outlives its frame feeds the GPU path garbage registers, and taking
         * a quick dump appeared to "fix" the screen only because the parity
         * frame performs the rewind. */
        port_hdma_vblank_reset();
        virtuappu_mode1_prepare_frame(
            &virtuappu_registers, sGpuIoPerLine[0], sGpuDispcntPerLine,
            sGpuAffRefX, sGpuAffRefY, &frameView.frameDispcnt);
        /* Claim the GPU frame first: preflight writes the vertex, index and
         * atlas buffers the previous frame may still be drawing from, so this
         * wait is a correctness barrier, not incidental ordering. It cannot be
         * moved later or dropped to shorten the block -- doing so overwrites
         * geometry still being drawn, which an emulator hides because its GPU
         * completes instantly. Removing the block properly means double
         * buffering those buffers so preflight can write B while the GPU reads A.
         *
         * Stage 2 covered both the frame-begin wait and the whole of preflight.
         * C3D_FrameBegin waits on the GX queue with no timeout, so a command
         * list the GPU never retires stops the main thread here forever. Split
         * them so the watchdog names which. */
        /* The PPU-render span is 4.249 ms average, of which preflight is 3.024
         * and the builder 2.906 -- so on AVERAGE this is CPU builder work, not
         * the GPU wait. But 89 of 1681 frames exceed 16 ms in this span and an
         * average cannot say which half spikes. Split them: if the wait spikes
         * it is GSP starvation (attack with app_cpu_limit / core placement); if
         * preflight spikes it is the builder, most likely `maps` at 2.31 ms
         * with atlas decode bursts on area change (111 decodes/frame average
         * over 199496). The two fixes have nothing in common, so guessing here
         * would waste a run. */
        Platform3DS_SetStage(20);
        const uint64_t beginStart = Platform3DS_SystemTick();
        const bool frameBegun = PlatformGpu3DS_BeginCustomTop();
        const uint64_t beginTicks = Platform3DS_SystemTick() - beginStart;
        Platform3DS_SetStage(21);
        gpuReady = frameBegun && PortPpuGpu3DS_Preflight(&frameView);
        const uint64_t preflightTicks = Platform3DS_SystemTick() - beginStart - beginTicks;
        Platform3DS_SetStage(3);
        sFrameBeginTicks += beginTicks;
        sPreflightTicks += preflightTicks;
        if (beginTicks > sFrameBeginMaxTicks) sFrameBeginMaxTicks = beginTicks;
        if (preflightTicks > sPreflightMaxTicks) sPreflightMaxTicks = preflightTicks;
        if (beginTicks > sPerfTopOver4msTicks) ++sFrameBeginOver4ms;
        if (beginTicks > sPerfTopOver16msTicks) ++sFrameBeginOver16ms;
        if (preflightTicks > sPerfTopOver4msTicks) ++sPreflightOver4ms;
        if (preflightTicks > sPerfTopOver16msTicks) ++sPreflightOver16ms;
        ++sFrameBeginSamples;
        if (!gpuReady) {
            port_hdma_vblank_reset();
            if (parityFrame) PortPpuGpu3DS_DeferParityCheck();
        }
    } else if (sGpuPpuInitialized && sGpuPpuDisabled) {
        PortPpuGpu3DS_RecordDisabledFrame();
    }
    Platform3DS_SetStage(4);
    if (!gpuReady) virtuappu_render_frame();
    Platform3DS_SetStage(5);
    const uint64_t renderEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t renderEnd = Platform3DS_Milliseconds();

    const unsigned diagnosticFrame = sDiagnosticFrames++;
    if (diagnosticFrame == 0)
        sDiagnosticStartMs = frameStart;
    if (diagnosticFrame < 3 || diagnosticFrame == 60 || diagnosticFrame == 180 || diagnosticFrame == 360) {
        char message[256];
        snprintf(message, sizeof(message),
                 "[tmc3ds] frame=%u dispcnt=%04x io=%02x%02x vram=%02x%02x%02x%02x pal=%04x,%04x out=%08lx,%08lx\n",
                 diagnosticFrame, dispcnt, gIoMem[0], gIoMem[1], gVram[0], gVram[1], gVram[2], gVram[3], gBgPltt[0],
                 gBgPltt[1], (unsigned long)sTopUpload[0], (unsigned long)sTopUpload[1]);
        Platform3DS_Debug(message);
    }
    if (diagnosticFrame == 2)
        DumpPpuSnapshot("tmc3ds-frame2.ppu1");
    if (diagnosticFrame == 60)
        DumpPpuSnapshot("tmc3ds-frame60.ppu1");
#endif

    /* Presentation is the largest remaining cost on an Old 3DS and the
     * emulator reports it as nearly free, which means it is a GPU wait rather
     * than CPU work. Splitting it says which half to attack. */
    if (!gpuReady) {
        Platform3DS_SetStage(6);
        PlatformGpu3DS_BeginTop(sTopUpload, (unsigned)sTopPresentWidth);
    } else if (Platform3DS_SetStage(7), !PlatformGpu3DS_BeginCustomTop()) {
        /* Since C3D_FrameBegin became non-blocking, a false return means the
         * GPU is merely busy -- which it often is, because the software path
         * transfers 512 KB a frame. Retiring the renderer for that threw away
         * the GPU path after ~1340 good frames and left the console rendering
         * everything in software for the rest of the session.
         *
         * Drop the frame rather than render it again in software. gpuReady was
         * true, so virtuappu_render_frame has not run and sTopUpload holds the
         * previous frame -- presenting that would show stale content, and
         * rendering it in software costs ~15 ms, which makes the next begin
         * more likely to fail too. Not presenting at all is an ordinary dropped
         * frame: the screen simply holds for one frame and the GPU catches up. */
        ++sGpuBusyFrameDrops;
    } else if (!PortPpuGpu3DS_DrawPrepared()) {
        /* A draw that genuinely failed is a different matter and still retires
         * the path. */
        PortPpuGpu3DS_Disable();
        sGpuPpuDisabled = true;
    } else {
        const uint64_t drawEndTick = Platform3DS_SystemTick();
        sTopDrawTicks += drawEndTick - renderEndTick;
        Platform3DS_SetStage(8);
        PlatformGpu3DS_DrawTopTexture(PortPpuGpu3DS_OutputTexture(),
                                      (unsigned)sTopPresentWidth);
        Platform3DS_SetStage(9);
        sTopBlitTicks += Platform3DS_SystemTick() - drawEndTick;
        if (parityFrame && !PortPpuGpu3DS_QueueParityCopy()) {
            /* The readback can fail simply because the GPU is busy. Retiring
             * the renderer for that is the same mistake as retiring it for a
             * skipped frame begin -- defer the comparison to a later frame. */
            PortPpuGpu3DS_DeferParityCheck();
        }
    }
    /* A screen that stays black means neither render path drew anything, and
     * the counters that say why are otherwise only visible in a quick dump --
     * which is hard to take when the screen is blank. Report the first frames
     * to the log so the state can be read straight off the SD card. */
    if (sFrameNumber == 1u) {
        /* Stamp the run with the switches in effect, so a log read later says
         * which configuration produced it. */
        char config[160];
        snprintf(config, sizeof(config),
                 "[tmc3ds] run config: gpu_renderer=%d frame_sync=%d "
                 "viewport_offset=%d scissor_mode=%d\n",
                 (int)Port_Config_GpuRenderer(), (int)Port_Config_GpuFrameSync(),
                 (int)Port_Config_GpuViewportOffset(),
                 Port_Config_GpuScissorMode());
        Platform3DS_Debug(config);
    }
    /* Platform3DS_Debug does fopen/fwrite/fclose per call, so this is a full SD
     * open-write-close on the main thread, inside the timed presentation span.
     * FS is a core-1 sysmodule holding the app's 20% quota, so the cycle costs
     * tens of milliseconds: amortised over 120 frames that is a few tenths of a
     * millisecond per frame, and the stall itself lands in the presentation
     * span, which is where the unexplained ~196 ms maximum shows up.
     *
     * The first frames stay unconditional -- they are boot forensics and cost
     * eight writes once. The periodic line is redundant with the quick dump,
     * which reports every one of these counters, so it is opt-in. */
    if (sFrameNumber <= 8u || (Port_Config_FrameLog() && (sFrameNumber % 120u) == 0u)) {
        PlatformGpu3DSStats presenter;
        PortPpuGpu3DSStats gpu;
        PlatformGpu3DS_GetStats(&presenter);
        PortPpuGpu3DS_GetStats(&gpu);
        char line[224];
        snprintf(line, sizeof(line),
                 "[tmc3ds] emptyDraws=%llu busyDrops=%llu\n"
                 "[tmc3ds] f=%lu gpuReady=%d init=%d disabled=%d beginFail=%llu "
                 "topXfer=%llu att=%llu rend=%llu fb=%llu dispcnt=%04x\n",
                 PortPpuGpu3DS_EmptyDrawsSkipped(), sGpuBusyFrameDrops,
                 (unsigned long)sFrameNumber, (int)gpuReady,
                 (int)sGpuPpuInitialized, (int)sGpuPpuDisabled,
                 (unsigned long long)presenter.frameBeginFailures,
                 (unsigned long long)presenter.topTransfers,
                 (unsigned long long)gpu.attemptedFrames,
                 (unsigned long long)gpu.renderedFrames,
                 (unsigned long long)gpu.fallbackFrames, dispcnt);
        Platform3DS_Debug(line);
    }

    /* Cadence summary in the log, so a run reports itself without a quick dump.
     *
     * The quick dump has all of this and more, but it needs L+R+A at the end of
     * a session -- and a run reaching frame 11160 has already happened with no
     * dump written, which cost that whole session's data. One SD write every
     * 1800 frames is ~30 s apart: even at 30 ms per open-write-close that is
     * 0.017 ms/frame, three orders below the per-120-frame line this replaces.
     *
     * Deliberately after frame 1800 only, so the numbers exclude warm-up. */
    if (sFrameNumber >= 1800u && (sFrameNumber % 1800u) == 0u && sPerfIntervalSamples > 0u &&
        sPerfSamples > 0u) {
        PlatformGpu3DSStats gpuCadence;
        PlatformGpu3DS_GetStats(&gpuCadence);
        Platform3DSRuntimeStats rt;
        Platform3DS_GetRuntimeStats(&rt);
        extern uint64_t Platform3DS_VblankWaitSamples(void);
        extern uint64_t Platform3DS_VblankWaitOverOnePeriod(void);
        const uint64_t waits = Platform3DS_VblankWaitSamples();
        const uint64_t over1 = Platform3DS_VblankWaitOverOnePeriod();
        char cadence[352];
        snprintf(cadence, sizeof(cadence),
                 "[tmc3ds] CADENCE f=%lu fps=%.2f logic=%.2f interval=%.3fms "
                 "skips=%llu clamps=%llu lostVblank=%llu/%llu over1=%llu over2=%llu "
                 "xfer=%.3f/%.3fms n=%llu\n",
                 (unsigned long)sFrameNumber,
                 (double)__atomic_load_n(&sAverageFpsX100, __ATOMIC_RELAXED) / 100.0,
                 rt.logicElapsedTicks
                         ? (double)rt.logicFrames * (double)Platform3DS_TicksPerSecond() /
                                   (double)rt.logicElapsedTicks
                         : 0.0,
                 TicksToMilliseconds(sPerfIntervalTicks) / (double)sPerfIntervalSamples,
                 (unsigned long long)rt.old3dsSkippedPresentations,
                 (unsigned long long)rt.old3dsDebtClampEvents, (unsigned long long)over1,
                 (unsigned long long)waits, (unsigned long long)sPerfFramesOver16ms,
                 (unsigned long long)sPerfFramesOver33ms,
                 gpuCadence.bottomTransfers
                         ? TicksToMilliseconds(gpuCadence.bottomTransferTicks) /
                                   (double)gpuCadence.bottomTransfers
                         : 0.0,
                 TicksToMilliseconds(gpuCadence.bottomTransferMaxTicks),
                 (unsigned long long)gpuCadence.bottomTransfers);
        Platform3DS_Debug(cadence);
    }
    const uint64_t topEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t topEnd = Platform3DS_Milliseconds();
#endif

    bool bottomChanged = false;
    if (sBottomWorkerPending && Platform3DS_TryFinishBottomWorker()) {
        sBottomFrontBuffer = sBottomWorkerBuffer;
        sBottomWorkerPending = false;
        sBottomReady = true;
        bottomChanged = true;
        sBottomBufferGeneration[sBottomFrontBuffer] = sBottomWorkerGeneration;
        sBottomBufferInGame[sBottomFrontBuffer] = sBottomWorkerSnapshot.inGame != 0;
        RecordBottomWorkerTiming();
    }

    /* Every third presentation cost 2759 repaints in 8336 frames, each a
     * software paint plus a 512 KB transfer -- about 169 KB a frame of GSP
     * work, and GSP runs on core 1 where the audio worker lives. Halving the
     * idle cadence on an Old 3DS halves that traffic. Only the animation rate
     * drops: anything that actually changes still repaints at once through
     * forcedUpdate below. */
    const uint32_t bottomInterval =
            Port_SecondScreen_IsDeveloperOverlayOpen()
                    ? 30u
                    : (Platform3DS_IsNew3DS() ? 3u : 6u);
    const bool cadenceDue = (sFrameNumber % bottomInterval) == 0u;
    /* The animation tick is free-running: it advances on the cadence, not on
     * whether a paint was scheduled. It used to be `sBottomTick++` inside the
     * schedulePaint branch below, which made the MAP-tab skip signature
     * self-referential — skip one paint and the tick freezes, so the
     * signature can never change again and the animation dies permanently.
     * Paints tracked the cadence 1:1 before this (2230 paints / 2229 checks),
     * so the animation rate is unchanged. */
    if (cadenceDue) {
        ++sBottomTick;
    }
    const bool forcedUpdate = !sBottomReady || Port_SecondScreen_3DS_NeedsRefresh();
    if (!sBottomWorkerPending && (forcedUpdate || cadenceDue)) {
        SecondScreenSnapshot nextSnapshot;
        Port_SecondScreenState_Read(&nextSnapshot);

        const bool snapshotChanged = Port_SecondScreen_3DS_SnapshotChangeNeedsRefresh(
            &sBottomWorkerSnapshot, &nextSnapshot, sBottomSnapshotValid);
        const bool animationRequired = Port_SecondScreen_3DS_NeedsPeriodicRefresh(
            &nextSnapshot, sBottomTick, sBottomWorkerTick, 320, 240);
        const bool schedulePaint = BottomFrameState3DS_ShouldSchedulePaint(
            sBottomWorkerPending, forcedUpdate, cadenceDue, bottomChanged, snapshotChanged, animationRequired);
        if (!forcedUpdate && cadenceDue && !bottomChanged) {
            ++sBottomPeriodicChecks;
            if (!schedulePaint)
                ++sBottomPeriodicSkips;
        }

        if (schedulePaint) {
            /* Every paint gets a distinct generation, including animation
             * paints whose engine snapshot did not change. Requests arriving
             * during this paint remain newer and therefore pending. */
            if (!Port_SecondScreen_3DS_NeedsRefresh()) {
                Port_SecondScreen_3DS_RequestRefresh();
            }
            ++sBottomPaintRequests;
            sBottomWorkerBuffer = 1 - sBottomFrontBuffer;
            sBottomWorkerSnapshot = nextSnapshot;
            sBottomSnapshotValid = true;
            sBottomWorkerTick = sBottomTick;
            sBottomWorkerGeneration = 0;
            if (Platform3DS_SubmitBottomWorker()) {
                sBottomWorkerPending = true;
            } else {
                Port_PPU_3DS_RenderBottomWorker();
                sBottomFrontBuffer = sBottomWorkerBuffer;
                sBottomReady = true;
                bottomChanged = true;
                sBottomBufferGeneration[sBottomFrontBuffer] = sBottomWorkerGeneration;
                sBottomBufferInGame[sBottomFrontBuffer] = sBottomWorkerSnapshot.inGame != 0;
                RecordBottomWorkerTiming();
            }
        }
    }
    if (!sBottomTextureReady) {
        bottomChanged = true;
        sBottomTextureReady = true;
    }
    sBottomTextureDirty = sBottomTextureDirty || bottomChanged;
    /* The gap after stage 9 covers the bottom-screen work and C3D_FrameEnd,
     * where a multi-second stall was seen at frame 5331. Split it. */
    Platform3DS_SetStage(10);
    if (PlatformGpu3DS_EndBottom(sBottomUploads[sBottomFrontBuffer], sBottomTextureDirty)) {
        if (sBottomTextureDirty) {
            sBottomTextureDirty = false;
            Port_SecondScreen_3DS_MarkSubmitted(sBottomBufferGeneration[sBottomFrontBuffer],
                                                sBottomBufferInGame[sBottomFrontBuffer]);
        }
    }
    const uint64_t frameEndTick = Platform3DS_SystemTick();

    if (parityFrame || parityCompletionFrame) {
        sPerfLastFrameTick = 0;
    } else if (sFrameNumber >= 120u) {
        const uint64_t renderTicks = renderEndTick - frameStartTick;
        const uint64_t topTicks = topEndTick - renderEndTick;
        const uint64_t totalTicks = frameEndTick - frameStartTick;
        if (sPerfLastFrameTick != 0) {
            const uint64_t intervalTicks = frameStartTick - sPerfLastFrameTick;
            sPerfIntervalLastTicks = intervalTicks;
            sPerfIntervalTicks += intervalTicks;
            ++sPerfIntervalSamples;
            if (intervalTicks < sPerfIntervalMinTicks)
                sPerfIntervalMinTicks = intervalTicks;
            if (intervalTicks > sPerfIntervalMaxTicks)
                sPerfIntervalMaxTicks = intervalTicks;
            const uint64_t ticksPerSecond = Platform3DS_TicksPerSecond();
            __atomic_store_n(&sCurrentFpsX100, (uint32_t)(ticksPerSecond * 100u / intervalTicks), __ATOMIC_RELAXED);
            /* Was `intervalTicks * 60 > ticksPerSecond`, i.e. "over 16.667 ms".
             * The 3DS LCD period is 16.715 ms and the GBA target 16.743 ms, so
             * every on-time frame counted as over and the figure read ~80%,
             * measuring nothing. Compare against the period actually being
             * paced to (GBA: 280896 cycles of a 16.777216 MHz clock). */
            const uint64_t periodTicks = ticksPerSecond * 280896u / 16777216u;
            if (intervalTicks > periodTicks)
                ++sPerfFramesOver16ms;
            if (intervalTicks > periodTicks * 2u)
                ++sPerfFramesOver33ms;
        }
        sPerfLastFrameTick = frameStartTick;
        ++sPerfSamples;
        sPerfRenderTicks += renderTicks;
        sPerfTopTicks += topTicks;
        sPerfTotalTicks += totalTicks;
        if (renderTicks > sPerfRenderMaxTicks)
            sPerfRenderMaxTicks = renderTicks;
        if (topTicks > sPerfTopMaxTicks)
            sPerfTopMaxTicks = topTicks;
        /* A maximum says nothing about frequency, and the 170-216 ms
         * presentation outlier has been treated as a recurring cost across
         * seven dumps without anyone knowing whether it happens once a run or
         * fifty times. One outlier is irrelevant; fifty cost several FPS.
         * C3D_FrameBegin blocks on the GX queue with no timeout and GSP retires
         * that queue on core 1 with the app's 20% quota, so bucket the span. */
        if (topTicks > sPerfTopOver4msTicks) ++sPerfTopOver4ms;
        if (topTicks > sPerfTopOver16msTicks) ++sPerfTopOver16ms;
        if (topTicks > sPerfTopOver50msTicks) ++sPerfTopOver50ms;
        /* Same blind spot on the other large span: PPU render has an 86.776 ms
         * maximum and no frequency. Atlas tile decodes burst on area changes
         * (475478 decodes across a run), which would show up here. */
        if (renderTicks > sPerfTopOver4msTicks) ++sPerfRenderOver4ms;
        if (renderTicks > sPerfTopOver16msTicks) ++sPerfRenderOver16ms;
        if (renderTicks > sPerfTopOver50msTicks) ++sPerfRenderOver50ms;
        if (totalTicks > sPerfTotalMaxTicks)
            sPerfTotalMaxTicks = totalTicks;
        if (sPerfIntervalTicks != 0) {
            uint64_t average = Platform3DS_TicksPerSecond() * sPerfIntervalSamples * 100u / sPerfIntervalTicks;
            __atomic_store_n(&sAverageFpsX100, (uint32_t)average, __ATOMIC_RELAXED);
        }
    }
#ifdef TMC_3DS_DIAGNOSTICS
    if (diagnosticFrame < 8 || diagnosticFrame == 60) {
        char message[160];
        const uint64_t frameEnd = Platform3DS_Milliseconds();
        snprintf(message, sizeof(message),
                 "[tmc3ds] timing frame=%u render=%llums top=%llums bottom=%llums total=%llums\n", diagnosticFrame,
                 (unsigned long long)(renderEnd - frameStart), (unsigned long long)(topEnd - renderEnd),
                 (unsigned long long)(frameEnd - topEnd), (unsigned long long)(frameEnd - frameStart));
        Platform3DS_Debug(message);
        if (diagnosticFrame == 60) {
            snprintf(message, sizeof(message), "[tmc3ds] cadence 60_frames=%llums\n",
                     (unsigned long long)(frameEnd - sDiagnosticStartMs));
            Platform3DS_Debug(message);
        }
    }
#endif
}

void Port_PPU_SetPresentIsFirstOfTick(bool first) {
    (void)first;
}
double Port_PPU_3DS_CurrentFps(void) {
    return (double)__atomic_load_n(&sCurrentFpsX100, __ATOMIC_RELAXED) / 100.0;
}
double Port_PPU_3DS_AverageFps(void) {
    return (double)__atomic_load_n(&sAverageFpsX100, __ATOMIC_RELAXED) / 100.0;
}
void Port_PPU_SetWindowTitle(const char* title) {
    (void)title;
}
void Port_PPU_ToggleFullscreen(void) {
}
bool Port_PPU_IsFullscreen(void) {
    return true;
}
void Port_PPU_CycleWindowScale(int direction) {
    (void)direction;
}
unsigned char Port_PPU_WindowScale(void) {
    return 1;
}
void Port_PPU_ApplyWindowScale(void) {
}
void Port_PPU_ToggleSmoothing(void) {
}
void Port_PPU_CyclePresentationMode(int direction) {
    (void)direction;
}
const char* Port_PPU_PresentationModeName(void) {
    return "3DS widescreen";
}
void Port_PPU_CycleFilter(int direction) {
    (void)direction;
}
const char* Port_PPU_FilterName(void) {
    return "Off";
}
void Port_PPU_SetVSync(bool enabled) {
    (void)enabled;
}
bool Port_PPU_VSyncEnabled(void) {
    return true;
}
unsigned Port_PPU_DisplayRefreshRate(void) {
    return 60;
}
void Port_PPU_SetColorCorrection(bool enabled) {
    sColorCorrection = enabled;
    virtuappu_mode1_set_color_correction(enabled);
}
bool Port_PPU_ColorCorrectionEnabled(void) {
    return sColorCorrection;
}
void Port_PPU_SetPersistence(bool enabled, float rho) {
    (void)enabled;
    (void)rho;
}
bool Port_PPU_3DS_UsesGpuPresenter(void) {
    return sGpuPresenterReady;
}
void Port_PPU_Shutdown(void) {
    sInitialized = false;
    Platform3DS_ShutdownBottomWorker();
    virtuappu_mode1_set_output_buffer(NULL, 0);
    if (sGpuPpuInitialized) {
        PortPpuGpu3DS_Shutdown();
        sGpuPpuInitialized = false;
        sGpuPpuDisabled = false;
    }
    if (!sGpuPresenterReady)
        return;
    PlatformGpu3DS_Shutdown();
    sGpuPresenterReady = false;
}
void Port_OpenInGameSettingsModal(void) {
}
bool Port_InGameSettingsModalIsOpen(void) {
    return false;
}
