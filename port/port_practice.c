/*
 * port_practice.c — speedrun practice mode state + logic.
 *
 * See port_practice.h for the overview. Everything here is host-side and
 * frame-driven from VBlankIntrWait() through Port_Practice_Tick(). The one
 * exception is gPracticeFrame, which is also listed in the quicksave region
 * table so it travels with savestates.
 */
#include "port_practice.h"
#include "port_runtime_config.h"

/* ---- Timer ----------------------------------------------------------- */

/* Absolute frame counter (shared with port_quicksave.c via extern). */
u64 gPracticeFrame = 0;

/* Stopwatch derived from gPracticeFrame:
 *   elapsed = (running ? gPracticeFrame : sStopFrame) - sBaseFrame
 * clamped to >= 0. sBaseFrame is host-only (not snapshotted), so a savestate
 * load that rewinds gPracticeFrame below sBaseFrame is re-synced to zero
 * rather than underflowing. */
static u64  sBaseFrame   = 0;
static u64  sStopFrame   = 0;
static bool sRunning     = true;

static u64 ElapsedFor(u64 frame) {
    if (frame < sBaseFrame) {
        /* gPracticeFrame went backwards relative to the stopwatch base
         * (savestate load to before the last reset). Re-anchor so the
         * timer reads zero instead of a huge unsigned wrap. */
        sBaseFrame = frame;
        sStopFrame = frame;
        return 0;
    }
    return frame - sBaseFrame;
}

u64 Port_Practice_ElapsedFrames(void) {
    return ElapsedFor(sRunning ? gPracticeFrame : sStopFrame);
}

bool Port_Practice_TimerRunning(void) { return sRunning; }

void Port_Practice_TimerReset(void) {
    sBaseFrame = gPracticeFrame;
    sStopFrame = gPracticeFrame;
}

void Port_Practice_TimerToggle(void) {
    if (sRunning) {
        /* Freeze the displayed value at the current frame. */
        sStopFrame = gPracticeFrame;
        sRunning = false;
    } else {
        /* Resume: shift the base forward by the paused gap so the elapsed
         * value continues from where it stopped. */
        if (gPracticeFrame >= sStopFrame) {
            sBaseFrame += (gPracticeFrame - sStopFrame);
        }
        sRunning = true;
    }
}

/* ---- Splits ---------------------------------------------------------- */

static u64 sSplits[PORT_PRACTICE_MAX_SPLITS];
static int sSplitCount = 0;

void Port_Practice_AddSplit(void) {
    if (sSplitCount >= PORT_PRACTICE_MAX_SPLITS) return;
    sSplits[sSplitCount++] = Port_Practice_ElapsedFrames();
}

int Port_Practice_SplitCount(void) { return sSplitCount; }

u64 Port_Practice_SplitAt(int i) {
    if (i < 0 || i >= sSplitCount) return 0;
    return sSplits[i];
}

void Port_Practice_ClearSplits(void) { sSplitCount = 0; }

/* ---- Input display --------------------------------------------------- */

/* Rolling history ring buffer of input masks, one sample per Tick. */
static u16 sHistory[PORT_PRACTICE_HISTORY];
static int sHistHead  = 0;   /* index where the next sample will be written */
static int sHistCount = 0;
static u16 sCurrentMask = 0;

static u16 SampleInputMask(void) {
    u16 mask = 0;
    /* The 10 GBA inputs occupy PORT_INPUT_A..PORT_INPUT_L (0..9). */
    for (int i = PORT_INPUT_A; i <= PORT_INPUT_L; i++) {
        if (Port_Config_InputPressed((PortInput)i)) {
            mask |= (u16)(1u << i);
        }
    }
    return mask;
}

u16 Port_Practice_CurrentInputMask(void) { return sCurrentMask; }

int Port_Practice_HistoryCount(void) { return sHistCount; }

u16 Port_Practice_HistoryAt(int index) {
    if (index < 0 || index >= sHistCount) return 0;
    /* Newest-first: index 0 is the most recently written sample. */
    int pos = (sHistHead - 1 - index);
    pos %= PORT_PRACTICE_HISTORY;
    if (pos < 0) pos += PORT_PRACTICE_HISTORY;
    return sHistory[pos];
}

/* ---- Practice point (defined in port_quicksave.c) -------------------- */
/* Declarations only — the savestate machinery lives in port_quicksave.c. */
int Port_QuickSave_SavePractice(void);
int Port_QuickSave_LoadPractice(void);
int Port_QuickSave_HasPractice(void);

int  Port_Practice_SetPoint(void)  { return Port_QuickSave_SavePractice(); }
int  Port_Practice_LoadPoint(void) { return Port_QuickSave_LoadPractice(); }
bool Port_Practice_HasPoint(void)  { return Port_QuickSave_HasPractice() != 0; }

/* ---- Pause / frame-advance ------------------------------------------- */

static bool sPaused = false;
static bool sStepRequested = false;

bool Port_Practice_IsPaused(void) { return sPaused; }

void Port_Practice_SetPaused(bool paused) {
    sPaused = paused;
    if (!paused) sStepRequested = false;
}

void Port_Practice_TogglePause(void) { Port_Practice_SetPaused(!sPaused); }

void Port_Practice_RequestStep(void) {
    if (sPaused) sStepRequested = true;
}

bool Port_Practice_ConsumeStep(void) {
    if (sStepRequested) {
        sStepRequested = false;
        return true;
    }
    return false;
}

/* ---- Per-frame driver ------------------------------------------------ */

void Port_Practice_Tick(void) {
    gPracticeFrame++;

    sCurrentMask = SampleInputMask();
    sHistory[sHistHead] = sCurrentMask;
    sHistHead = (sHistHead + 1) % PORT_PRACTICE_HISTORY;
    if (sHistCount < PORT_PRACTICE_HISTORY) sHistCount++;
}
