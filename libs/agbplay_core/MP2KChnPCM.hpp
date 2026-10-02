#pragma once

#include "MP2KChn.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct MP2KContext;

class MP2KChnPCM : public MP2KChn
{
private:
    struct ProcArgs
    {
        float lVol;
        float rVol;
        float lVolStep;
        float rVolStep;
        float interStep;
    };

public:
    MP2KChnPCM(MP2KContext &ctx, MP2KTrack *track, SampleInfo sInfo, ADSR env, const Note &note, bool fixed);
    MP2KChnPCM(const MP2KChnPCM &) = delete;
    MP2KChnPCM &operator=(const MP2KChnPCM &) = delete;

    void Process(std::span<sample> buffer, const MixingArgs &args);
    void SetVol(uint16_t vol, int16_t pan);
    void Release() noexcept override;
    bool IsReleasing() const noexcept;
    void SetPitch(int16_t pitch);
    bool TickNote() noexcept override;
    VoiceFlags GetVoiceType() const noexcept override;

private:
    void stepEnvelope();
    void updateVolFade();
    VolumeFade getVol() const;
    void processNormal(std::span<sample> buffer, ProcArgs &cargs);
    void processModPulse(std::span<sample> buffer, ProcArgs &cargs, float samplesPerBufferInv);
    void processSaw(std::span<sample> buffer, ProcArgs &cargs);
    void processTri(std::span<sample> buffer, ProcArgs &cargs);
    bool sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired);
    bool sampleFetchCallbackGFDPCMDecomp(std::vector<float> &fetchBuffer, size_t samplesRequires);
    bool sampleFetchCallbackMPTDecomp(std::vector<float> &fetchBuffer, size_t samplesRequires);

    enum class Type {
        INVALID,
        PCM,
        GAMEFREAK_DPCM,
        CAMELOT_ADPCM,
        SYNTH_PWM,
        SYNTH_SAWTOOTH,
        SYNTH_TRIANGLE
    } type = Type::INVALID;

    MP2KContext &ctx;
    SampleInfo sInfo;
    bool fixed;
    bool isSynth = false;
    int16_t levelMPTcompressed = 0;
    uint8_t shiftMPTcompressed = 0x38;

    /* all of these values have pairs of new and old value to allow smooth fades */
    uint8_t envInterStep = 0;
    uint8_t envLevelCur;
    uint8_t envLevelPrev;
    uint8_t leftVolCur = 0;
    uint8_t leftVolPrev;
    uint8_t rightVolCur = 0;
    uint8_t rightVolPrev;
};
