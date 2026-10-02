#include "MP2KContext.hpp"

#include "Constants.hpp"
#include "Debug.hpp"

#include <algorithm>
#include <cassert>

MP2KContext::MP2KContext(
    uint32_t sampleRate,
    int8_t maxLoops,
    const Rom &rom,
    const MP2KSoundMode &mp2kSoundMode,
    const AgbplaySoundMode &agbplaySoundMode,
    const SongTableInfo &songTableInfo,
    const PlayerTableInfo &playerTableInfo
) :
    rom(rom),
    reader(*this),
    mixer(*this, sampleRate, 1.0f),
    sampleRate(sampleRate),
    mp2kSoundMode(mp2kSoundMode),
    agbplaySoundMode(agbplaySoundMode),
    songTableInfo(songTableInfo),
    memaccArea(256),
    masterLoudnessCalculator(LOUDNESS_LP_FREQ, sampleRate),
    maxLoops(maxLoops)
{
    assert(playerTableInfo.size() <= 32);

    /* Reserve full capacity before any emplace_back so the vector never
     * reallocates during construction. Without this, intermediate
     * resizes move existing MP2KPlayer slots and any reference taken
     * during construction (e.g., child structures that captured a
     * &player) becomes dangling. Issue #111. */
    players.reserve(playerTableInfo.size());
    for (size_t i = 0; i < playerTableInfo.size(); i++)
        players.emplace_back(*this, playerTableInfo.at(i), static_cast<uint8_t>(i));

    mixer.UpdateFixedModeRate();
    mixer.UpdateReverb();
}

void MP2KContext::m4aSoundMain()
{
    reader.Process();
    mixer.Process();
}

void MP2KContext::m4aSoundMode(uint32_t mode)
{
    const uint8_t reverb = (mode >> 0) & 0xFF;
    m4aSoundModeReverb(reverb);

    const uint8_t channels = (mode >> 8) & 0xF;
    if (channels != 0) {
        mp2kSoundMode.maxChannels = channels;
        sndChannels.clear();
    }

    const uint8_t masterVol = (mode >> 12) & 0xF;
    m4aSoundModePCMVol(masterVol);

    const uint8_t freq = (mode >> 16) & 0xF;
    m4aSoundModePCMFreq(freq);

    const uint8_t dac = (mode >> 20) & 0xF;
    m4aSoundModeDacConfig(dac);
}

void MP2KContext::m4aSoundModeReverb(uint8_t reverb)
{
    if (reverb & MP2KSoundMode::REV_MASK_SET) {
        mp2kSoundMode.rev = reverb;
        mixer.UpdateReverb();
    }
}

void MP2KContext::m4aSoundModePCMVol(uint8_t vol)
{
    if (vol != 0)
        mp2kSoundMode.vol = vol;
}

void MP2KContext::m4aSoundModePCMFreq(uint8_t freq)
{
    if (freq != 0) {
        mp2kSoundMode.freq = freq;
        mixer.UpdateFixedModeRate();
    }
}

void MP2KContext::m4aSoundModeDacConfig(uint8_t dacConfig)
{
    if (dacConfig != 0)
        mp2kSoundMode.dacConfig = dacConfig;
}

void MP2KContext::m4aSongNumStart(uint16_t songId)
{
    if (songId >= songTableInfo.count) {
        Debug::print(
            "Failed to load song, which is out of range songId={} (total count={})", songId, songTableInfo.count
        );
        m4aMPlayStart(0, 0);
        return;
    }
    const size_t tablePos = songTableInfo.pos + songId * 8;
    const size_t songPos = (rom.ReadU32(tablePos) != 0) ? rom.ReadAgbPtrToPos(tablePos) : 0;
    const uint8_t playerIdx = m4aSongNumPlayerGet(songId);
    primaryPlayer = playerIdx;
    m4aMPlayStart(playerIdx, songPos);
}

void MP2KContext::m4aSongNumStartOrChange(uint16_t songId)
{
    if (songId >= songTableInfo.count) {
        Debug::print(
            "Failed to load song, which is out of range songId={} (total count={})", songId, songTableInfo.count
        );
        m4aMPlayStart(0, 0);
        return;
    }
    const size_t tablePos = songTableInfo.pos + songId * 8;
    const size_t songPos = (rom.ReadU32(tablePos) != 0) ? rom.ReadAgbPtrToPos(tablePos) : 0;
    const uint8_t playerIdx = m4aSongNumPlayerGet(songId);
    auto &player = players.at(playerIdx);
    primaryPlayer = playerIdx;

    if (songPos != player.bankPos || player.finished || !player.playing) {
        m4aMPlayStart(playerIdx, songPos);
    }
}

void MP2KContext::m4aSongNumStartOrContinue(uint16_t songId)
{
    if (songId >= songTableInfo.count) {
        Debug::print(
            "Failed to load song, which is out of range songId={} (total count={})", songId, songTableInfo.count
        );
        m4aMPlayStart(0, 0);
        return;
    }
    const size_t tablePos = songTableInfo.pos + songId * 8;
    const size_t songPos = (rom.ReadU32(tablePos) != 0) ? rom.ReadAgbPtrToPos(tablePos) : 0;
    const uint8_t playerIdx = m4aSongNumPlayerGet(songId);
    auto &player = players.at(playerIdx);
    primaryPlayer = playerIdx;

    if (songPos != player.songHeaderPos || player.finished)
        m4aMPlayStart(playerIdx, songPos);
    else if (!player.playing)
        m4aMPlayContinue(playerIdx);
}

void MP2KContext::m4aSongNumStop(uint16_t songId)
{
    if (songId >= songTableInfo.count) {
        throw Xcept("Cannot stop song, which is out of range songId={} (total count={})", songId, songTableInfo.count);
    }
    m4aMPlayStop(m4aSongNumPlayerGet(songId));
}

void MP2KContext::m4aMPlayStart(uint8_t playerIdx, size_t songPos)
{
    MP2KPlayer &player = players.at(playerIdx);

    if (player.usePriority && player.songHeaderPos != 0 && songPos != 0 && player.playing) {
        /* If priority of current song is higher than new song, do not start the song. */
        if (player.priority > rom.ReadU8(songPos + 2))
            return;
    }

    for (MP2KTrack &trk : player.tracks)
        trk.Stop();

    player.Init(rom, songPos);
    reader.Restart();
    mixer.ResetFade();

    m4aSoundModeReverb(player.reverb);
}

void MP2KContext::m4aMPlayStop(uint8_t playerIdx)
{
    MP2KPlayer &player = players.at(playerIdx);

    player.playing = false;

    for (MP2KTrack &trk : player.tracks)
        trk.Stop();
}

void MP2KContext::m4aMPlayAllStop()
{
    for (size_t i = 0; i < players.size(); i++)
        m4aMPlayStop(static_cast<uint8_t>(i));
}

void MP2KContext::m4aMPlayContinue(uint8_t playerIdx)
{
    players.at(playerIdx).playing = true;
}

void MP2KContext::m4aSoundClear()
{
    sndChannels.clear();
    sq1Channels.clear();
    sq2Channels.clear();
    waveChannels.clear();
    noiseChannels.clear();

    for (MP2KPlayer &player : players) {
        for (MP2KTrack &trk : player.tracks) {
            trk.reverb->Reset();
        }
    }
}

uint8_t MP2KContext::m4aSongNumPlayerGet(uint16_t songId) const
{
    return rom.ReadU8(songTableInfo.pos + songId * 8 + 4);
}

bool MP2KContext::m4aMPlayIsPlaying(uint8_t playerIdx) const
{
    return players.at(playerIdx).playing;
}

void MP2KContext::m4aSetMaxLoops(int8_t maxLoops)
{
    this->maxLoops = maxLoops;
}

bool MP2KContext::SongEnded() const
{
    return reader.EndReached() && mixer.IsFadeDone();
}

void MP2KContext::GetVisualizerState(MP2KVisualizerState &visualizerState)
{
    visualizerState.activeChannels = sndChannels.size();
    visualizerState.players.resize(players.size());
    visualizerState.primaryPlayer = primaryPlayer;

    for (size_t playerIdx = 0; playerIdx < players.size(); playerIdx++) {
        MP2KPlayer &player_src = players.at(playerIdx);
        auto &player_dst = visualizerState.players.at(playerIdx);

        player_dst.tracks.resize(player_src.tracks.size());
        player_dst.tracksUsed = player_src.tracksUsed;
        player_dst.time = player_src.frameCount / AGB_APPROX_FPS;
        player_dst.bpm = player_src.bpm;
        player_dst.bpmFactor = reader.GetSpeedFactor();

        for (size_t trackIdx = 0; trackIdx < player_src.tracks.size(); trackIdx++) {
            MP2KTrack &trk_src = player_src.tracks.at(trackIdx);
            auto &trk_dst = player_dst.tracks.at(trackIdx);

            trk_src.loudnessCalculator.CalcLoudness(trk_src.audioBuffer);
            trk_src.loudnessCalculator.GetLoudness(
                trk_dst.rmsLeft, trk_dst.rmsRight, trk_dst.peakLeft, trk_dst.peakRight
            );

            trk_dst.trackPtr = static_cast<uint32_t>(trk_src.pos);
            trk_dst.isCalling = trk_src.patternLevel > 0;
            trk_dst.isMuted = trk_src.muted;
            trk_dst.vol = trk_src.vol;
            trk_dst.mod = trk_src.mod;
            trk_dst.prog = trk_src.prog;
            trk_dst.pan = trk_src.pan;
            trk_dst.pitch = trk_src.pitch;
            trk_dst.envL = uint8_t(std::clamp<uint32_t>(uint32_t(trk_dst.rmsLeft * 768.f), 0, 255));
            trk_dst.envR = uint8_t(std::clamp<uint32_t>(uint32_t(trk_dst.rmsRight * 768.f), 0, 255));
            trk_dst.delay = std::max<uint8_t>(0, static_cast<uint8_t>(trk_src.delay));
            trk_dst.activeNotes = trk_src.activeNotes;
            trk_dst.activeVoiceTypes = trk_src.activeVoiceTypes;
        }
    }

    masterLoudnessCalculator.CalcLoudness(masterAudioBuffer);
    masterLoudnessCalculator.GetLoudness(
        visualizerState.masterRmsLeft,
        visualizerState.masterRmsRight,
        visualizerState.masterPeakLeft,
        visualizerState.masterPeakRight
    );
}
