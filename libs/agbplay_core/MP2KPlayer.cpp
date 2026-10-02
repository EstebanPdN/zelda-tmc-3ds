#include "MP2KPlayer.hpp"

#include "MP2KTrack.hpp"
#include "ReverbEffect.hpp"
#include "Rom.hpp" // TODO remove once Rom is deglobalized

MP2KPlayer::MP2KPlayer(const MP2KContext& ctx, const PlayerInfo& playerInfo, uint8_t playerIdx)
    : trackLimit(playerInfo.maxTracks), playerIdx(playerIdx), usePriority(playerInfo.usePriority) {
    for (uint8_t i = 0; i < trackLimit; i++)
        tracks.emplace_back(ctx, playerIdx, i);
}

void MP2KPlayer::Init(const Rom& rom, size_t songHeaderPos) {
    this->songHeaderPos = songHeaderPos;

    if (songHeaderPos != 0) {
        // read song header
        tracksUsed = std::min<uint8_t>(rom.ReadU8(songHeaderPos + 0), trackLimit);
        priority = rom.ReadU8(songHeaderPos + 2);
        reverb = rom.ReadU8(songHeaderPos + 3);
        if (rom.ValidPointer(rom.ReadU32(songHeaderPos + 4)))
            bankPos = rom.ReadAgbPtrToPos(songHeaderPos + 4);
        else
            bankPos = 0;

        // read track pointers
        for (size_t i = 0; i < tracksUsed; i++)
            tracks.at(i).Init(rom.ReadAgbPtrToPos(songHeaderPos + 8 + 4 * i));

        playing = true;
        finished = tracksUsed == 0;
    } else {
        tracksUsed = 0;
        playing = false;
        finished = true;
    }

    // reset rest of the tracks
    for (size_t i = tracksUsed; i < tracks.size(); i++)
        tracks.at(i).Init(0);

    // reset runtime variables
    tickProgress_32_32 = 0;
    bpm = 150;
    tickCount = 0;
    frameCount = 0;
    interframeCount = 0;
}
