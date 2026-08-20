#pragma once

#include "MP2KChn.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct MP2KContext;
struct MP2KTrack;

class MP2KChnPSG : public MP2KChn
{
public:
    MP2KChnPSG(MP2KContext &ctx, MP2KTrack *track, ADSR env, Note note, bool useStairstep = false);
    MP2KChnPSG(const MP2KChnPSG &) = delete;
#if defined(__3DS__)
protected:
    /* Hardware channel this voice was handed to, or -1 while it is being
     * synthesised in software (see ndsp_psg_offload). Lives in the base so the
     * destructor above can reach it for every voice kind. */
    int ndspSlot = -1;

public:
#endif
    MP2KChnPSG &operator=(const MP2KChnPSG &) = delete;
#if defined(__3DS__)
    /* Releases the hardware channel. A PSG voice can be destroyed without a
     * final Process() -- the MONO_STRICT polyphony suppressor calls
     * channels.clear() on essentially every repeated CGB note, and a context
     * rebuild destroys all four lists -- and the DEAD check inside Process()
     * is the only other release path. Without this the slot stays claimed and
     * the DSP keeps looping the waveform at its last volume forever. */
    virtual ~MP2KChnPSG();
#else
    virtual ~MP2KChnPSG() = default;
#endif

    virtual void Process(std::span<sample> buffer, MixingArgs &args) = 0;
    void SetVol(uint16_t vol, int16_t pan);
    void Release() noexcept override;
    void Release(bool fastRelease) noexcept;
    virtual void SetPitch(int16_t pitch) = 0;
    bool TickNote() noexcept override;
    bool IsFastReleasing() const;

protected:
    virtual bool IsChn3() const;
    void stepEnvelope();
    void updateVolFade();
    void applyVol();
    VolumeFade getVol() const;
    uint8_t getPseudoEchoLevel() const;

    static float timer2freq(float timer);
    static float freq2timer(float freq);

    MP2KContext &ctx;
    enum class Pan { LEFT, CENTER, RIGHT };
    const bool useStairstep;
    bool fastRelease = false;
    uint16_t vol = 0;
    int16_t pan = 0;
    bool mp2k_sus_vol_bug_update = false;

    /* all of these values have pairs of new and old value to allow smooth fades */
    uint8_t envInterStep = 0;
    uint8_t envLevelCur = 0;
    uint8_t envPeak = 0;
    uint8_t envSustain = 0;
    uint8_t envFrameCount = 0;
    float envFadeLevel = 0.0f;
    VolumeFade volFade{0.0f, 0.0f, 0.0f, 0.0f};
    Pan panCur = Pan::CENTER;
    Pan panPrev = Pan::CENTER;
};

class MP2KChnPSGSquare : public MP2KChnPSG
{
public:
    MP2KChnPSGSquare(MP2KContext &ctx, MP2KTrack *track, uint32_t instrDuty, ADSR env, Note note, uint8_t sweep);

    void SetPitch(int16_t pitch) override;
    void Process(std::span<sample> buffer, MixingArgs &args) override;
    VoiceFlags GetVoiceType() const noexcept override;

private:
    bool sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired);

    static bool isSweepEnabled(uint8_t sweep);
    static bool isSweepAscending(uint8_t sweep);
    static float sweep2coeff(uint8_t sweep);
    static float sweep2convergence(uint8_t sweep);
    static uint8_t sweepTime(uint8_t sweep);

    const uint32_t instrDuty;
    const float *pat = nullptr;
    int16_t sweepStartCount = -1;
    const uint8_t sweep;
    const bool sweepEnabled;
    const float sweepConvergence;
    const float sweepCoeff;
    /* sweepTimer is emulated with float instead of hardware int to get sub frame accuracy */
    float sweepTimer = 1.0f;
};

class MP2KChnPSGWave : public MP2KChnPSG
{
public:
    MP2KChnPSGWave(MP2KContext &ctx, MP2KTrack *track, uint32_t instrWave, ADSR env, Note note, bool useStairstep);

    void SetPitch(int16_t pitch) override;
    void Process(std::span<sample> buffer, MixingArgs &args) override;
    VoiceFlags GetVoiceType() const noexcept override;

private:
    bool IsChn3() const override;
    VolumeFade getVol() const;
    bool sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired);
    float dcCorrection100;
    float dcCorrection75;
    float dcCorrection50;
    float dcCorrection25;
    const uint8_t *wavePtr;
};

class MP2KChnPSGNoise : public MP2KChnPSG
{
public:
    MP2KChnPSGNoise(MP2KContext &ctx, MP2KTrack *track, uint32_t instrNp, ADSR env, Note note);

    void SetPitch(int16_t pitch) override;
    void Process(std::span<sample> buffer, MixingArgs &args) override;
    VoiceFlags GetVoiceType() const noexcept override;

private:
    bool sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired);
    std::unique_ptr<Resampler> srs;
    const uint32_t instrNp;
    uint16_t noiseState;
    uint16_t noiseLfsrMask;
};
