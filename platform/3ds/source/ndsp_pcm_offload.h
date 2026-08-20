#ifndef TMC_NDSP_PCM_OFFLOAD_H
#define TMC_NDSP_PCM_OFFLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hands MP2K's DirectSound (PCM) voices to NDSP hardware channels.
 *
 * Only the per-sample work moves: MP2K keeps its envelope, volume and pan
 * exactly as before and pushes the result down as a channel mix once per
 * block, so the envelope shape is unchanged. What the DSP takes over is the
 * fetch/resample/accumulate inner loop, which is the part that costs the
 * ARM11 its audio budget.
 *
 * Deliberately narrow. Only uncompressed Type::PCM voices qualify: the
 * DPCM/ADPCM and the three synth types would each need their own decode or
 * table, and they are a small share of the voices actually playing.
 *
 * Off unless audio_dsp_pcm=1. Unlike the CGB offload this one is audible in a
 * way only a listener can judge -- NDSP sums in hardware while MP2K sums in
 * float and clips, so a mixed software/hardware sum is not bit-identical. */

bool NdspPcm_Available(void);
void NdspPcm_Init(void);
void NdspPcm_Shutdown(void);

/* Starts or updates a voice. On the first successful call *slot is claimed.
 * Returns false if the voice cannot be offloaded, in which case the caller
 * must mix it in software as usual. */
/* *finished is set when an offloaded non-looping sample has played out. The
 * caller must then Kill() the voice exactly as the software path does; it
 * cannot resume in software, because MP2K's samplePos did not advance while
 * the DSP owned the voice. */
bool NdspPcm_Play(int* slot, const int8_t* samplePtr, uint32_t endPos,
                  uint32_t loopPos, bool loopEnabled, uint32_t startPos,
                  float rate, float leftVol, float rightVol, bool* finished);

void NdspPcm_Stop(int* slot);

/* The master fade is applied to the software track buffers only, so the mixer
 * publishes the current level here for folding into the hardware mix. */
void NdspPcm_SetMasterLevel(float level);

unsigned long long NdspPcm_VoicesOffloaded(void);
unsigned long long NdspPcm_VoicesDeclined(void);
/* Voices whose type the offload does not implement (compressed/synth). */
unsigned long long NdspPcm_VoicesUnsupported(void);
void NdspPcm_NoteUnsupported(void);

#ifdef __cplusplus
}
#endif

#endif
