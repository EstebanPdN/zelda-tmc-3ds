#include "old3ds_frame_pacer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void TestFullRateNeverSkips(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep = 99;
    for (uint64_t now = 0; now <= 2000; now += 100) {
        assert(Old3DSFramePacer_BeginTick(&pacer, now, &sleep));
        assert(sleep == 0);
    }
    assert(pacer.skippedPresentations == 0);
    assert(pacer.activeIntervals == 20);
    assert(pacer.activeElapsedTicks == 2000);
    assert(pacer.resyncs == 0);
}

static void TestThirtyFpsAlternatesPresentation(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t now = 0;
    uint64_t sleep;
    unsigned presents = 0;
    unsigned skips = 0;

    for (unsigned i = 0; i < 12; ++i) {
        const bool present = Old3DSFramePacer_BeginTick(&pacer, now, &sleep);
        presents += present ? 1u : 0u;
        skips += present ? 0u : 1u;
        now += present ? 200u : sleep;
    }
    assert(presents == 6);
    assert(skips == 6);
    assert(pacer.maxConsecutiveSkipsSeen == 1);
}

static void TestFortyFpsKeepsMorePictures(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t now = 0;
    uint64_t sleep;
    unsigned presents = 0;
    unsigned skips = 0;

    for (unsigned i = 0; i < 15; ++i) {
        const bool present = Old3DSFramePacer_BeginTick(&pacer, now, &sleep);
        presents += present ? 1u : 0u;
        skips += present ? 0u : 1u;
        now += present ? 150u : sleep;
    }
    assert(presents == 10);
    assert(skips == 5);
}

static void TestThreeSkipCap(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;

    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 600, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 600, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 600, &sleep));
    assert(Old3DSFramePacer_BeginTick(&pacer, 600, &sleep));
    assert(pacer.maxConsecutiveSkipsSeen == 3);
    assert(pacer.skippedPresentations == 3);
    assert(pacer.presentationDebtTicks == 100);
    assert(pacer.debtClampEvents == 1);
}

static void TestLongRenderCountsAndDebtIsBounded(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;

    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 1000, &sleep));
    assert(pacer.activeIntervals == 1);
    assert(pacer.activeElapsedTicks == 1000);
    assert(pacer.presentationDebtTicks == 400);
    assert(pacer.debtClampEvents == 1);
    assert(pacer.resyncs == 0);

    assert(!Old3DSFramePacer_BeginTick(&pacer, 2000, &sleep));
    assert(pacer.presentationDebtTicks == 400);
    assert(pacer.debtClampEvents == 2);
    assert(!Old3DSFramePacer_BeginTick(&pacer, 3000, &sleep));
    assert(Old3DSFramePacer_BeginTick(&pacer, 4000, &sleep));
    assert(pacer.presentationDebtTicks == 400);
    assert(pacer.debtClampEvents == 4);
    assert(pacer.maxConsecutiveSkipsSeen == 3);
}

static void TestSustainedSixtyMillisecondRenderKeepsLogicCadence(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t now = 0;
    uint64_t sleep;

    /* A 360-tick presentation is 60 ms with this test clock, matching the
     * slowest sustained PPU segment in the E1 Old 3DS dumps. Three bounded
     * skips let 999 active intervals stay within one percent of 60 Hz. */
    for (unsigned i = 0; i < 1000; ++i) {
        if (Old3DSFramePacer_BeginTick(&pacer, now, &sleep)) {
            now += 360;
        } else {
            now += sleep;
        }
    }
    assert(pacer.activeIntervals == 999);
    assert(pacer.maxConsecutiveSkipsSeen == 3);
    assert(pacer.skippedPresentations > 700);
    assert(now >= 99000 && now <= 101000);
}

static void TestExplicitAptDiscontinuity(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;

    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 300, &sleep));
    assert(pacer.presentationDebtTicks == 200);
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);

    assert(Old3DSFramePacer_BeginTick(&pacer, 1000, &sleep));
    assert(sleep == 0);
    assert(pacer.presentationDebtTicks == 0);
    assert(pacer.consecutiveSkips == 0);
    assert(pacer.resyncs == 1);
    assert(pacer.aptDiscontinuities == 1);
    assert(pacer.dumpDiscontinuities == 0);
    assert(pacer.activeIntervals == 1);
    assert(pacer.activeElapsedTicks == 300);

    assert(Old3DSFramePacer_BeginTick(&pacer, 1100, &sleep));
    assert(pacer.activeIntervals == 2);
    assert(pacer.activeElapsedTicks == 400);
}

static void TestAptAndDumpMarksCoalesce(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;

    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_NONE);
    Old3DSFramePacer_MarkDiscontinuity(&pacer, (Old3DSFramePacerDiscontinuity)(1u << 7));

    assert(Old3DSFramePacer_BeginTick(&pacer, 5000, &sleep));
    assert(pacer.resyncs == 1);
    assert(pacer.aptDiscontinuities == 1);
    assert(pacer.dumpDiscontinuities == 1);
    assert(pacer.activeIntervals == 0);
    assert(pacer.activeElapsedTicks == 0);
    assert(pacer.pendingDiscontinuities == 0);
}

static void TestMarkBeforeFirstBoundary(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;

    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    assert(Old3DSFramePacer_BeginTick(&pacer, 1234, &sleep));
    assert(pacer.resyncs == 1);
    assert(pacer.dumpDiscontinuities == 1);
    assert(pacer.activeIntervals == 0);
}

static void TestSmallOverrunGetsBoundedRemainderSleep(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;
    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 190, &sleep));
    assert(sleep == 10);
    assert(sleep <= pacer.framePeriodTicks / 10u);
    assert(pacer.sleepRequests == 1);
    assert(pacer.requestedSleepTicks == 10);
}

static void TestCadenceOnlyPathUsesExplicitMarks(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    assert(Old3DSFramePacer_RecordActiveInterval(&pacer, 100));
    assert(Old3DSFramePacer_RecordActiveInterval(&pacer, 600));
    assert(pacer.activeIntervals == 2);
    assert(pacer.activeElapsedTicks == 700);

    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_DUMP);
    assert(!Old3DSFramePacer_RecordActiveInterval(&pacer, 5000));
    assert(pacer.activeIntervals == 2);
    assert(pacer.activeElapsedTicks == 700);
    assert(pacer.resyncs == 1);
    assert(pacer.dumpDiscontinuities == 1);

    assert(Old3DSFramePacer_RecordActiveInterval(&pacer, 50));
    assert(pacer.activeIntervals == 3);
    assert(pacer.activeElapsedTicks == 750);
}

static void TestCadenceCountersSaturate(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    pacer.activeElapsedTicks = UINT64_MAX - 5u;
    pacer.activeIntervals = UINT64_MAX;
    assert(Old3DSFramePacer_RecordActiveInterval(&pacer, 10));
    assert(pacer.activeElapsedTicks == UINT64_MAX);
    assert(pacer.activeIntervals == UINT64_MAX);

    pacer.resyncs = UINT64_MAX;
    pacer.aptDiscontinuities = UINT64_MAX;
    Old3DSFramePacer_MarkDiscontinuity(&pacer, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
    assert(!Old3DSFramePacer_RecordActiveInterval(&pacer, 10));
    assert(pacer.resyncs == UINT64_MAX);
    assert(pacer.aptDiscontinuities == UINT64_MAX);
}

static void TestDegenerateAndNullInputs(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 0);
    assert(pacer.framePeriodTicks == 1);
    assert(pacer.skipThresholdTicks == 1);

    uint64_t sleep = 123;
    assert(Old3DSFramePacer_BeginTick(NULL, 0, &sleep));
    assert(sleep == 0);
    assert(!Old3DSFramePacer_RecordActiveInterval(NULL, 10));
    Old3DSFramePacer_Init(NULL, 0);
    Old3DSFramePacer_MarkDiscontinuity(NULL, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
}

int main(void) {
    TestFullRateNeverSkips();
    TestThirtyFpsAlternatesPresentation();
    TestFortyFpsKeepsMorePictures();
    TestThreeSkipCap();
    TestLongRenderCountsAndDebtIsBounded();
    TestSustainedSixtyMillisecondRenderKeepsLogicCadence();
    TestExplicitAptDiscontinuity();
    TestAptAndDumpMarksCoalesce();
    TestMarkBeforeFirstBoundary();
    TestSmallOverrunGetsBoundedRemainderSleep();
    TestCadenceOnlyPathUsesExplicitMarks();
    TestCadenceCountersSaturate();
    TestDegenerateAndNullInputs();
    puts("old3ds_frame_pacer_test: PASS");
    return 0;
}
