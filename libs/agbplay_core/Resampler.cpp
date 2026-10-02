#include "Resampler.hpp"

#include "Debug.hpp"
#include "Util.hpp"

namespace {

static double sinc_pi_compat(double x) {
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    return std::sin(x) / x;
}

} // namespace

std::unique_ptr<Resampler> Resampler::MakeResampler(ResamplerType t)
{
    switch (t) {
    case ResamplerType::NEAREST:
        return std::make_unique<NearestResampler>();
    case ResamplerType::LINEAR:
        return std::make_unique<LinearResampler>();
    case ResamplerType::SINC:
        return std::make_unique<SincResampler>();
    case ResamplerType::BLEP:
        return std::make_unique<BlepResampler>();
    case ResamplerType::BLAMP:
        return std::make_unique<BlampResampler>();
    }
    throw std::logic_error("MakeResampler: Trying to to instantiate resampler for invalid enum value");
}

Resampler::~Resampler()
{
}

NearestResampler::NearestResampler()
{
}

NearestResampler::~NearestResampler()
{
}

void NearestResampler::Reset()
{
    fetchBuffer.clear();
    phase = 0.0f;
}

bool NearestResampler::Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback)
{
    if (buffer.size() == 0)
        return true;

    phaseInc = std::max(phaseInc, 0.0f);

    size_t samplesRequired = size_t(phase + phaseInc * static_cast<float>(buffer.size()));
    // be sure and fetch one more sample in case of odd rounding errors
    samplesRequired += 1;
    const bool continuePlayback = fetchCallback(fetchBuffer, samplesRequired);

    int32_t fi = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
        buffer[i] = fetchBuffer[static_cast<size_t>(fi)];
        phase += phaseInc;
        const int32_t istep = static_cast<int32_t>(phase);
        phase -= static_cast<float>(istep);
        fi += istep;
    }

    // remove first fi elements from the fetch buffer since they are no longer needed
    fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + fi);

    return continuePlayback;
}

LinearResampler::LinearResampler()
{
    Reset();
}

LinearResampler::~LinearResampler()
{
}

void LinearResampler::Reset()
{
    fetchBuffer.clear();
    phase = 0.0f;
}

bool LinearResampler::Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback)
{
    if (buffer.size() == 0)
        return true;

    phaseInc = std::max(phaseInc, 0.0f);

    size_t samplesRequired = static_cast<size_t>(phase + phaseInc * static_cast<float>(buffer.size()));
    // be sure and fetch one more sample in case of odd rounding errors
    samplesRequired += 1;
    // fetch one more for linear interpolation
    samplesRequired += 1;
    const bool continuePlayback = fetchCallback(fetchBuffer, samplesRequired);

    int32_t fi = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
        const float a = fetchBuffer[static_cast<size_t>(fi)];
        const float b = fetchBuffer[static_cast<size_t>(fi) + 1];
        buffer[i] = a + phase * (b - a);
        phase += phaseInc;
        const int32_t istep = static_cast<int32_t>(phase);
        phase -= static_cast<float>(istep);
        fi += istep;
    }

    // remove first fi elements from the fetch buffer since they are no longer needed
    fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + fi);

    return continuePlayback;
}

SincResampler::SincResampler()
{
    Reset();
}

SincResampler::~SincResampler()
{
}

void SincResampler::Reset()
{
    fetchBuffer.clear();
    fetchBuffer.resize(INTERP_FILTER_SIZE, 0.0f);
    phase = 0.0f;
}

bool SincResampler::Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback)
{
    if (buffer.size() == 0)
        return true;

    phaseInc = std::max(phaseInc, 0.0f);

    size_t samplesRequired = static_cast<size_t>(phase + phaseInc * static_cast<float>(buffer.size()));
    // be sure and fetch one more sample in case of odd rounding errors
    samplesRequired += 1;
    // fetch a few more for complete windowed sinc interpolation
    samplesRequired += INTERP_FILTER_SIZE * 2;
    const bool continuePlayback = fetchCallback(fetchBuffer, samplesRequired);
    const float sincStep = phaseInc > INTERP_FILTER_CUTOFF_FREQ ? INTERP_FILTER_CUTOFF_FREQ / phaseInc : 1.00f;

    int32_t fi = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
        float sampleSum = 0.0f;
        float kernelSum = 0.0f;

        for (int wi = -INTERP_FILTER_SIZE + 1; wi <= INTERP_FILTER_SIZE; wi++) {
            const float sincIndex = (float(wi) - phase) * sincStep;
            const float windowIndex = float(wi) - phase;
            const float s = fast_sincf(sincIndex);
            const float w = window_func(windowIndex);
            const float kernel = s * w;
            sampleSum += kernel * fetchBuffer[static_cast<size_t>(fi + wi + INTERP_FILTER_SIZE) - 1];
            kernelSum += kernel;
        }

        phase += phaseInc;
        const int32_t istep = static_cast<int32_t>(phase);
        phase -= static_cast<float>(istep);
        fi += istep;

        buffer[i] = sampleSum / kernelSum;
    }

    // remove first fi elements from the fetch buffer since they are no longer needed
    fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + fi);

    return continuePlayback;
}

/*
 * fast trigonometric functions
 */

const std::array<float, Resampler::INTERP_FILTER_LUT_SIZE> SincResampler::cosLut = []() {
    std::array<float, INTERP_FILTER_LUT_SIZE> l;
    for (size_t i = 0; i < l.size(); i++) {
        float index = float(i) * float(2.0 * M_PI / double(INTERP_FILTER_LUT_SIZE));
        l[i] = std::cos(index);
    }
    return l;
}();

const std::array<float, Resampler::INTERP_FILTER_LUT_SIZE + 2> SincResampler::sincLut = []() {
    std::array<float, INTERP_FILTER_LUT_SIZE + 2> l;
    for (size_t i = 0; i < INTERP_FILTER_LUT_SIZE + 1; i++) {
        float index = float(i) * float(INTERP_FILTER_SIZE * M_PI / double(INTERP_FILTER_LUT_SIZE));
        l[i] = static_cast<float>(sinc_pi_compat(index));
    }
    l[INTERP_FILTER_LUT_SIZE + 1] = 0.0f;
    return l;
}();

const std::array<float, Resampler::INTERP_FILTER_LUT_SIZE + 2> SincResampler::winLut = []() {
    // hann window (raised cosine)
    std::array<float, INTERP_FILTER_LUT_SIZE + 2> l;
    for (size_t i = 0; i < INTERP_FILTER_LUT_SIZE + 1; i++) {
        float index = float(i) * float(M_PI / double(INTERP_FILTER_LUT_SIZE));
        l[i] = 0.5f + (0.5f * cosf(index));
    }
    l[INTERP_FILTER_LUT_SIZE + 1] = 0.0f;
    return l;
}();

inline float SincResampler::fast_sinf(float t)
{
    return SincResampler::fast_cosf(t - float(M_PI / 2.0));
}

inline float SincResampler::fast_cosf(float t)
{
    t = std::abs(t);
    t *= float(double(INTERP_FILTER_LUT_SIZE) / (2.0 * M_PI));
    uint32_t left_index = static_cast<uint32_t>(t);
    const float fraction = t - static_cast<float>(left_index);
    const uint32_t right_index = (left_index + 1) % INTERP_FILTER_LUT_SIZE;
    left_index %= INTERP_FILTER_LUT_SIZE;
    return cosLut[left_index] + fraction * (cosLut[right_index] - cosLut[left_index]);
}

inline float SincResampler::fast_sincf(float t)
{
    t = std::abs(t);
    // assert(t <= INTERP_FILTER_SIZE);
    t *= float(double(INTERP_FILTER_LUT_SIZE) / double(INTERP_FILTER_SIZE));
    const uint32_t left_index = static_cast<uint32_t>(t);
    const float fraction = t - static_cast<float>(left_index);
    const uint32_t right_index = left_index + 1;
    return sincLut[left_index] + fraction * (sincLut[right_index] - sincLut[left_index]);
}

inline float SincResampler::window_func(float t)
{
    // assert(t >= -float(INTERP_FILTER_SIZE));
    // assert(t <= +float(INTERP_FILTER_SIZE));
    t = std::abs(t);
    t *= float(double(INTERP_FILTER_LUT_SIZE) / double(INTERP_FILTER_SIZE));
    const uint32_t left_index = static_cast<uint32_t>(t);
    const float fraction = t - static_cast<float>(left_index);
    const uint32_t right_index = left_index + 1;
    return winLut[left_index] + fraction * (winLut[right_index] - winLut[left_index]);
}

BlepResampler::BlepResampler()
{
    Reset();
}

BlepResampler::~BlepResampler()
{
}

void BlepResampler::Reset()
{
    fetchBuffer.clear();
    fetchBuffer.resize(INTERP_FILTER_SIZE, 0.0f);
    phase = 0.0f;
}

bool BlepResampler::Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback)
{
    if (buffer.size() == 0)
        return true;

    phaseInc = std::max(phaseInc, 0.0f);

    size_t samplesRequired = static_cast<size_t>(phase + phaseInc * static_cast<float>(buffer.size()));
    // be sure and fetch one more sample in case of odd rounding errors
    samplesRequired += 1;
    // fetch a few more for complete windowed sinc interpolation
    samplesRequired += INTERP_FILTER_SIZE * 2;
    const bool continuePlayback = fetchCallback(fetchBuffer, samplesRequired);
    const float sincStep = INTERP_FILTER_CUTOFF_FREQ / phaseInc;

    int32_t fi = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
        float sampleSum = 0.0f;
        float kernelSum = 0.0f;

        float sl = fast_Si((float(-INTERP_FILTER_SIZE + 1) - phase - 0.5f) * sincStep);

        for (int wi = -INTERP_FILTER_SIZE + 1; wi <= INTERP_FILTER_SIZE; wi++) {
            const float sr = fast_Si((float(wi) - phase + 0.5f) * sincStep);
            const float kernel = sr - sl;
            sampleSum += kernel * fetchBuffer[static_cast<size_t>(fi + wi + INTERP_FILTER_SIZE) - 1];
            kernelSum += kernel;
            sl = sr;
        }

        phase += phaseInc;
        const int32_t istep = static_cast<int32_t>(phase);
        phase -= static_cast<float>(istep);
        fi += istep;

        buffer[i] = sampleSum / kernelSum;
    }

    // remove first i elements from the fetch buffer since they are no longer needed
    fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + fi);

    return continuePlayback;
}

const std::array<float, Resampler::INTERP_FILTER_LUT_SIZE + 2> BlepResampler::SiLut = []() {
    std::array<float, INTERP_FILTER_LUT_SIZE + 2> l;
    double acc = 0.0;
    const double step_per_index = double(INTERP_FILTER_SIZE) / double(INTERP_FILTER_LUT_SIZE);
    const double integration_inc = step_per_index / double(INTEGRAL_RESOLUTION);
    double index = 0.0;
    double prev_value = 1.0;

    for (size_t i = 0; i < INTERP_FILTER_LUT_SIZE + 1; i++) {
        double convergence_level = 0.5 - 0.5 * cos(double(i) * M_PI / double(INTERP_FILTER_LUT_SIZE));
        double integral_level = 1.0 - convergence_level;
        l[i] = static_cast<float>(acc * integral_level + 0.5 * convergence_level);
        for (size_t j = 0; j < INTEGRAL_RESOLUTION; j++) {
            index += integration_inc;
            double new_value = sinc_pi_compat(M_PI * index);
            acc += (new_value + prev_value) * integration_inc * 0.5;
            prev_value = new_value;
        }
    }
    l[INTERP_FILTER_LUT_SIZE + 1] = 0.5f;
    return l;
}();

BlampResampler::BlampResampler()
{
    Reset();
}

BlampResampler::~BlampResampler()
{
}

void BlampResampler::Reset()
{
    fetchBuffer.clear();
    fetchBuffer.resize(INTERP_FILTER_SIZE, 0.0f);
    phase = 0.0f;
}

bool BlampResampler::Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback)
{
    if (buffer.size() == 0)
        return true;

    phaseInc = std::max(phaseInc, 0.0f);

    size_t samplesRequired = static_cast<size_t>(phase + phaseInc * static_cast<float>(buffer.size()));
    // be sure and fetch one more sample in case of odd rounding errors
    samplesRequired += 1;
    // fetch a few more for complete windowed sinc interpolation
    samplesRequired += INTERP_FILTER_SIZE * 2;
    const bool continuePlayback = fetchCallback(fetchBuffer, samplesRequired);
    const float sincStep = INTERP_FILTER_CUTOFF_FREQ / phaseInc;

    int32_t fi = 0;
    for (size_t i = 0; i < buffer.size(); i++) {
        float sampleSum = 0.0f;
        float kernelSum = 0.0f;

        float sl = fast_Ti((float(-INTERP_FILTER_SIZE + 1) - phase - 1.0f) * sincStep);
        float sm = fast_Ti((float(-INTERP_FILTER_SIZE + 1) - phase) * sincStep);

        for (int wi = -INTERP_FILTER_SIZE + 1; wi <= INTERP_FILTER_SIZE; wi++) {
            const float TiIndexRight = (float(wi) - phase + 1.0f) * sincStep;
            const float sr = fast_Ti(TiIndexRight);
            const float kernel = sr - 2.0f * sm + sl;
            sampleSum += kernel * fetchBuffer[static_cast<size_t>(fi + wi + INTERP_FILTER_SIZE) - 1];
            kernelSum += kernel;
            sl = sm;
            sm = sr;
        }

        phase += phaseInc;
        const int32_t istep = static_cast<int32_t>(phase);
        phase -= static_cast<float>(istep);
        fi += istep;

        buffer[i] = sampleSum / kernelSum;
    }

    // remove first i elements from the fetch buffer since they are no longer needed
    fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + fi);

    return continuePlayback;
}

// I call "Ti" the integral of Si function. I don't know its proper name
const std::array<float, Resampler::INTERP_FILTER_LUT_SIZE + 2> BlampResampler::TiLut = []() {
    std::array<float, INTERP_FILTER_LUT_SIZE + 2> l;
    double acc = 0.0;
    double step_per_index = double(INTERP_FILTER_SIZE) / double(INTERP_FILTER_LUT_SIZE);
    double integration_inc = step_per_index / double(INTEGRAL_RESOLUTION);
    double index = 0.0;
    double prev_value = 1.0;

    for (size_t i = 0; i < INTERP_FILTER_LUT_SIZE + 1; i++) {
        const double t = double(i) * step_per_index;
        const double convergence_value = t * 0.5;
        const double function_value = t * acc + cos(M_PI * t) / (M_PI * M_PI);
        const double interpolation_t = 0.5 - 0.5 * cos(double(i) * M_PI / double(INTERP_FILTER_LUT_SIZE));
        const double interpolated_value = function_value + interpolation_t * (convergence_value - function_value);
        l[i] = static_cast<float>(interpolated_value);

        for (size_t j = 0; j < INTEGRAL_RESOLUTION; j++) {
            index += integration_inc;
            double new_value = sinc_pi_compat(M_PI * index);
            acc += (new_value + prev_value) * integration_inc * 0.5;
            prev_value = new_value;
        }
    }
    l[INTERP_FILTER_LUT_SIZE + 1] = static_cast<float>(((INTERP_FILTER_LUT_SIZE + 1) * step_per_index) * 0.5);
    return l;
}();
