#include "ReverbEffect.hpp"

#include "Constants.hpp"
#include "Util.hpp"
#include "Xcept.hpp"

#include <algorithm>
#include <cassert>

/*
 * public ReverbEffect
 */

ReverbEffect::ReverbEffect(uint8_t intensity, size_t streamRate, uint8_t numAgbBuffers) :
    reverbBuffer((streamRate / AGB_APPROX_FPS) * numAgbBuffers, sample{0.0f, 0.0f})
{
    SetLevel(intensity);
    const size_t bufferLen = streamRate / AGB_APPROX_FPS;
    bufferPos = 0;
    bufferPos2 = bufferLen;
}

ReverbEffect::~ReverbEffect()
{
}

void ReverbEffect::Process(std::span<sample> buffer)
{
    while (buffer.size() > 0) {
        // TODO change the semantics of ProcessInternal to return 'processed' instead of 'left' samples
        const size_t left = ProcessInternal(buffer);
        buffer = buffer.subspan(buffer.size() - left);
    }
}

void ReverbEffect::SetLevel(uint8_t level)
{
    intensity = level / 128.0f;
}

void ReverbEffect::Reset()
{
    std::fill(reverbBuffer.begin(), reverbBuffer.end(), sample{0.0f, 0.0f});
}

std::unique_ptr<ReverbEffect>
    ReverbEffect::MakeReverb(ReverbType reverbType, uint8_t intensity, size_t sampleRate, uint8_t numDmaBuffers)
{
    switch (reverbType) {
    case ReverbType::NORMAL:
        return std::make_unique<ReverbEffect>(intensity, sampleRate, numDmaBuffers);
    case ReverbType::NONE:
        return std::make_unique<ReverbEffect>(0, sampleRate, numDmaBuffers);
    case ReverbType::GS1:
        return std::make_unique<ReverbGS1>(intensity, sampleRate, numDmaBuffers);
    case ReverbType::GS2:
        return std::make_unique<ReverbGS2>(intensity, sampleRate, numDmaBuffers, 0.4140625f, -0.0625f);
        // Mario Power Tennis uses same coefficients as Mario Golf Advance Tour
    case ReverbType::MGAT:
        return std::make_unique<ReverbGS2>(intensity, sampleRate, numDmaBuffers, 0.25f, -0.046875f);
    case ReverbType::TEST:
        return std::make_unique<ReverbTest>(intensity, sampleRate, numDmaBuffers);
    default:
        throw Xcept("MakeReverb: Invalid Reverb Effect: {}", static_cast<int>(reverbType));
    }
}

/*
 * protected ReverbEffect
 */

size_t ReverbEffect::ProcessInternal(std::span<sample> buffer)
{
    std::vector<sample> &rbuf = reverbBuffer;
    const size_t count =
        std::min(std::min(reverbBuffer.size() - bufferPos2, reverbBuffer.size() - bufferPos), buffer.size());
    bool reset = false, reset2 = false;
    if (reverbBuffer.size() - bufferPos == count) {
        reset = true;
    }
    if (reverbBuffer.size() - bufferPos2 == count) {
        reset2 = true;
    }
    for (size_t i = 0; i < count; i++) {
        const float rev =
            (rbuf[bufferPos].left + rbuf[bufferPos].right + rbuf[bufferPos2].left + rbuf[bufferPos2].right) * intensity
            * (1.0f / 4.0f);
        rbuf[bufferPos].left = buffer[i].left += rev;
        rbuf[bufferPos].right = buffer[i].right += rev;
        bufferPos++;
        bufferPos2++;
    }
    if (reset2)
        bufferPos2 = 0;
    if (reset)
        bufferPos = 0;
    return buffer.size() - count;
}

/*
 * ReverbGS1
 */

ReverbGS1::ReverbGS1(uint8_t intensity, size_t streamRate, uint8_t numAgbBuffers) :
    ReverbEffect(intensity, streamRate, numAgbBuffers), gsBuffer((streamRate / AGB_APPROX_FPS), sample{0.0f, 0.0f})
{
    bufferPos2 = 0;
}

ReverbGS1::~ReverbGS1()
{
}

void ReverbGS1::Reset()
{
    ReverbEffect::Reset();
    std::fill(gsBuffer.begin(), gsBuffer.end(), sample{0.0f, 0.0f});
}

size_t ReverbGS1::ProcessInternal(std::span<sample> buffer)
{
    std::vector<sample> &rbuf = reverbBuffer;
    size_t count = std::min(std::min(reverbBuffer.size() - bufferPos, gsBuffer.size() - bufferPos2), buffer.size());
    bool reset = false, resetGS = false;

    if (count == reverbBuffer.size() - bufferPos)
        reset = true;
    if (count == gsBuffer.size() - bufferPos2)
        resetGS = true;

    for (size_t i = 0; i < count; i++) {
        const float mixL = buffer[i].left + gsBuffer[bufferPos2].left;
        const float mixR = buffer[i].right + gsBuffer[bufferPos2].right;

        const float lA = rbuf[bufferPos].left;
        const float rA = rbuf[bufferPos].right;

        buffer[i].left = rbuf[bufferPos].left = mixL;
        buffer[i].right = rbuf[bufferPos].right = mixR;

        const float lRMix = 0.25f * mixL + 0.25f * rA;
        const float rRMix = 0.25f * mixR + 0.25f * lA;

        gsBuffer[bufferPos2].left = lRMix;
        gsBuffer[bufferPos2].right = rRMix;

        bufferPos++;
        bufferPos2++;
    }

    if (resetGS)
        bufferPos2 = 0;
    if (reset)
        bufferPos = 0;
    return buffer.size() - count;
}

/*
 * ReverbGS2
 */

ReverbGS2::ReverbGS2(uint8_t intensity, size_t streamRate, uint8_t numAgbBuffers, float rPrimFac, float rSecFac) :
    ReverbEffect(intensity, streamRate, numAgbBuffers),
    gs2Buffer(streamRate / AGB_APPROX_FPS, sample{0.0f, 0.0f}),
    gs2Pos(0),
    rPrimFac(rPrimFac),
    rSecFac(rSecFac)
{
    // equivalent to the offset of -0xB0 samples for a 0x210 buffer size
    bufferPos2 = reverbBuffer.size() - (gs2Buffer.size() / 3);
}

ReverbGS2::~ReverbGS2()
{
}

void ReverbGS2::Reset()
{
    ReverbEffect::Reset();
    std::fill(gs2Buffer.begin(), gs2Buffer.end(), sample{0.0f, 0.0f});
}

size_t ReverbGS2::ProcessInternal(std::span<sample> buffer)
{
    std::vector<sample> &rbuf = reverbBuffer;
    size_t count = std::min(
        std::min(reverbBuffer.size() - bufferPos2, reverbBuffer.size() - bufferPos),
        std::min(buffer.size(), gs2Buffer.size() - gs2Pos)
    );
    bool reset = false, reset2 = false, resetgs2 = false;

    if (reverbBuffer.size() - bufferPos2 == count) {
        reset2 = true;
    }
    if (reverbBuffer.size() - bufferPos == count) {
        reset = true;
    }
    if ((gs2Buffer.size() / 2) - gs2Pos == count) {
        resetgs2 = true;
    }

    for (size_t i = 0; i < count; i++) {
        const float mixL = buffer[i].left + gs2Buffer[gs2Pos].left;
        const float mixR = buffer[i].right + gs2Buffer[gs2Pos].right;

        const float lA = rbuf[bufferPos].left;
        const float rA = rbuf[bufferPos].right;

        buffer[i].left = rbuf[bufferPos].left = mixL;
        buffer[i].right = rbuf[bufferPos].right = mixR;

        const float lRMix = lA * rPrimFac + rA * rSecFac;
        const float rRMix = rA * rPrimFac + lA * rSecFac;

        const float lB = rbuf[bufferPos2].right * 0.25f;
        const float rB = mixR * 0.25f;

        gs2Buffer[gs2Pos].left = lRMix + lB;
        gs2Buffer[gs2Pos].right = rRMix + rB;

        bufferPos++;
        bufferPos2++;
        gs2Pos++;
    }
    if (reset2)
        bufferPos2 = 0;
    if (reset)
        bufferPos = 0;
    if (resetgs2)
        gs2Pos = 0;
    return buffer.size() - count;
}

/*
 * ReverbTest
 */

ReverbTest::ReverbTest(uint8_t intensity, size_t streamRate, uint8_t numAgbBuffers) :
    ReverbEffect(intensity, streamRate, numAgbBuffers)
{
}

ReverbTest::~ReverbTest()
{
}

size_t ReverbTest::ProcessInternal(std::span<sample> buffer)
{
    std::vector<sample> &rbuf = reverbBuffer;
    size_t count = std::min(std::min(reverbBuffer.size() - bufferPos, reverbBuffer.size() - bufferPos2), buffer.size());
    bool reset = false, reset2 = false;
    if (reverbBuffer.size() - bufferPos2 == count) {
        reset2 = true;
    }
    if (reverbBuffer.size() - bufferPos == count) {
        reset = true;
    }
    for (size_t i = 0; i < count; i++) {
        const float g = 0.8f;
        float input_l = buffer[i].left;
        float input_r = buffer[i].right;

        float feedback_l = rbuf[bufferPos].left;
        float feedback_r = rbuf[bufferPos].right;

        float new_feedback_l = input_l + g * feedback_l;
        float new_feedback_r = input_r + g * feedback_r;

        float output_l = -g * new_feedback_l + feedback_l;
        float output_r = -g * new_feedback_r + feedback_r;

        buffer[i].left = output_l;
        buffer[i].right = output_r;

        rbuf[bufferPos].left = -new_feedback_l;
        rbuf[bufferPos].right = -new_feedback_r;
        /*
           float in_delay_1_l = rbuf[bufferPos * 2], in_delay_1_r = rbuf[bufferPos * 2 + 1];
           float in_delay_2_l = rbuf[bufferPos2 * 2], in_delay_2_r = rbuf[bufferPos2 * 2 + 1];

           delay1HPcarryL = (in_delay_1_l + delay1HPcarryL - delay1HPprevL) * 0.95f;
           delay1HPcarryR = (in_delay_1_r + delay1HPcarryR - delay1HPprevR) * 0.95f;
           delay2HPcarryL = (in_delay_2_l + delay2HPcarryL - delay2HPprevL) * 0.95f;
           delay2HPcarryR = (in_delay_2_r + delay2HPcarryR - delay2HPprevR) * 0.95f;

           delay1HPprevL = in_delay_1_l;
           delay1HPprevR = in_delay_1_r;
           delay2HPprevL = in_delay_2_l;
           delay2HPprevR = in_delay_2_r;

           in_delay_1_l = delay1HPcarryL;
           in_delay_1_r = delay1HPcarryR;
           in_delay_2_l = delay2HPcarryL;
           in_delay_2_r = delay2HPcarryR;

           float r_left = in_delay_1_r * (4.0f / 8.0f) + in_delay_2_l * (4.0f / 8.0f);
           float r_right = -in_delay_1_l * (4.0f / 8.0f) - in_delay_2_r * (4.0f / 8.0f);

           rbuf[bufferPos * 2] = buffer[i].left += r_left;
           rbuf[bufferPos * 2 + 1] = buffer[i].right += r_right;
           */

        bufferPos++;
        bufferPos2++;
    }
    if (reset2)
        bufferPos2 = 0;
    if (reset)
        bufferPos = 0;
    return buffer.size() - count;
}
