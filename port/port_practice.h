/*
 * port_practice.h — speedrun practice mode.
 *
 * A self-contained toolkit for practising segments: a frame-accurate
 * in-game timer (IGT) with manual splits, an on-screen input display with
 * a short rolling history, an instant "practice point" save/reload (a
 * dedicated savestate slot, separate from the F1..F5 manual slots), and
 * pause / frame-advance / slow-motion controls.
 *
 * State lives here (C, like port_debug_actions.c) so the C++ menu/overlay
 * translation units don't have to pull in game headers; everything the UI
 * needs is reachable through these extern "C" accessors. The module is
 * driven once per game frame from VBlankIntrWait() via Port_Practice_Tick().
 *
 * The IGT counter (gPracticeFrame) is included in the quicksave region list
 * (port_quicksave.c), so loading any savestate — including the practice
 * point — rewinds the timer to the value captured with that state. This is
 * what makes "reload the section and the timer comes back too" work.
 */
#pragma once

#include "port_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Absolute frame counter. Advances by one every non-paused Tick (~60/s),
 * is rewound by savestate loads, and is the time base for the IGT timer and
 * splits. Defined in port_practice.c; referenced by port_quicksave.c so it
 * round-trips through snapshots. */
extern u64 gPracticeFrame;

/* Per-frame driver. Call once from VBlankIntrWait(), next to
 * Port_QuickSave_AutoTick(). Advances gPracticeFrame (unless paused) and
 * samples the current input mask into the rolling history. */
void Port_Practice_Tick(void);

/* ---- Timer ----------------------------------------------------------- */
/* Elapsed frames on the (resettable, stoppable) stopwatch. */
u64  Port_Practice_ElapsedFrames(void);
bool Port_Practice_TimerRunning(void);
void Port_Practice_TimerReset(void);   /* zero the stopwatch (keeps running state) */
void Port_Practice_TimerToggle(void);  /* start <-> stop */

/* ---- Splits (manual) ------------------------------------------------- */
#define PORT_PRACTICE_MAX_SPLITS 64
void Port_Practice_AddSplit(void);     /* record current elapsed as a split */
int  Port_Practice_SplitCount(void);
/* Elapsed frames at split i (0-based). Returns 0 if out of range. */
u64  Port_Practice_SplitAt(int i);
void Port_Practice_ClearSplits(void);

/* ---- Input display --------------------------------------------------- */
/* Current GBA input mask: bit (1u << PORT_INPUT_x) set when that input is
 * held this frame. Only the 10 GBA inputs (PORT_INPUT_A..PORT_INPUT_L) are
 * represented. */
u16  Port_Practice_CurrentInputMask(void);
/* Rolling history, newest-first. index 0 == most recent sampled frame.
 * Returns 0 (no buttons) past the end of recorded history. */
#define PORT_PRACTICE_HISTORY 64
u16  Port_Practice_HistoryAt(int index);
int  Port_Practice_HistoryCount(void);

/* ---- Practice point (dedicated savestate) ---------------------------- */
/* Capture / restore the practice point. Implemented in port_quicksave.c
 * (reuses the snapshot machinery); declared here for the UI's convenience.
 * Return 1 on success, 0 on failure / empty. */
int  Port_Practice_SetPoint(void);
int  Port_Practice_LoadPoint(void);
bool Port_Practice_HasPoint(void);

/* ---- Pause / frame-advance ------------------------------------------- */
bool Port_Practice_IsPaused(void);
void Port_Practice_TogglePause(void);
void Port_Practice_SetPaused(bool paused);
void Port_Practice_RequestStep(void); /* advance exactly one frame while paused */
/* Consumed by the frame loop: returns true once per requested step and
 * clears the request. */
bool Port_Practice_ConsumeStep(void);

#ifdef __cplusplus
}
#endif
