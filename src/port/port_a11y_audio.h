/*
 * port_a11y_audio.h — PC-side spatialized tone cues for accessibility.
 *
 * A tiny synthesizer that mixes short, stereo-panned, pitched tones into
 * the SDL audio output, independent of the GBA m4a engine. Used by the
 * accessibility layer (port_a11y_cues.c) for non-verbal directional cues:
 * stereo pan conveys direction, pitch conveys distance.
 *
 * Threading: Port_A11yAudio_Beep is called from the game/input thread and
 * hands a request to a lock-free SPSC ring. Port_A11yAudio_Mix runs on the
 * SDL audio thread (from Port_Audio_Feed) and owns the voice state. Nothing
 * blocks the audio callback.
 */
#ifndef PORT_A11Y_AUDIO_H
#define PORT_A11Y_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    A11Y_WAVE_SINE = 0,
    A11Y_WAVE_SQUARE = 1,
    A11Y_WAVE_TRIANGLE = 2,
} A11yWave;

/* Enqueue a short spatialized tone. pan in [-1,1] (left..right), freq in
 * Hz, durationMs the length, gain in [0,1]. Safe from the game/input
 * thread; dropped silently if the request queue is full. */
void Port_A11yAudio_Beep(float pan, float freqHz, int durationMs, float gain, A11yWave wave);

/* Mix active tones into a stereo interleaved S16 buffer. Called on the SDL
 * audio thread from Port_Audio_Feed; `frames` is the number of stereo
 * frames in `buf`. No-op when nothing is playing. */
void Port_A11yAudio_Mix(int16_t* buf, int frames);

#ifdef __cplusplus
}
#endif

#endif /* PORT_A11Y_AUDIO_H */
