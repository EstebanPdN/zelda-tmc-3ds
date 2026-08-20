#ifndef NDSP_PSG_OFFLOAD_H
#define NDSP_PSG_OFFLOAD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Report Task 1, first slice: the GBA's four CGB voices are periodic waveforms,
 * which is exactly what an NDSP hardware channel plays for free. The DSP sits
 * idle at 134 MHz while the CPU synthesises them in floats on a non-pipelined
 * VFP, so moving them off is the cheapest part of the offload to express.
 *
 * PCM (DirectSound) voices stay on the software mixer for now: they need
 * per-note sample pointers, ADSR and pitch bends mapped onto channel state, and
 * they carry MP2K's clipping character, which is audible.
 *
 * Off unless audio_dsp=1. The software mixer remains the reference. */
bool NdspPsg_Available(void);

/* MP2K applies reverb to the software mix, and a voice playing on a hardware
 * channel never passes through it -- the CGB parts would lose their tail while
 * everything else kept it. Rather than let that happen silently, the offload
 * declines while reverb is engaged and the voices stay in software. Call this
 * whenever the reverb level changes. */
/* The mixer's master volume and fade level, which offloaded voices must apply
 * themselves -- they never reach the track-buffer stage that applies it. */
void NdspPsg_SetMasterLevel(float level);

/* Diagnostic only -- recorded for the dump, does not gate offload. */
void NdspPsg_SetReverbLevel(int level);

/* Claims a hardware channel on first use for this voice. Returns false when
 * offload is disabled or no channel is free, in which case the caller must
 * synthesise in software as before. */
bool NdspPsg_PlaySquare(int* slot, float fetchRateHz, int dutyIndex,
                        float volumeLeft, float volumeRight);

/* Wave voices carry their own 16-byte table of 4-bit samples, so the table is
 * rebuilt per voice; 32 samples make a period, hence rate = frequency * 32. */
bool NdspPsg_PlayWave(int* slot, float fetchRateHz, const unsigned char* wave16,
                      float volumeLeft, float volumeRight);

/* Noise is an LFSR sequence, pre-generated once for each of the two mask
 * lengths the hardware uses. One LFSR step per output sample, so rate is the
 * voice frequency directly. */
bool NdspPsg_PlayNoise(int* slot, float lfsrClockHz, bool shortPeriod,
                       float volumeLeft, float volumeRight);

void NdspPsg_Stop(int* slot);

/* Called once at audio start-up, after ndspInit. */
/* Voices handed to hardware so far, and the reverb level that would block
 * it. Reported in the quick dump so the switch answers whether the offload
 * ever engages for this game rather than leaving it to inference. */
unsigned long long NdspPsg_VoicesOffloaded(void);
unsigned long long NdspPsg_VoicesRateDeclined(void);
int NdspPsg_CurrentReverbLevel(void);

void NdspPsg_Init(void);
void NdspPsg_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
