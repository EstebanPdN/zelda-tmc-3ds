#ifndef TMC_PORT_M4A_MIXDOWN_H
#define TMC_PORT_M4A_MIXDOWN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MP2K track mixdown: per-track gain/pan, master volume, and the float->PCM16
 * quantise, extracted from RenderChunkLocked so it can be measured and tested
 * on the host.
 *
 * Why this is its own unit: a hardware dump measured the 3DS audio worker at
 * 5.367 ms per 15.64 ms buffer -- 34% of one 268 MHz core -- of which the
 * sequencer and software mix (`m4aSoundMain`, the part the NDSP offload
 * replaced) accounted for only 0.349 ms. The other 93% is this post-processing,
 * which the code had assumed was the cheap half and which nothing measured.
 *
 * The ARM11's VFPv2 is not pipelined, so every float op costs 4-9 cycles and
 * stalls integer execution. That makes the memory traffic around this loop as
 * expensive as the arithmetic in it, which is what the strategies below target:
 *
 *   0 tracks  -> no float work at all; the output is silence. This is the case
 *                the DSP offload creates, and the old code still paid two
 *                accumulator fills plus a full quantise pass to produce zeros.
 *   1 track   -> quantise straight from the track, with no accumulator round
 *                trip (the old code wrote 2*frames floats and read them back).
 *   N tracks  -> the first track writes the accumulators instead of adding to
 *                a zeroed buffer, which removes both fills.
 *
 * Every strategy is bit-identical to the naive reference: adding 0.0f is exact
 * in IEEE754, and track summation order is preserved. port_m4a_mixdown_test
 * proves this over randomised and edge-case inputs rather than asserting it.
 */
typedef struct PortM4AMixTrack {
    /* Interleaved L,R float frames. `frames` may be shorter than the request,
     * in which case the tail contributes silence. */
    const float* samples;
    size_t frames;
    float gain;
    float panL;
    float panR;
    /* Non-zero when gain and pan are both identity, so the sample can be added
     * without any multiply. Mirrors the volume==0xFF && pan==0 fast path. */
    int unity;
} PortM4AMixTrack;

/*
 * Writes `frames` interleaved PCM16 stereo frames to `out`.
 *
 * `accL`/`accR` are caller-owned scratch of at least `frames` floats. They are
 * only touched when trackCount > 1, so the common offloaded case needs no
 * scratch traffic. Passing NULL scratch is valid when trackCount <= 1.
 */
void PortM4A_Mixdown(const PortM4AMixTrack* tracks, size_t trackCount, size_t frames,
                     float masterVolume, float* accL, float* accR, int16_t* out);

#ifdef __cplusplus
}
#endif

#endif /* TMC_PORT_M4A_MIXDOWN_H */
