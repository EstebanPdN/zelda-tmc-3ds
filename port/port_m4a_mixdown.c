#include "port_m4a_mixdown.h"

#include <string.h>

/* Same quantiser as port_pcm_quantize.hpp, kept here so this unit has no C++
 * dependency. std::lroundf is a libm call on ARM11; clamping and adding a
 * signed half before C's truncating conversion is the same
 * round-half-away-from-zero for every finite PCM input in range. */
static inline int16_t Quantize(float value) {
    if (value > 1.0f) {
        value = 1.0f;
    } else if (value < -1.0f) {
        value = -1.0f;
    }
    const float scaled = value * 32767.0f;
    return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

void PortM4A_Mixdown(const PortM4AMixTrack* tracks, size_t trackCount, size_t frames,
                     float masterVolume, float* accL, float* accR, int16_t* out) {
    if (out == NULL || frames == 0) {
        return;
    }

    /* Nothing to mix: the result is silence, and memset beats a quantise pass
     * over two zeroed float arrays. This is the case a full DSP offload
     * produces, and it used to be the most expensive way to compute zero. */
    if (tracks == NULL || trackCount == 0) {
        memset(out, 0, frames * 2u * sizeof(int16_t));
        return;
    }

    const int scaleMaster = masterVolume != 1.0f;

    if (trackCount == 1) {
        /* Fused: quantise straight from the track. No accumulator is written
         * or read back, which on a machine with no L2 is most of the win. */
        const PortM4AMixTrack* t = &tracks[0];
        const size_t n = t->frames < frames ? t->frames : frames;
        const float* src = t->samples;
        if (t->unity) {
            for (size_t i = 0; i < n; ++i) {
                float l = src[i * 2u + 0u];
                float r = src[i * 2u + 1u];
                if (scaleMaster) {
                    l *= masterVolume;
                    r *= masterVolume;
                }
                out[i * 2u + 0u] = Quantize(l);
                out[i * 2u + 1u] = Quantize(r);
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                /* gain and pan stay separate multiplies, in the reference's
                 * order: folding them into one constant changes the rounding. */
                float l = src[i * 2u + 0u] * t->gain * t->panL;
                float r = src[i * 2u + 1u] * t->gain * t->panR;
                if (scaleMaster) {
                    l *= masterVolume;
                    r *= masterVolume;
                }
                out[i * 2u + 0u] = Quantize(l);
                out[i * 2u + 1u] = Quantize(r);
            }
        }
        /* A track shorter than the request contributes silence past its end. */
        if (n < frames) {
            memset(out + n * 2u, 0, (frames - n) * 2u * sizeof(int16_t));
        }
        return;
    }

    if (accL == NULL || accR == NULL) {
        memset(out, 0, frames * 2u * sizeof(int16_t));
        return;
    }

    /* The first track writes the accumulators rather than adding into a zeroed
     * buffer, which removes both fills. Adding 0.0f is exact, so this is
     * bit-identical to fill-then-accumulate. */
    for (size_t track = 0; track < trackCount; ++track) {
        const PortM4AMixTrack* t = &tracks[track];
        const size_t n = t->frames < frames ? t->frames : frames;
        const float* src = t->samples;
        if (track == 0) {
            if (t->unity) {
                for (size_t i = 0; i < n; ++i) {
                    accL[i] = src[i * 2u + 0u];
                    accR[i] = src[i * 2u + 1u];
                }
            } else {
                for (size_t i = 0; i < n; ++i) {
                    accL[i] = src[i * 2u + 0u] * t->gain * t->panL;
                    accR[i] = src[i * 2u + 1u] * t->gain * t->panR;
                }
            }
            for (size_t i = n; i < frames; ++i) {
                accL[i] = 0.0f;
                accR[i] = 0.0f;
            }
            continue;
        }
        if (t->unity) {
            for (size_t i = 0; i < n; ++i) {
                accL[i] += src[i * 2u + 0u];
                accR[i] += src[i * 2u + 1u];
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                accL[i] += src[i * 2u + 0u] * t->gain * t->panL;
                accR[i] += src[i * 2u + 1u] * t->gain * t->panR;
            }
        }
    }

    /* Master volume is hoisted out of the sample loop: it is 1.0 unless the
     * player moved the slider, so the common path has no multiply at all. */
    if (scaleMaster) {
        for (size_t i = 0; i < frames; ++i) {
            out[i * 2u + 0u] = Quantize(accL[i] * masterVolume);
            out[i * 2u + 1u] = Quantize(accR[i] * masterVolume);
        }
    } else {
        for (size_t i = 0; i < frames; ++i) {
            out[i * 2u + 0u] = Quantize(accL[i]);
            out[i * 2u + 1u] = Quantize(accR[i]);
        }
    }
}
