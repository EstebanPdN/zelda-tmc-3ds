#pragma once

#include "Constants.hpp"
#include "MP2KTrack.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class Rom;
struct MP2KContext;

struct MP2KPlayer
{
    MP2KPlayer(const MP2KContext &ctx, const PlayerInfo &playerInfo, uint8_t playerIdx);
    MP2KPlayer(const MP2KPlayer &) = delete;
    MP2KPlayer(MP2KPlayer &&) = default;
    MP2KPlayer &operator=(const MP2KPlayer &) = delete;

    void Init(const Rom &rom, size_t songHeaderPos);

    std::vector<MP2KTrack> tracks;

    /* playback state */
    bool playing = false;
    bool finished = true;

    /* player timing state */
    size_t interframeCount = 0;
    size_t frameCount = 0;
    size_t tickCount = 0;
    uint64_t tickProgress_32_32 = 0;
    uint16_t bpm = 0;

    /* variables initialized by song start */
    size_t songHeaderPos = 0;
    size_t bankPos = 0;
    uint8_t tracksUsed = 0;
    uint8_t reverb = 0;
    uint8_t priority = 0;

    /* player constants */
    const uint8_t trackLimit;
    const uint8_t playerIdx;
    const bool usePriority;
};
