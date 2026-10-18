#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#if !defined(TMC_N64) && !defined(TMC_3DS)
#include <SDL3/SDL.h>
#endif

bool Port_Audio_Init(void);
void Port_Audio_Shutdown(void);
void Port_Audio_Reset(void);

/* GBA-accurate audio toggle (F8 → Audio). This is the single front door:
 * it records the flag the audio thread reads to bypass the output-DSP
 * post-process chain, AND forwards to the synth backend (NEAREST resampling,
 * no forced reverb). Default off = enhanced (SINC + full DSP chain). */
void Port_Audio_SetGbaAccurate(bool accurate);
bool Port_Audio_IsGbaAccurate(void);

/* Mid/side stereo-widen gain (enhanced path only). Range [1.00, 1.50];
 * 1.00 = mono image, 1.20 = shipped default. Mid is never scaled, so mono
 * fold-down is unchanged at any value. */
void Port_Audio_SetWidth(float width);
float Port_Audio_GetWidth(void);

/* Forced PCM-only reverb level (enhanced path only). 0 = off (default, dry);
 * 1..24 adds a short room tail to sampled drums/bass; chiptune leads stay dry.
 * Applied live (no song restart). Forwards to the agbplay backend. */
void Port_Audio_SetReverbLevel(int level);
int Port_Audio_GetReverbLevel(void);

/* Game master volume [0,1] applied to the final mixed output. 1.0 = unchanged
 * (default). Persisted via the port config; active in both accurate and
 * enhanced modes (a level control, not a tone enhancement). */
void Port_Audio_SetMasterVolume(float volume);
float Port_Audio_GetMasterVolume(void);

#endif
