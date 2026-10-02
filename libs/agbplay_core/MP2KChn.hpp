#pragma once

#include "Resampler.hpp"
#include "Types.hpp"

#include <memory>

struct MP2KTrack;

struct MP2KChn
{
    MP2KChn(MP2KTrack *track, const Note &note, const ADSR &env);
    MP2KChn(const MP2KChn &) = delete;
    MP2KChn &operator=(const MP2KChn &) = delete;
    virtual ~MP2KChn();

    void RemoveFromTrack() noexcept;

    bool IsReleasing() const noexcept;
    void Kill() noexcept;
    virtual void Release() noexcept = 0;
    // TODO: Does TickNote really have to deviate between channel types?
    virtual bool TickNote() noexcept = 0;
    virtual VoiceFlags GetVoiceType() const noexcept = 0;

    /* linked list of channels inside a track. */
    MP2KChn *prev = nullptr;
    MP2KChn *next = nullptr;
    MP2KTrack *track = nullptr;

    /* this track pointer is identical, but is not NULLed once the channel is released */
    MP2KTrack *trackOrg = nullptr;

    std::unique_ptr<Resampler> rs;

    Note note;
    ADSR env;
    EnvState envState = EnvState::INIT;

    uint32_t pos = 0;
    float interPos = 0.0f;
    float freq = 0.0f;

    bool stop = false;
};
