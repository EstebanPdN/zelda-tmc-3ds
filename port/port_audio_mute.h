/*
 * port/port_audio_mute.h — per-category SFX mute toggles.
 *
 * Plumbed into src/sound.c::SoundReq so individual sound IDs (or
 * ranges) can be suppressed at dispatch time without touching the
 * caller. The toggles are surfaced in the F8 → Audio tab.
 *
 * Adding a new toggle: extend AudioMuteCategory, add a default in
 * port_audio_mute.cpp::sCategories[], and add the relevant SFX IDs to
 * Port_AudioMute_ShouldSuppress's switch.
 */

#ifndef PORT_AUDIO_MUTE_H
#define PORT_AUDIO_MUTE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_MUTE_EZLO_VOICE,     /* Ezlo's "yip" / hint voice samples */
    AUDIO_MUTE_NPC_VOICE,      /* All SFX_VO_* — NPCs, animals, etc. */
    AUDIO_MUTE_LOW_HEALTH_BEEP,/* The repeating beep when below 1/4 HP */
    AUDIO_MUTE_COUNT,
} AudioMuteCategory;

bool Port_AudioMute_IsEnabled(AudioMuteCategory c);
void Port_AudioMute_SetEnabled(AudioMuteCategory c, bool on);
const char* Port_AudioMute_Label(AudioMuteCategory c);
const char* Port_AudioMute_Description(AudioMuteCategory c);

/* Filter hook called from src/sound.c::SoundReq. Returns true when
 * the given sound ID should be dropped (i.e. *not* played) based on
 * the current toggle state. The full SoundReq word is passed so range
 * checks can include the high-half "SONG_*" command bits if ever
 * needed. */
bool Port_AudioMute_ShouldSuppress(unsigned int soundReq);

#ifdef __cplusplus
}
#endif

#endif /* PORT_AUDIO_MUTE_H */
