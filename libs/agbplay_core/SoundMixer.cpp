#include "SoundMixer.hpp"

#include "MP2KContext.hpp"
#include "Util.hpp"
#include "Xcept.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

/*
 * public SoundMixer
 */

SoundMixer::SoundMixer(MP2KContext &ctx, uint32_t sampleRate, float masterVolume) :
    ctx(ctx), sampleRate(sampleRate), masterVolume(masterVolume), scratchBuffer(samplesPerBuffer)
{
}

void SoundMixer::UpdateReverb()
{
    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            trk.reverb->SetLevel(GetReverbLevel());
        }
    }
}

void SoundMixer::UpdateFixedModeRate()
{
    static const std::array<uint32_t, 16> rateTable{
        0, 5734, 7884, 10512, 13379, 15768, 18157, 21024, 26758, 31536, 36314, 40137, 42048, 0, 0, 0
    };

    fixedModeRate = rateTable[ctx.mp2kSoundMode.freq % rateTable.size()];

    assert(fixedModeRate > 0);

    const uint8_t numDmaBuffers = std::max(
        static_cast<uint8_t>(2), static_cast<uint8_t>(ctx.agbplaySoundMode.dmaBufferLen / (fixedModeRate / AGB_APPROX_FPS))
    );

    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            trk.reverb = ReverbEffect::MakeReverb(
                ctx.agbplaySoundMode.reverbType, GetReverbLevel(), sampleRate, numDmaBuffers
            );
        }
    }
}

void SoundMixer::Process()
{
#ifdef TMC_3DS
    constexpr uint16_t REVERB_TAIL_BLOCKS = 240;
    const uint8_t reverbLevel = GetReverbLevel();

    previousActiveTracks.clear();
    previousActiveTracks.swap(activeTracks);
    for (MP2KTrack *trk : previousActiveTracks) {
        trk->audioBufferActive = trk->audioTailBlocks > 0;
        if (!trk->audioBufferActive)
            continue;
        std::fill(trk->audioBuffer.begin(), trk->audioBuffer.end(), sample{0.0f, 0.0f});
        trk->audioTailBlocks--;
        activeTracks.push_back(trk);
    }

    MixingArgs margs;
    margs.vol = static_cast<float>((ctx.mp2kSoundMode.vol + 1) / 16.0f);
    margs.fixedModeRate = fixedModeRate;
    margs.sampleRateInv = 1.0f / static_cast<float>(sampleRate);
    margs.samplesPerBufferInv = 1.0f / static_cast<float>(samplesPerBuffer);

    auto mixFunc = [&](auto &channels, bool feedsReverb) {
        for (auto &chn : channels) {
            MP2KTrack &trk = *chn.trackOrg;
            if (!trk.audioBufferActive) {
                std::fill(trk.audioBuffer.begin(), trk.audioBuffer.end(), sample{0.0f, 0.0f});
                trk.audioBufferActive = true;
                activeTracks.push_back(&trk);
            }
            if (feedsReverb && reverbLevel != 0)
                trk.audioTailBlocks = REVERB_TAIL_BLOCKS;
            chn.Process(trk.audioBuffer, margs);
        }
    };

    mixFunc(ctx.sndChannels, true);

    if (reverbLevel != 0) {
        for (MP2KPlayer &player : ctx.players) {
            for (MP2KTrack &trk : player.tracks) {
                if (trk.audioBufferActive)
                    trk.reverb->Process(trk.audioBuffer);
            }
        }
    }

    mixFunc(ctx.sq1Channels, false);
    mixFunc(ctx.sq2Channels, false);
    mixFunc(ctx.waveChannels, false);
    mixFunc(ctx.noiseChannels, false);

    auto removeFunc = [](const auto &chn) { return chn.envState == EnvState::DEAD; };
    ctx.sndChannels.remove_if(removeFunc);
    ctx.sq1Channels.remove_if(removeFunc);
    ctx.sq2Channels.remove_if(removeFunc);
    ctx.waveChannels.remove_if(removeFunc);
    ctx.noiseChannels.remove_if(removeFunc);

    float masterFrom = masterVolume;
    float masterTo = masterVolume;
    if (fadeMicroframesLeft > 0) {
        masterFrom = fadePos < 0.f ? 0.f : masterFrom * powf(fadePos, 10.0f / 6.0f);
        fadePos += fadeStepPerMicroframe;
        masterTo = fadePos < 0.f ? 0.f : masterTo * powf(fadePos, 10.0f / 6.0f);
        fadeMicroframesLeft--;
    }

    std::sort(activeTracks.begin(), activeTracks.end(), [](const MP2KTrack *left, const MP2KTrack *right) {
        if (left->playerIdx != right->playerIdx)
            return left->playerIdx < right->playerIdx;
        return left->trackIdx < right->trackIdx;
    });

    if (masterFrom != 1.0f || masterTo != 1.0f) {
        for (MP2KTrack *trk : activeTracks) {
            const float masterStep = (masterTo - masterFrom) * margs.samplesPerBufferInv;
            float masterLevel = masterFrom;
            for (size_t i = 0; i < samplesPerBuffer; i++) {
                trk->audioBuffer[i].left *= masterLevel;
                trk->audioBuffer[i].right *= masterLevel;
                masterLevel += masterStep;
            }
        }
    }

#else
    /* 1. clear the mixing buffer before processing channels */
    ctx.masterAudioBuffer.resize(samplesPerBuffer);
    std::fill(ctx.masterAudioBuffer.begin(), ctx.masterAudioBuffer.end(), sample{0.0f, 0.0f});

    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            trk.audioBuffer.resize(samplesPerBuffer);
            std::fill(trk.audioBuffer.begin(), trk.audioBuffer.end(), sample{0.0f, 0.0f});
        }
    }

    /* 2. prepare arguments for mixing */
    MixingArgs margs;
    margs.vol = static_cast<float>((ctx.mp2kSoundMode.vol + 1) / 16.0f);
    margs.fixedModeRate = fixedModeRate;
    margs.sampleRateInv = 1.0f / static_cast<float>(sampleRate);
    margs.samplesPerBufferInv = 1.0f / static_cast<float>(samplesPerBuffer);

    /* 3. mix channels which are affected by reverb (PCM only) */
    auto mixFunc = [&](auto &channels) {
        for (auto &chn : channels)
            chn.Process(chn.trackOrg->audioBuffer, margs);
    };
    mixFunc(ctx.sndChannels);

    /* 4. apply reverb */
    // TODO add player for-loop for multiple players
    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            trk.reverb->Process(trk.audioBuffer);
        }
    }

    /* 5. mix channels which are not affected by reverb (CGB) */
    mixFunc(ctx.sq1Channels);
    mixFunc(ctx.sq2Channels);
    mixFunc(ctx.waveChannels);
    mixFunc(ctx.noiseChannels);

    /* 6. clean up all stopped channels */
    auto removeFunc = [](const auto &chn) { return chn.envState == EnvState::DEAD; };
    ctx.sndChannels.remove_if(removeFunc);
    ctx.sq1Channels.remove_if(removeFunc);
    ctx.sq2Channels.remove_if(removeFunc);
    ctx.waveChannels.remove_if(removeFunc);
    ctx.noiseChannels.remove_if(removeFunc);

    /* 7. apply fadeout if active */
    // TODO move this to FadeOutMain
    float masterFrom = masterVolume;
    float masterTo = masterVolume;
    if (fadeMicroframesLeft > 0) {
        if (fadePos < 0.f) {
            masterFrom = 0.f;
        } else {
            masterFrom *= powf(fadePos, 10.0f / 6.0f);
        }
        fadePos += fadeStepPerMicroframe;
        if (fadePos < 0.f) {
            masterTo = 0.f;
        } else {
            masterTo *= powf(fadePos, 10.0f / 6.0f);
        }
        fadeMicroframesLeft--;
    }

    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            const float masterStep = (masterTo - masterFrom) * margs.samplesPerBufferInv;
            float masterLevel = masterFrom;
            for (size_t i = 0; i < samplesPerBuffer; i++) {
                trk.audioBuffer[i].left *= masterLevel;
                trk.audioBuffer[i].right *= masterLevel;

                masterLevel += masterStep;
            }
        }
    }

    /* 8. master mixdown */
    for (MP2KPlayer &player : ctx.players) {
        for (MP2KTrack &trk : player.tracks) {
            if (trk.muted)
                continue;

            assert(ctx.masterAudioBuffer.size() == trk.audioBuffer.size());
            for (size_t i = 0; i < ctx.masterAudioBuffer.size(); i++) {
                ctx.masterAudioBuffer[i].left += trk.audioBuffer[i].left;
                ctx.masterAudioBuffer[i].right += trk.audioBuffer[i].right;
            }
        }
    }
#endif
}

size_t SoundMixer::GetSamplesPerBuffer() const
{
    return samplesPerBuffer;
}

double SoundMixer::GetBufferLengthSpeedCorrection() const
{
    return static_cast<double>(samplesPerBuffer) / samplesPerBufferExact;
}

void SoundMixer::ResetFade()
{
    fadePos = 0.0f;
    fadeMicroframesLeft = 0;
}

void SoundMixer::StartFadeOut(float millis)
{
    fadePos = 1.0f;
    fadeMicroframesLeft = size_t(millis / 1000.0f * float(AGB_APPROX_FPS * INTERFRAMES));
    fadeStepPerMicroframe = -1.0f / float(fadeMicroframesLeft);
}

void SoundMixer::StartFadeIn(float millis)
{
    fadePos = 0.0f;
    fadeMicroframesLeft = size_t(millis / 1000.0f * float(AGB_APPROX_FPS * INTERFRAMES));
    fadeStepPerMicroframe = 1.0f / float(fadeMicroframesLeft);
}

bool SoundMixer::IsFadeDone() const
{
    return fadeMicroframesLeft == 0;
}

uint8_t SoundMixer::GetReverbLevel() const
{
    if (ctx.agbplaySoundMode.reverbForce & MP2KSoundMode::REV_MASK_SET)
        return ctx.agbplaySoundMode.reverbForce & MP2KSoundMode::REV_MASK_VAL;
    else
        return ctx.mp2kSoundMode.rev & MP2KSoundMode::REV_MASK_VAL;
}

#ifdef TMC_3DS
void SoundMixer::ReserveActiveTracks(size_t count)
{
    activeTracks.reserve(count);
    previousActiveTracks.reserve(count);
}

std::span<MP2KTrack *const> SoundMixer::GetActiveTracks() const
{
    return activeTracks;
}
#endif
