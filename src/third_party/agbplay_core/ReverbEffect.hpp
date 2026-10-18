#pragma once

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// TODO rename to Reverb

class ReverbEffect
{
public:
    ReverbEffect(uint8_t intesity, size_t streamRate, uint8_t numAgbBuffers);
    virtual ~ReverbEffect();
    void Process(std::span<sample> buffer);
    void SetLevel(uint8_t level);
    virtual void Reset();

    static std::unique_ptr<ReverbEffect>
        MakeReverb(ReverbType reverbType, uint8_t intensity, size_t sampleRate, uint8_t numDmaBuffers);

protected:
    virtual size_t ProcessInternal(std::span<sample> buffer);
    float intensity;
    // size_t streamRate;
    std::vector<sample> reverbBuffer;
    size_t bufferPos;
    size_t bufferPos2;
};

class ReverbGS1 : public ReverbEffect
{
public:
    ReverbGS1(uint8_t intensity, size_t streamRate, uint8_t numAgbBuffers);
    ~ReverbGS1() override;
    void Reset() override;

protected:
    size_t ProcessInternal(std::span<sample> buffer) override;
    std::vector<sample> gsBuffer;
};

class ReverbGS2 : public ReverbEffect
{
public:
    ReverbGS2(uint8_t intesity, size_t streamRate, uint8_t numAgbBuffers, float rPrimFac, float rSecFac);
    ~ReverbGS2() override;
    void Reset() override;

protected:
    size_t ProcessInternal(std::span<sample> buffer) override;
    std::vector<sample> gs2Buffer;
    size_t gs2Pos;
    float rPrimFac, rSecFac;
};

class ReverbTest : public ReverbEffect
{
public:
    ReverbTest(uint8_t intesity, size_t streamRate, uint8_t numAgbBuffers);
    ~ReverbTest() override;

protected:
    size_t ProcessInternal(std::span<sample> buffer) override;
};
