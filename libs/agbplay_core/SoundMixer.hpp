#pragma once

#include "Constants.hpp"
#include "ReverbEffect.hpp"

#include <bitset>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

struct MP2KContext;

class SoundMixer
{
public:
    SoundMixer(MP2KContext &ctx, uint32_t sampleRate, float masterVolume);
    SoundMixer(const SoundMixer &) = delete;
    SoundMixer &operator=(const SoundMixer &) = delete;

    void UpdateReverb();
    void UpdateFixedModeRate();

    void Process();
    size_t GetSamplesPerBuffer() const;
    double GetBufferLengthSpeedCorrection() const;
    void ResetFade();
    void StartFadeOut(float millis);
    void StartFadeIn(float millis);
    bool IsFadeDone() const;
    uint8_t GetReverbLevel() const;

private:
    MP2KContext &ctx;

    const uint32_t sampleRate;
    uint32_t fixedModeRate = 13379;
    const size_t samplesPerBuffer = static_cast<size_t>(sampleRate / (AGB_EXACT_FPS * INTERFRAMES));
    const double samplesPerBufferExact = static_cast<double>(sampleRate) / static_cast<double>(AGB_EXACT_FPS * INTERFRAMES);

    // volume control related stuff

    const float masterVolume;
    float fadePos = 1.0f;
    float fadeStepPerMicroframe = 0.0f;
    size_t fadeMicroframesLeft = 0;

public:
    std::vector<float> scratchBuffer;
};
