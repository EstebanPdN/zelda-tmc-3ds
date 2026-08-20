#include "ndsp_psg_offload.h"

#include <3ds.h>
#include <string.h>

#include <stdbool.h>
bool Port_Config_AudioDspInterpLinear(void);
void SpeakerEq3DS_ApplyToChannel(int channel);

/* Four CGB voices exist on a GBA, so four hardware channels are enough.
 * Channel 0 belongs to the software mix. */
#define PSG_FIRST_CHANNEL 1
#define PSG_CHANNEL_COUNT 4

/* One period of a duty cycle is eight samples. A buffer of a single period is
 * too short for the DSP to loop comfortably, so each table holds many periods
 * and the sample rate carries the pitch: rate = frequency * 8. */
/* NDSP outputs at 32728 Hz and resamples each channel to it. Asking for a
 * playback rate far above that does not produce a high note -- it produces
 * garbage, which is what a table-driven PSG offload walks straight into:
 *   square  rate = freq * 8   (up to 64 kHz)
 *   wave    rate = freq * 32  (up to 256 kHz)
 *   noise   rate = freq       where freq is the LFSR CLOCK, up to 524288 Hz
 * The noise clock alone is 16x the output rate. Voices above the ceiling are
 * declined and stay in software rather than being clamped, because clamping
 * would play them at the wrong pitch. */
/* NDSP outputs at 32728 Hz and resamples each channel to it; a rate far above
 * that yields garbage, not a high note. The rate handed to a channel is MP2K's
 * pattern-fetch rate directly -- NOT the tone frequency and NOT the fetch rate
 * scaled by the table length. The software path consumes exactly one table
 * entry per source sample (interStep = freq * sampleRateInv), so the table
 * advances at `freq` and the audible tone is freq / entries-per-period.
 * Scaling by the period length here played squares three octaves sharp and
 * wave voices five. Voices whose rate still exceeds the ceiling are declined
 * rather than clamped, since clamping would retune them. */
#define PSG_MAX_RATE 32728.0f

#define PSG_PERIOD_SAMPLES 8
#define PSG_PERIODS 64
#define PSG_TABLE_SAMPLES (PSG_PERIOD_SAMPLES * PSG_PERIODS)

extern bool Port_Config_AudioDsp(void);

static const float kDuty[4][PSG_PERIOD_SAMPLES] = {
    { 0.875f, -0.125f, -0.125f, -0.125f, -0.125f, -0.125f, -0.125f, -0.125f },
    { 0.75f, 0.75f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f },
    { 0.50f, 0.50f, 0.50f, 0.50f, -0.50f, -0.50f, -0.50f, -0.50f },
    { 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, -0.75f, -0.75f },
};

/* GB noise: a 15-bit LFSR, or 7-bit in short mode. Generated once. */
#define PSG_NOISE_LONG 32767
#define PSG_NOISE_SHORT 127

/* MP2K clocks the noise LFSR as fast as 524288 Hz, far past what a channel can
 * be played at, which had 88% of CGB voices declining to software. Decimated
 * tables fix that: table D holds every (1 << D)-th LFSR output, so it can be
 * played at freq / (1 << D) and still advance the sequence at `freq`. That is
 * close to what the software path ends up with anyway, since it band-limits
 * the LFSR down to the output rate through a sinc stage.
 *
 * The LFSR periods (32767 = 7*31*151, and 127) are both odd, so every power-of-
 * two decimation is coprime to them and the decimated sequence keeps the full
 * period rather than collapsing into a short loop.
 *
 * The top MP2K clock is 524288 Hz and a shift of 4 leaves 32768 Hz -- 40 Hz
 * over the ceiling, which declined the fastest noise setting by a hair and
 * accounted for most of the surviving fallbacks. Six levels (max shift 5,
 * 16384 Hz) clear it. Built on demand: the long table is 64 KB per level and
 * most songs touch only one or two. */
#define PSG_NOISE_DECIM_LEVELS 6
/* One period of wave RAM is 32 four-bit samples. */
#define PSG_WAVE_SAMPLES 32

static int16_t* sTables[4];
static int16_t* sNoiseLong[PSG_NOISE_DECIM_LEVELS];
static int16_t* sNoiseShort[PSG_NOISE_DECIM_LEVELS];
static int16_t* sWaveTable[PSG_CHANNEL_COUNT];
/* Which wave RAM each slot's table was built from, so it is rebuilt only
 * when the game actually rewrites it -- resetting the channel every buffer
 * would restart the waveform sixty times a second. */
static const unsigned char* sWaveSource[PSG_CHANNEL_COUNT];
/* Table currently queued on each slot, so a voice is reconfigured only when
 * its waveform actually changes -- re-adding a wave buffer every block
 * restarts the waveform sixty times a second. */
static const int16_t* sSlotData[PSG_CHANNEL_COUNT];
static ndspWaveBuf sWave[PSG_CHANNEL_COUNT];
static bool sChannelBusy[PSG_CHANNEL_COUNT];
static bool sReady;
/* Non-zero disables the offload: see NdspPsg_SetReverbLevel. */
static int sReverbLevel;
static unsigned long long sVoicesOffloaded;
/* Declined because the required rate exceeds what the DSP will play. High
 * noise and wave pitches dominate this, so the split says how much of the
 * CGB work the offload actually takes off the CPU. */
static unsigned long long sVoicesRateDeclined;
/* getVol() carries the voice's own envelope and pan but not the mixer's master
 * volume, which the software path applies to the track buffers afterwards --
 * a stage an offloaded voice never reaches. Without this the user's volume
 * slider and every song fade would have no effect on the CGB voices. */
static float sMasterLevel = 1.0f;

void NdspPsg_Init(void) {
    if (sReady || !Port_Config_AudioDsp()) return;
    for (int duty = 0; duty < 4; ++duty) {
        sTables[duty] = (int16_t*)linearAlloc(PSG_TABLE_SAMPLES * sizeof(int16_t));
        if (!sTables[duty]) { NdspPsg_Shutdown(); return; }
        for (int i = 0; i < PSG_TABLE_SAMPLES; ++i) {
            const float v = kDuty[duty][i % PSG_PERIOD_SAMPLES];
            sTables[duty][i] = (int16_t)(v * 32767.0f);
        }
        DSP_FlushDataCache(sTables[duty], PSG_TABLE_SAMPLES * sizeof(int16_t));
    }
    for (int i = 0; i < PSG_CHANNEL_COUNT; ++i) {
        sWaveTable[i] = (int16_t*)linearAlloc(PSG_WAVE_SAMPLES * sizeof(int16_t));
        if (!sWaveTable[i]) { NdspPsg_Shutdown(); return; }
    }
    memset(sWave, 0, sizeof(sWave));
    memset(sChannelBusy, 0, sizeof(sChannelBusy));
    sReady = true;
}

/* Builds (once) the noise table for a decimation level and returns it. Level 0
 * is the raw LFSR. */
static const int16_t* noise_table(bool shortPeriod, int level, int* samplesOut) {
    if (level < 0 || level >= PSG_NOISE_DECIM_LEVELS) return NULL;
    const int count = shortPeriod ? PSG_NOISE_SHORT : PSG_NOISE_LONG;
    if (samplesOut) *samplesOut = count;
    int16_t** slot = shortPeriod ? &sNoiseShort[level] : &sNoiseLong[level];
    if (*slot) return *slot;

    int16_t* out = (int16_t*)linearAlloc((size_t)count * sizeof(int16_t));
    if (!out) return NULL;
    const int stride = 1 << level;
    uint16_t state = 0x4000u;
    for (int i = 0; i < count; ++i) {
        /* Advance `stride` LFSR steps per stored sample; the stored bit is the
         * one the hardware would be outputting at that instant. */
        for (int step = 0; step < stride; ++step) {
            const uint16_t feedback = (uint16_t)((state ^ (state >> 1)) & 1u);
            state >>= 1u;
            state |= (uint16_t)(feedback << (shortPeriod ? 6u : 14u));
        }
        out[i] = (state & 1u) ? 16383 : -16384;
    }
    DSP_FlushDataCache(out, (size_t)count * sizeof(int16_t));
    *slot = out;
    return out;
}

void NdspPsg_Shutdown(void) {
    for (int i = 0; i < PSG_CHANNEL_COUNT; ++i) {
        if (sChannelBusy[i]) ndspChnWaveBufClear(PSG_FIRST_CHANNEL + i);
        sChannelBusy[i] = false;
    }
    for (int duty = 0; duty < 4; ++duty) {
        if (sTables[duty]) { linearFree(sTables[duty]); sTables[duty] = NULL; }
    }
    for (int i = 0; i < PSG_CHANNEL_COUNT; ++i) {
        if (sWaveTable[i]) { linearFree(sWaveTable[i]); sWaveTable[i] = NULL; }
    }
    for (int i = 0; i < PSG_NOISE_DECIM_LEVELS; ++i) {
        if (sNoiseLong[i]) { linearFree(sNoiseLong[i]); sNoiseLong[i] = NULL; }
        if (sNoiseShort[i]) { linearFree(sNoiseShort[i]); sNoiseShort[i] = NULL; }
    }
    sReady = false;
}

/* No reverb gate. MP2K applies reverb per-track in SoundMixer::Process BEFORE
 * the CGB channels mix (SoundMixer.cpp: reverb pass ~L91-99, then sq1/sq2/wave/
 * noise at L100-103), and ReverbEffect only captures what is already in the
 * track buffer. PSG output is therefore dry in the software path as well, so
 * offloading it to the DSP costs no reverb tail. */
bool NdspPsg_Available(void) { return sReady; }

unsigned long long NdspPsg_VoicesOffloaded(void) { return sVoicesOffloaded; }
unsigned long long NdspPsg_VoicesRateDeclined(void) { return sVoicesRateDeclined; }
int NdspPsg_CurrentReverbLevel(void) { return sReverbLevel; }

void NdspPsg_SetReverbLevel(int level) { sReverbLevel = level; /* reported only */ }

void NdspPsg_SetMasterLevel(float level) {
    sMasterLevel = level < 0.0f ? 0.0f : level;
}

static bool psg_claim(int* slot);
static bool psg_decline(int* slot);
static void psg_configure(int slot, const int16_t* data, int samples);
static void psg_set(int slot, float rateHz, float volumeLeft, float volumeRight);

bool NdspPsg_PlaySquare(int* slot, float fetchRateHz, int dutyIndex,
                        float volumeLeft, float volumeRight) {
    if (!slot) return false;
    if (!NdspPsg_Available()) return psg_decline(slot);
    if (dutyIndex < 0 || dutyIndex > 3) return psg_decline(slot);
    if (fetchRateHz <= 1.0f) return psg_decline(slot);
    if (fetchRateHz > PSG_MAX_RATE) { ++sVoicesRateDeclined; return psg_decline(slot); }

    if (!psg_claim(slot)) return false;
    if (sSlotData[*slot] != sTables[dutyIndex])
        psg_configure(*slot, sTables[dutyIndex], PSG_TABLE_SAMPLES);
    psg_set(*slot, fetchRateHz, volumeLeft, volumeRight);
    return true;
}

/* Claims a hardware channel only. Configuration is separate because the wave
 * voice has to build its table before it has anything to queue -- the previous
 * combined helper queued a looping wave buffer with a NULL pointer and zero
 * samples on every fresh wave note. */
/* Returns false, releasing any channel the voice already holds. Declining
 * while still holding one made the voice play in hardware and software at the
 * same time -- e.g. vibrato pushing the rate past the ceiling mid-note. */
static bool psg_decline(int* slot) {
    if (slot && *slot >= 0) NdspPsg_Stop(slot);
    return false;
}

static bool psg_claim(int* slot) {
    if (*slot >= 0) return true;
    for (int i = 0; i < PSG_CHANNEL_COUNT; ++i) {
        if (!sChannelBusy[i]) { *slot = i; break; }
    }
    if (*slot < 0) return false;  /* all four in use: stay in software */
    sChannelBusy[*slot] = true;
    sSlotData[*slot] = NULL;
    sWaveSource[*slot] = NULL;
    ++sVoicesOffloaded;
    return true;
}

static void psg_configure(int slot, const int16_t* data, int samples) {
    if (!data || samples <= 0) return;
    const int chn = PSG_FIRST_CHANNEL + slot;
    ndspChnWaveBufClear(chn);
    ndspChnReset(chn);
    /* Same reasoning as the PCM offload: match channel 0's LINEAR so hardware
     * and software voices in one mix share a resampling character. */
    ndspChnSetInterp(chn, Port_Config_AudioDspInterpLinear() ? NDSP_INTERP_LINEAR
                                                            : NDSP_INTERP_NONE);
    ndspChnSetFormat(chn, NDSP_FORMAT_MONO_PCM16);
    /* ndspChnReset above cleared the channel's IIR config; without this the
     * filter would silently apply to only part of the mix. */
    SpeakerEq3DS_ApplyToChannel(chn);
    ndspWaveBuf* buf = &sWave[slot];
    memset(buf, 0, sizeof(*buf));
    buf->data_pcm16 = (int16_t*)data;
    buf->nsamples = (u32)samples;
    buf->looping = true;
    ndspChnWaveBufAdd(chn, buf);
    sSlotData[slot] = data;
}

static void psg_set(int slot, float rateHz, float volumeLeft,
                    float volumeRight) {
    const int chn = PSG_FIRST_CHANNEL + slot;
    ndspChnSetRate(chn, rateHz);
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = volumeLeft * sMasterLevel;
    mix[1] = volumeRight * sMasterLevel;
    ndspChnSetMix(chn, mix);
}

bool NdspPsg_PlayWave(int* slot, float fetchRateHz, const unsigned char* wave16,
                      float volumeLeft, float volumeRight) {
    if (!slot) return false;
    if (!NdspPsg_Available() || !wave16) return psg_decline(slot);
    if (fetchRateHz <= 1.0f) return psg_decline(slot);
    if (fetchRateHz > PSG_MAX_RATE) { ++sVoicesRateDeclined; return psg_decline(slot); }

    if (!psg_claim(slot)) return false;
    if (sWaveSource[*slot] != wave16) {
        /* Wave RAM is sixteen bytes of paired 4-bit samples, high nibble
         * first, and the game rewrites it between notes -- so the table is
         * rebuilt rather than cached. */
        int16_t* table = sWaveTable[*slot];
        for (int i = 0; i < PSG_WAVE_SAMPLES; ++i) {
            const unsigned byte = wave16[i >> 1];
            const unsigned nibble = (i & 1) ? (byte & 0x0fu) : (byte >> 4u);
            table[i] = (int16_t)(((int)nibble - 8) * 4095);
        }
        DSP_FlushDataCache(table, PSG_WAVE_SAMPLES * sizeof(int16_t));
        sWaveSource[*slot] = wave16;
        psg_configure(*slot, table, PSG_WAVE_SAMPLES);
    }
    psg_set(*slot, fetchRateHz, volumeLeft, volumeRight);
    return true;
}

bool NdspPsg_PlayNoise(int* slot, float lfsrClockHz, bool shortPeriod,
                       float volumeLeft, float volumeRight) {
    if (!slot) return false;
    if (!NdspPsg_Available()) return psg_decline(slot);
    if (lfsrClockHz <= 1.0f) return psg_decline(slot);

    /* Pick the coarsest decimation that brings the clock under the ceiling.
     * One LFSR step per table sample at level 0; 1 << level steps above that. */
    int level = 0;
    float rate = lfsrClockHz;
    while (rate > PSG_MAX_RATE && level + 1 < PSG_NOISE_DECIM_LEVELS) {
        ++level;
        rate = lfsrClockHz / (float)(1 << level);
    }
    if (rate > PSG_MAX_RATE) { ++sVoicesRateDeclined; return psg_decline(slot); }

    int samples = 0;
    const int16_t* data = noise_table(shortPeriod, level, &samples);
    if (!data) { ++sVoicesRateDeclined; return psg_decline(slot); }

    if (!psg_claim(slot)) return false;
    if (sSlotData[*slot] != data)
        psg_configure(*slot, data, samples);
    psg_set(*slot, rate, volumeLeft, volumeRight);
    return true;
}

void NdspPsg_Stop(int* slot) {
    if (!sReady || !slot || *slot < 0) return;
    ndspChnWaveBufClear(PSG_FIRST_CHANNEL + *slot);
    sChannelBusy[*slot] = false;
    sWaveSource[*slot] = NULL;
    sSlotData[*slot] = NULL;
    *slot = -1;
}
