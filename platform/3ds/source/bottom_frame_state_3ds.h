#ifndef TMC_BOTTOM_FRAME_STATE_3DS_H
#define TMC_BOTTOM_FRAME_STATE_3DS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The bottom-screen compositor publishes its hit boxes when a CPU paint
 * finishes.  Painting, submitting the texture to Citro3D, and making that
 * submission visible are three different events, so touch must not treat
 * them as one.  All fields are accessed through the functions below; the
 * volatile qualifiers only make accidental direct access conspicuous while
 * the implementation supplies the required acquire/release ordering.
 */
typedef struct {
    volatile uint32_t requested;
    volatile uint32_t painted;
    volatile uint32_t submitted;
    volatile uint32_t visible;
    volatile uint32_t submittedInGame;
    volatile uint32_t visibleInGame;
    volatile uint32_t touchBlocked;

    volatile uint32_t requestCount;
    volatile uint32_t paintCount;
    volatile uint32_t submissionCount;
    volatile uint32_t visibilityCount;
    volatile uint32_t touchGenerationRejects;
    volatile uint32_t touchModeRejects;
} BottomFrameState3DS;

typedef struct {
    uint32_t requested;
    uint32_t painted;
    uint32_t submitted;
    uint32_t visible;
    uint32_t requestCount;
    uint32_t paintCount;
    uint32_t submissionCount;
    uint32_t visibilityCount;
    uint32_t touchGenerationRejects;
    uint32_t touchModeRejects;
} BottomFrameState3DSStats;

void BottomFrameState3DS_Reset(BottomFrameState3DS* state);
uint32_t BottomFrameState3DS_Request(BottomFrameState3DS* state);
uint32_t BottomFrameState3DS_RequestTouchRefresh(BottomFrameState3DS* state);
uint32_t BottomFrameState3DS_BeginPaint(BottomFrameState3DS* state);
void BottomFrameState3DS_FinishPaint(BottomFrameState3DS* state, uint32_t generation);
int BottomFrameState3DS_NeedsPaint(const BottomFrameState3DS* state);
void BottomFrameState3DS_MarkSubmitted(BottomFrameState3DS* state, uint32_t generation, int inGame);
int BottomFrameState3DS_PromoteSubmitted(BottomFrameState3DS* state);
int BottomFrameState3DS_CanDispatchTouch(BottomFrameState3DS* state, int currentInGame);
int BottomFrameState3DS_VisibleModeMatches(const BottomFrameState3DS* state, int currentInGame);
int BottomFrameState3DS_ShouldSchedulePaint(int workerPending, int forcedUpdate, int cadenceDue,
                                           int justPromoted, int snapshotChanged,
                                           int animationRequired);
void BottomFrameState3DS_GetStats(const BottomFrameState3DS* state, BottomFrameState3DSStats* out);

#ifdef __cplusplus
}
#endif

#endif
