#include "gba/m4a.h"
#include "port_m4a_backend.h"
#include "sound.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "port_debug_verbose.h" /* Port_DebugVerbose — gates [bgm] trace */

#define M4A_PLAYER_COUNT 32
#define M4A_MAX_TRACKS 16

#define MUSICPLAYER_STATUS_TRACK 0x0000ffffu
#define MUSICPLAYER_STATUS_PAUSE 0x80000000u

#define FADE_VOL_SHIFT 2
#define TEMPORARY_FADE 0x0001
#define FADE_IN 0x0002

#define SOUND_MODE_FREQ_15768 0x00050000
#define SOUND_MODE_DA_BIT_8 0x00900000
#define SOUND_MODE_MASVOL_SHIFT 12
#define SOUND_MODE_MAXCHN_SHIFT 8

extern u8 gMPlayMemAccArea[0x10];

static bool sM4aInitialized;
static bool sM4aVsyncEnabled = true;
static u32 sM4aSoundMode;

static const char* const sSongLabels[] = {
#include "port_song_midi_table.inc"
};

static inline u8 ClampU8(u32 value) {
    return (value > 0xff) ? 0xff : (u8)value;
}

static inline u32 BuildTrackMask(u32 trackCount) {
    if (trackCount == 0) {
        return 0;
    }
    if (trackCount >= M4A_MAX_TRACKS) {
        return MUSICPLAYER_STATUS_TRACK;
    }
    return (1u << trackCount) - 1u;
}

const char* Port_GetSongLabel(uint16_t songId) {
    if (songId >= ARRAY_COUNT(sSongLabels)) {
        return NULL;
    }

    return sSongLabels[songId];
}

static int FindPlayerIndexByInfo(MusicPlayerInfo* mplayInfo) {
    u32 i;

    if (mplayInfo == NULL) {
        return -1;
    }

    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        if (gMusicPlayers[i].info == mplayInfo) {
            return (int)i;
        }
    }

    return -1;
}

static MusicPlayerInfo* ResolveSongPlayer(u16 songId, const SongHeader** outHeader, u16* outPlayerIndex) {
    const Song* song;
    u16 playerIndex;

    if (songId > SFX_221) {
        return NULL;
    }

    song = &gSongTable[songId];
    playerIndex = SONG_MPLAYER_INDEX(songId);
    if (playerIndex >= M4A_PLAYER_COUNT) {
        return NULL;
    }

    if (outHeader != NULL) {
        *outHeader = song->header;
    }
    if (outPlayerIndex != NULL) {
        *outPlayerIndex = playerIndex;
    }

    return gMusicPlayers[playerIndex].info;
}

static void ForEachTrackedPlayer(void (*cb)(MusicPlayerInfo*)) {
    u32 i;

    if (cb == NULL) {
        return;
    }

    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;
        if (info != NULL) {
            cb(info);
        }
    }
}


static void StopPlayer(MusicPlayerInfo* mplayInfo) {
    int playerIndex;

    if (mplayInfo == NULL) {
        return;
    }

    playerIndex = FindPlayerIndexByInfo(mplayInfo);
    if (playerIndex >= 0) {
        Port_M4A_Backend_StopPlayer((u8)playerIndex);
    }

    mplayInfo->status &= ~(MUSICPLAYER_STATUS_TRACK | MUSICPLAYER_STATUS_PAUSE);
}

static void ContinuePlayer(MusicPlayerInfo* mplayInfo) {
    int playerIndex;

    if (mplayInfo == NULL) {
        return;
    }

    playerIndex = FindPlayerIndexByInfo(mplayInfo);
    if (playerIndex >= 0) {
        Port_M4A_Backend_ContinuePlayer((u8)playerIndex);
    }

    mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
    mplayInfo->status |= BuildTrackMask(mplayInfo->trackCount);
}

void MPlayOpen(MusicPlayerInfo* mplayInfo, MusicPlayerTrack* tracks, u8 trackCount) {
    u32 i;

    if (mplayInfo == NULL || tracks == NULL) {
        return;
    }

    if (trackCount > M4A_MAX_TRACKS) {
        trackCount = M4A_MAX_TRACKS;
    }

    memset(mplayInfo, 0, sizeof(*mplayInfo));
    mplayInfo->tracks = tracks;
    mplayInfo->trackCount = trackCount;
    mplayInfo->memAccArea = gMPlayMemAccArea;
    mplayInfo->tempoD = 0x100;
    mplayInfo->tempoU = 0x100;
    mplayInfo->tempoI = 0x100;

    /* Critical: iterate only 0..trackCount-1, not M4A_MAX_TRACKS=16. The
     * sound.c MusicPlayer table assigns each player just 1 or 2 tracks
     * out of the shared gMPlayTracks pool; iterating 16 here memsets
     * 14+ adjacent tracks (or runs off the end of gMPlayTracks entirely
     * for high-index players) and clobbers neighbouring globals — caught
     * with a tripwire, gAreaRoomHeaders[6] zeroed shortly after
     * m4aSoundInit ran, then sub_080A6EE0 NULL-deref'd on the windcrest
     * pin draw (#53 second-stage). The GBA original (m4a.c::MPlayOpen)
     * uses `while (trackCount != 0) { ... ; trackCount--; tracks++; }`
     * — only touches the player's own tracks. */
    for (i = 0; i < trackCount; i++) {
        MusicPlayerTrack* track = &tracks[i];
        memset(track, 0, sizeof(*track));
        track->flags = MPT_FLG_EXIST;
        track->bendRange = 2;
        track->volX = 64;
        track->lfoSpeed = 22;
        track->lfoSpeedC = 22;
        track->tone.type = 1;
    }
}

void MPlayStart(MusicPlayerInfo* mplayInfo, const SongHeader* songHeader) {
    int playerIndex;

    if (mplayInfo == NULL) {
        return;
    }

    playerIndex = FindPlayerIndexByInfo(mplayInfo);
    mplayInfo->songHeader = songHeader;
    mplayInfo->clock = 0;
    mplayInfo->cmd = 0;
    if (songHeader == NULL) {
        mplayInfo->status = 0;
        if (playerIndex >= 0) {
            Port_M4A_Backend_StopPlayer((u8)playerIndex);
        }
        return;
    }

    mplayInfo->trackCount = songHeader->trackCount;
    mplayInfo->status = (mplayInfo->status & ~MUSICPLAYER_STATUS_PAUSE) | BuildTrackMask(songHeader->trackCount);
}

void MPlayStop(MusicPlayerInfo* mplayInfo) {
    StopPlayer(mplayInfo);
}

void MPlayContinue(MusicPlayerInfo* mplayInfo) {
    ContinuePlayer(mplayInfo);
}

void MPlayFadeOut(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }

    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = (64u << FADE_VOL_SHIFT);
}

void SoundClear(void) {
    ForEachTrackedPlayer(StopPlayer);
}

void m4aSoundInit(void) {
    u32 i;

    sM4aSoundMode =
        SOUND_MODE_DA_BIT_8 | SOUND_MODE_FREQ_15768 | (0xf << SOUND_MODE_MASVOL_SHIFT) | (8 << SOUND_MODE_MAXCHN_SHIFT);

    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;
        MusicPlayerTrack* tracks = gMusicPlayers[i].tracks;
        u8 trackCount = gMusicPlayers[i].nTracks;

        if (info == NULL || tracks == NULL || trackCount == 0) {
            continue;
        }

        MPlayOpen(info, tracks, trackCount);
        info->unk_B = gMusicPlayers[i].unk_A;
    }

    Port_M4A_Backend_SoundInit(sM4aSoundMode);
    sM4aInitialized = true;
    sM4aVsyncEnabled = true;
}

void m4aSoundMain(void) {
    u32 i;

    if (!sM4aInitialized) {
        return;
    }

    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;

        if (info == NULL) {
            continue;
        }
        if ((info->status & MUSICPLAYER_STATUS_TRACK) == 0) {
            continue;
        }
        if ((info->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
            continue;
        }

        info->clock++;
    }
}

void m4aSoundMode(u32 mode) {
    sM4aSoundMode = mode;
    Port_M4A_Backend_SetSoundMode(mode);
}

void m4aSoundVSyncOff(void) {
    sM4aVsyncEnabled = false;
    Port_M4A_Backend_SetVSyncEnabled(false);
}

void m4aSoundVSyncOn(void) {
    sM4aVsyncEnabled = true;
    Port_M4A_Backend_SetVSyncEnabled(true);
}

void m4aSoundVSync(void) {
    if (sM4aVsyncEnabled) {
        m4aSoundMain();
    }
}

void m4aSongNumStart(u16 songId) {
    if (Port_DebugVerbose) {
        const char* _l = Port_GetSongLabel(songId);
        fprintf(stderr, "[bgm] start id=0x%X (%s)\n", songId, _l ? _l : "?");
    }
    const SongHeader* header = NULL;
    u16 playerIndex = 0;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header, &playerIndex);

    if (mplayInfo == NULL) {
        return;
    }

    MPlayStart(mplayInfo, header);
    if (header == NULL) {
        MPlayStop(mplayInfo);
        return;
    }
    if (!Port_M4A_Backend_StartSongById((u8)playerIndex, songId)) {
        MPlayStop(mplayInfo);
    }
}

void m4aSongNumStartOrChange(u16 songId) {
    const SongHeader* header = NULL;
    u16 playerIndex = 0;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header, &playerIndex);

    (void)playerIndex;
    if (mplayInfo == NULL) {
        return;
    }

    if (mplayInfo->songHeader != header || (mplayInfo->status & MUSICPLAYER_STATUS_TRACK) == 0 ||
        (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
        MPlayStart(mplayInfo, header);
        if (header == NULL) {
            MPlayStop(mplayInfo);
            return;
        }
        if (!Port_M4A_Backend_StartSongById((u8)playerIndex, songId)) {
            MPlayStop(mplayInfo);
        }
    }
}

void m4aSongNumStartOrContinue(u16 songId) {
    const SongHeader* header = NULL;
    u16 playerIndex = 0;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header, &playerIndex);

    (void)playerIndex;
    if (mplayInfo == NULL) {
        return;
    }

    if (mplayInfo->songHeader != header || (mplayInfo->status & MUSICPLAYER_STATUS_TRACK) == 0) {
        MPlayStart(mplayInfo, header);
        if (header == NULL) {
            MPlayStop(mplayInfo);
            return;
        }
        if (!Port_M4A_Backend_StartSongById((u8)playerIndex, songId)) {
            MPlayStop(mplayInfo);
        }
    } else if ((mplayInfo->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
        MPlayContinue(mplayInfo);
    }
}

void m4aSongNumStop(u16 songId) {
    if (Port_DebugVerbose) {
        const char* _l = Port_GetSongLabel(songId);
        fprintf(stderr, "[bgm] stop id=0x%X (%s)\n", songId, _l ? _l : "?");
    }
    const SongHeader* header = NULL;
    u16 playerIndex = 0;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header, &playerIndex);

    (void)playerIndex;
    if (mplayInfo == NULL) {
        return;
    }

    if (mplayInfo->songHeader == header) {
        MPlayStop(mplayInfo);
    }
}

void m4aSongNumContinue(u16 songId) {
    const SongHeader* header = NULL;
    u16 playerIndex = 0;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header, &playerIndex);

    (void)playerIndex;
    if (mplayInfo == NULL) {
        return;
    }

    if (mplayInfo->songHeader == header) {
        MPlayContinue(mplayInfo);
    }
}

void m4aMPlayAllStop(void) {
    if (Port_DebugVerbose) fprintf(stderr, "[bgm] allstop\n");
    ForEachTrackedPlayer(StopPlayer);
}

void m4aMPlayImmInit(MusicPlayerInfo* mplayInfo) {
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        MusicPlayerTrack* track = &mplayInfo->tracks[i];
        if (track->flags & MPT_FLG_EXIST) {
            memset(track, 0, sizeof(*track));
            track->flags = MPT_FLG_EXIST;
            track->bendRange = 2;
            track->volX = 64;
            track->lfoSpeed = 22;
            track->lfoSpeedC = 22;
            track->tone.type = 1;
        }
    }
}

void m4aMPlayTempoControl(MusicPlayerInfo* mplayInfo, u16 tempo) {
    if (mplayInfo == NULL) {
        return;
    }

    mplayInfo->tempoD = tempo;
    mplayInfo->tempoU = tempo;
}

void m4aMPlayVolumeControl(MusicPlayerInfo* mplayInfo, u16 trackBits, u16 volume) {
    int playerIndex;
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].volX = ClampU8(volume);
        mplayInfo->tracks[i].flags |= MPT_FLG_VOLCHG;
    }

    playerIndex = FindPlayerIndexByInfo(mplayInfo);
    if (playerIndex >= 0) {
        Port_M4A_Backend_SetTrackVolume((u8)playerIndex, trackBits, volume);
    }
}

void m4aMPlayPitchControl(MusicPlayerInfo* mplayInfo, u16 trackBits, s16 pitch) {
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].pitM = (u8)pitch;
        mplayInfo->tracks[i].flags |= MPT_FLG_PITCHG;
    }
}

void m4aMPlayPanpotControl(MusicPlayerInfo* mplayInfo, u16 trackBits, s8 pan) {
    int playerIndex;
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].pan = pan;
        mplayInfo->tracks[i].flags |= MPT_FLG_VOLCHG;
    }

    playerIndex = FindPlayerIndexByInfo(mplayInfo);
    if (playerIndex >= 0) {
        Port_M4A_Backend_SetTrackPan((u8)playerIndex, trackBits, pan);
    }
}

void m4aMPlayModDepthSet(MusicPlayerInfo* mplayInfo, u16 trackBits, u8 modDepth) {
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].mod = modDepth;
    }
}

void m4aMPlayLFOSpeedSet(MusicPlayerInfo* mplayInfo, u16 trackBits, u8 lfoSpeed) {
    u32 i;

    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }

    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].lfoSpeed = lfoSpeed;
        mplayInfo->tracks[i].lfoSpeedC = lfoSpeed;
    }
}

void m4aMPlayContinue(MusicPlayerInfo* mplayInfo) {
    MPlayContinue(mplayInfo);
}

void m4aMPlayAllContinue(void) {
    ForEachTrackedPlayer(ContinuePlayer);
}

void m4aMPlayFadeOut(MusicPlayerInfo* mplayInfo, u16 speed) {
    MPlayFadeOut(mplayInfo, speed);
}

void m4aMPlayFadeOutTemporarily(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }

    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = (64u << FADE_VOL_SHIFT) | TEMPORARY_FADE;
}

void m4aMPlayFadeIn(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }

    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = FADE_IN;
    mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
}
