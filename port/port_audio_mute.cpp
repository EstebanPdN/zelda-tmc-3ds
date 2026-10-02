/*
 * port/port_audio_mute.cpp — toggle store for per-category SFX mute.
 * Toggles are process-local (no on-disk persistence yet) and surface
 * in the F8 → Audio tab.
 */

#include "port_audio_mute.h"
#include "sound.h"

namespace {

struct {
    bool enabled;
    const char* label;
    const char* description;
} sCategories[AUDIO_MUTE_COUNT] = {
    /* AUDIO_MUTE_EZLO_VOICE */
    {
        false,
        "Mute Ezlo voice",
        "Suppress the SFX_VO_EZLO* samples — Ezlo's yip / hint vocal "
        "noises. Text dialogue still plays normally.",
    },
    /* AUDIO_MUTE_NPC_VOICE */
    {
        false,
        "Mute all NPC voice",
        "Suppress every SFX_VO_* — Ezlo, Zelda, King, Tingle, Minish, "
        "Beedle, Sturgeon, dog/cat/cow/cucco/Epona, etc. Animals stop "
        "barking; NPCs stop yelping.",
    },
    /* AUDIO_MUTE_LOW_HEALTH_BEEP */
    {
        false,
        "Mute low-health beep",
        "Suppress the once-per-1.5s beep that fires when below 1/4 HP. "
        "Doesn't change game-feel — the HUD still pulses red.",
    },
};

} /* namespace */

extern "C" bool Port_AudioMute_IsEnabled(AudioMuteCategory c) {
    if ((unsigned)c >= AUDIO_MUTE_COUNT) return false;
    return sCategories[c].enabled;
}

extern "C" void Port_AudioMute_SetEnabled(AudioMuteCategory c, bool on) {
    if ((unsigned)c >= AUDIO_MUTE_COUNT) return;
    sCategories[c].enabled = on;
}

extern "C" const char* Port_AudioMute_Label(AudioMuteCategory c) {
    if ((unsigned)c >= AUDIO_MUTE_COUNT) return "";
    return sCategories[c].label;
}

extern "C" const char* Port_AudioMute_Description(AudioMuteCategory c) {
    if ((unsigned)c >= AUDIO_MUTE_COUNT) return "";
    return sCategories[c].description;
}

extern "C" bool Port_AudioMute_ShouldSuppress(unsigned int soundReq) {
    /* SoundReq packs the song ID in the low 16 bits; the high 16 bits
     * are SONG_* command flags (STOP, FADE_IN, etc.). The voice/beep
     * SFX are plain song IDs (high half zero), so suppression only
     * needs to look at the lower half. */
    unsigned int song = soundReq & 0xFFFFu;

    /* SFX_VO_EZLO1..7 are contiguous in include/sound.h. */
    const bool isEzlo =
        (song >= (unsigned)SFX_VO_EZLO1 && song <= (unsigned)SFX_VO_EZLO7);
    if (isEzlo) {
        if (sCategories[AUDIO_MUTE_EZLO_VOICE].enabled) return true;
        if (sCategories[AUDIO_MUTE_NPC_VOICE].enabled)  return true;
    }

    /* The wider NPC-voice net. Listed individually because the SFX_VO_*
     * IDs aren't a single contiguous range in include/sound.h
     * (interrupted by SFX_9B..SFX_A0, SFX_A8..SFX_AC, SFX_B5..SFX_BD,
     * etc.). Keep this in sync with new SFX_VO_* additions. */
    if (sCategories[AUDIO_MUTE_NPC_VOICE].enabled) {
        switch (song) {
        case SFX_VO_ZELDA1: case SFX_VO_ZELDA2: case SFX_VO_ZELDA3:
        case SFX_VO_ZELDA4: case SFX_VO_ZELDA5: case SFX_VO_ZELDA6:
        case SFX_VO_ZELDA7:
        case SFX_VO_TINGLE1: case SFX_VO_TINGLE2:
        case SFX_VO_KING1: case SFX_VO_KING2: case SFX_VO_KING3:
        case SFX_VO_KING4: case SFX_VO_KING5:
        case SFX_VO_BEEDLE:
        case SFX_VO_MINISH1: case SFX_VO_MINISH2: case SFX_VO_MINISH3:
        case SFX_VO_MINISH4:
        case SFX_VO_DOG: case SFX_VO_CAT: case SFX_VO_EPONA:
        case SFX_VO_COW: case SFX_VO_CUCCO_CALL: case SFX_VO_CHEEP:
        case SFX_VO_STURGEON:
        case SFX_VO_GORON1: case SFX_VO_GORON2: case SFX_VO_GORON3:
        case SFX_VO_GORON4:
        case SFX_VO_CUCCO1: case SFX_VO_CUCCO2: case SFX_VO_CUCCO3:
            return true;
        default: break;
        }
    }

    if (sCategories[AUDIO_MUTE_LOW_HEALTH_BEEP].enabled) {
        if (song == (unsigned)SFX_LOW_HEALTH) return true;
    }

    return false;
}
