#include "LoudnessCalculator.hpp"

#include "Constants.hpp"
#include "Util.hpp"

#include <cassert>
#include <cmath>
#include <numbers>

LoudnessCalculator::LoudnessCalculator(float lowpassFreq, uint32_t sampleRate) :
    lpAlpha(calcAlpha(lowpassFreq, sampleRate))
{
}

void LoudnessCalculator::CalcLoudness(std::span<const sample> buffer)
{
    using std::numbers::sqrt2_v;

    for (size_t i = 0; i < buffer.size(); i++) {
        peakLeft = std::max(peakLeft * (1.0f - lpAlpha), 0.0f);
        peakRight = std::max(peakRight * (1.0f - lpAlpha), 0.0f);
        float l = buffer[i].left;
        float r = buffer[i].right;
        peakLeft = std::max(std::abs(peakLeft), l);
        peakRight = std::max(std::abs(peakRight), r);
        l *= l;
        r *= r;
        avgVolLeftSq = avgVolLeftSq + lpAlpha * (l - avgVolLeftSq);
        avgVolRightSq = avgVolRightSq + lpAlpha * (r - avgVolRightSq);
    }

    // normalize RMS value to 1.0 for a 1.0 amplitude sine wave
    rmsLeft = sqrtf(avgVolLeftSq) * sqrt2_v<float>;
    rmsRight = sqrtf(avgVolRightSq) * sqrt2_v<float>;
}

void LoudnessCalculator::GetLoudness(float &rmsLeft, float &rmsRight, float &peakLeft, float &peakRight) const
{
    rmsLeft = this->rmsLeft;
    rmsRight = this->rmsRight;
    peakLeft = this->peakLeft;
    peakRight = this->peakRight;
}

void LoudnessCalculator::Reset()
{
    avgVolLeftSq = 0.0f;
    avgVolRightSq = 0.0f;
    rmsLeft = 0.0f;
    rmsRight = 0.0f;
    peakLeft = 0.0f;
    peakRight = 0.0f;
}

float LoudnessCalculator::calcAlpha(float lowpassFreq, uint32_t sampleRate)
{
    const float rc = 1.0f / (lowpassFreq * 2.0f * std::numbers::pi_v<float>);
    const float dt = 1.0f / static_cast<float>(sampleRate);
    return dt / (rc + dt);
}
