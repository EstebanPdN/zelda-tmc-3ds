#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// TODO remove dependency for NUM_NOTES, and possibly remove active notes state?
#include "Constants.hpp"
#include "LoudnessCalculator.hpp"
#include "Types.hpp"

#define TRACK_CALL_STACK_SIZE 3

enum class MODT : int { PITCH = 0, VOL, PAN };

struct MP2KChn;
struct MP2KContext;
class ReverbEffect;

struct MP2KTrack {
    MP2KTrack(const MP2KContext& ctx, uint8_t playerIdx, uint8_t trackIdx);
    MP2KTrack(const MP2KTrack&) = delete;
    MP2KTrack(MP2KTrack&&) = default;
    MP2KTrack& operator=(const MP2KTrack&) = delete;

    void Init(size_t pos);
    void Stop();

    int16_t GetPitch();
    uint16_t GetVol();
    int16_t GetPan();
    void ResetLfoValue();

    std::bitset<NUM_NOTES> activeNotes;
    VoiceFlags activeVoiceTypes;
    std::vector<sample> audioBuffer;
    std::unique_ptr<ReverbEffect> reverb;
    LoudnessCalculator loudnessCalculator;

    size_t pos;
    size_t returnPos[TRACK_CALL_STACK_SIZE];
    uint8_t patternLevel;
    MODT modt = MODT::PITCH;
    uint8_t lastCmd;
    int16_t pitch;
    uint8_t lastNoteKey;
    uint8_t lastNoteVel;
    uint8_t lastNoteLen;
    uint8_t reptCount;
    uint8_t prog;
    uint8_t vol;
    uint8_t mod;
    uint8_t bendr;
    uint8_t priority;
    uint8_t lfos;
    uint8_t lfodl;
    uint8_t lfodlCount;
    uint8_t lfoPhase;
    int8_t lfoValue;
    uint8_t pseudoEchoVol;
    uint8_t pseudoEchoLen;
    uint16_t delay;
    int8_t pan;
    int8_t bend;
    int8_t tune;
    int8_t keyShift;
    bool muted;
    bool enabled;
    bool updateVolume;
    bool updatePitch;
#ifdef TMC_3DS
    bool audioBufferActive;
    uint16_t audioTailBlocks;
#endif

    /* Stable cross-player ordering key (see SequenceReader's PCM channel
     * steal): hardware resolves ties by track ADDRESS, which is a fixed
     * (player, track) order in GBA RAM. The port's tracks live in per-player
     * heap vectors, so pointer comparison across players is indeterminate —
     * compare (playerIdx, trackIdx) instead. */
    const uint8_t playerIdx;
    const uint8_t trackIdx;

    MP2KChn* channels = nullptr;
};
