#include "MP2KChnPCM.hpp"

#include "Constants.hpp"
#include "Debug.hpp"
#include "MP2KContext.hpp"
#include "Util.hpp"
#include "Xcept.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <string>

/*
 * public MP2KChnPCM
 */

MP2KChnPCM::MP2KChnPCM(MP2KContext &ctx, MP2KTrack *track, SampleInfo sInfo, ADSR env, const Note &note, bool fixed) :
    MP2KChn(track, note, env), ctx(ctx), sInfo(sInfo), fixed(fixed)
{
    if (sInfo.loopPos == 0 && sInfo.endPos == 0) {
        if (!ctx.rom.ValidRange(sInfo.samplePos, 16 + 8)) {
            Debug::print("Sample Error: Sample data reaches beyond end of file: [{:#08x}]", sInfo.samplePos);
            envState = EnvState::DEAD;
            return;
        }

        // Golden Sun's synth instruments are marked by having a length of zero and a loop of zero
        if (sInfo.samplePtr[1] == 0)
            type = Type::SYNTH_PWM;
        else if (sInfo.samplePtr[1] == 1)
            type = Type::SYNTH_SAWTOOTH;
        else
            type = Type::SYNTH_TRIANGLE;
        isSynth = true;
        return;
    }

    const ResamplerType t = fixed ? ctx.agbplaySoundMode.resamplerTypeFixed : ctx.agbplaySoundMode.resamplerTypeNormal;
    this->rs = Resampler::MakeResampler(t);

    if (sInfo.gamefreakCompressed) {
        type = Type::GAMEFREAK_DPCM;
        const size_t realEndPos = (sInfo.endPos + 63) / 64 * 0x21;
        if (!ctx.rom.ValidRange(sInfo.samplePos, 16 + realEndPos)) {
            Debug::print("Sample Error: DPCM data reaches beyond end of file: [{:#08x}]", sInfo.samplePos);
            envState = EnvState::DEAD;
            return;
        }
    } else if (sInfo.endPos >= 0x80000000) {
        // Mario Power Tennis compressed instruments have a 'negative' length
        // strictly speaking, these are originally only available at 'fixed' frequency,
        // but we enhance song #17 which otherwise would have garbled/no sound
        type = Type::CAMELOT_ADPCM;
        // flip it to it's intended length
        this->sInfo.endPos = -this->sInfo.endPos;
        if (!ctx.rom.ValidRange(sInfo.samplePos, 16 + this->sInfo.endPos / 2u)) {
            Debug::print("Sample Error: ADPCM data reaches beyond end of file: [{:#08x}]", sInfo.samplePos);
            envState = EnvState::DEAD;
            return;
        }
    } else {
        type = Type::PCM;
        if (!ctx.rom.ValidRange(sInfo.samplePos, 16 + sInfo.endPos)) {
            Debug::print("Sample Error: PCM data reaches beyond end of file: [{:#08x}]", sInfo.samplePos);
            envState = EnvState::DEAD;
            return;
        }
    }
}

void MP2KChnPCM::Process(std::span<sample> buffer, const MixingArgs &args)
{
    if (envState == EnvState::DEAD)
        return;
    stepEnvelope();
    if (envState == EnvState::DEAD)
        return;
    if (buffer.size() == 0)
        return;

    const float samplesPerBufferInv = 1.0f / float(buffer.size());

    VolumeFade vol = getVol();
    vol.fromVolLeft *= args.vol;
    vol.fromVolRight *= args.vol;
    vol.toVolLeft *= args.vol;
    vol.toVolRight *= args.vol;

    ProcArgs cargs;
    cargs.lVolStep = (vol.toVolLeft - vol.fromVolLeft) * samplesPerBufferInv;
    cargs.rVolStep = (vol.toVolRight - vol.fromVolRight) * samplesPerBufferInv;
    cargs.lVol = vol.fromVolLeft;
    cargs.rVol = vol.fromVolRight;

    if (fixed && !isSynth)
        cargs.interStep = float(args.fixedModeRate) * args.sampleRateInv;
    else
        cargs.interStep = freq * args.sampleRateInv;

    if (isSynth) {
        cargs.interStep /= 64.f;    // different scale for GS
        // switch by GS type
        if (type == Type::SYNTH_PWM) {
            processModPulse(buffer, cargs, samplesPerBufferInv);
        } else if (type == Type::SYNTH_SAWTOOTH) {
            processSaw(buffer, cargs);
        } else if (type == Type::SYNTH_TRIANGLE) {
            processTri(buffer, cargs);
        } else {
            assert(false);
        }
    } else {
        processNormal(buffer, cargs);
    }
    updateVolFade();
}

void MP2KChnPCM::SetVol(uint16_t vol, int16_t pan)
{
    if (!stop) {
        int combinedPan = std::clamp(pan + note.rhythmPan, -128, +128);
        /* original doesn't do the if statement below, but it retains the 0 center
         * panorama position while allowing the maximum pan valume of 126 (i.e. 63 on tracK)
         * to be completely right sided. */
        if (combinedPan >= 126)
            combinedPan = 128;
        this->leftVolCur = static_cast<uint8_t>(std::clamp(note.velocity * vol * (-combinedPan + 128) >> 15, 0, 255));
        this->rightVolCur = static_cast<uint8_t>(std::clamp(note.velocity * vol * (combinedPan + 128) >> 15, 0, 255));
    }
}

VolumeFade MP2KChnPCM::getVol() const
{
    float envBase = float(envLevelPrev);
    float envDelta = (float(envLevelCur) - envBase) / float(INTERFRAMES);
    float finalFromEnv = envBase + envDelta * float(envInterStep);
    float finalToEnv = envBase + envDelta * float(envInterStep + 1);

    VolumeFade retval;
    retval.fromVolLeft = float(leftVolPrev) * finalFromEnv * (1.0f / 65536.0f);
    retval.fromVolRight = float(rightVolPrev) * finalFromEnv * (1.0f / 65536.0f);
    retval.toVolLeft = float(leftVolCur) * finalToEnv * (1.0f / 65536.0f);
    retval.toVolRight = float(rightVolCur) * finalToEnv * (1.0f / 65536.0f);
    return retval;
}

void MP2KChnPCM::Release() noexcept
{
    stop = true;
}

bool MP2KChnPCM::IsReleasing() const noexcept
{
    return stop;
}

void MP2KChnPCM::SetPitch(int16_t pitch)
{
    // non original quality improving behavior
    if (!stop || freq <= 0.0f)
        freq = sInfo.midCfreq
               * powf(2.0f, float(note.midiKeyPitch - 60) * (1.0f / 12.0f) + float(pitch) * (1.0f / 768.0f));
}

bool MP2KChnPCM::TickNote() noexcept
{
    if (!stop) {
        if (note.length > 0) {
            note.length--;
            if (note.length == 0) {
                stop = true;
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}

VoiceFlags MP2KChnPCM::GetVoiceType() const noexcept
{
    switch (type) {
    case Type::PCM:
        return VoiceFlags::PCM;
    case Type::GAMEFREAK_DPCM:
        return VoiceFlags::DPCM_GAMEFREAK;
    case Type::CAMELOT_ADPCM:
        return VoiceFlags::ADPCM_CAMELOT;
    case Type::SYNTH_PWM:
        return VoiceFlags::SYNTH_PWM;
    case Type::SYNTH_SAWTOOTH:
        return VoiceFlags::SYNTH_SAW;
    case Type::SYNTH_TRIANGLE:
        return VoiceFlags::SYNTH_TRI;
    default:
        assert(false);
        return VoiceFlags::NONE;
    }
}

void MP2KChnPCM::stepEnvelope()
{
    if (envState == EnvState::INIT) {
        if (stop) {
            envState = EnvState::DEAD;
            return;
        }
        /* it's important to initialize the volume ramp here because in the constructor
         * the initial volume is not yet known (i.e. 0) */
        updateVolFade();

        /* Because we are smoothly fading all our amplitude changes, we avoid the case
         * where the fastest attack value will still cause a 16.6ms ramp instead of being
         * instant maximum amplitude. */
        if (env.att == 0xFF)
            envLevelPrev = 0xFF;
        else
            envLevelPrev = 0x0;

        envLevelCur = 0;
        envInterStep = 0;
        envState = EnvState::ATK;
    } else {
        /* On GBA, envelopes update every frame but because we do a multiple of updates per frame
         * (to increase timing accuracy of Note ONs), only every so many sub-frames we actually update
         * the envelope state. */
        if (++envInterStep < INTERFRAMES)
            return;
        envLevelPrev = envLevelCur;
        envInterStep = 0;
    }

    if (envState == EnvState::PSEUDO_ECHO) {
        assert(note.pseudoEchoLen != 0);
        if (--note.pseudoEchoLen == 0) {
            envState = EnvState::DIE;
            envLevelCur = 0;
        }
    } else if (stop) {
        if (envState == EnvState::DIE) {
            /* This is really just a transitional state that is supposed to be the last GBA frame
             * of fadeout. Because we smoothly ramp out envelopes, out envelopes are actually one
             * frame longer than on hardware- As soon as this state is reached the channel is disabled */
            envState = EnvState::DEAD;
        } else {
            envLevelCur = static_cast<uint8_t>((envLevelCur * env.rel) >> 8);
            /* ORIGINAL "BUG":
             * Even when pseudo echo has no length, the following condition will kick in and may cause
             * an earlier then intended note release */
            if (envLevelCur <= note.pseudoEchoVol) {
            release:
                if (note.pseudoEchoVol == 0 || note.pseudoEchoLen == 0) {
                    envState = EnvState::DIE;
                    envLevelCur = 0;
                } else {
                    envState = EnvState::PSEUDO_ECHO;
                    envLevelCur = note.pseudoEchoVol;
                }
            }
        }
    } else {
        if (envState == EnvState::DEC) {
            envLevelCur = static_cast<uint8_t>((envLevelCur * env.dec) >> 8);
            if (envLevelCur <= env.sus) {
                envLevelCur = env.sus;
                if (envLevelCur == 0)
                    goto release;
                envState = EnvState::SUS;
            }
        } else if (envState == EnvState::ATK) {
            uint32_t newLevel = envLevelCur + env.att;
            if (newLevel >= 0xFF) {
                envLevelCur = 0xFF;
                envState = EnvState::DEC;
            } else {
                envLevelCur = static_cast<uint8_t>(newLevel);
            }
        }
    }
}

void MP2KChnPCM::updateVolFade()
{
    leftVolPrev = leftVolCur;
    rightVolPrev = rightVolCur;
}

/*
 * private MP2KChnPCM
 */

void MP2KChnPCM::processNormal(std::span<sample> buffer, ProcArgs &cargs)
{
    if (buffer.size() == 0)
        return;
    assert(ctx.mixer.scratchBuffer.size() == buffer.size());

    FetchCallback cb;
    if (type == Type::PCM)
        cb = std::bind(&MP2KChnPCM::sampleFetchCallback, this, std::placeholders::_1, std::placeholders::_2);
    else if (type == Type::GAMEFREAK_DPCM)
        cb =
            std::bind(&MP2KChnPCM::sampleFetchCallbackGFDPCMDecomp, this, std::placeholders::_1, std::placeholders::_2);
    else if (type == Type::CAMELOT_ADPCM)
        cb = std::bind(&MP2KChnPCM::sampleFetchCallbackMPTDecomp, this, std::placeholders::_1, std::placeholders::_2);
    else
        assert(false);

    const bool running = rs->Process(ctx.mixer.scratchBuffer, cargs.interStep, cb);

    for (size_t i = 0; i < buffer.size(); i++) {
        const float samp = ctx.mixer.scratchBuffer[i];
        buffer[i].left += samp * cargs.lVol;
        buffer[i].right += samp * cargs.rVol;
        cargs.lVol += cargs.lVolStep;
        cargs.rVol += cargs.rVolStep;
    }
    if (!running)
        Kill();
}

void MP2KChnPCM::processModPulse(std::span<sample> buffer, ProcArgs &cargs, float samplesPerBufferInv)
{
#define DUTY_BASE 2
#define DUTY_STEP 3
#define DEPTH     4
#define INIT_DUTY 5
    uint32_t fromPos;

    if (envInterStep == 0)
        fromPos = pos += uint32_t(sInfo.samplePtr[DUTY_STEP] << 24);
    else
        fromPos = pos;

    uint32_t toPos = fromPos + uint32_t(sInfo.samplePtr[DUTY_STEP] << 24);

    auto calcThresh = [](uint32_t val, uint8_t base, uint8_t depth, uint8_t init) {
        uint32_t iThreshold = uint32_t(init << 24) + val;
        iThreshold = int32_t(iThreshold) < 0 ? ~iThreshold >> 8 : iThreshold >> 8;
        iThreshold = iThreshold * depth + uint32_t(base << 24);
        return float(iThreshold) / float(0x100000000);
    };

    float fromThresh = calcThresh(
        fromPos,
        (uint8_t)sInfo.samplePtr[DUTY_BASE],
        (uint8_t)sInfo.samplePtr[DEPTH],
        (uint8_t)sInfo.samplePtr[INIT_DUTY]
    );
    float toThresh = calcThresh(
        toPos, (uint8_t)sInfo.samplePtr[DUTY_BASE], (uint8_t)sInfo.samplePtr[DEPTH], (uint8_t)sInfo.samplePtr[INIT_DUTY]
    );

    float deltaThresh = toThresh - fromThresh;
    float baseThresh = fromThresh + (deltaThresh * (float(envInterStep) * (1.0f / float(INTERFRAMES))));
    float threshStep = deltaThresh * (1.0f / float(INTERFRAMES)) * samplesPerBufferInv;
    float fThreshold = baseThresh;
#undef DUTY_BASE
#undef DUTY_STEP
#undef DEPTH
#undef INIT_DUTY

    for (size_t i = 0; i < buffer.size(); i++) {
        float baseSamp = interPos < fThreshold ? 0.5f : -0.5f;
        // correct dc offset
        baseSamp += 0.5f - fThreshold;
        fThreshold += threshStep;
        buffer[i].left += baseSamp * cargs.lVol;
        buffer[i].right += baseSamp * cargs.rVol;

        cargs.lVol += cargs.lVolStep;
        cargs.rVol += cargs.rVolStep;

        interPos += cargs.interStep;
        // this below might glitch for too high frequencies, which usually shouldn't be used anyway
        if (interPos >= 1.0f)
            interPos -= 1.0f;
    }
}

void MP2KChnPCM::processSaw(std::span<sample> buffer, ProcArgs &cargs)
{
    const uint32_t fix = 0x70;

    for (size_t i = 0; i < buffer.size(); i++) {
        /*
         * Sorry that the baseSamp calculation looks ugly.
         * For accuracy it's a 1 to 1 translation of the original assembly code
         * Could probably be reimplemented easier. Not sure if it's a perfect saw wave
         */
        interPos += cargs.interStep;
        if (interPos >= 1.0f)
            interPos -= 1.0f;
        uint32_t var1 = uint32_t(interPos * 256) - fix;
        uint32_t var2 = uint32_t(interPos * 65536.0f) << 17;
        uint32_t var3 = var1 - (var2 >> 27);
        pos = var3 + uint32_t(int32_t(pos) >> 1);

        const float baseSamp = float((int32_t)pos) / 256.0f;

        buffer[i].left += baseSamp * cargs.lVol;
        buffer[i].right += baseSamp * cargs.rVol;

        cargs.lVol += cargs.lVolStep;
        cargs.rVol += cargs.rVolStep;
    }
}

void MP2KChnPCM::processTri(std::span<sample> buffer, ProcArgs &cargs)
{
    for (size_t i = 0; i < buffer.size(); i++) {
        interPos += cargs.interStep;
        if (interPos >= 1.0f)
            interPos -= 1.0f;
        float baseSamp;
        if (interPos < 0.5f) {
            baseSamp = (4.0f * interPos) - 1.0f;
        } else {
            baseSamp = 3.0f - (4.0f * interPos);
        }

        buffer[i].left += baseSamp * cargs.lVol;
        buffer[i].right += baseSamp * cargs.rVol;

        cargs.lVol += cargs.lVolStep;
        cargs.rVol += cargs.rVolStep;
    }
}

bool MP2KChnPCM::sampleFetchCallback(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    do {
        size_t samplesTilLoop = sInfo.endPos - pos;
        size_t thisFetch = std::min(samplesTilLoop, samplesToFetch);

        samplesToFetch -= thisFetch;
        do {
            fetchBuffer[i++] = float(sInfo.samplePtr[pos++]) / 128.0f;
        } while (--thisFetch > 0);

        if (pos >= sInfo.endPos) {
            if (sInfo.loopEnabled) {
                pos = sInfo.loopPos;
            } else {
                std::fill(fetchBuffer.begin() + i, fetchBuffer.end(), 0.0f);
                return false;
            }
        }
    } while (samplesToFetch > 0);
    return true;
}

bool MP2KChnPCM::sampleFetchCallbackGFDPCMDecomp(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    const size_t DPCM_BLOCK_SIZE = 64;
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    std::array<int8_t, DPCM_BLOCK_SIZE> decodeBuffer;
    size_t decodedBlockIdx = ~static_cast<size_t>(0);

    do {
        size_t samplesTilLoop = sInfo.endPos - pos;
        size_t thisFetch = std::min(samplesTilLoop, samplesToFetch);

        samplesToFetch -= thisFetch;
        do {
            const size_t currentBlock = pos / DPCM_BLOCK_SIZE;
            if (decodedBlockIdx != currentBlock) [[unlikely]] {
                static const std::array<int8_t, 16> deltaTable = {
                    0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1
                };

                const size_t currentBlockPos = currentBlock * 0x21;

                int8_t acc = sInfo.samplePtr[currentBlockPos];
                decodeBuffer[0] = acc;
                acc += deltaTable[sInfo.samplePtr[currentBlockPos + 1] & 0xF];
                decodeBuffer[1] = acc;
                for (size_t j = 2, h = 2; j < DPCM_BLOCK_SIZE; j += 2, h++) {
                    acc += deltaTable[(sInfo.samplePtr[currentBlockPos + h] & 0xF0) >> 4];
                    decodeBuffer[j + 0] = acc;
                    acc += deltaTable[sInfo.samplePtr[currentBlockPos + h] & 0xF];
                    decodeBuffer[j + 1] = acc;
                }
                decodedBlockIdx = currentBlock;
            }

            fetchBuffer[i++] = static_cast<float>(decodeBuffer[pos++ % DPCM_BLOCK_SIZE]) / 128.0f;
        } while (--thisFetch > 0);

        if (pos >= sInfo.endPos) {
            if (sInfo.loopEnabled) {
                pos = sInfo.loopPos;
            } else {
                std::fill(fetchBuffer.begin() + i, fetchBuffer.end(), 0.0f);
                return false;
            }
        }
    } while (samplesToFetch > 0);
    return true;
}

bool MP2KChnPCM::sampleFetchCallbackMPTDecomp(std::vector<float> &fetchBuffer, size_t samplesRequired)
{
    if (fetchBuffer.size() >= samplesRequired)
        return true;
    size_t samplesToFetch = samplesRequired - fetchBuffer.size();
    size_t i = fetchBuffer.size();
    fetchBuffer.resize(samplesRequired);

    do {
        size_t samplesTilLoop = sInfo.endPos - pos;
        size_t thisFetch = std::min(samplesTilLoop, samplesToFetch);

        samplesToFetch -= thisFetch;
        do {
            // once again, I just took over the assembly implementation
            // there is probably plenty of room to make this nicer, but it at least works for now
            bool loNibble = pos & 1;
            uint32_t samplePos = pos++ >> 1u;
            int8_t data = sInfo.samplePtr[samplePos];

            // 4 bit nibble is shifted up to bit 31..28
            uint32_t nibble;
            if (loNibble)
                nibble = static_cast<uint32_t>(data << 28) & 0xF0000000u;
            else
                nibble = static_cast<uint32_t>(data << 24) & 0xF0000000u;

            // in the ARM ASM you can easily just shift by more than 31, but this does not work on x86/C++
            if (shiftMPTcompressed <= 63) {
                int32_t actualShift = (int32_t)(shiftMPTcompressed >> 1u);
                levelMPTcompressed = int16_t(levelMPTcompressed + (static_cast<int32_t>(nibble) >> actualShift));
            }

            if (static_cast<uint32_t>(nibble) & 0x80000000)
                nibble = static_cast<uint32_t>(-static_cast<int32_t>(nibble));

            shiftMPTcompressed = uint8_t(shiftMPTcompressed + 4);
            shiftMPTcompressed = uint8_t((uint32_t)shiftMPTcompressed - (nibble >> 28u));

            fetchBuffer[i++] = float(levelMPTcompressed) / 128.0f;
        } while (--thisFetch > 0);

        if (pos >= sInfo.endPos) {
            // MPT compressed sample cannot loop
            std::fill(fetchBuffer.begin() + i, fetchBuffer.end(), 0.0f);
            return false;
        }
    } while (samplesToFetch > 0);
    return true;
}
