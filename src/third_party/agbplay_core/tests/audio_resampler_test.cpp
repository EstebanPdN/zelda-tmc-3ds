#include "Resampler.hpp"
#include "port_pcm_quantize.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <vector>

namespace {
bool gCountAllocations = false;
size_t gAllocationCount = 0;
}

void* operator new(std::size_t size) {
    if (gCountAllocations) {
        ++gAllocationCount;
    }
    if (void* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {

constexpr size_t kFramesPerBlock = 68;
constexpr size_t kParityBlocks = 4096;
constexpr size_t kBenchmarkBlocks = 50000;

static_assert(std::is_trivially_copyable_v<FetchCallback>);
static_assert(sizeof(FetchCallback) == sizeof(void*) * 2);

class PcmSource {
public:
    bool Append(float* output, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            state_ = state_ * 1664525u + 1013904223u;
            const int8_t pcm = static_cast<int8_t>(state_ >> 24);
            output[i] = static_cast<float>(pcm) * (1.0f / 128.0f);
        }
        fetched_ += count;
        return true;
    }

    bool Fetch(std::vector<float>& buffer, size_t required) {
        if (buffer.size() >= required) {
            return true;
        }

        const size_t oldSize = buffer.size();
        buffer.resize(required);
        return Append(buffer.data() + oldSize, required - oldSize);
    }

    size_t Fetched() const { return fetched_; }

private:
    uint32_t state_ = 0x31415926u;
    size_t fetched_ = 0;
};

class ReferenceNearest {
public:
    bool Process(std::span<float> output, float phaseIncrement, PcmSource& source) {
        if (output.empty()) {
            return true;
        }

        phaseIncrement = std::max(phaseIncrement, 0.0f);
        size_t required = static_cast<size_t>(phase_ + phaseIncrement * static_cast<float>(output.size()));
        required += 1;
        const bool playing = source.Fetch(fetchBuffer_, required);

        int32_t consumed = 0;
        for (float& value : output) {
            value = fetchBuffer_[static_cast<size_t>(consumed)];
            phase_ += phaseIncrement;
            const int32_t step = static_cast<int32_t>(phase_);
            phase_ -= static_cast<float>(step);
            consumed += step;
        }
        fetchBuffer_.erase(fetchBuffer_.begin(), fetchBuffer_.begin() + consumed);
        return playing;
    }

private:
    std::vector<float> fetchBuffer_;
    float phase_ = 0.0f;
};

class ReferenceBlep : public BlepResampler {
public:
    bool ProcessReference(std::span<float> output, float phaseIncrement, const FetchCallback& fetch) {
        if (output.empty()) {
            return true;
        }
        phaseIncrement = std::max(phaseIncrement, 0.0f);
        size_t required = static_cast<size_t>(phase + phaseIncrement * static_cast<float>(output.size())) + 1;
        required += INTERP_FILTER_SIZE * 2;
        const bool playing = fetch(fetchBuffer, required);
        const float sincStep = INTERP_FILTER_CUTOFF_FREQ / phaseIncrement;

        int32_t consumed = 0;
        for (float& value : output) {
            float sampleSum = 0.0f;
            float kernelSum = 0.0f;
            float left = fast_Si((float(-INTERP_FILTER_SIZE + 1) - phase - 0.5f) * sincStep);
            for (int wi = -INTERP_FILTER_SIZE + 1; wi <= INTERP_FILTER_SIZE; ++wi) {
                const float right = fast_Si((float(wi) - phase + 0.5f) * sincStep);
                const float kernel = right - left;
                sampleSum += kernel * fetchBuffer[static_cast<size_t>(consumed + wi + INTERP_FILTER_SIZE) - 1];
                kernelSum += kernel;
                left = right;
            }
            phase += phaseIncrement;
            const int32_t step = static_cast<int32_t>(phase);
            phase -= static_cast<float>(step);
            consumed += step;
            value = sampleSum / kernelSum;
        }
        fetchBuffer.erase(fetchBuffer.begin(), fetchBuffer.begin() + consumed);
        return playing;
    }
};

uint32_t Crc32Update(uint32_t crc, const void* bytes, size_t size) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

int16_t Quantize(float sample) {
    sample = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(sample * 32767.0f));
}

void CheckQuantizer(void) {
    auto check = [](float value) {
        const int16_t reference = Quantize(value);
        const int16_t actual = Port_QuantizePcm16(value);
        if (actual != reference) {
            std::fprintf(stderr, "PCM quantizer mismatch value=%a got=%d expected=%d\n",
                         static_cast<double>(value), static_cast<int>(actual), static_cast<int>(reference));
            std::exit(1);
        }
    };

    for (int32_t integer = -32767; integer < 32767; ++integer) {
        float value = (static_cast<float>(integer) + 0.5f) * (1.0f / 32767.0f);
        for (int step = 0; step < 4; ++step) {
            check(value);
            check(std::nextafter(value, -1.0f));
            check(std::nextafter(value, 1.0f));
            value = std::nextafter(value, integer < 0 ? -1.0f : 1.0f);
        }
    }

    uint32_t state = 0xC001D00Du;
    for (size_t i = 0; i < 1000000; ++i) {
        state = state * 1664525u + 1013904223u;
        const int32_t signedBits = static_cast<int32_t>(state);
        check(static_cast<float>(signedBits) * (1.0f / 2147483648.0f));
    }
    check(-1.0f);
    check(-2.0f);
    check(std::numeric_limits<float>::lowest());
    check(-0.0f);
    check(0.0f);
    check(1.0f);
    check(2.0f);
    check(std::numeric_limits<float>::max());
    std::printf("PCM quantizer: exhaustive half-step neighbors + 1000000 deterministic values match lroundf\n");
}

float PhaseIncrement(size_t block) {
    static constexpr std::array<float, 8> kPattern = {
        0.0f, 0.125f, 0.5f, 0.999f, 1.0f, 1.33333337f, 2.75f, 7.9375f,
    };
    return kPattern[block % kPattern.size()];
}

float BlepPhaseIncrement(size_t block) {
    static constexpr std::array<float, 8> kPattern = {
        0.01171875f, 0.03125f, 0.0625f, 0.125f, 0.33333334f, 0.75f, 1.25f, 3.5f,
    };
    return kPattern[block % kPattern.size()];
}

uint32_t RunBlepParity(void) {
    BlepResampler production;
    ReferenceBlep reference;
    PcmSource productionSource;
    PcmSource referenceSource;
    std::array<float, kFramesPerBlock> actual{};
    std::array<float, kFramesPerBlock> expected{};
    uint32_t crc = 0xFFFFFFFFu;
    uint64_t productionNs = 0;
    uint64_t referenceNs = 0;

    auto productionCallback = [&productionSource](std::vector<float>& buffer, size_t required) {
        return productionSource.Fetch(buffer, required);
    };
    auto referenceCallback = [&referenceSource](std::vector<float>& buffer, size_t required) {
        return referenceSource.Fetch(buffer, required);
    };
    FetchCallback productionFetch(productionCallback);
    FetchCallback referenceFetch(referenceCallback);

    for (size_t block = 0; block < 1024; ++block) {
        const float increment = BlepPhaseIncrement(block);
        const auto productionStart = std::chrono::steady_clock::now();
        production.Process(actual, increment, productionFetch);
        const auto productionEnd = std::chrono::steady_clock::now();
        reference.ProcessReference(expected, increment, referenceFetch);
        const auto referenceEnd = std::chrono::steady_clock::now();
        productionNs += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(productionEnd - productionStart).count());
        referenceNs += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(referenceEnd - productionEnd).count());
        if (std::memcmp(actual.data(), expected.data(), sizeof(actual)) != 0) {
            for (size_t frame = 0; frame < actual.size(); ++frame) {
                if (std::bit_cast<uint32_t>(actual[frame]) != std::bit_cast<uint32_t>(expected[frame])) {
                    std::fprintf(stderr, "BLEP mismatch block=%zu frame=%zu got=%08x expected=%08x\n",
                                 block, frame, std::bit_cast<uint32_t>(actual[frame]),
                                 std::bit_cast<uint32_t>(expected[frame]));
                    break;
                }
            }
            std::exit(1);
        }
        crc = Crc32Update(crc, actual.data(), sizeof(actual));
    }
    if (productionSource.Fetched() != referenceSource.Fetched()) {
        std::fprintf(stderr, "BLEP source-duration mismatch got=%zu expected=%zu\n",
                     productionSource.Fetched(), referenceSource.Fetched());
        std::exit(1);
    }
    std::printf("BLEP parity: 1024 blocks, %zu frames, %zu source samples, CRC32 %08x\n",
                1024 * kFramesPerBlock, productionSource.Fetched(), ~crc);
    std::printf("BLEP optimized/reference: %.3f/%.3f ms, optimized %.1f%% of reference\n",
                static_cast<double>(productionNs) / 1000000.0,
                static_cast<double>(referenceNs) / 1000000.0,
                static_cast<double>(productionNs) * 100.0 / static_cast<double>(referenceNs));
    return ~crc;
}

uint32_t RunParity(void) {
    NearestResampler production;
    ReferenceNearest reference;
    PcmSource productionSource;
    PcmSource referenceSource;
    std::array<float, kFramesPerBlock> actual{};
    std::array<float, kFramesPerBlock> expected{};
    std::array<int16_t, kFramesPerBlock> pcm{};
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t block = 0; block < kParityBlocks; ++block) {
        const float phaseIncrement = PhaseIncrement(block);
        if (!production.ProcessDirect(actual, phaseIncrement,
                                      [&productionSource](float* output, size_t count) {
                                          return productionSource.Append(output, count);
                                      }) ||
            !reference.Process(expected, phaseIncrement, referenceSource)) {
            std::fprintf(stderr, "unexpected end of deterministic PCM stream at block %zu\n", block);
            std::exit(1);
        }
        if (std::memcmp(actual.data(), expected.data(), sizeof(actual)) != 0) {
            for (size_t frame = 0; frame < actual.size(); ++frame) {
                if (std::bit_cast<uint32_t>(actual[frame]) != std::bit_cast<uint32_t>(expected[frame])) {
                    std::fprintf(stderr,
                                 "nearest mismatch block=%zu frame=%zu got=%08x expected=%08x\n",
                                 block, frame, std::bit_cast<uint32_t>(actual[frame]),
                                 std::bit_cast<uint32_t>(expected[frame]));
                    break;
                }
            }
            std::exit(1);
        }
        for (size_t frame = 0; frame < actual.size(); ++frame) {
            pcm[frame] = Quantize(actual[frame]);
        }
        crc = Crc32Update(crc, pcm.data(), sizeof(pcm));
    }

    if (productionSource.Fetched() != referenceSource.Fetched()) {
        std::fprintf(stderr, "fetch duration mismatch got=%zu expected=%zu\n",
                     productionSource.Fetched(), referenceSource.Fetched());
        std::exit(1);
    }

    std::printf("PCM parity: %zu blocks, %zu frames, %zu source samples, CRC32 %08x\n",
                kParityBlocks, kParityBlocks * kFramesPerBlock, productionSource.Fetched(), ~crc);
    return ~crc;
}

void RunBenchmark(void) {
    NearestResampler genericResampler;
    NearestResampler directResampler;
    PcmSource genericSource;
    PcmSource directSource;
    std::array<float, kFramesPerBlock> output{};
    for (size_t block = 0; block < 64; ++block) {
        auto callback = [&genericSource](std::vector<float>& buffer, size_t required) {
            return genericSource.Fetch(buffer, required);
        };
        FetchCallback fetch = callback;
        genericResampler.Process(output, PhaseIncrement(block), fetch);
        directResampler.ProcessDirect(output, PhaseIncrement(block),
                                      [&directSource](float* destination, size_t count) {
                                          return directSource.Append(destination, count);
                                      });
    }

    const auto genericStart = std::chrono::steady_clock::now();
    for (size_t block = 0; block < kBenchmarkBlocks; ++block) {
        auto callback = [&genericSource](std::vector<float>& buffer, size_t required) {
            return genericSource.Fetch(buffer, required);
        };
        FetchCallback fetch = callback;
        genericResampler.Process(output, PhaseIncrement(block), fetch);
    }
    const auto genericEnd = std::chrono::steady_clock::now();

    const auto directStart = std::chrono::steady_clock::now();
    for (size_t block = 0; block < kBenchmarkBlocks; ++block) {
        directResampler.ProcessDirect(output, PhaseIncrement(block),
                                      [&directSource](float* destination, size_t count) {
                                          return directSource.Append(destination, count);
                                      });
    }
    const auto directEnd = std::chrono::steady_clock::now();
    const auto genericNs = std::chrono::duration_cast<std::chrono::nanoseconds>(genericEnd - genericStart).count();
    const auto directNs = std::chrono::duration_cast<std::chrono::nanoseconds>(directEnd - directStart).count();
    const double genericPerBlock = static_cast<double>(genericNs) / static_cast<double>(kBenchmarkBlocks);
    const double directPerBlock = static_cast<double>(directNs) / static_cast<double>(kBenchmarkBlocks);
    std::printf("Nearest generic/direct: %.3f/%.3f ms, %.1f/%.1f ns/block, direct %.1f%% of generic\n",
                static_cast<double>(genericNs) / 1000000.0, static_cast<double>(directNs) / 1000000.0,
                genericPerBlock, directPerBlock, directPerBlock * 100.0 / genericPerBlock);

    gAllocationCount = 0;
    gCountAllocations = true;
    for (size_t block = 0; block < kParityBlocks; ++block) {
        auto callback = [&genericSource](std::vector<float>& buffer, size_t required) {
            return genericSource.Fetch(buffer, required);
        };
        FetchCallback fetch(callback);
        genericResampler.Process(output, PhaseIncrement(block), fetch);
    }
    gCountAllocations = false;
    const size_t genericAllocations = gAllocationCount;

    gAllocationCount = 0;
    gCountAllocations = true;
    for (size_t block = 0; block < kParityBlocks; ++block) {
        directResampler.ProcessDirect(output, PhaseIncrement(block),
                                      [&directSource](float* destination, size_t count) {
                                          return directSource.Append(destination, count);
                                      });
    }
    gCountAllocations = false;
    const size_t directAllocations = gAllocationCount;

    std::printf("Steady-state allocations: generic callback=%zu, direct PCM=%zu\n",
                genericAllocations, directAllocations);
    if (genericAllocations != 0 || directAllocations != 0) {
        std::fprintf(stderr, "audio hot path allocated after warmup: callback=%zu direct=%zu\n",
                     genericAllocations, directAllocations);
        std::exit(1);
    }
}

} // namespace

int main(int argc, char** argv) {
    const bool lockCanonicalCrc = argc == 1;
    if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--portable-fp") != 0)) {
        std::fprintf(stderr, "usage: %s [--portable-fp]\n", argv[0]);
        return 2;
    }

    CheckQuantizer();
    const uint32_t blepCrc = RunBlepParity();
    if (lockCanonicalCrc && blepCrc != 0x9ABDD7C0u) {
        std::fprintf(stderr, "unexpected BLEP CRC32 %08x\n", blepCrc);
        return 1;
    }
    const uint32_t crc = RunParity();
    if (lockCanonicalCrc && crc != 0xB01520A8u) {
        std::fprintf(stderr, "unexpected PCM CRC32 %08x\n", crc);
        return 1;
    }
    RunBenchmark();
    return 0;
}
