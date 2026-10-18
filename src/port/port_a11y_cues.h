/*
 * port_a11y_cues.h — Accessibility audio cues for Project Picori.
 *
 * Navigation aid for blind / low-vision players. The engine renders
 * GBA hardware that a screen reader can't see, so this module turns the
 * live entity pool and room data into spoken cues via the TTS service
 * (port_tts).
 *
 * Phase 1 (this file): an on-demand "scan my surroundings" action,
 * bound to F10. It enumerates nearby points of interest — chests,
 * collectible pickups (rupees / hearts / kinstones / keys / …), NPCs
 * and animals, enemies, and room exits — and speaks each as
 * "<label>, <direction>, <distance>", nearest first.
 *
 * Phase 2: passive cues driven each gameplay frame (Port_A11y_Update) —
 * a tonal enemy radar (stereo pan = direction, pitch = distance),
 * footstep / surface sounds, fall-hazard warnings, and wall bumps —
 * with per-category toggles. Tones are synthesized by port_a11y_audio.
 *
 * Everything here is read-only with respect to game state and runs on
 * the main (game/input) thread inside Port_PumpEvents, so there is no
 * concurrency against the entity update. No-op when TTS is unavailable
 * or disabled, or when the player is not in normal gameplay.
 */
#ifndef PORT_A11Y_CUES_H
#define PORT_A11Y_CUES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Speak a summary of nearby points of interest (F10). Safe to call any
 * time; a no-op outside gameplay or when TTS is off. */
void Port_A11y_ScanSurroundings(void);

/* On-demand: step to the next nearby point of interest (beep + spoken
 * label/direction/distance), and an orientation readout (surface under
 * the player, cardinal open/wall/blocked, room exits). */
void Port_A11y_CycleNext(void);
void Port_A11y_LookAround(void);

/* Per-frame passive cue driver. Call once per gameplay frame; internally
 * gated on the engine task and the per-category toggles below. */
void Port_A11y_Update(void);

/* Load persisted cue toggles from config. Call once after config load. */
void Port_A11y_Init(void);

/* Passive cue toggles (master + per-category), persisted via
 * port_runtime_config. */
void Port_A11y_SetPassiveEnabled(bool on);
bool Port_A11y_GetPassiveEnabled(void);
void Port_A11y_SetFootstepsEnabled(bool on);
bool Port_A11y_GetFootstepsEnabled(void);
void Port_A11y_SetHazardsEnabled(bool on);
bool Port_A11y_GetHazardsEnabled(void);
void Port_A11y_SetRadarEnabled(bool on);
bool Port_A11y_GetRadarEnabled(void);
void Port_A11y_SetWallsEnabled(bool on);
bool Port_A11y_GetWallsEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_A11Y_CUES_H */
