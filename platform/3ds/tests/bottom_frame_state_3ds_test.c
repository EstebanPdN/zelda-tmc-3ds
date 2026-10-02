#include "bottom_frame_state_3ds.h"

#include <assert.h>
#include <stdio.h>

static void TestPaintSubmitVisibleOrder(void) {
    BottomFrameState3DS state;
    BottomFrameState3DS_Reset(&state);

    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 0));
    const uint32_t first = BottomFrameState3DS_Request(&state);
    assert(first != 0);
    assert(BottomFrameState3DS_NeedsPaint(&state));
    assert(BottomFrameState3DS_BeginPaint(&state) == first);
    BottomFrameState3DS_FinishPaint(&state, first);
    assert(!BottomFrameState3DS_NeedsPaint(&state));
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 1));

    BottomFrameState3DS_MarkSubmitted(&state, first, 1);
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 1));
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 1));
    assert(BottomFrameState3DS_VisibleModeMatches(&state, 1));
    assert(!BottomFrameState3DS_VisibleModeMatches(&state, 0));
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 0));
    assert(!BottomFrameState3DS_PromoteSubmitted(&state));
}

static void TestRequestDuringPaintIsNotLost(void) {
    BottomFrameState3DS state;
    BottomFrameState3DS_Reset(&state);

    const uint32_t first = BottomFrameState3DS_Request(&state);
    assert(BottomFrameState3DS_BeginPaint(&state) == first);
    const uint32_t second = BottomFrameState3DS_Request(&state);
    assert(second != first);
    BottomFrameState3DS_FinishPaint(&state, first);
    assert(BottomFrameState3DS_NeedsPaint(&state));

    BottomFrameState3DS_MarkSubmitted(&state, first, 0);
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 0));

    assert(BottomFrameState3DS_BeginPaint(&state) == second);
    BottomFrameState3DS_FinishPaint(&state, second);
    /* The hidden frame has already published different hit boxes. */
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 0));
    BottomFrameState3DS_MarkSubmitted(&state, second, 0);
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 0));
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 0));
}

static void TestNewerPaintKeepsOlderVisibleFrameClosed(void) {
    BottomFrameState3DS state;
    BottomFrameState3DS_Reset(&state);

    const uint32_t first = BottomFrameState3DS_Request(&state);
    BottomFrameState3DS_FinishPaint(&state, first);
    BottomFrameState3DS_MarkSubmitted(&state, first, 1);

    const uint32_t second = BottomFrameState3DS_Request(&state);
    BottomFrameState3DS_FinishPaint(&state, second);
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 1));

    BottomFrameState3DS_MarkSubmitted(&state, second, 1);
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 1));
}

static void TestInteractiveRequestBlocksRepeatedTap(void) {
    BottomFrameState3DS state;
    BottomFrameState3DS_Reset(&state);

    const uint32_t first = BottomFrameState3DS_Request(&state);
    BottomFrameState3DS_FinishPaint(&state, first);
    BottomFrameState3DS_MarkSubmitted(&state, first, 1);
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 1));

    const uint32_t second = BottomFrameState3DS_RequestTouchRefresh(&state);
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 1));
    BottomFrameState3DS_FinishPaint(&state, second);
    BottomFrameState3DS_MarkSubmitted(&state, second, 1);
    assert(!BottomFrameState3DS_CanDispatchTouch(&state, 1));
    assert(BottomFrameState3DS_PromoteSubmitted(&state));
    assert(BottomFrameState3DS_CanDispatchTouch(&state, 1));
}

static void TestStatsExposeThePipeline(void) {
    BottomFrameState3DS state;
    BottomFrameState3DSStats stats;
    BottomFrameState3DS_Reset(&state);

    const uint32_t generation = BottomFrameState3DS_Request(&state);
    BottomFrameState3DS_FinishPaint(&state, generation);
    (void)BottomFrameState3DS_CanDispatchTouch(&state, 1);
    BottomFrameState3DS_MarkSubmitted(&state, generation, 1);
    BottomFrameState3DS_PromoteSubmitted(&state);
    (void)BottomFrameState3DS_CanDispatchTouch(&state, 0);
    BottomFrameState3DS_GetStats(&state, &stats);

    assert(stats.requested == generation);
    assert(stats.painted == generation);
    assert(stats.submitted == generation);
    assert(stats.visible == generation);
    assert(stats.requestCount == 1);
    assert(stats.paintCount == 1);
    assert(stats.submissionCount == 1);
    assert(stats.visibilityCount == 1);
    assert(stats.touchGenerationRejects == 1);
    assert(stats.touchModeRejects == 1);
}

static void TestStaticPaintSuppression(void) {
    assert(!BottomFrameState3DS_ShouldSchedulePaint(1, 1, 1, 0, 1, 1));
    assert(BottomFrameState3DS_ShouldSchedulePaint(0, 1, 0, 1, 0, 0));
    assert(!BottomFrameState3DS_ShouldSchedulePaint(0, 0, 0, 0, 1, 1));
    assert(!BottomFrameState3DS_ShouldSchedulePaint(0, 0, 1, 1, 1, 1));
    assert(!BottomFrameState3DS_ShouldSchedulePaint(0, 0, 1, 0, 0, 0));
    assert(BottomFrameState3DS_ShouldSchedulePaint(0, 0, 1, 0, 1, 0));
    assert(BottomFrameState3DS_ShouldSchedulePaint(0, 0, 1, 0, 0, 1));
}

int main(void) {
    TestPaintSubmitVisibleOrder();
    TestRequestDuringPaintIsNotLost();
    TestNewerPaintKeepsOlderVisibleFrameClosed();
    TestInteractiveRequestBlocksRepeatedTap();
    TestStatsExposeThePipeline();
    TestStaticPaintSuppression();
    puts("bottom frame state 3DS tests passed");
    return 0;
}
