#include "MP2KTrack.hpp"

#include "MP2KChn.hpp"
#include "MP2KContext.hpp"
#include "ReverbEffect.hpp"

#include <cassert>

MP2KTrack::MP2KTrack(const MP2KContext& ctx, uint8_t playerIdx, uint8_t trackIdx)
    : loudnessCalculator(LOUDNESS_LP_FREQ, ctx.sampleRate), playerIdx(playerIdx), trackIdx(trackIdx) {
#ifdef TMC_3DS
    /* Allocate each fixed-size microblock once while the audio context is
     * built, never on the first note of a fanfare or combat sound. */
    audioBuffer.resize(ctx.mixer.GetSamplesPerBuffer());
#endif
    Init(0);
}

void MP2KTrack::Init(size_t pos) {
    this->pos = pos;
    patternLevel = 0;
    modt = MODT::PITCH;
    lastCmd = 0;
    pitch = 0;
    lastNoteKey = 0;
    lastNoteVel = 0;
    lastNoteLen = 0;
    reptCount = 0;
    prog = PROG_UNDEFINED; // TODO replace this with an instrument definition like in original MP2K
    vol = 0;
    mod = 0;
    bendr = 2;
    priority = 0;
    lfos = 22;
    lfodl = 0;
    lfodlCount = 0;
    lfoPhase = 0;
    lfoValue = 0;
    pseudoEchoVol = 0;
    pseudoEchoLen = 0;
    delay = 0;
    pan = 0;
    bend = 0;
    tune = 0;
    keyShift = 0;
    muted = false;
    enabled = pos != 0;
    updateVolume = false;
    updatePitch = false;
#ifdef TMC_3DS
    audioBufferActive = false;
    audioTailBlocks = 0;
#endif
    channels = nullptr;
    activeNotes.reset();
    activeVoiceTypes = VoiceFlags::NONE;
}

void MP2KTrack::Stop() {
    if (!enabled)
        return;

    for (MP2KChn* chn = channels; chn != nullptr; chn = chn->next)
        chn->Kill();

    assert(channels == nullptr);
}

int16_t MP2KTrack::GetPitch() {
    int p = tune + bend * bendr + keyShift * 64;
    if (modt == MODT::PITCH)
        p += lfoValue * 4;
    return static_cast<int16_t>(p);
}

uint16_t MP2KTrack::GetVol() {
    int32_t v = vol << 1;
    if (modt == MODT::VOL)
        v = (v * (lfoValue + 128)) >> 7;
    return static_cast<uint16_t>(v);
}

int16_t MP2KTrack::GetPan() {
    int p = pan << 1;
    if (modt == MODT::PAN)
        p += lfoValue;
    return static_cast<int16_t>(p);
}

void MP2KTrack::ResetLfoValue() {
    lfoValue = 0;
    lfoPhase = 0;

    if (modt == MODT::PITCH)
        updatePitch = true;
    else
        updateVolume = true;
}
