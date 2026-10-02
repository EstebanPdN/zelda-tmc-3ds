#ifndef TMC_OLD3DS_FRAME_PACER_H
#define TMC_OLD3DS_FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum Old3DSFramePacerDiscontinuity {
    OLD3DS_FRAME_PACER_DISCONTINUITY_NONE = 0,
    OLD3DS_FRAME_PACER_DISCONTINUITY_APT = 1u << 0,
    OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP = 1u << 1,
} Old3DSFramePacerDiscontinuity;

/*
 * Old 3DS rendering can take more than one 60 Hz frame period.  Keep that
 * wall-clock overrun as presentation debt so a later game tick may reuse the
 * previous picture instead of slowing the game simulation down with it.
 *
 * This state machine contains no libctru calls, which keeps the scheduling
 * policy deterministic and host-testable.  The platform layer performs the
 * optional sleep returned for a skipped presentation.
 */
typedef struct Old3DSFramePacer {
    uint64_t framePeriodTicks;
    uint64_t skipThresholdTicks;
    uint64_t lastBoundaryTick;
    int64_t presentationDebtTicks;
    uint32_t consecutiveSkips;
    uint32_t maxConsecutiveSkipsSeen;
    uint64_t skippedPresentations;
    uint64_t sleepRequests;
    uint64_t requestedSleepTicks;
    uint64_t activeElapsedTicks;
    uint64_t activeIntervals;
    uint64_t resyncs;
    uint64_t aptDiscontinuities;
    uint64_t dumpDiscontinuities;
    uint64_t debtClampEvents;
    uint32_t pendingDiscontinuities;
    bool initialized;
} Old3DSFramePacer;

void Old3DSFramePacer_Init(Old3DSFramePacer* pacer, uint64_t ticksPerSecond);
/* Thread-safe event mark. The next boundary consumes all pending reasons as
 * one resync, excludes that interval from cadence, and clears stale debt. */
void Old3DSFramePacer_MarkDiscontinuity(Old3DSFramePacer* pacer,
                                       Old3DSFramePacerDiscontinuity reason);
/* Returns false only for an invalid pacer or when this interval consumed an
 * explicit discontinuity; elapsed duration alone never implies a pause. */
bool Old3DSFramePacer_RecordActiveInterval(Old3DSFramePacer* pacer, uint64_t elapsedTicks);
bool Old3DSFramePacer_BeginTick(Old3DSFramePacer* pacer, uint64_t nowTick,
                               uint64_t* skippedTickSleep);

#endif
