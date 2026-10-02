#include "SequenceReader.hpp"

#include "Debug.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include "Util.hpp"
#include "Xcept.hpp"

#include <cassert>
#include <cmath>

#define NOTE_TIE -1
#define NOTE_ALL 0xFE
#define LOOP_ENDLESS -1

/*
 * SequenceReader data
 */

const std::map<uint8_t, uint8_t> SequenceReader::delayLut = {
    { 0x80, 0 },  { 0x81, 1 },  { 0x82, 2 },  { 0x83, 3 },  { 0x84, 4 },  { 0x85, 5 },  { 0x86, 6 },
    { 0x87, 7 },  { 0x88, 8 },  { 0x89, 9 },  { 0x8A, 10 }, { 0x8B, 11 }, { 0x8C, 12 }, { 0x8D, 13 },
    { 0x8E, 14 }, { 0x8F, 15 }, { 0x90, 16 }, { 0x91, 17 }, { 0x92, 18 }, { 0x93, 19 }, { 0x94, 20 },
    { 0x95, 21 }, { 0x96, 22 }, { 0x97, 23 }, { 0x98, 24 }, { 0x99, 28 }, { 0x9A, 30 }, { 0x9B, 32 },
    { 0x9C, 36 }, { 0x9D, 40 }, { 0x9E, 42 }, { 0x9F, 44 }, { 0xA0, 48 }, { 0xA1, 52 }, { 0xA2, 54 },
    { 0xA3, 56 }, { 0xA4, 60 }, { 0xA5, 64 }, { 0xA6, 66 }, { 0xA7, 68 }, { 0xA8, 72 }, { 0xA9, 76 },
    { 0xAA, 78 }, { 0xAB, 80 }, { 0xAC, 84 }, { 0xAD, 88 }, { 0xAE, 90 }, { 0xAF, 92 }, { 0xB0, 96 }
};

namespace {

constexpr uint8_t kMaxDirectSoundChannels = 12;

uint8_t DirectSoundChannelLimit(const MP2KContext& ctx) {
    const uint8_t configuredLimit = ctx.mp2kSoundMode.maxChannels;
    if (configuredLimit == MP2KSoundMode::CHN_AUTO || configuredLimit == 0)
        return kMaxDirectSoundChannels;
    if (configuredLimit > kMaxDirectSoundChannels)
        return kMaxDirectSoundChannels;
    return configuredLimit;
}

/* Stable (player, track) ordering — replaces raw trackOrg pointer compares,
 * which are indeterminate across players (per-player heap vectors) and made
 * PCM channel eviction nondeterministic vs hardware (fixed RAM addresses). */
bool TrackOrderedAfter(const MP2KTrack* a, const MP2KTrack* b) {
    if (a == nullptr || b == nullptr)
        return a != nullptr; /* null (detached) sorts first */
    if (a->playerIdx != b->playerIdx)
        return a->playerIdx > b->playerIdx;
    return a->trackIdx > b->trackIdx;
}

bool IsLowerPriorityPcmVictim(const MP2KChnPCM& candidate, const MP2KChnPCM& current) {
    if (candidate.note.priority != current.note.priority)
        return candidate.note.priority < current.note.priority;
    return TrackOrderedAfter(candidate.trackOrg, current.trackOrg);
}

bool CanStealPcmChannel(const MP2KChnPCM& channel, const Note& note, const MP2KTrack& track) {
    if (channel.IsReleasing())
        return true;
    if (channel.note.priority != note.priority)
        return channel.note.priority < note.priority;
    return channel.trackOrg == &track || TrackOrderedAfter(channel.trackOrg, &track);
}

bool EnsureDirectSoundChannelSlot(MP2KContext& ctx, const Note& note, const MP2KTrack& track) {
    const uint8_t channelLimit = DirectSoundChannelLimit(ctx);
    size_t liveChannels = 0;
    MP2KChnPCM* victim = nullptr;
    bool victimIsReleasing = false;

    for (MP2KChnPCM& channel : ctx.sndChannels) {
        if (channel.envState == EnvState::DEAD)
            continue;

        liveChannels++;
        if (!CanStealPcmChannel(channel, note, track))
            continue;

        const bool channelIsReleasing = channel.IsReleasing();
        if (victim == nullptr || (!victimIsReleasing && channelIsReleasing) ||
            (victimIsReleasing == channelIsReleasing && IsLowerPriorityPcmVictim(channel, *victim))) {
            victim = &channel;
            victimIsReleasing = channelIsReleasing;
        }
    }

    if (liveChannels < channelLimit)
        return true;
    if (victim == nullptr)
        return false;

    victim->Kill();
    return true;
}

} // namespace

const std::map<uint8_t, uint8_t> SequenceReader::noteLut = {
    { 0xCF, 0 },  { 0xD0, 1 },  { 0xD1, 2 },  { 0xD2, 3 },  { 0xD3, 4 },  { 0xD4, 5 },  { 0xD5, 6 },
    { 0xD6, 7 },  { 0xD7, 8 },  { 0xD8, 9 },  { 0xD9, 10 }, { 0xDA, 11 }, { 0xDB, 12 }, { 0xDC, 13 },
    { 0xDD, 14 }, { 0xDE, 15 }, { 0xDF, 16 }, { 0xE0, 17 }, { 0xE1, 18 }, { 0xE2, 19 }, { 0xE3, 20 },
    { 0xE4, 21 }, { 0xE5, 22 }, { 0xE6, 23 }, { 0xE7, 24 }, { 0xE8, 28 }, { 0xE9, 30 }, { 0xEA, 32 },
    { 0xEB, 36 }, { 0xEC, 40 }, { 0xED, 42 }, { 0xEE, 44 }, { 0xEF, 48 }, { 0xF0, 52 }, { 0xF1, 54 },
    { 0xF2, 56 }, { 0xF3, 60 }, { 0xF4, 64 }, { 0xF5, 66 }, { 0xF6, 68 }, { 0xF7, 72 }, { 0xF8, 76 },
    { 0xF9, 78 }, { 0xFA, 80 }, { 0xFB, 84 }, { 0xFC, 88 }, { 0xFD, 90 }, { 0xFE, 92 }, { 0xFF, 96 }
};

/*
 * public SequenceReader
 */

SequenceReader::SequenceReader(MP2KContext& ctx) : ctx(ctx) {
}

void SequenceReader::Process() {
    bool playing = false;
    /* Iterate by INDEX with a bounds check rather than by reference
     * (range-for would bind &player at loop entry). If something inside
     * PlayerMain mutates ctx.players (e.g. through an
     * m4aMPlayStart/Stop reentry via a callback), each lookup
     * re-validates against the current vector state instead of relying
     * on a possibly-invalidated reference. Issue #111. */
    const size_t playerCount = ctx.players.size();
    for (size_t i = 0; i < playerCount; ++i) {
        if (i >= ctx.players.size())
            break; /* the vector shrank mid-iteration — bail out cleanly */
        playing |= PlayerMain(ctx.players[i]);
    }

    if (!playing && !endReached) {
        ctx.mixer.StartFadeOut(SONG_FINISH_TIME);
        endReached = true;
    }
}

bool SequenceReader::EndReached() const {
    return endReached;
}

void SequenceReader::Restart() {
    numLoops = 0;
    endReached = false;
}

void SequenceReader::SetSpeedFactor(float speedFactor) {
    this->speedFactor = speedFactor;
}

float SequenceReader::GetSpeedFactor() const {
    return speedFactor;
}

/*
 * private SequenceReader
 */

bool SequenceReader::PlayerMain(MP2KPlayer& player) {
    if (!player.playing || player.finished)
        return false;

    // TODO implement player based fadeout

    const double UNIT_BEAT_DUR = 24.0 / AGB_APPROX_FPS;
    const double UNIT_BPM = 60.0 / UNIT_BEAT_DUR;
    const double UNIT_FP_32_32 = static_cast<double>(1ull << 32ull);
    const uint64_t UNIT_STEP_32_32 = static_cast<uint64_t>(UNIT_BPM * INTERFRAMES * UNIT_FP_32_32);

    // UNIT_BPM refers to the beats per minute, which the engine runs at when
    // each tick is exactly one frame

    // The GBA runs at slightly less than 60 FPS (59.7275), but MP2K internally assumes an FPS
    // of exact 60 for tempo calculation. This means that on original MP2K the engine will run
    // slightly too slow. We ask the mixer to give us the required correction factor to
    // compensate for that. This will also compensate for buffer sizes, which do not divide
    // evenly by the framerate (e.g. 44100 / (4 * 60) == 183.75).

    player.tickProgress_32_32 +=
        static_cast<uint64_t>(player.bpm * speedFactor * ctx.mixer.GetBufferLengthSpeedCorrection() * UNIT_FP_32_32);

    while (player.tickProgress_32_32 >= UNIT_STEP_32_32) {
        bool playing = false;
        for (MP2KTrack& trk : player.tracks)
            playing |= TrackMain(player, trk);

        player.tickCount++;
        player.tickProgress_32_32 -= UNIT_STEP_32_32;
        if (!playing) {
            player.finished = true;
            player.playing = false;
        }
    }

    for (MP2KTrack& trk : player.tracks)
        TrackVolPitchMain(trk);

    player.interframeCount++;
    player.frameCount = player.interframeCount / INTERFRAMES;

    return player.playing;
}

bool SequenceReader::TrackMain(MP2KPlayer& player, MP2KTrack& trk) {
    const Rom& rom = ctx.rom;

    if (!trk.enabled)
        return false;

    /* Count down note duration and end notes if necessary. */
    TickTrackNotes(trk);

    /* Count down track delay and process events if necessary. */
    while (trk.delay == 0) {
        uint8_t cmd = rom.ReadU8(trk.pos);

        // check if a previous command should be repeated
        if (cmd < 0x80) {
            cmd = trk.lastCmd;
            if (cmd < 0x80) {
                // song data error, command not initialized
                cmdPlayFine(trk);
                return false;
            }
        } else {
            trk.pos++;
            if (cmd >= 0xBD) {
                // repeatable command
                trk.lastCmd = cmd;
            }
        }

        if (cmd >= 0xCF) {
            // note command
            cmdPlayNote(player, trk, cmd);
        } else if (cmd >= 0xB1) {
            // state altering command
            cmdPlayCommand(player, trk, cmd);
            if (!trk.enabled)
                return false;
        } else {
            trk.delay = delayLut.at(cmd);
        }
    }

    trk.delay--;

    if (trk.lfos != 0 && trk.mod != 0) {
        if (trk.lfodlCount == 0) {
            trk.lfoPhase += trk.lfos;
            int lfoPoint;
            if (static_cast<int8_t>(trk.lfoPhase - 64) >= 0)
                lfoPoint = 128 - trk.lfoPhase;
            else
                lfoPoint = static_cast<int8_t>(trk.lfoPhase);
            lfoPoint *= trk.mod;
            lfoPoint >>= 6;

            if (trk.lfoValue != lfoPoint) {
                trk.lfoValue = static_cast<int8_t>(lfoPoint);
                if (trk.modt == MODT::PITCH)
                    trk.updatePitch = true;
                else
                    trk.updateVolume = true;
            }
        } else {
            trk.lfodlCount--;
        }
    }

    return true;
}

void SequenceReader::TrackVolPitchMain(MP2KTrack& trk) {
    if (!trk.enabled)
        return;

    /* The pitch variable is not required to be stored as state variable,
     * but it allows UI tracking. */
    trk.pitch = trk.GetPitch();

    if (!trk.updateVolume && !trk.updatePitch)
        return;

    TrackVolPitchSet(trk, trk.GetVol(), trk.GetPan(), trk.pitch, trk.updateVolume, trk.updatePitch);

    trk.updateVolume = false;
    trk.updatePitch = false;
}

int SequenceReader::TickTrackNotes(MP2KTrack& trk) {
    trk.activeNotes.reset();
    trk.activeVoiceTypes = VoiceFlags::NONE;
    int active = 0;

    for (MP2KChn* chn = trk.channels; chn != nullptr; chn = chn->next) {
        if (chn->TickNote()) {
            active++;
            AddNoteToState(trk, *chn);
        }
    }

    return active;
}

void SequenceReader::AddNoteToState(MP2KTrack& trk, const MP2KChn& chn) {
    trk.activeNotes[chn.note.midiKeyTrackData % NUM_NOTES] = true;
    trk.activeVoiceTypes =
        static_cast<VoiceFlags>(static_cast<int>(trk.activeVoiceTypes) | static_cast<int>(chn.GetVoiceType()));
}

void SequenceReader::TrackVolPitchSet(MP2KTrack& trk, uint16_t vol, int16_t pan, int16_t pitch, bool updateVolume,
                                      bool updatePitch) {
    // TODO: replace this with a normal linked list scan:
    // Why do we have to set pitch after release for SQ1 sweep sounds?
    // because after release the note is not expected to be in linked list
    auto setFunc = [&](auto& channels) {
        for (auto& chn : channels) {
            if (chn.track == &trk) {
                if (updateVolume)
                    chn.SetVol(vol, pan);
                if (updatePitch)
                    chn.SetPitch(pitch);
            }
        }
    };

    setFunc(ctx.sndChannels);
    setFunc(ctx.sq1Channels);
    setFunc(ctx.sq2Channels);
    setFunc(ctx.waveChannels);
    setFunc(ctx.noiseChannels);
}

void SequenceReader::cmdPlayNote(MP2KPlayer& player, MP2KTrack& trk, uint8_t cmd) {
    const Rom& rom = ctx.rom;

    trk.lastNoteLen = noteLut.at(cmd);

    // parse command from track data
    if (rom.ReadU8(trk.pos) < 0x80) {
        trk.lastNoteKey = rom.ReadU8(trk.pos++);

        if (rom.ReadU8(trk.pos) < 0x80) {
            trk.lastNoteVel = rom.ReadU8(trk.pos++);

            if (rom.ReadU8(trk.pos) < 0x80) {
                trk.lastNoteLen += rom.ReadU8(trk.pos++);
            }
        }
    }

    // don't play invalid instruments
    if (trk.prog > 127)
        return;

    // find instrument definition
    uint8_t midiKeyPitch;
    int8_t rhythmPan = 0;
    size_t instrPos = player.bankPos + trk.prog * 12;
    if (const uint8_t bankDataType = rom.ReadU8(instrPos + 0x0); bankDataType & BANKDATA_TYPE_SPLIT) {
        const size_t subBankPos = rom.ReadAgbPtrToPos(instrPos + 0x4);
        const size_t subKeyMap = rom.ReadAgbPtrToPos(instrPos + 0x8);
        instrPos = subBankPos + rom.ReadU8(subKeyMap + trk.lastNoteKey) * 12;
        if (rom.ReadU8(instrPos + 0x0) & (BANKDATA_TYPE_SPLIT | BANKDATA_TYPE_RHYTHM)) {
            Debug::print("cmdPlayNote: attempting to play recursive key split");
            return;
        }
        midiKeyPitch = trk.lastNoteKey;
    } else if (bankDataType == BANKDATA_TYPE_RHYTHM) {
        const size_t subBankPos = rom.ReadAgbPtrToPos(instrPos + 0x4);
        instrPos = subBankPos + trk.lastNoteKey * 12;
        if (rom.ReadU8(instrPos + 0x0) & (BANKDATA_TYPE_SPLIT | BANKDATA_TYPE_RHYTHM)) {
            Debug::print("cmdPlayNote: attempting to play recursive rhythm part");
            return;
        }
        if (const uint8_t instrPan = rom.ReadU8(instrPos + 0x3); instrPan & 0x80)
            rhythmPan = static_cast<int8_t>((instrPan - 0xC0) * 2);
        midiKeyPitch = rom.ReadU8(instrPos + 0x1);
    } else {
        midiKeyPitch = trk.lastNoteKey;
    }

    // init LFO
    trk.lfodlCount = trk.lfodl;
    if (trk.lfodl != 0)
        trk.ResetLfoValue();

    // initialize note data
    Note note;
    note.length = trk.lastNoteLen;
    note.midiKeyTrackData = trk.lastNoteKey;
    note.midiKeyPitch = midiKeyPitch;
    note.velocity = trk.lastNoteVel;
    note.priority = trk.priority;
    note.rhythmPan = rhythmPan;
    note.pseudoEchoVol = trk.pseudoEchoVol;
    note.pseudoEchoLen = trk.pseudoEchoLen;
    note.trackIdx = trk.trackIdx;
    note.playerIdx = player.playerIdx;

    ADSR adsr;
    adsr.att = rom.ReadU8(instrPos + 0x8);
    adsr.dec = rom.ReadU8(instrPos + 0x9);
    adsr.sus = rom.ReadU8(instrPos + 0xA);
    adsr.rel = rom.ReadU8(instrPos + 0xB);

    // TODO move this to external function
    // TODO the track address comparison is not well defined in terms of the relative location
    // of track from multiple players. This should be replaced by a player+track combined priority
    // prepare cgb polyphony suppression
    auto cgbPolyphonySuppressFunc = [&](auto& channels) {
        // return 'true' if a note is allowed to play, 'false' if others with higher priority are playing
        if (ctx.agbplaySoundMode.cgbPolyphony == CGBPolyphony::MONO_STRICT) {
            // only one tone should play in mono strict mode
            assert(channels.size() <= 1);
            if (channels.size() > 0) {
                const Note& playing_note = channels.front().note;

                if (!channels.front().IsReleasing()) {
                    if (playing_note.priority > note.priority)
                        return false;
                    if (playing_note.priority == note.priority) {
                        if (channels.front().track < &trk)
                            return false;
                    }
                }
            }
            channels.clear();
        } else if (ctx.agbplaySoundMode.cgbPolyphony == CGBPolyphony::MONO_SMOOTH) {
            for (auto& chn : channels) {
                if (chn.envState < EnvState::PSEUDO_ECHO && !chn.IsFastReleasing()) {
                    const Note& playing_note = chn.note;
                    if (playing_note.priority > note.priority)
                        return false;
                    if (playing_note.priority == note.priority) {
                        if (chn.track < &trk)
                            return false;
                    }
                }
                chn.Release(true);
            }
        }
        return true;
    };

    const MP2KChn* chn = nullptr;
    const uint8_t instrType = rom.ReadU8(instrPos);

    // enqueue actual note
    if (instrType & BANKDATA_TYPE_CGB) {
        const uint8_t sweep = rom.ReadU8(instrPos + 0x3);
        const uint32_t instrDutyWaveNp = rom.ReadU32(instrPos + 0x4);

        switch (instrType & BANKDATA_TYPE_CGB) {
            case BANKDATA_TYPE_SQ1:
                if (!cgbPolyphonySuppressFunc(ctx.sq1Channels))
                    return;
                ctx.sq1Channels.emplace_back(ctx, &trk, instrDutyWaveNp, adsr, note, sweep);
                chn = &ctx.sq1Channels.back();
                break;
            case BANKDATA_TYPE_SQ2:
                if (!cgbPolyphonySuppressFunc(ctx.sq2Channels))
                    return;
                ctx.sq2Channels.emplace_back(ctx, &trk, instrDutyWaveNp, adsr, note, 0);
                chn = &ctx.sq2Channels.back();
                break;
            case BANKDATA_TYPE_WAVE:
                if (!cgbPolyphonySuppressFunc(ctx.waveChannels))
                    return;
                ctx.waveChannels.emplace_back(ctx, &trk, instrDutyWaveNp, adsr, note,
                                              ctx.agbplaySoundMode.accurateCh3Volume);
                chn = &ctx.waveChannels.back();
                break;
            case BANKDATA_TYPE_NOISE:
                if (!cgbPolyphonySuppressFunc(ctx.noiseChannels))
                    return;
                ctx.noiseChannels.emplace_back(ctx, &trk, instrDutyWaveNp, adsr, note);
                chn = &ctx.noiseChannels.back();
                break;
            default:
                Debug::print("CGB Error: Invalid CGB Type: [{:08X}]={:02X}, instrument: [{:08X}]", instrPos,
                             rom.ReadU8(instrPos), instrPos);
                return;
        }
    } else {
        const size_t samplePos = rom.ReadAgbPtrToPos(instrPos + 0x4);
        SampleInfo sinfo;

        if (rom.ReadU8(samplePos + 0x0) == 0) {
            sinfo.gamefreakCompressed = false;
        } else if (rom.ReadU8(samplePos + 0x0) == 1) {
            sinfo.gamefreakCompressed = true;
        } else {
            Debug::print("Sample Error: Unknown/unsupported sample mode: [{:08X}]={:02X}, instrument: [{:08X}]",
                         samplePos, rom.ReadU8(samplePos), instrPos);
            return;
        }

        sinfo.loopEnabled = rom.ReadU8(samplePos + 0x3) & 0xC0;
        sinfo.midCfreq = static_cast<float>(rom.ReadU32(samplePos + 4)) / 1024.0f;
        sinfo.loopPos = rom.ReadU32(samplePos + 8);
        sinfo.endPos = rom.ReadU32(samplePos + 12);

        /* Fix malformed loops found in some romhacks */
        if (sinfo.loopPos > sinfo.endPos) {
            Debug::print("Sample Warning: Loop start is after loop end, instrument: [{:#08X}]", samplePos);
            sinfo.loopPos = 0;
        }
        if (sinfo.loopPos == sinfo.endPos) {
            sinfo.loopEnabled = false;
        }

        if (!rom.ValidRange(samplePos, 16)) {
            Debug::print("Sample Error: Sample header reaches beyond end of file: instrument: [{:08X}]", instrPos);
            return;
        }

        sinfo.samplePos = samplePos;
        sinfo.samplePtr = static_cast<const int8_t*>(rom.GetPtr(samplePos + 16));

        if (!EnsureDirectSoundChannelSlot(ctx, note, trk))
            return;

        ctx.sndChannels.emplace_back(ctx, &trk, sinfo, adsr, note, instrType & BANKDATA_TYPE_FIX);
        chn = &ctx.sndChannels.back();
    }

    /* New notes should be added to the visualizer state immediately. Otherwise they won't be
     * present during the first tick. */
    assert(chn != nullptr);
    AddNoteToState(trk, *chn);

    // new notes need correct pitch and volume applied
    trk.updateVolume = true;
    trk.updatePitch = true;
}

void SequenceReader::cmdPlayCommand(MP2KPlayer& player, MP2KTrack& trk, uint8_t cmd) {
    const Rom& rom = ctx.rom;

    switch (cmd) {
        case 0xB1:
            // FINE
            cmdPlayFine(trk);
            break;
        case 0xB2:
            // GOTO
            if (trk.trackIdx == 0) {
                // handle agbplay's internal loop counter
                if (ctx.maxLoops != LOOP_ENDLESS && numLoops++ >= static_cast<size_t>(ctx.maxLoops) && !endReached) {
                    endReached = true;
                    ctx.mixer.StartFadeOut(SONG_FADE_OUT_TIME);
                }
            }
            trk.pos = rom.ReadAgbPtrToPos(trk.pos);
            break;
        case 0xB3:
            // PATT
            if (trk.patternLevel >= TRACK_CALL_STACK_SIZE) {
                cmdPlayFine(trk);
                break;
            }
            trk.returnPos[trk.patternLevel++] = trk.pos + 4;
            trk.pos = rom.ReadAgbPtrToPos(trk.pos);
            break;
        case 0xB4:
            // PEND
            if (trk.patternLevel == 0)
                break;

            trk.pos = trk.returnPos[--trk.patternLevel];
            break;
        case 0xB5:
            // REPT
            {
                uint8_t count = rom.ReadU8(trk.pos++);
                if (count == 0) {
                    cmdPlayFine(trk);
                    break;
                }
                if (++trk.reptCount < count) {
                    trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                } else {
                    trk.reptCount = 0;
                    trk.pos += 4;
                }
            }
            break;
        case 0xB9:
            // MEMACC
            cmdPlayMemacc(trk);
            break;
        case 0xBA:
            // PRIO
            trk.priority = rom.ReadU8(trk.pos++);
            break;
        case 0xBB:
            // TEMPO
            player.bpm = static_cast<uint16_t>(rom.ReadU8(trk.pos++) * 2);
            break;
        case 0xBC:
            // KEYSH
            trk.keyShift = rom.ReadS8(trk.pos++);
            break;
        case 0xBD:
            // VOICE
            trk.prog = rom.ReadU8(trk.pos++);
            break;
        case 0xBE:
            // VOL
            trk.vol = rom.ReadU8(trk.pos++);
            trk.updateVolume = true;
            break;
        case 0xBF:
            // PAN
            trk.pan = static_cast<int8_t>(rom.ReadS8(trk.pos++) - 0x40);
            trk.updateVolume = true;
            break;
        case 0xC0:
            // BEND
            trk.bend = static_cast<int8_t>(rom.ReadS8(trk.pos++) - 0x40);
            trk.updatePitch = true;
            break;
        case 0xC1:
            // BENDR
            trk.bendr = rom.ReadU8(trk.pos++);
            trk.updatePitch = true;
            break;
        case 0xC2:
            // LFOS
            trk.lfos = rom.ReadU8(trk.pos++);
            if (trk.lfos == 0)
                trk.ResetLfoValue();
            break;
        case 0xC3:
            // LFODL
            trk.lfodlCount = trk.lfodl = rom.ReadU8(trk.pos++);
            break;
        case 0xC4:
            // MOD
            trk.mod = rom.ReadU8(trk.pos++);
            if (trk.mod == 0)
                trk.ResetLfoValue();
            break;
        case 0xC5:
            // MODT
            {
                uint8_t modt = rom.ReadU8(trk.pos++);
                if (static_cast<MODT>(modt) == trk.modt)
                    return;
                trk.modt = static_cast<MODT>(modt);
                trk.updateVolume = true;
                trk.updatePitch = true;
            }
            break;
        case 0xC8:
            // TUNE
            trk.tune = static_cast<int8_t>(rom.ReadS8(trk.pos++) - 0x40);
            trk.updatePitch = true;
            break;
        case 0xCD:
            // xCMD
            cmdPlayXCmd(trk);
            break;
        case 0xCE:
            // EOT
            {
                uint8_t key = rom.ReadU8(trk.pos);
                if (key >= 0x80) {
                    key = trk.lastNoteKey;
                } else {
                    trk.pos++;
                    trk.lastNoteKey = key;
                }
                for (MP2KChn* chn = trk.channels; chn != nullptr; chn = chn->next) {
                    assert(chn->trackOrg == &trk);
                    if (chn->envState == EnvState::DEAD)
                        continue;
                    if (chn->IsReleasing())
                        continue;
                    if (chn->note.midiKeyTrackData == key) {
                        chn->Release();
                        break;
                    }
                }
            }
            break;
        default:
            cmdPlayFine(trk);
            break;
    }
}

void SequenceReader::cmdPlayFine(MP2KTrack& trk) {
    for (MP2KChn* chn = trk.channels; chn != nullptr; chn = chn->next) {
        chn->Release();
        chn->RemoveFromTrack();
    }

    trk.enabled = false;
    trk.activeNotes.reset();
    trk.activeVoiceTypes = VoiceFlags::NONE;
}

void SequenceReader::cmdPlayMemacc(MP2KTrack& trk) {
    const Rom& rom = ctx.rom;

    uint8_t op = rom.ReadU8(trk.pos++);
    uint8_t& memory = ctx.memaccArea[rom.ReadU8(trk.pos++)];
    uint8_t data = rom.ReadU8(trk.pos++);

    switch (op) {
        case 0:
            memory = data;
            return;
        case 1:
            memory += data;
            return;
        case 2:
            memory -= data;
            return;
        case 3:
            memory = ctx.memaccArea[data];
            return;
        case 4:
            memory += ctx.memaccArea[data];
            return;
        case 5:
            memory -= ctx.memaccArea[data];
            return;
        case 6:
            if (memory == data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 7:
            if (memory != data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 8:
            if (memory > data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 9:
            if (memory >= data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 10:
            if (memory <= data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 11:
            if (memory < data) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 12:
            if (memory == ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 13:
            if (memory != ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 14:
            if (memory > ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 15:
            if (memory >= ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 16:
            if (memory <= ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        case 17:
            if (memory < ctx.memaccArea[data]) {
                trk.pos = rom.ReadAgbPtrToPos(trk.pos);
                return;
            }
            break;
        default:
            return;
    }

    // only "false" jump commands should come by here
    trk.pos += 4;
}

void SequenceReader::cmdPlayXCmd(MP2KTrack& trk) {
    const Rom& rom = ctx.rom;

    uint8_t xCmdNo = rom.ReadU8(trk.pos++);

    switch (xCmdNo) {
        case 0:
            // XXX
            cmdPlayFine(trk);
            break;
        case 1:
            // XWAVE (stub)
            trk.pos += 4;
            break;
        case 2:
            // XTYPE (stub)
            trk.pos++;
            break;
        case 3:
            // XXX
            cmdPlayFine(trk);
            break;
        case 4:
            // XATTA (stub)
            trk.pos++;
            break;
        case 5:
            // XDECA (stub)
            trk.pos++;
            break;
        case 6:
            // XSUST (stub)
            trk.pos++;
            break;
        case 7:
            // XRELA (stub)
            trk.pos++;
            break;
        case 8:
            // XIECV
            trk.pseudoEchoVol = rom.ReadU8(trk.pos++);
            break;
        case 9:
            // XIECL
            trk.pseudoEchoLen = rom.ReadU8(trk.pos++);
            break;
        case 10:
            // XLENG (stub)
            trk.pos++;
            break;
        case 11:
            // XSWEE (stub)
            trk.pos++;
            break;
        case 12:
            // XWAIT
            trk.delay = rom.ReadU16(trk.pos);
            trk.pos += 2;
            break;
        case 13:
            // XSOFF (stub)
            trk.pos += 4;
            break;
        default:
            cmdPlayFine(trk);
            break;
    }
}
