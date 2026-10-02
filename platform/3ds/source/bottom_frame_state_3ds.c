#include "bottom_frame_state_3ds.h"

#include <string.h>

static uint32_t LoadAcquire(const volatile uint32_t* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static uint32_t LoadRelaxed(const volatile uint32_t* value) {
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void StoreRelease(volatile uint32_t* value, uint32_t next) {
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static void Count(volatile uint32_t* value) {
    __atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

void BottomFrameState3DS_Reset(BottomFrameState3DS* state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

uint32_t BottomFrameState3DS_Request(BottomFrameState3DS* state) {
    if (!state) return 0;
    uint32_t generation = __atomic_add_fetch(&state->requested, 1u, __ATOMIC_ACQ_REL);
    /* Generation zero means "no frame".  Skipping it also makes rollover
     * fail closed instead of briefly validating an uninitialized frame. */
    if (generation == 0) generation = __atomic_add_fetch(&state->requested, 1u, __ATOMIC_ACQ_REL);
    Count(&state->requestCount);
    return generation;
}

uint32_t BottomFrameState3DS_RequestTouchRefresh(BottomFrameState3DS* state) {
    const uint32_t generation = BottomFrameState3DS_Request(state);
    if (state && generation != 0) StoreRelease(&state->touchBlocked, 1u);
    return generation;
}

uint32_t BottomFrameState3DS_BeginPaint(BottomFrameState3DS* state) {
    if (!state) return 0;
    uint32_t generation = LoadAcquire(&state->requested);
    /* Initial and periodic paints normally reserve a generation in the PPU
     * scheduler.  Keep this defensive path so no caller can publish targets
     * under generation zero. */
    if (generation == 0) generation = BottomFrameState3DS_Request(state);
    return generation;
}

void BottomFrameState3DS_FinishPaint(BottomFrameState3DS* state, uint32_t generation) {
    if (!state || generation == 0) return;
    StoreRelease(&state->painted, generation);
    Count(&state->paintCount);
}

int BottomFrameState3DS_NeedsPaint(const BottomFrameState3DS* state) {
    if (!state) return 0;
    return LoadAcquire(&state->requested) != LoadAcquire(&state->painted);
}

void BottomFrameState3DS_MarkSubmitted(BottomFrameState3DS* state, uint32_t generation, int inGame) {
    if (!state || generation == 0) return;
    /* Publish mode before generation; the acquire load of submitted in
     * PromoteSubmitted then observes the matching mode. */
    __atomic_store_n(&state->submittedInGame, inGame ? 1u : 0u, __ATOMIC_RELAXED);
    StoreRelease(&state->submitted, generation);
    Count(&state->submissionCount);
}

int BottomFrameState3DS_PromoteSubmitted(BottomFrameState3DS* state) {
    if (!state) return 0;
    const uint32_t generation = LoadAcquire(&state->submitted);
    if (generation == 0 || generation == LoadAcquire(&state->visible)) return 0;
    const uint32_t inGame = LoadRelaxed(&state->submittedInGame);
    __atomic_store_n(&state->visibleInGame, inGame, __ATOMIC_RELAXED);
    StoreRelease(&state->visible, generation);
    /* An interactive mutation invalidates the old panel immediately.  It is
     * safe again only when the newest requested paint, its published hit
     * boxes, and the confirmed visible texture all converge. */
    if (generation == LoadAcquire(&state->painted) &&
        generation == LoadAcquire(&state->requested)) {
        StoreRelease(&state->touchBlocked, 0u);
    }
    Count(&state->visibilityCount);
    return 1;
}

int BottomFrameState3DS_CanDispatchTouch(BottomFrameState3DS* state, int currentInGame) {
    if (!state) return 0;
    if (LoadAcquire(&state->touchBlocked) != 0) {
        Count(&state->touchGenerationRejects);
        return 0;
    }
    const uint32_t painted = LoadAcquire(&state->painted);
    const uint32_t visible = LoadAcquire(&state->visible);
    if (painted == 0 || painted != visible) {
        Count(&state->touchGenerationRejects);
        return 0;
    }
    const uint32_t visibleInGame = LoadAcquire(&state->visibleInGame);
    if (visibleInGame != (currentInGame ? 1u : 0u)) {
        Count(&state->touchModeRejects);
        return 0;
    }
    return 1;
}

int BottomFrameState3DS_VisibleModeMatches(const BottomFrameState3DS* state, int currentInGame) {
    if (!state || LoadAcquire(&state->visible) == 0) return 0;
    return LoadAcquire(&state->visibleInGame) == (currentInGame ? 1u : 0u);
}

int BottomFrameState3DS_ShouldSchedulePaint(int workerPending, int forcedUpdate, int cadenceDue,
                                           int justPromoted, int snapshotChanged,
                                           int animationRequired) {
    if (workerPending) return 0;
    if (forcedUpdate) return 1;
    if (!cadenceDue || justPromoted) return 0;
    return snapshotChanged || animationRequired;
}

void BottomFrameState3DS_GetStats(const BottomFrameState3DS* state, BottomFrameState3DSStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!state) return;
    out->requested = LoadAcquire(&state->requested);
    out->painted = LoadAcquire(&state->painted);
    out->submitted = LoadAcquire(&state->submitted);
    out->visible = LoadAcquire(&state->visible);
    out->requestCount = LoadRelaxed(&state->requestCount);
    out->paintCount = LoadRelaxed(&state->paintCount);
    out->submissionCount = LoadRelaxed(&state->submissionCount);
    out->visibilityCount = LoadRelaxed(&state->visibilityCount);
    out->touchGenerationRejects = LoadRelaxed(&state->touchGenerationRejects);
    out->touchModeRejects = LoadRelaxed(&state->touchModeRejects);
}
