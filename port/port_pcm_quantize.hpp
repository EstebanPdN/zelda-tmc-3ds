#pragma once

#include <algorithm>
#include <cstdint>

/* std::lroundf is a libm call on ARM11. After clamping, adding a signed half
 * and using C++'s truncation-toward-zero conversion is exactly the same
 * round-half-away-from-zero operation for every finite PCM input in range. */
static inline int16_t Port_QuantizePcm16(float value) noexcept
{
    value = std::clamp(value, -1.0f, 1.0f);
    const float scaled = value * 32767.0f;
    return static_cast<int16_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}
