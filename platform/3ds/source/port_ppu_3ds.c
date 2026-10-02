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
#define TOP_PITCH 512

static uint32_t* sBottomUploads[2];
static uint32_t* sTopUpload;
static uint32_t sBottomTick;
static bool sBottomReady;
static bool sBottomTextureReady;
static bool sBottomTextureDirty;
static bool sBottomWorkerPending;
static int sBottomFrontBuffer;
static int sBottomWorkerBuffer;
static uint32_t sBottomWorkerTick;
static SecondScreenSnapshot sBottomWorkerSnapshot;
static int sPendingBottomInGame;
static uint64_t sBottomWorkerLastTicks;
static bool sGpuPresenterReady;
static bool sInitialized;
static bool sColorCorrection;
static uint32_t sFrameNumber;
static uint64_t sPerfFirstFrameTick;
static uint64_t sPerfLastFrameTick;
static uint64_t sPerfRenderTicks;
static uint64_t sPerfTopTicks;
static uint64_t sPerfBottomTicks;
static uint64_t sPerfTotalTicks;
static uint64_t sPerfSamples;
static uint64_t sPerfBottomSamples;
static uint64_t sPerfRenderMaxTicks;
static uint64_t sPerfTopMaxTicks;
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

static int TopFrameWidth(void) {
    Port_Widescreen_SetWindowPixels(400, 240);
    if (Port_Widescreen_IsActive() && Port_Widescreen_ShadowsLive()) {
        int width = Port_Widescreen_EffectiveViewWidth();
        if (width > MODE1_GBA_WIDTH) width = MODE1_GBA_WIDTH;
        if (width > GBA_NATIVE_W) return width;
    }
    return GBA_NATIVE_W;
}

#ifdef TMC_3DS_DIAGNOSTICS
static unsigned sDiagnosticFrames;
static uint64_t sDiagnosticStartMs;

static void DumpPpuSnapshot(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return;
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
    if (!file) return false;
    const bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) return false;
    return ok;
}

static bool WritePalettes(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    bool ok = fwrite(gBgPltt, 1, sizeof(gBgPltt), file) == sizeof(gBgPltt);
    ok = fwrite(gObjPltt, 1, sizeof(gObjPltt), file) == sizeof(gObjPltt) && ok;
    if (fclose(file) != 0) ok = false;
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

static bool CreateDumpDirectory(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    if (mkdir("dumps", 0777) != 0 && errno != EEXIST) return false;

    char stamp[32];
    MakeTimestamp(stamp, sizeof(stamp));
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (attempt == 0) {
            snprintf(out, outSize, "dumps/dump-%s", stamp);
        } else {
            snprintf(out, outSize, "dumps/dump-%s-%02d", stamp, attempt);
        }
        if (mkdir(out, 0777) == 0) return true;
        if (errno != EEXIST) break;
    }
    out[0] = 0;
    return false;
}

static double TicksToMilliseconds(uint64_t ticks) {
    return (double)ticks * 1000.0 / (double)Platform3DS_TicksPerSecond();
}

void Port_PPU_3DS_WriteQuickDump(void) {
    if (!sInitialized) return;
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
    const bool screensOk = Platform3DS_SaveDisplayedScreensDetailed(
        topPath, bottomPath, topRawPath, bottomRawPath, &captureStats);
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
        const double elapsedSeconds = sPerfLastFrameTick > sPerfFirstFrameTick
                                          ? (double)(sPerfLastFrameTick - sPerfFirstFrameTick) /
                                                (double)Platform3DS_TicksPerSecond()
                                          : 0.0;
        const double measuredFps = elapsedSeconds > 0.0 && sPerfSamples > 1
                                       ? (double)(sPerfSamples - 1u) / elapsedSeconds
                                       : 0.0;
        Platform3DSRuntimeStats runtimeStats;
        PlatformGpu3DSStats gpuStats;
        PortAudio3DSStats audioStats;
        PortSaveStats saveStats;
        VirtuaPPUMode13DSStats workerStats;
        Platform3DS_GetRuntimeStats(&runtimeStats);
        PlatformGpu3DS_GetStats(&gpuStats);
        Port_Audio_3DSGetStats(&audioStats);
        Port_Save_GetStats(&saveStats);
        virtuappu_mode1_get_3ds_stats(&workerStats);
        const uint64_t engineSamples = runtimeStats.logicFrames > 1 ? runtimeStats.logicFrames - 1u : 1u;
        const uint64_t vblankSamples = runtimeStats.presentedFrames ? runtimeStats.presentedFrames : 1u;
        const uint64_t audioSamples = audioStats.buffersRendered ? audioStats.buffersRendered : 1u;
        const uint16_t dumpDispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
        const uint8_t dumpMode = dumpDispcnt & 7u;
        const uint32_t avoidedFlushBytes = gpuStats.linearHeapBytes > gpuStats.c2dFlushBytes
                                               ? gpuStats.linearHeapBytes - gpuStats.c2dFlushBytes
                                               : 0;
        const double loadIntervalTicks = sPerfIntervalLastTicks
                                             ? (double)sPerfIntervalLastTicks
                                             : (double)Platform3DS_TicksPerSecond() / 60.0;
        fprintf(info, "The Minish Cap 3DS v" TMC_PORT_VERSION " quick dump\n");
        fprintf(info, "\n[System]\n");
        fprintf(info, "Model: %s\n", Platform3DS_IsNew3DS() ? "New Nintendo 3DS" : "Old Nintendo 3DS");
        fprintf(info, "CPU profile requested: %s\n", runtimeStats.speedupRequested ? "804 MHz + L2" : "268 MHz");
        fprintf(info, "Kernel version: 0x%08lX\n", (unsigned long)runtimeStats.kernelVersion);
        fprintf(info, "FIRM version: 0x%08lX\n", (unsigned long)runtimeStats.firmVersion);
        fprintf(info, "System core version: %lu\n", (unsigned long)runtimeStats.systemCoreVersion);
        fprintf(info, "Application memory type: %lu\n", (unsigned long)runtimeStats.applicationMemoryType);
        fprintf(info, "Main thread priority: %ld\n", (long)runtimeStats.mainThreadPriority);
        fprintf(info, "Core 1 time limit: %u%%\n", Platform3DS_Core1TimeLimit());
        fprintf(info, "PPU workers: %lu (core 1: %s, New 3DS core 2: %s)\n",
                (unsigned long)workerStats.workerCount,
                Platform3DS_CanUseCore1() ? "enabled" : "unavailable",
                Platform3DS_IsNew3DS() ? "enabled" : "unavailable");
        fprintf(info, "Application memory free: %lu bytes\n", (unsigned long)runtimeStats.applicationMemoryFree);
        fprintf(info, "System memory free: %lu bytes\n", (unsigned long)runtimeStats.systemMemoryFree);
        fprintf(info, "Base memory free: %lu bytes\n", (unsigned long)runtimeStats.baseMemoryFree);
        fprintf(info, "Linear memory free: %lu bytes\n", (unsigned long)runtimeStats.linearMemoryFree);
        fprintf(info, "Stack pointer / region: 0x%08lX / 0x%08lX-0x%08lX\n",
                (unsigned long)runtimeStats.currentStackPointer,
                (unsigned long)runtimeStats.stackRegionBase,
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
        fprintf(info, "Keys held/down: 0x%08lX / 0x%08lX\n",
                (unsigned long)runtimeStats.keyMaskHeld, (unsigned long)runtimeStats.keyMaskDown);
        fprintf(info, "Circle Pad: %d, %d\n", runtimeStats.circleX, runtimeStats.circleY);
        fprintf(info, "C-stick: %d, %d\n", runtimeStats.cstickX, runtimeStats.cstickY);
        fprintf(info, "Turbo: %s, multiplier x%u\n",
                runtimeStats.turboHeld ? "held" : "released", Platform3DS_TurboMultiplier());

        fprintf(info, "\n[Cadence]\n");
        fprintf(info, "Engine logic frames: %llu\n", (unsigned long long)runtimeStats.logicFrames);
        fprintf(info, "Presented frames: %llu\n", (unsigned long long)runtimeStats.presentedFrames);
        fprintf(info, "Turbo logic frames: %llu\n", (unsigned long long)runtimeStats.turboLogicFrames);
        fprintf(info, "Turbo-skipped presentations: %llu\n",
                (unsigned long long)runtimeStats.turboSkippedPresentations);
        fprintf(info, "Top screenshot: 400x240 displayed framebuffer BMP\n");
        fprintf(info, "Bottom screenshot: 320x240 displayed framebuffer BMP\n");
        fprintf(info, "Raw physical framebuffers: top-screen.raw, bottom-screen.raw\n");
        fprintf(info, "Displayed framebuffer capture: %s\n", screensOk ? "OK" : "FAILED");
        fprintf(info, "Top capture format/stride/address: %lu / %lu / 0x%08lX\n",
                (unsigned long)captureStats.topFormat, (unsigned long)captureStats.topStride,
                (unsigned long)captureStats.topAddress);
        fprintf(info, "Bottom capture format/stride/address: %lu / %lu / 0x%08lX\n",
                (unsigned long)captureStats.bottomFormat, (unsigned long)captureStats.bottomStride,
                (unsigned long)captureStats.bottomAddress);
        fprintf(info, "Measured cadence: %.2f FPS\n", measuredFps);
        fprintf(info, "Frame interval: last %.3f ms, average %.3f ms, minimum %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfIntervalLastTicks),
                TicksToMilliseconds(sPerfIntervalTicks) / intervalSampleCount,
                TicksToMilliseconds(sPerfIntervalMinTicks == UINT64_MAX ? 0 : sPerfIntervalMinTicks),
                TicksToMilliseconds(sPerfIntervalMaxTicks));
        fprintf(info, "Frames over 16.67 ms / 33.33 ms: %llu / %llu\n",
                (unsigned long long)sPerfFramesOver16ms, (unsigned long long)sPerfFramesOver33ms);
        fprintf(info, "Engine work between frame boundaries: last %.3f ms, average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(runtimeStats.engineWorkLastTicks),
                TicksToMilliseconds(runtimeStats.engineWorkTicks) / (double)engineSamples,
                TicksToMilliseconds(runtimeStats.engineWorkMaxTicks));
        fprintf(info, "VBlank wait: last %.3f ms, average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(runtimeStats.vblankWaitLastTicks),
                TicksToMilliseconds(runtimeStats.vblankWaitTicks) / (double)vblankSamples,
                TicksToMilliseconds(runtimeStats.vblankWaitMaxTicks));

        fprintf(info, "\n[Renderer]\n");
        fprintf(info, "PPU render: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfRenderTicks) / sampleCount, TicksToMilliseconds(sPerfRenderMaxTicks));
        fprintf(info, "Top presentation CPU work: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfTopTicks) / sampleCount, TicksToMilliseconds(sPerfTopMaxTicks));
        fprintf(info, "Bottom-screen paint worker: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfBottomTicks) / bottomSampleCount,
                TicksToMilliseconds(sPerfBottomMaxTicks));
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
                    TicksToMilliseconds(workerStats.workerMaxTicks[i]),
                    (unsigned long)workerStats.workerLastLines[i]);
            fprintf(info, "PPU core %d measured load in last frame interval: %.1f%%\n", i + 1,
                    (double)workerStats.workerLastTicks[i] * 100.0 / loadIntervalTicks);
        }
        fprintf(info, "GPU frames / begin failures: %llu / %llu\n",
                (unsigned long long)gpuStats.frames, (unsigned long long)gpuStats.frameBeginFailures);
        fprintf(info, "GPU top/bottom transfers: %llu / %llu\n",
                (unsigned long long)gpuStats.topTransfers, (unsigned long long)gpuStats.bottomTransfers);
        fprintf(info, "Citro3D drawing/processing time: %.3f / %.3f ms\n",
                gpuStats.drawingTime, gpuStats.processingTime);
        fprintf(info, "Linear heap full flush: disabled (%lu bytes avoided per frame)\n",
                (unsigned long)avoidedFlushBytes);
        fprintf(info, "Bounded C2D cache flush: %lu bytes per frame, %llu bytes total\n",
                (unsigned long)gpuStats.c2dFlushBytes, (unsigned long long)gpuStats.boundedFlushBytes);
        fprintf(info, "C2D flush address: 0x%08lX; upload buffers: 0x%08lX / 0x%08lX / 0x%08lX\n",
                (unsigned long)gpuStats.c2dFlushAddress, (unsigned long)gpuStats.topUploadAddress,
                (unsigned long)gpuStats.bottomUploadAddress[0],
                (unsigned long)gpuStats.bottomUploadAddress[1]);
        fprintf(info, "Bottom-screen refresh: 20 Hz\n");

        fprintf(info, "\n[Audio]\n");
        fprintf(info, "Initialized/playing: %s / %s\n",
                audioStats.initialized ? "yes" : "no", audioStats.channelPlaying ? "yes" : "no");
        fprintf(info, "Paused for quick dump: %s\n", audioStats.paused ? "yes" : "no");
        fprintf(info, "Sample rate: %lu Hz; buffer geometry: %lu x %lu frames\n",
                (unsigned long)audioStats.sampleRate, (unsigned long)audioStats.bufferCount,
                (unsigned long)audioStats.bufferFrames);
        fprintf(info, "Worker running/core/priority: %s / %ld / %ld\n",
                audioStats.audioThreadRunning ? "yes" : "no", (long)audioStats.threadCore,
                (long)audioStats.threadPriority);
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
        fprintf(info, "Wave buffers free/queued/playing/done: %lu/%lu/%lu/%lu\n",
                (unsigned long)audioStats.freeBuffers, (unsigned long)audioStats.queuedBuffers,
                (unsigned long)audioStats.playingBuffers, (unsigned long)audioStats.doneBuffers);
        fprintf(info, "Maximum buffers per worker wake / maximum DONE backlog: %lu / %lu\n",
                (unsigned long)audioStats.maxBuffersPerWake, (unsigned long)audioStats.maxDoneBuffersObserved);
        fprintf(info, "Fallback renders / sample position: %llu / %lu\n",
                (unsigned long long)audioStats.fallbackRenders, (unsigned long)audioStats.samplePosition);
        fprintf(info, "NDSP frames / dropped frames: %lu / %lu\n",
                (unsigned long)audioStats.ndspFrames, (unsigned long)audioStats.ndspDroppedFrames);

        fprintf(info, "\n[Save storage]\n");
        fprintf(info, "Active path: %s\n", saveStats.activePath);
        fprintf(info, "Initialized/dirty/transaction depth: %s / %s / %ld\n",
                saveStats.initialized ? "yes" : "no", saveStats.dirty ? "yes" : "no",
                (long)saveStats.transactionDepth);
        fprintf(info, "Flush attempts / successes / failures: %llu / %llu / %llu\n",
                (unsigned long long)saveStats.flushAttempts, (unsigned long long)saveStats.flushSuccesses,
                (unsigned long long)saveStats.flushFailures);
        fprintf(info, "Interrupted recoveries / rollback restores / rollback failures: %llu / %llu / %llu\n",
                (unsigned long long)saveStats.interruptedRecoveries,
                (unsigned long long)saveStats.rollbackRestores,
                (unsigned long long)saveStats.rollbackFailures);
        fprintf(info, "Last persistence stage / errno: %s / %ld (%s)\n",
                Port_Save_StageName(saveStats.lastStage), (long)saveStats.lastErrno,
                saveStats.lastErrno ? strerror(saveStats.lastErrno) : "none");

        fprintf(info, "\n[Game and GBA PPU]\n");
        fprintf(info, "Task/state/substate/sleep: %u/%u/%u/%u\n",
                gMain.task, gMain.state, gMain.substate, gMain.sleepStatus);
        fprintf(info, "Subtask state: next/load=%u/%u, pause origin=%u, menu=%u/%u/%u\n",
                gUI.nextToLoad, gUI.state, gUI.pauseFadeIn, gMenu.menuType,
                gMenu.overlayType, gMenu.transitionTimer);
        fprintf(info, "Game ticks: %u; pause frames/count/interval: %u/%u/%u\n",
                gMain.ticks, gMain.pauseFrames, gMain.pauseCount, gMain.pauseInterval);
        fprintf(info, "DISPCNT: 0x%04X; display mode: %u\n", dumpDispcnt, dumpMode);
        fprintf(info, "VRAM/EWRAM/IWRAM bytes: %lu/%lu/%lu\n",
                (unsigned long)sizeof(gVram), (unsigned long)sizeof(gEwram), (unsigned long)sizeof(gIwram));
        fprintf(info, "Native pointer / MapLayer bytes: %lu/%lu\n",
                (unsigned long)sizeof(void*), (unsigned long)sizeof(MapLayer));
        fprintf(info, "MapLayer native offsets: map=0x%lX collision=0x%lX original=0x%lX "
                      "types=0x%lX indices=0x%lX subtiles=0x%lX act=0x%lX\n",
                (unsigned long)offsetof(MapLayer, mapData), (unsigned long)offsetof(MapLayer, collisionData),
                (unsigned long)offsetof(MapLayer, mapDataOriginal), (unsigned long)offsetof(MapLayer, tileTypes),
                (unsigned long)offsetof(MapLayer, tileIndices), (unsigned long)offsetof(MapLayer, subTiles),
                (unsigned long)offsetof(MapLayer, actTiles));
        fprintf(info, "Room area/id/origin/size/scroll: 0x%02X/0x%02X, %d,%d, %u,%u, %d,%d\n",
                gRoomControls.area, gRoomControls.room, gRoomControls.origin_x, gRoomControls.origin_y,
                gRoomControls.width, gRoomControls.height, gRoomControls.scroll_x, gRoomControls.scroll_y);
        fprintf(info, "Player position/z: %d,%d,%d; action/subaction: %u/%u; direction/animation: %u/%u\n",
                gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, gPlayerEntity.base.z.HALF.HI,
                gPlayerEntity.base.action, gPlayerEntity.base.subAction, gPlayerEntity.base.direction,
                gPlayerEntity.base.animationState);
        fprintf(info, "Player control/framestate/layer/draw/flags: %u/%u/%u/%u/0x%02X\n",
                gPlayerState.controlMode, gPlayerState.framestate, gPlayerEntity.base.collisionLayer,
                gPlayerEntity.base.spriteSettings.draw, gPlayerEntity.base.flags);
        fprintf(info, "Camera target/player pointers: 0x%08lX / 0x%08lX\n",
                (unsigned long)(uintptr_t)gRoomControls.camera_target,
                (unsigned long)(uintptr_t)&gPlayerEntity.base);
        fprintf(info, "Pending transition: out=%u destination=0x%02X/0x%02X position=%d,%d\n",
                gRoomTransition.transitioningOut, gRoomTransition.player_status.area_next,
                gRoomTransition.player_status.room_next, gRoomTransition.player_status.start_pos_x,
                gRoomTransition.player_status.start_pos_y);
        fprintf(info, "Map BG controls: bottom=0x%04X top=0x%04X\n",
                gMapBottom.bgSettings ? gMapBottom.bgSettings->control : 0,
                gMapTop.bgSettings ? gMapTop.bgSettings->control : 0);
        fprintf(info, "Top rendered width: %d pixels (capacity %d; widescreen %s)\n",
                sTopPresentWidth, MODE1_GBA_WIDTH,
                Port_Config_WidescreenEnabled() ? "enabled" : "disabled");

        fprintf(info, "\n[Files]\n");
        fprintf(info, "top-screen.bmp, bottom-screen.bmp, top-screen.raw, bottom-screen.raw\n");
        fprintf(info, "ewram.bin, iwram.bin, vram.bin, io-registers.bin, palettes.bin, oam.bin, main-state.bin\n");
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
    Port_SecondScreen_3DS_PaintInto(sBottomUploads[sBottomWorkerBuffer], 320, 240, 512,
                                    &sBottomWorkerSnapshot, sBottomWorkerTick);
    sBottomWorkerLastTicks = Platform3DS_SystemTick() - startTick;
}

static void RecordBottomWorkerTiming(void) {
    if (sFrameNumber < 120u) return;
    ++sPerfBottomSamples;
    sPerfBottomTicks += sBottomWorkerLastTicks;
    if (sBottomWorkerLastTicks > sPerfBottomMaxTicks) sPerfBottomMaxTicks = sBottomWorkerLastTicks;
}

void Port_PPU_Init(SDL_Window* window) {
    (void)window;
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    sColorCorrection = Port_Config_GetColorCorrection();
    virtuappu_mode1_set_color_correction(sColorCorrection);
    Port_Widescreen_SetWindowPixels(400, 240);
    virtuappu_registers.frame_width = GBA_NATIVE_W;
    virtuappu_registers.frame_pitch = TOP_PITCH;
    virtuappu_registers.mode = 1;
    Port_SecondScreen_Init();
    sBottomReady = false;
    sBottomTextureReady = false;
    sBottomTextureDirty = false;
    sBottomWorkerPending = false;
    sBottomFrontBuffer = 0;
    sBottomWorkerBuffer = 1;
    sPendingBottomInGame = 0;
    sBottomTick = 0;
    sFrameNumber = 0;
    sPerfFirstFrameTick = 0;
    sPerfLastFrameTick = 0;
    sPerfRenderTicks = 0;
    sPerfTopTicks = 0;
    sPerfBottomTicks = 0;
    sPerfTotalTicks = 0;
    sPerfSamples = 0;
    sPerfBottomSamples = 0;
    sPerfRenderMaxTicks = 0;
    sPerfTopMaxTicks = 0;
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
    sGpuPresenterReady = PlatformGpu3DS_Init();
    sTopUpload = PlatformGpu3DS_TopBuffer();
    sBottomUploads[0] = PlatformGpu3DS_BottomBuffer(0);
    sBottomUploads[1] = PlatformGpu3DS_BottomBuffer(1);
    sInitialized = sGpuPresenterReady && sTopUpload && sBottomUploads[0] && sBottomUploads[1];
    virtuappu_mode1_set_output_buffer(sInitialized ? sTopUpload : NULL, TOP_PITCH);
}

void Port_PPU_PresentFrame(void) {
    if (!sInitialized) return;
    ++sFrameNumber;
    const uint64_t frameStartTick = Platform3DS_SystemTick();

#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t frameStart = Platform3DS_Milliseconds();
#endif
    const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
    const uint8_t mode = (uint8_t)(dispcnt & 7);
    virtuappu_registers.mode = (mode == 1 || mode == 2) ? 2 : 1;
    sTopPresentWidth = TopFrameWidth();
    virtuappu_registers.frame_width = sTopPresentWidth;
    virtuappu_registers.frame_pitch = TOP_PITCH;
    virtuappu_mode1_pre_line_callback = port_hdma_has_active_channels() ? port_hdma_step_line : NULL;
    virtuappu_mode1_bg2x_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x28, gIoMem + 0x2c) != 0;
    virtuappu_mode1_bg2y_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x2c, gIoMem + 0x30) != 0;

    virtuappu_render_frame();
    const uint64_t renderEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t renderEnd = Platform3DS_Milliseconds();

    const unsigned diagnosticFrame = sDiagnosticFrames++;
    if (diagnosticFrame == 0) sDiagnosticStartMs = frameStart;
    if (diagnosticFrame < 3 || diagnosticFrame == 60 || diagnosticFrame == 180 || diagnosticFrame == 360) {
        char message[256];
        snprintf(message, sizeof(message),
                 "[tmc3ds] frame=%u dispcnt=%04x io=%02x%02x vram=%02x%02x%02x%02x pal=%04x,%04x out=%08lx,%08lx\n",
                 diagnosticFrame, dispcnt, gIoMem[0], gIoMem[1], gVram[0], gVram[1], gVram[2], gVram[3],
                 gBgPltt[0], gBgPltt[1], (unsigned long)sTopUpload[0],
                 (unsigned long)sTopUpload[1]);
        Platform3DS_Debug(message);
    }
    if (diagnosticFrame == 2) DumpPpuSnapshot("tmc3ds-frame2.ppu1");
    if (diagnosticFrame == 60) DumpPpuSnapshot("tmc3ds-frame60.ppu1");
#endif

    PlatformGpu3DS_BeginTop(sTopUpload, (unsigned)sTopPresentWidth);
    const uint64_t topEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t topEnd = Platform3DS_Milliseconds();
#endif

    bool bottomChanged = false;
    int promotedBottomInGame = -1;
    if (sBottomWorkerPending && Platform3DS_TryFinishBottomWorker()) {
        sBottomFrontBuffer = sBottomWorkerBuffer;
        sBottomWorkerPending = false;
        sBottomReady = true;
        bottomChanged = true;
        promotedBottomInGame = sBottomWorkerSnapshot.inGame != 0;
        RecordBottomWorkerTiming();
    }

    const uint32_t bottomInterval = Port_SecondScreen_IsDeveloperOverlayOpen() ? 30u : 3u;
    const bool bottomUpdateDue = !sBottomReady || Port_SecondScreen_3DS_NeedsRefresh() ||
                                 (sFrameNumber % bottomInterval) == 0u;
    if (!sBottomWorkerPending && bottomUpdateDue) {
        sBottomWorkerBuffer = 1 - sBottomFrontBuffer;
        Port_SecondScreenState_Read(&sBottomWorkerSnapshot);
        sBottomWorkerTick = sBottomTick++;
        if (Platform3DS_SubmitBottomWorker()) {
            sBottomWorkerPending = true;
        } else {
            Port_PPU_3DS_RenderBottomWorker();
            sBottomFrontBuffer = sBottomWorkerBuffer;
            sBottomReady = true;
            bottomChanged = true;
            promotedBottomInGame = sBottomWorkerSnapshot.inGame != 0;
            RecordBottomWorkerTiming();
        }
    }
    if (!sBottomTextureReady) {
        bottomChanged = true;
        sBottomTextureReady = true;
    }
    if (promotedBottomInGame >= 0) {
        sPendingBottomInGame = promotedBottomInGame;
    }
    sBottomTextureDirty = sBottomTextureDirty || bottomChanged;
    if (PlatformGpu3DS_EndBottom(sBottomUploads[sBottomFrontBuffer], sBottomTextureDirty) &&
        sBottomTextureDirty) {
        /* A failed C3D_FrameBegin leaves the old physical texture visible.
         * Keep both the dirty upload and its mode pending until EndBottom
         * actually submits the frame; only then may touch target that mode. */
        sBottomTextureDirty = false;
        Port_SecondScreen_3DS_SetVisibleInGame(sPendingBottomInGame);
    }
    const uint64_t frameEndTick = Platform3DS_SystemTick();

    if (sFrameNumber >= 120u) {
        const uint64_t renderTicks = renderEndTick - frameStartTick;
        const uint64_t topTicks = topEndTick - renderEndTick;
        const uint64_t totalTicks = frameEndTick - frameStartTick;
        if (sPerfSamples == 0) sPerfFirstFrameTick = frameStartTick;
        if (sPerfLastFrameTick != 0) {
            const uint64_t intervalTicks = frameStartTick - sPerfLastFrameTick;
            sPerfIntervalLastTicks = intervalTicks;
            sPerfIntervalTicks += intervalTicks;
            ++sPerfIntervalSamples;
            if (intervalTicks < sPerfIntervalMinTicks) sPerfIntervalMinTicks = intervalTicks;
            if (intervalTicks > sPerfIntervalMaxTicks) sPerfIntervalMaxTicks = intervalTicks;
            const uint64_t ticksPerSecond = Platform3DS_TicksPerSecond();
            __atomic_store_n(&sCurrentFpsX100, (uint32_t)(ticksPerSecond * 100u / intervalTicks),
                             __ATOMIC_RELAXED);
            if (intervalTicks * 60u > ticksPerSecond) ++sPerfFramesOver16ms;
            if (intervalTicks * 30u > ticksPerSecond) ++sPerfFramesOver33ms;
        }
        sPerfLastFrameTick = frameStartTick;
        ++sPerfSamples;
        sPerfRenderTicks += renderTicks;
        sPerfTopTicks += topTicks;
        sPerfTotalTicks += totalTicks;
        if (renderTicks > sPerfRenderMaxTicks) sPerfRenderMaxTicks = renderTicks;
        if (topTicks > sPerfTopMaxTicks) sPerfTopMaxTicks = topTicks;
        if (totalTicks > sPerfTotalMaxTicks) sPerfTotalMaxTicks = totalTicks;
        if (sPerfIntervalTicks != 0) {
            uint64_t average = Platform3DS_TicksPerSecond() * sPerfIntervalSamples * 100u / sPerfIntervalTicks;
            __atomic_store_n(&sAverageFpsX100, (uint32_t)average, __ATOMIC_RELAXED);
        }
    }
#ifdef TMC_3DS_DIAGNOSTICS
    if (diagnosticFrame < 8 || diagnosticFrame == 60) {
        char message[160];
        const uint64_t frameEnd = Platform3DS_Milliseconds();
        snprintf(message, sizeof(message), "[tmc3ds] timing frame=%u render=%llums top=%llums bottom=%llums total=%llums\n",
                 diagnosticFrame, (unsigned long long)(renderEnd - frameStart),
                 (unsigned long long)(topEnd - renderEnd), (unsigned long long)(frameEnd - topEnd),
                 (unsigned long long)(frameEnd - frameStart));
        Platform3DS_Debug(message);
        if (diagnosticFrame == 60) {
            snprintf(message, sizeof(message), "[tmc3ds] cadence 60_frames=%llums\n",
                     (unsigned long long)(frameEnd - sDiagnosticStartMs));
            Platform3DS_Debug(message);
        }
    }
#endif
}

void Port_PPU_SetPresentIsFirstOfTick(bool first) { (void)first; }
double Port_PPU_3DS_CurrentFps(void) {
    return (double)__atomic_load_n(&sCurrentFpsX100, __ATOMIC_RELAXED) / 100.0;
}
double Port_PPU_3DS_AverageFps(void) {
    return (double)__atomic_load_n(&sAverageFpsX100, __ATOMIC_RELAXED) / 100.0;
}
void Port_PPU_SetWindowTitle(const char* title) { (void)title; }
void Port_PPU_ToggleFullscreen(void) {}
bool Port_PPU_IsFullscreen(void) { return true; }
void Port_PPU_CycleWindowScale(int direction) { (void)direction; }
unsigned char Port_PPU_WindowScale(void) { return 1; }
void Port_PPU_ApplyWindowScale(void) {}
void Port_PPU_ToggleSmoothing(void) {}
void Port_PPU_CyclePresentationMode(int direction) { (void)direction; }
const char* Port_PPU_PresentationModeName(void) { return "3DS widescreen"; }
void Port_PPU_CycleFilter(int direction) { (void)direction; }
const char* Port_PPU_FilterName(void) { return "Off"; }
void Port_PPU_SetVSync(bool enabled) { (void)enabled; }
bool Port_PPU_VSyncEnabled(void) { return true; }
unsigned Port_PPU_DisplayRefreshRate(void) { return 60; }
void Port_PPU_SetColorCorrection(bool enabled) {
    sColorCorrection = enabled;
    virtuappu_mode1_set_color_correction(enabled);
}
bool Port_PPU_ColorCorrectionEnabled(void) { return sColorCorrection; }
void Port_PPU_SetPersistence(bool enabled, float rho) { (void)enabled; (void)rho; }
bool Port_PPU_3DS_UsesGpuPresenter(void) { return sGpuPresenterReady; }
void Port_PPU_Shutdown(void) {
    sInitialized = false;
    Platform3DS_ShutdownBottomWorker();
    virtuappu_mode1_set_output_buffer(NULL, 0);
    if (!sGpuPresenterReady) return;
    PlatformGpu3DS_Shutdown();
    sGpuPresenterReady = false;
}
void Port_OpenInGameSettingsModal(void) {}
bool Port_InGameSettingsModalIsOpen(void) { return false; }
