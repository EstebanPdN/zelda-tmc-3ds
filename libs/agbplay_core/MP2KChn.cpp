#include "MP2KChn.hpp"

#include "MP2KTrack.hpp"

#include <cassert>

MP2KChn::MP2KChn(MP2KTrack *track, const Note &note, const ADSR &env) : note(note), env(env)
{
    this->track = track;
    this->trackOrg = track;

    prev = nullptr;
    next = track->channels;

    if (track->channels)
        track->channels->prev = this;

    track->channels = this;
}

MP2KChn::~MP2KChn()
{
    RemoveFromTrack();
}

void MP2KChn::RemoveFromTrack() noexcept
{
    if (track == nullptr)
        return;

    if (prev)
        prev->next = next;
    else
        track->channels = next;

    if (next)
        next->prev = prev;

    // Do not clear pointers in order to not break for loops over the channel list.
    // This possibly leads to dangling pointers!
    // Do not use these pointers if track == nullptr.
    // prev = nullptr;
    // next = nullptr;
    track = nullptr;
}

bool MP2KChn::IsReleasing() const noexcept
{
    return stop;
}

void MP2KChn::Kill() noexcept
{
    envState = EnvState::DEAD;
    RemoveFromTrack();
}
