#ifndef TMC_SPEAKER_EQ_3DS_H
#define TMC_SPEAKER_EQ_3DS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Output shaping for the 3DS's built-in speakers.
 *
 * The 3DS drivers are small mylar cones with no useful output below roughly
 * 200-300 Hz. Content down there is not reproduced as tone; it consumes
 * amplifier headroom and cone excursion, which shows up as distortion and
 * rattle and as intermodulation that muddies the mids. High-passing it makes
 * the same digital level sound louder and cleaner through the speakers.
 *
 * The Teak DSP has a per-channel IIR biquad, so this costs zero ARM11 cycles.
 *
 * Two rules this module exists to enforce:
 *
 *   1. Apply to EVERY channel that carries audio, never a subset. Voices are
 *      spread across the software mix (channel 0) and the offload channels, and
 *      filtering only some of them reproduces the split-character mix bug that
 *      made offloaded voices sound brighter than software ones.
 *   2. Re-apply after ndspChnReset(), which clears channel configuration. The
 *      offload resets a channel on every note-on, so a filter applied only at
 *      init would silently disappear from those voices.
 *
 * Bypassed automatically while headphones are connected: the cutoff is a
 * correction for a specific transducer, and headphones do not need it.
 *
 * This deliberately departs from GBA output, so it is opt-in.
 */

/* Applies or clears the filter on one channel according to current config and
 * headphone state. Call after any ndspChnReset() on that channel. */
void SpeakerEq3DS_ApplyToChannel(int channel);

/* Re-applies to every channel the port uses. Cheap; call when config or
 * headphone state changes. */
void SpeakerEq3DS_ApplyAll(void);

/* Polls headphone state and re-applies everything if it changed. Intended for
 * the audio pump, which already runs once per frame. */
void SpeakerEq3DS_Poll(void);

/* Whether the filter is currently shaping output, for the quick dump. */
bool SpeakerEq3DS_Active(void);

#ifdef __cplusplus
}
#endif

#endif /* TMC_SPEAKER_EQ_3DS_H */
