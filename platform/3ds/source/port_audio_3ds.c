#include "port_audio.h"
#include "port_audio_3ds.h"
#include "port_m4a_backend.h"

#include <3ds.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* MP2K mode 5 mixes PCM at 15.768 kHz. A nearby DSP-friendly rate keeps the
 * native mixer workload bounded while NDSP performs final interpolation. */
#define SAMPLE_RATE 16364
#define BUFFER_FRAMES 256
#define BUFFER_COUNT 4
#define AUDIO_THREAD_STACK (64u * 1024u)
#define AUDIO_THREAD_CORE 0
#define AUDIO_RECOVERY_PAUSE_NS 500000LL

static ndspWaveBuf sWave[BUFFER_COUNT];
static int16_t* sSamples;
static bool sInitialized;
static bool sPaused;
static volatile bool sAudioThreadRunning;
static Thread sAudioThread;
static LightEvent sAudioWake;
static LightLock sStatsLock;
static uint32_t sCallbackSignals;
/* The bottom-screen worker reads the live volume label while taps on the main
 * thread update the mixer. A word-sized atomic prevents a C data race without
 * putting the real-time audio path behind an SD/configuration lock. */
static _Atomic float sVolume = 1.0f;
static PortAudio3DSStats sStats;

extern float Port_Config_GetMasterVolume(void);

static void FillBuffer(int index) {
    const uint64_t startTick = svcGetSystemTick();
    int16_t* dst = sSamples + index * BUFFER_FRAMES * 2;
    Port_M4A_Backend_Render(dst, BUFFER_FRAMES, false);
    DSP_FlushDataCache(dst, BUFFER_FRAMES * 2 * sizeof(int16_t));
    sWave[index].data_pcm16 = dst;
    sWave[index].nsamples = BUFFER_FRAMES;
    sWave[index].status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(0, &sWave[index]);
    const uint64_t elapsed = svcGetSystemTick() - startTick;
    LightLock_Lock(&sStatsLock);
    ++sStats.buffersRendered;
    sStats.renderTicks += elapsed;
    sStats.renderLastTicks = elapsed;
    if (elapsed > sStats.renderMaxTicks) sStats.renderMaxTicks = elapsed;
    LightLock_Unlock(&sStatsLock);
}

static uint32_t CountDoneBuffers(void) {
    uint32_t done = 0;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        if (sWave[i].status == NDSP_WBUF_DONE) ++done;
    }
    return done;
}

static int FindDoneBuffer(void) {
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        if (sWave[i].status == NDSP_WBUF_DONE) return i;
    }
    return -1;
}

static void AudioFrameFinished(void* data) {
    (void)data;
    if (CountDoneBuffers() == 0) return;
    __atomic_add_fetch(&sCallbackSignals, 1u, __ATOMIC_RELAXED);
    LightEvent_Signal(&sAudioWake);
}

static void AudioThreadMain(void* argument) {
    (void)argument;
    while (__atomic_load_n(&sAudioThreadRunning, __ATOMIC_ACQUIRE)) {
        LightEvent_Wait(&sAudioWake);
        if (!__atomic_load_n(&sAudioThreadRunning, __ATOMIC_ACQUIRE)) break;

        const uint32_t backlogBefore = CountDoneBuffers();
        const int bufferIndex = FindDoneBuffer();
        const uint32_t refillCount = bufferIndex >= 0 ? 1u : 0u;
        if (bufferIndex >= 0) FillBuffer(bufferIndex);
        const uint32_t backlogAfter = CountDoneBuffers();

        LightLock_Lock(&sStatsLock);
        ++sStats.workerWakeups;
        if (refillCount > sStats.maxBuffersPerWake) sStats.maxBuffersPerWake = refillCount;
        if (backlogBefore > sStats.maxDoneBuffersObserved) sStats.maxDoneBuffersObserved = backlogBefore;
        if (backlogAfter > 0) ++sStats.workerRequeues;
        LightLock_Unlock(&sStatsLock);

        if (backlogAfter > 0) {
            svcSleepThread(AUDIO_RECOVERY_PAUSE_NS);
            LightEvent_Signal(&sAudioWake);
        }
    }
}

bool Port_Audio_Init(void) {
    if (sInitialized) return true;
    if (ndspInit() != 0) return false;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { 1.0f, 0.0f, 0.0f, 1.0f };
    ndspChnSetMix(0, mix);

    sSamples = (int16_t*)linearAlloc(BUFFER_COUNT * BUFFER_FRAMES * 2 * sizeof(int16_t));
    if (!sSamples || !Port_M4A_Backend_Init(SAMPLE_RATE)) {
        if (sSamples) linearFree(sSamples);
        sSamples = NULL;
        ndspExit();
        return false;
    }
    sVolume = fmaxf(0.0f, fminf(1.0f, Port_Config_GetMasterVolume()));
    Port_M4A_Backend_SetMasterVolume(sVolume);
    memset(sWave, 0, sizeof(sWave));
    memset(sSamples, 0, BUFFER_COUNT * BUFFER_FRAMES * 2 * sizeof(int16_t));
    LightLock_Init(&sStatsLock);
    LightEvent_Init(&sAudioWake, RESET_ONESHOT);
    memset(&sStats, 0, sizeof(sStats));
    sStats.sampleRate = SAMPLE_RATE;
    sStats.bufferFrames = BUFFER_FRAMES;
    sStats.bufferCount = BUFFER_COUNT;
    sStats.threadCore = AUDIO_THREAD_CORE;
    sStats.initialized = true;
    sCallbackSignals = 0;
    sInitialized = true;
    sPaused = false;
    ndspSetCallback(AudioFrameFinished, NULL);
    for (int i = 0; i < BUFFER_COUNT; ++i) FillBuffer(i);

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (priority > 0x18) --priority;
    sStats.threadPriority = priority;
    __atomic_store_n(&sAudioThreadRunning, true, __ATOMIC_RELEASE);
    sAudioThread = threadCreate(AudioThreadMain, NULL, AUDIO_THREAD_STACK, priority, AUDIO_THREAD_CORE, false);
    if (!sAudioThread) {
        __atomic_store_n(&sAudioThreadRunning, false, __ATOMIC_RELEASE);
    }
    return true;
}

void Port_Audio_3DSPump(void) {
    if (!sInitialized) return;
    if (sPaused) return;
    const bool playing = ndspChnIsPlaying(0);
    const uint32_t doneBuffers = CountDoneBuffers();
    const bool callbackMissed = doneBuffers > 0;

    LightLock_Lock(&sStatsLock);
    ++sStats.pumps;
    if (!playing) ++sStats.underrunObservations;
    if (callbackMissed) ++sStats.queueRecoveries;
    if (doneBuffers > sStats.maxDoneBuffersObserved) sStats.maxDoneBuffersObserved = doneBuffers;
    LightLock_Unlock(&sStatsLock);

    if (callbackMissed) LightEvent_Signal(&sAudioWake);

    if (!sAudioThread) {
        const int bufferIndex = FindDoneBuffer();
        const uint32_t refillCount = bufferIndex >= 0 ? 1u : 0u;
        if (bufferIndex >= 0) FillBuffer(bufferIndex);
        LightLock_Lock(&sStatsLock);
        sStats.fallbackRenders += refillCount;
        if (refillCount > sStats.maxBuffersPerWake) sStats.maxBuffersPerWake = refillCount;
        LightLock_Unlock(&sStatsLock);
    }
}

void Port_Audio_3DSGetStats(PortAudio3DSStats* stats) {
    if (!stats) return;
    if (!sInitialized) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    LightLock_Lock(&sStatsLock);
    *stats = sStats;
    LightLock_Unlock(&sStatsLock);
    stats->initialized = true;
    stats->samplePosition = ndspChnGetSamplePos(0);
    stats->channelPlaying = ndspChnIsPlaying(0);
    stats->audioThreadRunning = __atomic_load_n(&sAudioThreadRunning, __ATOMIC_ACQUIRE);
    stats->paused = sPaused;
    stats->callbackSignals = __atomic_load_n(&sCallbackSignals, __ATOMIC_RELAXED);
    stats->ndspFrames = ndspGetFrameCount();
    stats->ndspDroppedFrames = ndspGetDroppedFrames();
    stats->freeBuffers = 0;
    stats->queuedBuffers = 0;
    stats->playingBuffers = 0;
    stats->doneBuffers = 0;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        switch (sWave[i].status) {
            case NDSP_WBUF_FREE: ++stats->freeBuffers; break;
            case NDSP_WBUF_QUEUED: ++stats->queuedBuffers; break;
            case NDSP_WBUF_PLAYING: ++stats->playingBuffers; break;
            case NDSP_WBUF_DONE: ++stats->doneBuffers; break;
        }
    }
}

void Port_Audio_Shutdown(void) {
    if (!sInitialized) return;
    ndspSetCallback(NULL, NULL);
    __atomic_store_n(&sAudioThreadRunning, false, __ATOMIC_RELEASE);
    LightEvent_Signal(&sAudioWake);
    if (sAudioThread) {
        threadJoin(sAudioThread, U64_MAX);
        threadFree(sAudioThread);
        sAudioThread = NULL;
    }
    ndspChnWaveBufClear(0);
    Port_M4A_Backend_Shutdown();
    linearFree(sSamples);
    sSamples = NULL;
    ndspExit();
    sInitialized = false;
    sPaused = false;
    sStats.initialized = false;
}

void Port_Audio_3DSSetPaused(bool paused) {
    if (!sInitialized || sPaused == paused) return;
    sPaused = paused;
    ndspChnSetPaused(0, paused);
    if (!paused) LightEvent_Signal(&sAudioWake);
}

void Port_Audio_Reset(void) { Port_M4A_Backend_Reset(); }
void Port_Audio_SetGbaAccurate(bool accurate) { Port_M4A_Backend_SetGbaAccurate(accurate); }
bool Port_Audio_IsGbaAccurate(void) { return Port_M4A_Backend_GetGbaAccurate(); }
void Port_Audio_SetWidth(float width) { (void)width; }
float Port_Audio_GetWidth(void) { return 1.0f; }
void Port_Audio_SetReverbLevel(int level) { Port_M4A_Backend_SetReverbLevel(level); }
int Port_Audio_GetReverbLevel(void) { return Port_M4A_Backend_GetReverbLevel(); }
void Port_Audio_SetMasterVolume(float volume) {
    sVolume = fmaxf(0.0f, fminf(1.0f, volume));
    Port_M4A_Backend_SetMasterVolume(sVolume);
}
float Port_Audio_GetMasterVolume(void) { return sVolume; }
