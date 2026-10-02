#include "old3ds_frame_pacer.h"

#include <limits.h>
#include <string.h>

enum {
    OLD3DS_TARGET_HZ = 60,
    OLD3DS_MAX_CONSECUTIVE_SKIPS = 3,
    OLD3DS_MAX_DEBT_PERIODS = 4,
};

static void IncrementSaturating(uint64_t* value) {
    if (*value != UINT64_MAX) ++*value;
}

static bool ConsumeDiscontinuity(Old3DSFramePacer* pacer) {
    const uint32_t reasons = __atomic_exchange_n(&pacer->pendingDiscontinuities, 0u,
                                                  __ATOMIC_ACQ_REL);
    if (reasons == 0u) return false;

    pacer->presentationDebtTicks = 0;
    pacer->consecutiveSkips = 0;
    IncrementSaturating(&pacer->resyncs);
    if ((reasons & OLD3DS_FRAME_PACER_DISCONTINUITY_APT) != 0u) {
        IncrementSaturating(&pacer->aptDiscontinuities);
    }
    if ((reasons & OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP) != 0u) {
        IncrementSaturating(&pacer->dumpDiscontinuities);
    }
    return true;
}

void Old3DSFramePacer_Init(Old3DSFramePacer* pacer, uint64_t ticksPerSecond) {
    if (!pacer) return;
    memset(pacer, 0, sizeof(*pacer));
    pacer->framePeriodTicks = ticksPerSecond / OLD3DS_TARGET_HZ;
    if (pacer->framePeriodTicks == 0) pacer->framePeriodTicks = 1;
    /* A whole missed frame is presentation debt.  The small tolerance catches
     * a nominal two-VBlank frame despite timer quantization without turning a
     * minor VBlank phase difference into constant skipping. */
    pacer->skipThresholdTicks = pacer->framePeriodTicks - pacer->framePeriodTicks / 10u;
}

void Old3DSFramePacer_MarkDiscontinuity(Old3DSFramePacer* pacer,
                                       Old3DSFramePacerDiscontinuity reason) {
    if (!pacer) return;
    const uint32_t knownReasons = (uint32_t)reason &
                                  (OLD3DS_FRAME_PACER_DISCONTINUITY_APT |
                                   OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    if (knownReasons == 0u) return;
    __atomic_fetch_or(&pacer->pendingDiscontinuities, knownReasons, __ATOMIC_RELEASE);
}

bool Old3DSFramePacer_RecordActiveInterval(Old3DSFramePacer* pacer, uint64_t elapsedTicks) {
    if (!pacer || pacer->framePeriodTicks == 0) return false;
    if (ConsumeDiscontinuity(pacer)) return false;

    /* Long intervals are real engine time unless an APT hook or the quick-dump
     * path marked an explicit discontinuity.  In particular, a slow 100+ ms
     * render must remain visible to both the cadence diagnostic and scheduler. */
    if (elapsedTicks > UINT64_MAX - pacer->activeElapsedTicks) {
        pacer->activeElapsedTicks = UINT64_MAX;
    } else {
        pacer->activeElapsedTicks += elapsedTicks;
    }
    IncrementSaturating(&pacer->activeIntervals);
    return true;
}

bool Old3DSFramePacer_BeginTick(Old3DSFramePacer* pacer, uint64_t nowTick,
                               uint64_t* skippedTickSleep) {
    if (skippedTickSleep) *skippedTickSleep = 0;
    if (!pacer || pacer->framePeriodTicks == 0) return true;

    if (!pacer->initialized) {
        pacer->initialized = true;
        pacer->lastBoundaryTick = nowTick;
        ConsumeDiscontinuity(pacer);
        return true;
    }

    const uint64_t elapsed = nowTick - pacer->lastBoundaryTick;
    pacer->lastBoundaryTick = nowTick;
    if (!Old3DSFramePacer_RecordActiveInterval(pacer, elapsed)) {
        /* Explicit APT and quick-dump pauses are not game time to replay. */
        return true;
    }

    /* Keep the cadence diagnostic on the scheduler's explicitly active
     * window. Counting an APT/dump pause would make a healthy loop look slow. */
    int64_t delta;
    if (elapsed >= pacer->framePeriodTicks) {
        const uint64_t positive = elapsed - pacer->framePeriodTicks;
        delta = positive > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)positive;
    } else {
        delta = -(int64_t)(pacer->framePeriodTicks - elapsed);
    }
    const int64_t maximumDebt = (int64_t)(pacer->framePeriodTicks * OLD3DS_MAX_DEBT_PERIODS);
    if (delta > 0 && delta > maximumDebt - pacer->presentationDebtTicks) {
        pacer->presentationDebtTicks = maximumDebt;
        IncrementSaturating(&pacer->debtClampEvents);
    } else {
        pacer->presentationDebtTicks += delta;
    }

    /* Do not let an unusually cheap tick create a long-term speed credit. */
    const int64_t minimumDebt = -(int64_t)pacer->framePeriodTicks;
    if (pacer->presentationDebtTicks < minimumDebt) pacer->presentationDebtTicks = minimumDebt;

    if (pacer->presentationDebtTicks < (int64_t)pacer->skipThresholdTicks ||
        pacer->consecutiveSkips >= OLD3DS_MAX_CONSECUTIVE_SKIPS) {
        pacer->consecutiveSkips = 0;
        return true;
    }

    ++pacer->consecutiveSkips;
    ++pacer->skippedPresentations;
    if (pacer->consecutiveSkips > pacer->maxConsecutiveSkipsSeen) {
        pacer->maxConsecutiveSkipsSeen = pacer->consecutiveSkips;
    }

    /* If the overrun is slightly under a full tick, sleep only the remainder.
     * This prevents a skip from making the simulation run faster than 60 Hz.
     * BeginTick may reach this branch only at >=90% debt, so this compensation
     * is bounded to <=10% of one period (~1.67 ms at the 60 Hz target). */
    if (pacer->presentationDebtTicks < (int64_t)pacer->framePeriodTicks) {
        const uint64_t sleepTicks =
            pacer->framePeriodTicks - (uint64_t)pacer->presentationDebtTicks;
        if (skippedTickSleep) *skippedTickSleep = sleepTicks;
        if (sleepTicks != 0) {
            ++pacer->sleepRequests;
            pacer->requestedSleepTicks += sleepTicks;
        }
    }
    return false;
}
