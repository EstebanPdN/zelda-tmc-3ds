#pragma once

#include "LoudnessCalculator.hpp"
#include "MP2KChnPCM.hpp"
#include "MP2KChnPSG.hpp"
#include "MP2KPlayer.hpp"
#include "Rom.hpp"
#include "SequenceReader.hpp"
#include "SoundMixer.hpp"

#include <cstdint>
#include <list>
#include <vector>

/* Instead of defining lots of global objects, we define
 * a context with all the things we need. So anything which
 * needs anything out of this context only needs a reference
 * to a MP2KContext */

struct MP2KContext
{
    MP2KContext(
        uint32_t sampleRate,
        int8_t maxLoops,
        const Rom &rom,
        const MP2KSoundMode &mp2kSoundMode,
        const AgbplaySoundMode &agbplaySoundMode,
        const SongTableInfo &songTableInfo,
        const PlayerTableInfo &playerTableInfo
    );
    MP2KContext(const MP2KContext &) = delete;
    MP2KContext &operator=(const MP2KContext &) = delete;

    /* original API functions */
    void m4aSoundMain();
    void m4aSoundMode(uint32_t mode);
    void m4aSoundModeReverb(uint8_t reverb);
    void m4aSoundModePCMVol(uint8_t vol);
    void m4aSoundModePCMFreq(uint8_t freq);
    void m4aSoundModeDacConfig(uint8_t dacConfig);
    void m4aSongNumStart(uint16_t songId);
    void m4aSongNumStartOrChange(uint16_t songId);
    void m4aSongNumStartOrContinue(uint16_t songId);
    void m4aSongNumStop(uint16_t songId);
    void m4aMPlayStart(uint8_t playerIdx, size_t songPos);
    void m4aMPlayStop(uint8_t playerIdx);
    void m4aMPlayAllStop();
    void m4aMPlayContinue(uint8_t playerIdx);
    void m4aMPlayAllContinue();

    /* custom helper functions */
    void m4aSoundClear();
    void m4aMPlayKill(uint8_t playerIdx);
    void m4aMPlayAllKill();
    uint8_t m4aSongNumPlayerGet(uint16_t songId) const;
    bool m4aMPlayIsPlaying(uint8_t playerIdx) const;
    void m4aSetMaxLoops(int8_t maxLoops);

    bool SongEnded() const;
    void GetVisualizerState(MP2KVisualizerState &visualizerState);

    const Rom &rom;
    SequenceReader reader;
    SoundMixer mixer;
    const uint32_t sampleRate;
    MP2KSoundMode mp2kSoundMode;
    AgbplaySoundMode agbplaySoundMode;
    SongTableInfo songTableInfo;
    std::vector<MP2KPlayer> players;
    std::vector<uint8_t> memaccArea;    // TODO, this will have to be accessible from outside for emulator support
    std::vector<sample> masterAudioBuffer;
    LoudnessCalculator masterLoudnessCalculator;

    // sound channels
    std::list<MP2KChnPCM> sndChannels;
    std::list<MP2KChnPSGSquare> sq1Channels;
    std::list<MP2KChnPSGSquare> sq2Channels;
    std::list<MP2KChnPSGWave> waveChannels;
    std::list<MP2KChnPSGNoise> noiseChannels;

    uint8_t primaryPlayer = 0;    // <-- this is only used for visualization, perhaps move outside from here
    int8_t maxLoops = 1;
};
