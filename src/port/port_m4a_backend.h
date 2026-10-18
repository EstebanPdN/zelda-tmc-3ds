#ifndef PORT_M4A_BACKEND_H
#define PORT_M4A_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct SongHeader SongHeader;

#ifdef __cplusplus
extern "C" {
#endif

bool Port_M4A_Backend_Init(uint32_t sampleRate);
void Port_M4A_Backend_Shutdown(void);
void Port_M4A_Backend_Reset(void);
void Port_M4A_Backend_SoundInit(uint32_t soundMode);
void Port_M4A_Backend_SetSoundMode(uint32_t soundMode);
void Port_M4A_Backend_SetVSyncEnabled(bool enabled);
bool Port_M4A_Backend_StartSongById(uint8_t playerIndex, uint16_t songId);
void Port_M4A_Backend_StopPlayer(uint8_t playerIndex);
void Port_M4A_Backend_ContinuePlayer(uint8_t playerIndex);
void Port_M4A_Backend_SetTrackVolume(uint8_t playerIndex, uint16_t trackBits, uint16_t volume);
void Port_M4A_Backend_SetTrackPan(uint8_t playerIndex, uint16_t trackBits, int8_t pan);
void Port_M4A_Backend_Render(int16_t* outSamples, uint32_t frameCount, bool mute);
const char* Port_GetSongLabel(uint16_t songId);
/* Returns true while the player still has tracks actively running (i.e. song
 * hasn't reached its `ply_fine`). Used to detect when a high-priority SFX has
 * finished so the BGM can be unmuted. */
bool Port_M4A_Backend_IsPlayerActive(uint8_t playerIndex);

/* GBA-accurate audio toggle. When true, the synth uses NEAREST resampling
 * (the GBA's no-interpolation sample-and-hold "crunch") and no forced reverb,
 * for A/B comparison against hardware/mGBA. When false (default), the enhanced
 * path (SINC resampling) is used. The output-DSP post-process bypass lives on
 * the Port_Audio side; this controls only the agbplay synth knobs. */
void Port_M4A_Backend_SetGbaAccurate(bool accurate);
bool Port_M4A_Backend_GetGbaAccurate(void);

/* Forced PCM-only reverb level (F8 -> Audio "Reverb"). level 0 = OFF/dry
 * (default, ships unchanged); 1..24 engages agbplay's NORMAL comb on PCM
 * tracks only (CGB/PSG voices stay dry). Applied live via SoundMixer::
 * UpdateReverb — no context rebuild, the current song is not restarted.
 * Forced fully dry whenever GBA-accurate mode is on. */
void Port_M4A_Backend_SetReverbLevel(int level);
int Port_M4A_Backend_GetReverbLevel(void);
void Port_M4A_Backend_SetMasterVolume(float volume);
float Port_M4A_Backend_GetMasterVolume(void);

#ifdef __cplusplus
}
#endif

#endif
