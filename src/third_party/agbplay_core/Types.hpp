#pragma once

#include "Constants.hpp"

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

enum class CGBType : int { SQ1 = 0, SQ2, WAVE, NOISE };
enum class EnvState : int { INIT = 0, ATK, DEC, SUS, REL, PSEUDO_ECHO, DIE, DEAD };
enum class NoisePatt : int { FINE = 0, ROUGH };
enum class ReverbType : int { NORMAL, GS1, GS2, MGAT, TEST, NONE };
enum class ResamplerType : int { NEAREST, LINEAR, SINC, BLEP, BLAMP };
enum class CGBPolyphony { MONO_STRICT, MONO_SMOOTH, POLY };

enum class VoiceFlags : int {
    NONE = 0x0,
    PCM = 0x1,
    DPCM_GAMEFREAK = 0x2,
    ADPCM_CAMELOT = 0x4,
    SYNTH_PWM = 0x8,
    SYNTH_SAW = 0x10,
    SYNTH_TRI = 0x20,
    PSG_SQ_12 = 0x40,
    PSG_SQ_25 = 0x80,
    PSG_SQ_50 = 0x100,
    PSG_SQ_75 = 0x200,
    PSG_SQ_12_SWEEP = 0x400,
    PSG_SQ_25_SWEEP = 0x800,
    PSG_SQ_50_SWEEP = 0x1000,
    PSG_SQ_75_SWEEP = 0x2000,
    PSG_WAVE = 0x4000,
    PSG_NOISE_7 = 0x8000,
    PSG_NOISE_15 = 0x10000,
};

ReverbType str2rev(const std::string &str);
std::string rev2str(ReverbType t);
ResamplerType str2res(const std::string &str);
std::string res2str(ResamplerType t);
CGBPolyphony str2cgbPoly(const std::string &str);
std::string cgbPoly2str(CGBPolyphony t);

struct MixingArgs
{
    float vol;
    uint32_t fixedModeRate;
    float sampleRateInv;
    float samplesPerBufferInv;
};

struct VolumeFade
{
    float fromVolLeft;
    float fromVolRight;
    float toVolLeft;
    float toVolRight;
};

struct ADSR
{
    uint8_t att = 0xFF;
    uint8_t dec = 0x00;
    uint8_t sus = 0xFF;
    uint8_t rel = 0x00;
};

struct Note
{
    uint8_t length;
    uint8_t midiKeyTrackData;
    uint8_t midiKeyPitch;
    uint8_t velocity;
    uint8_t priority;
    int8_t rhythmPan;
    uint8_t pseudoEchoVol;
    uint8_t pseudoEchoLen;
    uint8_t trackIdx;
    uint8_t playerIdx;
};

struct SampleInfo
{
    const int8_t *samplePtr;
    size_t samplePos;
    float midCfreq;
    uint32_t loopPos;
    uint32_t endPos;
    bool loopEnabled;
    bool gamefreakCompressed;
};

struct MP2KSoundMode
{
    static const uint8_t VOL_AUTO = 0xFF;
    static const uint8_t REV_MASK_VAL = 0x7F;
    static const uint8_t REV_MASK_SET = 0x80;
    static const uint8_t FREQ_AUTO = 0xFF;
    static const uint8_t CHN_AUTO = 0xFF;
    static const uint8_t DAC_AUTO = 0xFF;

    // "rev" is intentionally not checked for something like "REV_AUTO".
    // IsAuto is currently only used to detect, whether scanning was successful.
    // For configuration it doesn't matter, since "rev" is not saved if the bit masked by REV_MASK_SET is not set.
    bool IsAuto() const {
        return vol == VOL_AUTO || freq == FREQ_AUTO || maxChannels == CHN_AUTO || dacConfig == DAC_AUTO;
    }

    uint8_t vol = VOL_AUTO;
    uint8_t rev = 0;
    uint8_t freq = FREQ_AUTO;
    uint8_t maxChannels = CHN_AUTO;
    uint8_t dacConfig = DAC_AUTO;      // currently unused
};

struct AgbplaySoundMode
{
    ResamplerType resamplerTypeNormal = ResamplerType::BLAMP;
    ResamplerType resamplerTypeFixed = ResamplerType::BLEP;
    ReverbType reverbType = ReverbType::NORMAL;
    uint8_t reverbForce = 0;
    CGBPolyphony cgbPolyphony = CGBPolyphony::MONO_STRICT;
    uint32_t dmaBufferLen = 0x630;
    bool accurateCh3Quantization = true;
    bool accurateCh3Volume = true;
    bool emulateCgbSustainBug =
        true;    // other places may call this 'simulate', should probably use 'emulate' everywhere
};

struct SongTableInfo
{
    static const size_t POS_AUTO = 0;
    static const uint16_t COUNT_AUTO = 0xFFFF;

    bool IsAuto() const {
        return pos == POS_AUTO || count == COUNT_AUTO;
    }

    size_t pos = POS_AUTO;
    uint16_t count = COUNT_AUTO;
    uint8_t tableIdx = 0;
};

struct PlayerInfo
{
    uint8_t maxTracks = 0;
    uint8_t usePriority = 0;
};

typedef std::vector<PlayerInfo> PlayerTableInfo;

struct SongInfo
{
    size_t songHeaderPos;
    size_t voiceTablePos;
    uint8_t reverb;
    uint8_t priority;
    uint8_t playerIdx;
};

struct sample
{
    float left;
    float right;
};

struct MP2KVisualizerStateTrack
{
    uint32_t trackPtr = 0;
    bool isCalling = false;
    bool isMuted = false;
    uint8_t vol = 100;                // range 0 to 127
    uint8_t mod = 0;                  // range 0 to 127
    uint8_t prog = PROG_UNDEFINED;    // range 0 to 127
    int8_t pan = 0;                   // range -64 to 63
    int16_t pitch = 0;                // range -32768 to 32767
    uint8_t envL = 0;                 // range 0 to 255
    uint8_t envR = 0;                 // range 0 to 255
    uint8_t delay = 0;                // range 0 to 96
                                      //
    std::bitset<NUM_NOTES> activeNotes;
    VoiceFlags activeVoiceTypes = VoiceFlags::NONE;
    float rmsLeft = 0.0f, rmsRight = 0.0f;
    float peakLeft = 0.0f, peakRight = 0.0f;
};

struct MP2KVisualizerStatePlayer
{
    std::vector<MP2KVisualizerStateTrack> tracks;
    float bpmFactor = 1.0f;
    uint16_t bpm = 0;
    uint8_t tracksUsed = 0;
    size_t time;
};

struct MP2KVisualizerState
{
    std::vector<MP2KVisualizerStatePlayer> players;

    float masterRmsLeft = 0.0f, masterRmsRight = 0.0f;
    float masterPeakLeft = 0.0f, masterPeakRight = 0.0f;
    size_t activeChannels = 0;
    uint8_t primaryPlayer = 0;
};
