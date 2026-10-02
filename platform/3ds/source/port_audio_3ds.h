#ifndef TMC_PORT_AUDIO_3DS_H
#define TMC_PORT_AUDIO_3DS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PortAudio3DSStats {
    uint64_t pumps;
    uint64_t buffersRendered;
    uint64_t underrunObservations;
    uint64_t workerWakeups;
    uint64_t workerRequeues;
    uint64_t callbackSignals;
    uint64_t queueRecoveries;
    uint64_t fallbackRenders;
    uint64_t renderTicks;
    uint64_t renderLastTicks;
    uint64_t renderMaxTicks;
    uint64_t renderDeadlineMisses;
    uint64_t multiBufferWakeups;
    uint32_t sampleRate;
    uint32_t bufferFrames;
    uint32_t bufferCount;
    uint32_t samplePosition;
    uint32_t freeBuffers;
    uint32_t queuedBuffers;
    uint32_t playingBuffers;
    uint32_t doneBuffers;
    uint32_t maxBuffersPerWake;
    uint32_t maxDoneBuffersObserved;
    uint32_t ndspFrames;
    uint32_t ndspDroppedFrames;
    int32_t threadPriority;
    int32_t threadCore;
    bool initialized;
    bool channelPlaying;
    bool audioThreadRunning;
    bool paused;
} PortAudio3DSStats;

void Port_Audio_3DSPump(void);
void Port_Audio_3DSGetStats(PortAudio3DSStats* stats);
void Port_Audio_3DSSetPaused(bool paused);

#endif
