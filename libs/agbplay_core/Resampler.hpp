#pragma once

#include "Types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

/*
 * res_data_fetch_cb fetches samplesRequired samples to fetchBuffer
 * so that the buffer can provide exactly samplesRequired samples
 *
 * returns false in case of 'end of stream'
 */
typedef std::function<bool(std::vector<float> &fetchBuffer, size_t samplesRequired)> FetchCallback;

class Resampler
{
public:
    static std::unique_ptr<Resampler> MakeResampler(ResamplerType t);

    // return value false by Process signals the "end of stream"
    virtual bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) = 0;
    virtual void Reset() = 0;
    virtual ~Resampler();

protected:
    std::vector<float> fetchBuffer;
    float phase = 0.0f;

    /* Filter is symmetric, so the actual filter size is double the size specified. */
    static inline const uint16_t INTERP_FILTER_SIZE = 16;
    /* Normally in the DSP world, a frequency is specified as normalized frequency (i.e. 0.5fs).
     * However, we express it as a ratio to this normalized frequency. Accordingly, the cutoff needs
     * to occur a bit before the normalized frequency, to give the transition band enough headroom
     * and thus to avoid aliasing. 0.85 seems to work well for 48kHz. */
    static inline const float INTERP_FILTER_CUTOFF_FREQ = 0.85f;
    /* LUT size to use for interpolation filter tables. Must be
     * a power of two in order to not break AVX2 support, and also to
     * not plumment performance. */
    static inline const uint16_t INTERP_FILTER_LUT_SIZE = 256;
    /* Integral resolution specifies how exact the SiLut is calculated.
     * A numerical integration is performed with N samples per value.
     * Not required to be power-of-two, but perhaps a good idea to be. */
    static inline const uint16_t INTEGRAL_RESOLUTION = 256;
};

class NearestResampler : public Resampler
{
public:
    NearestResampler();
    ~NearestResampler() override;
    bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) override;
    void Reset() override;
};

class LinearResampler : public Resampler
{
public:
    LinearResampler();
    ~LinearResampler() override;
    bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) override;
    void Reset() override;
};

class SincResampler : public Resampler
{
public:
    SincResampler();
    virtual ~SincResampler() override;
    virtual bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) override;
    void Reset() override;

private:
    static float fast_sinf(float t);
    static float fast_cosf(float t);
    static float fast_sincf(float t);
    static float window_func(float t);

protected:
    static const std::array<float, INTERP_FILTER_LUT_SIZE> cosLut;
    static const std::array<float, INTERP_FILTER_LUT_SIZE + 2> sincLut;
    static const std::array<float, INTERP_FILTER_LUT_SIZE + 2> winLut;
};

class BlepResampler : public Resampler
{
public:
    BlepResampler();
    virtual ~BlepResampler() override;
    virtual bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) override;
    void Reset() override;

protected:
    static inline float fast_Si(float t)
    {
        const float signed_t = t;
        t = std::abs(t);
        t = std::min(t, float(INTERP_FILTER_SIZE));
        t *= float(double(INTERP_FILTER_LUT_SIZE) / double(INTERP_FILTER_SIZE));
        const uint32_t left_index = static_cast<uint32_t>(t);
        const float fraction = t - static_cast<float>(left_index);
        const uint32_t right_index = left_index + 1;
        const float retval = SiLut[left_index] + fraction * (SiLut[right_index] - SiLut[left_index]);
        return std::copysignf(retval, signed_t);
    }

    static const std::array<float, INTERP_FILTER_LUT_SIZE + 2> SiLut;
};

class BlampResampler : public Resampler
{
public:
    BlampResampler();
    ~BlampResampler() override;
    bool Process(std::span<float> buffer, float phaseInc, const FetchCallback &fetchCallback) override;
    void Reset() override;

protected:
    static float fast_Ti(float t)
    {
        t = std::abs(t);
        const float old_t = t;
        t = std::min(t, float(INTERP_FILTER_SIZE));
        t *= float(double(INTERP_FILTER_LUT_SIZE) / double(INTERP_FILTER_SIZE));
        const uint32_t left_index = static_cast<uint32_t>(t);
        const float fraction = t - static_cast<float>(left_index);
        const uint32_t right_index = left_index + 1;
        const float retval = TiLut[left_index] + fraction * (TiLut[right_index] - TiLut[left_index]);
        if (old_t > float(INTERP_FILTER_SIZE))
            return old_t * 0.5f;
        else
            return retval;
    }

    static const std::array<float, INTERP_FILTER_LUT_SIZE + 2> TiLut;
};
