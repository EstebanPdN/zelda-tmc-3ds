/*
 * port_a11y_audio.c — see port_a11y_audio.h.
 *
 * Producer (game thread) pushes ToneCmd into an SPSC ring; consumer
 * (audio thread, Port_A11yAudio_Mix) drains the ring into a small private
 * voice pool and mixes each voice with an attack/release envelope. Tones
 * are summed on top of the already-rendered game audio and hard-clamped.
 */
#include "port_a11y_audio.h"

#include <math.h>

/* Must match Port_Audio's output rate (port_audio.c PORT_AUDIO_SAMPLE_RATE). */
#define A11Y_SAMPLE_RATE 48000
#define A11Y_MAX_VOICES  8
#define A11Y_CMD_RING    32   /* power-of-two not required; modulo is fine here */
#define A11Y_TWO_PI      6.283185307179586

typedef struct {
    float pan;
    float freq;
    float gain;
    int   durationMs;
    int   wave;
} ToneCmd;

/* SPSC ring: sHead written by producer, sTail by consumer. */
static ToneCmd  sRing[A11Y_CMD_RING];
static uint32_t sHead;
static uint32_t sTail;

typedef struct {
    int    active;
    double phase;
    double phaseInc;
    int    remaining; /* samples left to play */
    int    total;     /* total samples (for envelope position) */
    float  gainL;
    float  gainR;
    int    wave;
} Voice;
static Voice sVoices[A11Y_MAX_VOICES]; /* audio-thread private */

void Port_A11yAudio_Beep(float pan, float freqHz, int durationMs, float gain, A11yWave wave) {
    uint32_t head = __atomic_load_n(&sHead, __ATOMIC_RELAXED);
    uint32_t next = (head + 1u) % A11Y_CMD_RING;
    if (next == __atomic_load_n(&sTail, __ATOMIC_ACQUIRE))
        return; /* ring full — drop the cue rather than block */

    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    if (freqHz < 50.0f) freqHz = 50.0f;
    if (freqHz > 8000.0f) freqHz = 8000.0f;
    if (durationMs < 10) durationMs = 10;
    if (durationMs > 1500) durationMs = 1500;

    sRing[head].pan = pan;
    sRing[head].freq = freqHz;
    sRing[head].gain = gain;
    sRing[head].durationMs = durationMs;
    sRing[head].wave = (int)wave;
    __atomic_store_n(&sHead, next, __ATOMIC_RELEASE);
}

static void A11yAudio_StartVoice(const ToneCmd* c) {
    int i, slot = -1;
    float theta;
    Voice* v;
    for (i = 0; i < A11Y_MAX_VOICES; i++) {
        if (!sVoices[i].active) { slot = i; break; }
    }
    if (slot < 0) return; /* all voices busy — drop */
    v = &sVoices[slot];
    /* Equal-power pan: theta sweeps 0..pi/2 as pan goes -1..+1. */
    theta = (c->pan + 1.0f) * 0.25f * 3.14159265f;
    v->gainL = cosf(theta) * c->gain;
    v->gainR = sinf(theta) * c->gain;
    v->phase = 0.0;
    v->phaseInc = A11Y_TWO_PI * (double)c->freq / (double)A11Y_SAMPLE_RATE;
    v->total = v->remaining = (c->durationMs * A11Y_SAMPLE_RATE) / 1000;
    v->wave = c->wave;
    v->active = 1;
}

static inline float A11yAudio_Osc(int wave, double phase) {
    double s = sin(phase);
    switch (wave) {
        case A11Y_WAVE_SQUARE:   return s >= 0.0 ? 1.0f : -1.0f;
        case A11Y_WAVE_TRIANGLE: return (float)(asin(s) * (2.0 / 3.141592653589793));
        default:                 return (float)s;
    }
}

void Port_A11yAudio_Mix(int16_t* buf, int frames) {
    uint32_t tail = __atomic_load_n(&sTail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&sHead, __ATOMIC_ACQUIRE);
    int vi;

    /* Drain queued requests into free voices. */
    while (tail != head) {
        A11yAudio_StartVoice(&sRing[tail]);
        tail = (tail + 1u) % A11Y_CMD_RING;
    }
    __atomic_store_n(&sTail, tail, __ATOMIC_RELEASE);

    for (vi = 0; vi < A11Y_MAX_VOICES; vi++) {
        Voice* v = &sVoices[vi];
        const int atk = 64;   /* attack ramp (samples) — declick */
        const int rel = 512;  /* release ramp (samples) — declick */
        int i;
        if (!v->active) continue;
        for (i = 0; i < frames && v->remaining > 0; i++, v->remaining--) {
            int pos = v->total - v->remaining; /* 0..total */
            float env = 1.0f;
            float s, addL, addR;
            int l, r;
            if (pos < atk)
                env = (float)pos / (float)atk;
            else if (v->remaining < rel)
                env = (float)v->remaining / (float)rel;
            s = A11yAudio_Osc(v->wave, v->phase) * env;
            v->phase += v->phaseInc;
            if (v->phase >= A11Y_TWO_PI) v->phase -= A11Y_TWO_PI;
            addL = s * v->gainL * 12000.0f; /* tone amplitude, leaves headroom */
            addR = s * v->gainR * 12000.0f;
            l = buf[i * 2 + 0] + (int)addL;
            r = buf[i * 2 + 1] + (int)addR;
            if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
            buf[i * 2 + 0] = (int16_t)l;
            buf[i * 2 + 1] = (int16_t)r;
        }
        if (v->remaining <= 0) v->active = 0;
    }
}
