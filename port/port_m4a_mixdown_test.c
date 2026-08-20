/*
 * Proves PortM4A_Mixdown is bit-identical to the naive reference it replaced.
 *
 * The optimisation exists because a hardware dump put this loop at 32% of one
 * 268 MHz core. It takes three different code paths depending on track count,
 * so "it sounds fine" is not evidence: a wrong path would be a quiet rounding
 * drift, not an obvious break. Every path is compared sample-for-sample
 * against a transcription of the original RenderChunkLocked loop.
 */
#include "port_m4a_mixdown.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 68 /* MP2KContext::GetSamplesPerBuffer() at 16364 Hz */
#define MAX_TRACKS 16

static int16_t QuantizeRef(float value) {
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    const float scaled = value * 32767.0f;
    return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

/*
 * Transcription of the original loop: zero the accumulators, add every track
 * with gain/pan, then apply master volume per sample and quantise.
 */
static void MixdownReference(const PortM4AMixTrack* tracks, size_t trackCount, size_t frames,
                             float masterVolume, int16_t* out) {
    static float accL[MAX_FRAMES];
    static float accR[MAX_FRAMES];
    for (size_t i = 0; i < frames; ++i) {
        accL[i] = 0.0f;
        accR[i] = 0.0f;
    }
    for (size_t t = 0; t < trackCount; ++t) {
        const PortM4AMixTrack* tr = &tracks[t];
        const size_t n = tr->frames < frames ? tr->frames : frames;
        if (tr->unity) {
            for (size_t i = 0; i < n; ++i) {
                accL[i] += tr->samples[i * 2u + 0u];
                accR[i] += tr->samples[i * 2u + 1u];
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                accL[i] += tr->samples[i * 2u + 0u] * tr->gain * tr->panL;
                accR[i] += tr->samples[i * 2u + 1u] * tr->gain * tr->panR;
            }
        }
    }
    for (size_t i = 0; i < frames; ++i) {
        float l = accL[i];
        float r = accR[i];
        if (masterVolume != 1.0f) {
            l *= masterVolume;
            r *= masterVolume;
        }
        out[i * 2u + 0u] = QuantizeRef(l);
        out[i * 2u + 1u] = QuantizeRef(r);
    }
}

static uint32_t sRng = 0x13579BDFu;
static uint32_t NextRandom(void) {
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

/* Values that actually stress the quantiser: exact rails, just past the rails
 * so clamping engages, signed zeroes, and tiny magnitudes. */
static float RandomSample(void) {
    switch (NextRandom() % 16u) {
        case 0: return 0.0f;
        case 1: return -0.0f;
        case 2: return 1.0f;
        case 3: return -1.0f;
        case 4: return 1.5f;
        case 5: return -1.5f;
        case 6: return 0.5f / 32767.0f;
        case 7: return -0.5f / 32767.0f;
        default: {
            const float unit = (float)(NextRandom() % 200001u) / 100000.0f - 1.0f;
            return unit;
        }
    }
}

int main(void) {
    static float storage[MAX_TRACKS][MAX_FRAMES * 2];
    static int16_t got[MAX_FRAMES * 2];
    static int16_t want[MAX_FRAMES * 2];
    static float accL[MAX_FRAMES];
    static float accR[MAX_FRAMES];
    PortM4AMixTrack tracks[MAX_TRACKS];

    const float masters[] = { 1.0f, 0.5f, 0.25f, 0.0f, 1.0f / 3.0f };
    unsigned cases = 0;

    /* Sweep every track count, including 0 and 1 where the fused paths live. */
    for (size_t trackCount = 0; trackCount <= MAX_TRACKS; ++trackCount) {
        for (unsigned mi = 0; mi < sizeof(masters) / sizeof(masters[0]); ++mi) {
            for (unsigned iteration = 0; iteration < 24u; ++iteration) {
                const size_t frames = 1u + (NextRandom() % MAX_FRAMES);
                for (size_t t = 0; t < trackCount; ++t) {
                    for (size_t i = 0; i < frames * 2u; ++i) {
                        storage[t][i] = RandomSample();
                    }
                    tracks[t].samples = storage[t];
                    /* Exercise short tracks: the tail must contribute silence. */
                    tracks[t].frames = (NextRandom() % 4u == 0u)
                                           ? (NextRandom() % (frames + 1u))
                                           : frames;
                    tracks[t].unity = (int)(NextRandom() % 2u);
                    if (tracks[t].unity) {
                        tracks[t].gain = 1.0f;
                        tracks[t].panL = 1.0f;
                        tracks[t].panR = 1.0f;
                    } else {
                        tracks[t].gain = (float)(NextRandom() % 256u) / 255.0f;
                        const float pan = (float)((int)(NextRandom() % 129u) - 64) / 64.0f;
                        tracks[t].panL = pan > 0.0f ? 1.0f - (pan < 1.0f ? pan : 1.0f) : 1.0f;
                        tracks[t].panR = pan < 0.0f ? 1.0f - (-pan < 1.0f ? -pan : 1.0f) : 1.0f;
                    }
                }

                memset(got, 0xA5, sizeof(got));
                memset(want, 0x5A, sizeof(want));
                MixdownReference(tracks, trackCount, frames, masters[mi], want);
                PortM4A_Mixdown(tracks, trackCount, frames, masters[mi], accL, accR, got);

                assert(memcmp(got, want, frames * 2u * sizeof(int16_t)) == 0);
                ++cases;
            }
        }
    }

    /* The zero- and one-track paths must not require scratch, which is what
     * lets the offloaded case skip accumulator traffic entirely. */
    for (size_t trackCount = 0; trackCount <= 1u; ++trackCount) {
        const size_t frames = MAX_FRAMES;
        for (size_t i = 0; i < frames * 2u; ++i) storage[0][i] = RandomSample();
        tracks[0].samples = storage[0];
        tracks[0].frames = frames;
        tracks[0].gain = 0.75f;
        tracks[0].panL = 1.0f;
        tracks[0].panR = 0.5f;
        tracks[0].unity = 0;
        MixdownReference(tracks, trackCount, frames, 0.5f, want);
        PortM4A_Mixdown(tracks, trackCount, frames, 0.5f, NULL, NULL, got);
        assert(memcmp(got, want, frames * 2u * sizeof(int16_t)) == 0);
        ++cases;
    }

    printf("port_m4a_mixdown_test: PASS (%u cases, track counts 0-%d, bit-identical)\n", cases,
           MAX_TRACKS);
    return 0;
}
