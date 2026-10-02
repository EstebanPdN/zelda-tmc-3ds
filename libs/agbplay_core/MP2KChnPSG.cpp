#include "MP2KChnPSG.hpp"

#include "CGBPatterns.hpp"
#include "Constants.hpp"
#include "Debug.hpp"
#include "MP2KContext.hpp"
#include "Util.hpp"
#include "Xcept.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

/*
 * public MP2KChnPSG
 */

MP2KChnPSG::MP2KChnPSG(MP2KContext &ctx, MP2KTrack *track, ADSR env, Note note, bool useStairstep) :
    MP2KChn(track, note, env), ctx(ctx), useStairstep(useStairstep)
{
    this->env.att &= 0x7;
    this->env.dec &= 0x7;
    this->env.sus &= 0xF;
    this->env.rel &= 0x7;

    // if (note.trackIdx == 6)
    //     Debug::print("note start: this=%p att=%d dec=%d sus=%d rel=%d", this, (int)env.att, (int)env.dec,
    //     (int)env.sus, (int)env.rel);
}

void MP2KChnPSG::SetVol(uint16_t vol, int16_t pan)
{
    if (stop)
        return;

    /* CGB volume and pan aren't applied immediately due to the nature of being heavily
     * intertwined with the envelope handling. So save them and actually update them during envelope handling */
    this->vol = vol;
    /* Due to aggressive rounding apply pan "perfectly" like for PCM channels,
     * so clamp to 127 instead of 128 */
    this->pan = std::clamp<int16_t>(pan, -128, 127);
    this->mp2k_sus_vol_bug_update = true;
}

VolumeFade MP2KChnPSG::getVol() const
{
    return volFade;
}

uint8_t MP2KChnPSG::getPseudoEchoLevel() const
{
    return static_cast<uint8_t>(((envPeak * note.pseudoEchoVol) + 0xFF) >> 8);
}

float MP2KChnPSG::timer2freq(float timer)
{
    assert(timer >= 0.0f);
    assert(timer <= 2047.0f);
    return 131072.0f / static_cast<float>(2048.0f - timer);
}

float MP2KChnPSG::freq2timer(float freq)
{
    assert(freq > 0.0f);
    return 2048.0f - std::min(131072.0f / freq, 2047.0f);
}

void MP2KChnPSG::Release() noexcept
{
    Release(false);
}

void MP2KChnPSG::Release(bool fastRelease) noexcept
{
    this->stop = true;
    this->fastRelease = fastRelease;

    // if (note.trackIdx == 6)
    //     Debug::print("releasing: %d", fastRelease);
}

bool MP2KChnPSG::TickNote() noexcept
{
    if (envState < EnvState::REL) {
        if (note.length > 0) {
            note.length--;
            if (note.length == 0) {
                /* Notes that stop on their own never release fast */
                Release(false);
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}

bool MP2KChnPSG::IsFastReleasing() const
{
    return fastRelease;
}

bool MP2KChnPSG::IsChn3() const
{
    return false;
}

void MP2KChnPSG::stepEnvelope()
{
    if (envState == EnvState::INIT) {
        if (stop) {
            envState = EnvState::DEAD;
            return;
        }

        applyVol();
        panPrev = panCur;

        envInterStep = 0;

        envLevelCur = 0;
        envFrameCount = env.att;
        envState = EnvState::ATK;

        if (envFrameCount > 0) {
            envFadeLevel = 0.0f;
            return;
        } else {
            if (env.dec > 0) {
                envFadeLevel = envPeak;
            } else if (envSustain > 0) {
                envFadeLevel = envSustain;
            } else if (getPseudoEchoLevel() > 0) {
                envFadeLevel = getPseudoEchoLevel();
            }
            goto decay_start;
        }
    } else {
        if (fastRelease && envState != EnvState::DIE) {
            /* if note is already releasing but we are now releasing fast, do not wait
             * until the next real frame but stop the note immerdiately */
            if (env.rel == 0 || envState == EnvState::PSEUDO_ECHO)
                envInterStep = INTERFRAMES - 1;
            else
                envInterStep = 0;
            envState = EnvState::DIE;
            envFrameCount = 1;
            return;
        }

        if (++envInterStep < INTERFRAMES)
            return;

        envInterStep = 0;

        assert(envFrameCount > 0);
        envFrameCount--;
    }

    /* if fastRelease == true then stop == true */
    assert(!fastRelease || stop);

    if (envState == EnvState::PSEUDO_ECHO) {
        assert(note.pseudoEchoLen != 0);
        envFrameCount = 1;
        if (--note.pseudoEchoLen == 0) {
            envState = EnvState::DIE;
            envInterStep = INTERFRAMES - 1;
        }
    } else if (stop && envState < EnvState::REL) {
        envState = EnvState::REL;
        envFrameCount = env.rel;
        if (envLevelCur == 0 || envFrameCount == 0) {
            /* original doesn't branch on envLevelCur == 0, but we do in order to avoid
             * decreasing into the negative */
            goto pseudo_echo_start;
        } else {
            return;
        }
    } else if (envFrameCount == 0) {
        applyVol();

        if (envState == EnvState::REL) {
            assert(envLevelCur > 0);
            envLevelCur--;
            assert((int8_t)envLevelCur >= 0);

            if (envLevelCur == 0) {
            pseudo_echo_start:
                envFrameCount = 1;
                envLevelCur = getPseudoEchoLevel();
                if (envLevelCur != 0 && note.pseudoEchoLen != 0) {
                    /* original doesn't check for pseudoEchoLen */
                    envState = EnvState::PSEUDO_ECHO;
                } else {
                    envState = EnvState::DIE;
                    envInterStep = INTERFRAMES - 1;
                    return;
                }
            } else {
                envFrameCount = env.rel;
                assert(env.rel != 0);
            }
        } else if (envState == EnvState::SUS) {
        sustain_state:
            if (ctx.agbplaySoundMode.emulateCgbSustainBug) {
                envFrameCount = 7;
                if (IsChn3())
                    envLevelCur = envSustain;
                // else:
                //   envLevelCur is updated conditionally in applyVol() when when the sustain bug simulation is enabled
            } else {
                envFrameCount = 1;
                envLevelCur = envSustain;
            }
        } else if (envState == EnvState::DEC) {
            envLevelCur--;
            assert((int8_t)envLevelCur >= 0);

            if (envLevelCur <= envSustain) {
            sustain_start:
                if (env.sus == 0) {
                    envState = EnvState::REL;
                    goto pseudo_echo_start;
                } else {
                    envState = EnvState::SUS;
                    envLevelCur = envSustain;
                    goto sustain_state;
                }
            }
            envFrameCount = env.dec;
            assert(env.dec != 0);
        } else if (envState == EnvState::ATK) {
            envLevelCur++;

            if (envLevelCur >= envPeak) {
            decay_start:
                envState = EnvState::DEC;
                envFrameCount = env.dec;
                if (envPeak == 0 || envFrameCount == 0 || envPeak == envSustain) {
                    /* original code doesn't branch on envPeak == 0, but we do that we don't do a decrease envelope even
                     * though the level is already zero. Original also doesn't branch when peak == sustain, which we to
                     * do avoid an envelope decrease that will increase right after again. */
                    goto sustain_start;
                } else {
                    envLevelCur = envPeak;
                }
            } else {
                envFrameCount = env.att;
                assert(env.att != 0);
            }
        } else if (envState == EnvState::DIE) {
            envState = EnvState::DEAD;
            return;
        }

        assert(envFrameCount != 0);
    }
}

void MP2KChnPSG::updateVolFade()
{
    size_t fadeInterframesCount = envFrameCount * INTERFRAMES - envInterStep;
    assert(fadeInterframesCount != 0);

    uint8_t envFadeLevelTo = 0xFF;
    switch (envState) {
    case EnvState::INIT:
        assert(false);
        break;
    case EnvState::ATK:
        assert(envLevelCur < 15);
        envFadeLevelTo = envLevelCur + 1;
        break;
    case EnvState::DEC:
    case EnvState::REL:
        assert(envLevelCur > 0);
        envFadeLevelTo = envLevelCur - 1;
        break;
    case EnvState::SUS:
    case EnvState::PSEUDO_ECHO:
        envFadeLevelTo = envLevelCur;
        fadeInterframesCount = 1;
        break;
    case EnvState::DIE:
        envFadeLevelTo = 0;
        break;
    case EnvState::DEAD:
        assert(false);
        break;
    }
    assert(envFadeLevelTo != 0xFF);

    float envFadeLevelNew;
    if (useStairstep) {
        /* In stairstep mode, the envelope will change in the last interframe before the next envelope update.
         * This fixes wave channels to behave like on hardware. */
        if (fadeInterframesCount == 1)
            envFadeLevelNew = envFadeLevelTo;
        else
            envFadeLevelNew = envFadeLevel;
    } else {
        envFadeLevelNew = envFadeLevel + (envFadeLevelTo - envFadeLevel) / static_cast<float>(fadeInterframesCount);
    }

    volFade.fromVolLeft = (panPrev == Pan::RIGHT) ? 0.0f : envFadeLevel * (1.0f / 32.0f);
    volFade.fromVolRight = (panPrev == Pan::LEFT) ? 0.0f : envFadeLevel * (1.0f / 32.0f);
    volFade.toVolLeft = (panCur == Pan::RIGHT) ? 0.0f : envFadeLevelNew * (1.0f / 32.0f);
    volFade.toVolRight = (panCur == Pan::LEFT) ? 0.0f : envFadeLevelNew * (1.0f / 32.0f);

    // if (note.trackIdx == 6)
    //     Debug::print("this=%p envState=%d envLevelCur=%d envFadeLevel=%f envFadeLevelNew=%f envFrameCount=%d
    //     envInterStep=%d",
    //             this, (int)envState, (int)envLevelCur, envFadeLevel, envFadeLevelNew, (int)envFrameCount,
    //             (int)envInterStep);

    panPrev = panCur;
    envFadeLevel = envFadeLevelNew;
}

void MP2KChnPSG::applyVol()
{
    /* because agbplay generally wants to avoid the center panorama dilemma
     * (i.e. 0 pan not being in the center of the [-128,127] range)
     * we delay applying pan changes at the channel level, so we don't do the slightly
     * off center rounding for PCM channels. */

    int trkVolML = ((127 - pan) * vol) >> 8;
    int trkVolMR = ((pan + 128) * vol) >> 8;
    int chnVolL = (((127 - note.rhythmPan) * note.velocity) * trkVolML) >> 14;
    int chnVolR = (((note.rhythmPan + 128) * note.velocity) * trkVolMR) >> 14;
    assert(chnVolL >= 0);
    assert(chnVolR >= 0);

    /* strictly speaking the panorama also is affected by the sustain bug, but I'm too lazy to implement
     * is since most games probably won't be affected by this. */
    if (chnVolR / 2 >= chnVolL)
        this->panCur = Pan::RIGHT;
    else if (chnVolL / 2 >= chnVolR)
        this->panCur = Pan::LEFT;
    else
        this->panCur = Pan::CENTER;

    /* This envLevelCur assignment used to be below envSustain assignment and the only reason
     * it's now up here is because of a bug in the original mp2k where only 1 in 7 cases the
     * envelope sustain level would be applied correctly. */
    if (ctx.agbplaySoundMode.emulateCgbSustainBug) {
        if (!IsChn3() && mp2k_sus_vol_bug_update && envState == EnvState::SUS) {
            envLevelCur = envSustain;
            mp2k_sus_vol_bug_update = false;
        }
    }

    envPeak = static_cast<uint8_t>(std::clamp((chnVolL + chnVolR) >> 4, 0, 15));
    envSustain = static_cast<uint8_t>(std::clamp((envPeak * env.sus + 15) >> 4, 0, 15));
}

/*
 * public MP2KChnPSGSquare
 */

MP2KChnPSGSquare::MP2KChnPSGSquare(
    MP2KContext &ctx, MP2KTrack *track, uint32_t instrDuty, ADSR env, Note note, uint8_t sweep
) :
    MP2KChnPSG(ctx, track, env, note),
    instrDuty(instrDuty),
    sweep(sweep),
    sweepEnabled(isSweepEnabled(sweep)),
    sweepConvergence(sweep2convergence(sweep)),
    sweepCoeff(sweep2coeff(sweep))
{
    static const float *patterns[4] = {
        CGBPatterns::pat_sq12,
        CGBPatterns::pat_sq25,
        CGBPatterns::pat_sq50,
        CGBPatterns::pat_sq75,
    };

    this->pat = patterns[instrDuty % 4];
    this->rs = Resampler::MakeResampler(ResamplerType::BLEP);
}

void MP2KChnPSGSquare::SetPitch(int16_t pitch)
{
    // non original quality improving behavior
    if (!stop || freq <= 0.0f)
        freq = 3520.0f * powf(2.0f, float(note.midiKeyPitch - 69) * (1.0f / 12.0f) + float(pitch) * (1.0f / 768.0f));

    if (sweepEnabled && sweepStartCount < 0) {
        sweepTimer = freq2timer(freq / 8.0f);
        /* Because the initial frequency of a sweeped sound can only be set
         * in the beginning, we have to differentiate between the first and the other
         * SetPitch calls. */
        uint8_t time = sweepTime(sweep);
        assert(time != 0);
        sweepStartCount = static_cast<int16_t>(time * AGB_APPROX_FPS * INTERFRAMES);
    }
}

void MP2KChnPSGSquare::Process(std::span<sample> buffer, MixingArgs &args)
{
    if (envState == EnvState::DEAD)
        return;
    stepEnvelope();
    if (envState == EnvState::DEAD)
        return;

    updateVolFade();

    if (buffer.size() == 0)
        return;

    VolumeFade vol = getVol();
    assert(pat);
    float lVolStep = (vol.toVolLeft - vol.fromVolLeft) * args.samplesPerBufferInv;
    float rVolStep = (vol.toVolRight - vol.fromVolRight) * args.samplesPerBufferInv;
    float lVol = vol.fromVolLeft;
    float rVol = vol.fromVolRight;
    float interStep;

    if (sweepEnabled) {
        // Debug::print("sweepTimer=%f sweepConvergence=%f sweepCoeff=%f resultFreq=%f",
        //         sweepTimer, sweepConvergence, sweepCoeff, timer2freq(sweepTimer));
        interStep = 8.0f * timer2freq(sweepTimer) * args.sampleRateInv;
    } else {
        interStep = freq * args.sampleRateInv;
    }

    assert(buffer.size() == ctx.mixer.scratchBuffer.size());
    FetchCallback cb =
        std::bind(&MP2KChnPSGSquare::sampleFetchCallback, this, std::placeholders::_1, std::placeholders::_2);
    rs->Process(ctx.mixer.scratchBuffer, interStep, cb);

    for (size_t i = 0; i < buffer.size(); i++) {
        const float samp = ctx.mixer.scratchBuffer[i];
        buffer[i].left += samp * lVol;
        buffer[i].right += samp * rVol;
        lVol += lVolStep;
        rVol += rVolStep;
    }

    if (sweepEnabled) {
        assert(sweepStartCount >= 0);
        if (sweepStartCount == 0) {
            sweepTimer *= sweepCoeff;
            if (isSweepAscending(sweep))
                sweepTimer = std::min(sweepTimer, sweepConvergence);
            else
                sweepTimer = std::max(sweepTimer, sweepConvergence);
        } else {
            sweepStartCount -= 128;
            if (sweepStartCount < 0)
                sweepStartCount = 0;
        }
    }
}

VoiceFlags MP2KChnPSGSquare::GetVoiceType() const noexcept
{
    if (sweep != 0) {
        switch (instrDuty) {
        case 0:
            return VoiceFlags::PSG_SQ_12_SWEEP;
        case 1:
            return VoiceFlags::PSG_SQ_25_SWEEP;
        case 2:
            return VoiceFlags::PSG_SQ_50_SWEEP;
        default:
            return VoiceFlags::PSG_SQ_75_SWEEP;
        }
    } else {
        switch (instrDuty) {
        case 0:
            return VoiceFlags::PSG_SQ_12;
        case 1:
            return VoiceFlags::PSG_SQ_25;
        case 2:
            return VoiceFlags::PSG_SQ_50;
        default:
            return VoiceFlags::PSG_SQ_75;
        }
    }
}

bool MP2KChnPSGSquare::sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    do {
        fetchBuffer[i++] = pat[pos++];
        pos %= 8;
    } while (--samplesToFetch > 0);
    return true;
}

bool MP2KChnPSGSquare::isSweepEnabled(uint8_t sweep)
{
    if (sweep >= 0x80 || (sweep & 0x7) == 0)
        return false;
    else
        return true;
}

bool MP2KChnPSGSquare::isSweepAscending(uint8_t sweep)
{
    if (sweep & 8)
        return false;
    else
        return true;
}

float MP2KChnPSGSquare::sweep2coeff(uint8_t sweep)
{
    /* if sweep time is zero, don't change pitch */
    const int sweep_time = sweepTime(sweep);
    if (sweep_time == 0)
        return 1.0f;

    const int shifts = sweep & 7;
    float step_coeff;

    if (isSweepAscending(sweep)) {
        /* if ascending */
        step_coeff = static_cast<float>(128 + (128 >> shifts)) / 128.0f;
    } else {
        /* if descending */
        step_coeff = static_cast<float>(128 - (128 >> shifts)) / 128.0f;
    }

    /* convert the sweep pitch timer coefficient to the rate that agbplay runs at */
    const float hardware_sweep_rate = 128 / static_cast<float>(sweep_time);
    const float agbplay_sweep_rate = AGB_APPROX_FPS * INTERFRAMES;
    const float coeff = powf(step_coeff, hardware_sweep_rate / agbplay_sweep_rate);

    return coeff;
}

float MP2KChnPSGSquare::sweep2convergence(uint8_t sweep)
{
    if (isSweepAscending(sweep)) {
        /* if ascending:
         *
         * Convergance is always at the maximum timer value to prevent hardware overflow */
        return 2047.0f;
    } else {
        /* if descending:
         *
         * Because hardware calculates sweep with:
         * timer -= timer >> shift
         * the timer converges to the value which timer >> shift always results zero. */
        const int shifts = sweep & 7;
        return static_cast<float>((1 << shifts) - 1);
    }
}

uint8_t MP2KChnPSGSquare::sweepTime(uint8_t sweep)
{
    return static_cast<uint8_t>((sweep & 0x70) >> 4);
}

/*
 * public MP2KChnPSGWave
 */

MP2KChnPSGWave::MP2KChnPSGWave(
    MP2KContext &ctx, MP2KTrack *track, uint32_t instrWave, ADSR env, Note note, bool useStairstep
) :
    MP2KChnPSG(ctx, track, env, note, useStairstep)
{
    static const uint8_t dummyWave[16] = {0};
    if (instrWave < AGB_MAP_ROM) {
        wavePtr = dummyWave;
    } else {
        instrWave -= AGB_MAP_ROM;
        if (ctx.rom.ValidRange(instrWave, 16))
            wavePtr = static_cast<const uint8_t *>(ctx.rom.GetPtr(instrWave));
        else
            wavePtr = dummyWave;
    }

    this->rs = Resampler::MakeResampler(ResamplerType::BLEP);

    /* wave samples are unsigned by default, so we'll calculate the required
     * DC offset correction */
    float sum = 0.0f;
    for (int i = 0; i < 16; i++) {
        uint8_t twoNibbles = wavePtr[i];
        int nibbleA = twoNibbles >> 4;
        int nibbleB = twoNibbles & 0xF;
        sum += static_cast<float>(nibbleA) / 16.0f;
        sum += static_cast<float>(nibbleB) / 16.0f;
    }
    dcCorrection100 = -sum * (1.0f / 32.0f);

    if (ctx.agbplaySoundMode.accurateCh3Quantization) {
        sum = 0.0f;
        for (int i = 0; i < 16; i++) {
            uint8_t twoNibbles = wavePtr[i];
            int nibbleA = twoNibbles >> 4;
            int nibbleB = twoNibbles & 0xF;
            sum += static_cast<float>((nibbleA >> 2) + (nibbleA >> 1)) / 16.0f;
            sum += static_cast<float>((nibbleB >> 2) + (nibbleB >> 1)) / 16.0f;
        }
        dcCorrection75 = -sum * (1.0f / 32.0f);

        sum = 0.0f;
        for (int i = 0; i < 16; i++) {
            uint8_t twoNibbles = wavePtr[i];
            int nibbleA = twoNibbles >> 4;
            int nibbleB = twoNibbles & 0xF;
            sum += static_cast<float>((nibbleA >> 1)) / 16.0f;
            sum += static_cast<float>((nibbleB >> 1)) / 16.0f;
        }
        dcCorrection50 = -sum * (1.0f / 32.0f);

        sum = 0.0f;
        for (int i = 0; i < 16; i++) {
            uint8_t twoNibbles = wavePtr[i];
            int nibbleA = twoNibbles >> 4;
            int nibbleB = twoNibbles & 0xF;
            sum += static_cast<float>((nibbleA >> 2)) / 16.0f;
            sum += static_cast<float>((nibbleB >> 2)) / 16.0f;
        }
        dcCorrection25 = -sum * (1.0f / 32.0f);
    }
}

void MP2KChnPSGWave::SetPitch(int16_t pitch)
{
    freq =
        (440.0f * 16.0f) * powf(2.0f, float(note.midiKeyPitch - 69) * (1.0f / 12.0f) + float(pitch) * (1.0f / 768.0f));
}

void MP2KChnPSGWave::Process(std::span<sample> buffer, MixingArgs &args)
{
    if (envState == EnvState::DEAD)
        return;
    stepEnvelope();
    if (envState == EnvState::DEAD)
        return;

    updateVolFade();

    if (buffer.size() == 0)
        return;
    VolumeFade vol = getVol();

    float lVolStep = (vol.toVolLeft - vol.fromVolLeft) * args.samplesPerBufferInv;
    float rVolStep = (vol.toVolRight - vol.fromVolRight) * args.samplesPerBufferInv;
    float lVol = vol.fromVolLeft;
    float rVol = vol.fromVolRight;
    float interStep = freq * args.sampleRateInv;

    assert(ctx.mixer.scratchBuffer.size() == buffer.size());
    FetchCallback cb =
        std::bind(&MP2KChnPSGWave::sampleFetchCallback, this, std::placeholders::_1, std::placeholders::_2);
    rs->Process(ctx.mixer.scratchBuffer, interStep, cb);

    for (size_t i = 0; i < buffer.size(); i++) {
        const float samp = ctx.mixer.scratchBuffer[i];
        buffer[i].left += samp * lVol;
        buffer[i].right += samp * rVol;
        lVol += lVolStep;
        rVol += rVolStep;
    }
}

VoiceFlags MP2KChnPSGWave::GetVoiceType() const noexcept
{
    return VoiceFlags::PSG_WAVE;
}

bool MP2KChnPSGWave::IsChn3() const
{
    return true;
}

VolumeFade MP2KChnPSGWave::getVol() const
{
    auto retval = MP2KChnPSG::getVol();

    if (!ctx.agbplaySoundMode.accurateCh3Volume) {
        return retval;
    }

    auto snapFunc = [](float x) {
        if (x < 1.5f / 32.0f)
            return 0.0f / 32.0f;
        else if (x < 5.5f / 32.0f)
            return 4.0f / 32.0f;
        else if (x < 9.5f / 32.0f)
            return 8.0f / 32.0f;
        else if (x < 13.5f / 32.0f)
            return 12.0f / 32.0f;
        else
            return 16.0f / 32.0f;
    };

    retval.fromVolLeft = snapFunc(retval.fromVolLeft);
    retval.fromVolRight = snapFunc(retval.fromVolRight);
    retval.toVolLeft = snapFunc(retval.toVolLeft);
    retval.toVolRight = snapFunc(retval.toVolRight);
    return retval;
}

bool MP2KChnPSGWave::sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    if (ctx.agbplaySoundMode.accurateCh3Quantization) {
        /* WARNING; VERY UGLY AND HACKY */
        VolumeFade fade = getVol();
        const float fromVol = std::max(fade.fromVolLeft, fade.fromVolRight);
        const float toVol = std::max(fade.toVolLeft, fade.toVolRight);
        /* I'm not entirely certain whether the hardware uses arithmetic shifts or logic shifts (with bias).
         * Using logic shifts for now, hopefully works good enough... */
        auto mapFunc =
            [this](float x, float &compensationScale, float &dcCorrection, uint32_t &shiftA, uint32_t &shiftB) {
                if (x < 6.0f / 32.0f) {
                    shiftA = 2;
                    shiftB = 4;
                    compensationScale = 4.0f;
                    dcCorrection = dcCorrection25;
                } else if (x < 10.0f / 32.0f) {
                    shiftA = 1;
                    shiftB = 4;
                    compensationScale = 2.0f;
                    dcCorrection = dcCorrection50;
                } else if (x < 14.0f / 32.0f) {
                    shiftA = 1;
                    shiftB = 2;
                    compensationScale = 4.0f / 3.0f;
                    dcCorrection = dcCorrection75;
                } else {
                    shiftA = 0;
                    shiftB = 4;
                    compensationScale = 1.0f;
                    dcCorrection = dcCorrection100;
                }
            };
        uint32_t shiftAFrom, shiftATo, shiftBFrom, shiftBTo;
        float compensationScaleFrom, compensationScaleTo;
        float dcCorrectionFrom, dcCorrectionTo;
        mapFunc(fromVol, compensationScaleFrom, dcCorrectionFrom, shiftAFrom, shiftBFrom);
        mapFunc(toVol, compensationScaleTo, dcCorrectionTo, shiftATo, shiftBTo);
        dcCorrectionFrom *= 16.0f;
        dcCorrectionTo *= 16.0f;

        float t = 0.0f;
        float t_inc = 1.0f / static_cast<float>(samplesToFetch);
        while (samplesToFetch-- > 0) {
            const uint32_t pos = (this->pos++) % 32;
            uint8_t nibble;
            if (pos % 2 == 0)
                nibble = static_cast<uint8_t>(wavePtr[pos / 2] >> 4u);
            else
                nibble = static_cast<uint8_t>(wavePtr[pos / 2] & 0xF);
            float sampleFrom = (static_cast<float>((nibble >> shiftAFrom) + (nibble >> shiftBFrom)) + dcCorrectionFrom)
                               * compensationScaleFrom;
            float sampleTo = (static_cast<float>((nibble >> shiftATo) + (nibble >> shiftBTo)) + dcCorrectionTo)
                             * compensationScaleTo;
            float sample = (sampleFrom + t * (sampleTo - sampleFrom)) * (1.0f / 16.0f);
            t += t_inc;
            fetchBuffer[i++] = sample;
        }
    } else {
        while (samplesToFetch-- > 0) {
            const uint32_t pos = (this->pos++) % 32;
            uint8_t nibble;
            if (pos % 2 == 0)
                nibble = static_cast<uint8_t>(wavePtr[pos / 2] >> 4u);
            else
                nibble = static_cast<uint8_t>(wavePtr[pos / 2] & 0xF);
            float sample = nibble * (1.0f / 16.0f) + dcCorrection100;
            fetchBuffer[i++] = sample;
        }
    }

    return true;
}

/*
 * public MP2KChnPSGNoise
 */

MP2KChnPSGNoise::MP2KChnPSGNoise(MP2KContext &ctx, MP2KTrack *track, uint32_t instrNp, ADSR env, Note note) :
    MP2KChnPSG(ctx, track, env, note), instrNp(instrNp)
{
    /* Noise is first generated at the generators frequency and converted to
     * AGB's DAC output rate using nearest neighbor.
     * In order to sound good, we then resample this signal again to our actual
     * output rate using a bandlimited sinc resampler. */
    this->rs = Resampler::MakeResampler(ResamplerType::NEAREST);
    this->srs = Resampler::MakeResampler(ResamplerType::SINC);
    if ((instrNp & 0x1) == 0) {
        noiseState = 0x4000;
        noiseLfsrMask = 0x6000;
    } else {
        noiseState = 0x40;
        noiseLfsrMask = 0x60;
    }
}

void MP2KChnPSGNoise::SetPitch(int16_t pitch)
{
    float fkey = note.midiKeyPitch + static_cast<float>(pitch) * (1.0f / 64.0f);
    float noisefreq;
    if (fkey < 76.0f)
        noisefreq = 4096.0f * powf(8.0f, (fkey - 60.0f) * (1.0f / 12.0f));
    else if (fkey < 78.0f)
        noisefreq = 65536.0f * powf(2.0f, (fkey - 76.0f) * (1.0f / 2.0f));
    else if (fkey < 80.0f)
        noisefreq = 131072.0f * powf(2.0f, (fkey - 78.0f));
    else
        noisefreq = 524288.0f;

    freq = std::max(4.5714f, noisefreq);
}

void MP2KChnPSGNoise::Process(std::span<sample> buffer, MixingArgs &args)
{
    if (envState == EnvState::DEAD)
        return;
    stepEnvelope();
    if (envState == EnvState::DEAD)
        return;

    updateVolFade();

    if (buffer.size() == 0)
        return;

    static const std::array<float, 4> noiseFreqs{32768.0f, 65536.0f, 131072.0f, 262144.0f};
    const float noiseFreq = noiseFreqs[ctx.mp2kSoundMode.dacConfig % noiseFreqs.size()];

    VolumeFade vol = getVol();
    float lVolStep = (vol.toVolLeft - vol.fromVolLeft) * args.samplesPerBufferInv;
    float rVolStep = (vol.toVolRight - vol.fromVolRight) * args.samplesPerBufferInv;
    float lVol = vol.fromVolLeft;
    float rVol = vol.fromVolRight;
    float interStep = freq / noiseFreq;

    assert(ctx.mixer.scratchBuffer.size() == buffer.size());
    /* In order to get accurate noise sound like on hardware, we first nearest-neighbour/zero-order-hold
     * interpolate the generated noise to whatever is the current DAC PWM rate.
     * After that, we use the bandlimited sinc resampler to convert this to our actual output rate to
     * avoid aliasing.
     * Accordingly, we need to perform two resampling steps, thus the somewhat confusing lambda. */
    FetchCallback cbSinc = [this, interStep](std::vector<float> &fetchBuffer, size_t samplesRequired) {
        if (fetchBuffer.size() >= samplesRequired)
            return true;
        const size_t samplesToFetch = samplesRequired - fetchBuffer.size();
        const size_t i = fetchBuffer.size();
        fetchBuffer.resize(samplesRequired);
        FetchCallback cbNearest =
            std::bind(&MP2KChnPSGNoise::sampleFetchCallback, this, std::placeholders::_1, std::placeholders::_2);
        return rs->Process({&fetchBuffer[i], samplesToFetch}, interStep, cbNearest);
    };

    srs->Process(ctx.mixer.scratchBuffer, noiseFreq / float(ctx.sampleRate), cbSinc);

    for (size_t i = 0; i < buffer.size(); i++) {
        const float samp = ctx.mixer.scratchBuffer[i];
        buffer[i].left += samp * lVol;
        buffer[i].right += samp * rVol;
        lVol += lVolStep;
        rVol += rVolStep;
    }
}

VoiceFlags MP2KChnPSGNoise::GetVoiceType() const noexcept
{
    if (instrNp == 0x0)
        return VoiceFlags::PSG_NOISE_15;
    else
        return VoiceFlags::PSG_NOISE_7;
}

bool MP2KChnPSGNoise::sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    do {
        float sample;
        if (noiseState & 1) {
            sample = 0.5f;
            noiseState >>= 1;
            noiseState ^= noiseLfsrMask;
        } else {
            sample = -0.5f;
            noiseState >>= 1;
        }
        fetchBuffer[i++] = sample;
    } while (--samplesToFetch > 0);

    return true;
}
