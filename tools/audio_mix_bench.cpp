#include "Constants.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include "Types.hpp"
#include "port_pcm_quantize.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 16364;
constexpr uint16_t kItemGetSong = 0x109;
constexpr size_t kDefaultBlocks = 512;

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

uint32_t ReadU32(const std::vector<uint8_t>& rom, size_t offset) {
    if (offset + 4 > rom.size()) {
        throw std::runtime_error("ROM read outside file");
    }
    return static_cast<uint32_t>(rom[offset]) |
           (static_cast<uint32_t>(rom[offset + 1]) << 8) |
           (static_cast<uint32_t>(rom[offset + 2]) << 16) |
           (static_cast<uint32_t>(rom[offset + 3]) << 24);
}

size_t DetectSongTable(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 0xB0) {
        return 0;
    }
    const std::string code(reinterpret_cast<const char*>(&bytes[0xAC]), 4);
    if (code == "BZME") {
        return 0xA11DBC;
    }
    if (code == "BZMP") {
        return 0xB1D414;
    }
    return 0;
}

MP2KSoundMode MakeSoundMode(void) {
    MP2KSoundMode mode;
    mode.vol = 0x0F;
    mode.rev = 0x80;
    mode.freq = 0x05;
    mode.maxChannels = 0x08;
    mode.dacConfig = 0x09;
    return mode;
}

AgbplaySoundMode MakeAgbplayMode(void) {
    AgbplaySoundMode mode;
    mode.resamplerTypeNormal = ResamplerType::NEAREST;
    mode.resamplerTypeFixed = ResamplerType::NEAREST;
    mode.reverbType = ReverbType::NORMAL;
    mode.reverbForce = 0x80;
    mode.cgbPolyphony = CGBPolyphony::MONO_STRICT;
    mode.dmaBufferLen = 0x630;
    mode.accurateCh3Quantization = true;
    mode.accurateCh3Volume = true;
    mode.emulateCgbSustainBug = true;
    return mode;
}

PlayerTableInfo MakePlayerTable(void) {
    PlayerTableInfo players(32);
    for (size_t i = 0; i < players.size(); ++i) {
        players[i].maxTracks = i >= 30 ? 12 : (i == 1 || i == 2 ? 1 : 2);
        players[i].usePriority = i < 30 ? 1 : 0;
    }
    return players;
}

void Usage(const char* argv0) {
    std::fprintf(stderr, "usage: %s <clean-USA-or-Europe-ROM.gba> [microblocks]\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        Usage(argv[0]);
        return 2;
    }

    const size_t blockCount = argc == 3 ? static_cast<size_t>(std::strtoull(argv[2], nullptr, 0)) : kDefaultBlocks;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "cannot open ROM: %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const size_t songTable = DetectSongTable(bytes);
    if (songTable == 0 || songTable + (static_cast<size_t>(kItemGetSong) + 1) * 8 > bytes.size()) {
        std::fprintf(stderr, "unsupported or truncated ROM\n");
        return 2;
    }

    try {
        Rom rom = Rom::LoadFromBufferRef(std::span<uint8_t>(bytes.data(), bytes.size()));
        SongTableInfo songInfo;
        songInfo.pos = SongTableInfo::POS_AUTO;
        songInfo.count = 0;
        songInfo.tableIdx = 0;
        MP2KContext context(kSampleRate, -1, rom, MakeSoundMode(), MakeAgbplayMode(), songInfo, MakePlayerTable());
        context.m4aSoundMode(0);

        const size_t entry = songTable + static_cast<size_t>(kItemGetSong) * 8;
        const uint32_t songPointer = ReadU32(bytes, entry);
        const uint8_t player = bytes[entry + 4];
        if (!rom.ValidPointer(songPointer) || player >= context.players.size()) {
            std::fprintf(stderr, "invalid item-get song entry\n");
            return 2;
        }
        context.m4aMPlayStart(player, static_cast<size_t>(songPointer - AGB_MAP_ROM));

        const size_t framesPerBlock = context.mixer.GetSamplesPerBuffer();
        std::vector<int16_t> pcm(framesPerBlock * 2);
        uint32_t crc = 0xFFFFFFFFu;
        uint64_t maxBlockNs = 0;
        uint64_t totalBlockNs = 0;
        size_t maxPcmChannels = 0;
        size_t maxPsgChannels = 0;

        for (size_t block = 0; block < blockCount; ++block) {
            const auto start = std::chrono::steady_clock::now();
            context.m4aSoundMain();

            for (size_t frame = 0; frame < framesPerBlock; ++frame) {
                float left = 0.0f;
                float right = 0.0f;
                for (const MP2KPlayer& mp : context.players) {
                    for (const MP2KTrack& track : mp.tracks) {
                        if (!track.audioBufferActive || track.muted || frame >= track.audioBuffer.size()) {
                            continue;
                        }
                        left += track.audioBuffer[frame].left;
                        right += track.audioBuffer[frame].right;
                    }
                }
                pcm[frame * 2] = Port_QuantizePcm16(left);
                pcm[frame * 2 + 1] = Port_QuantizePcm16(right);
            }
            const auto end = std::chrono::steady_clock::now();
            const uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
            totalBlockNs += elapsed;
            maxBlockNs = std::max(maxBlockNs, elapsed);
            maxPcmChannels = std::max(maxPcmChannels, context.sndChannels.size());
            maxPsgChannels = std::max(maxPsgChannels,
                                      context.sq1Channels.size() + context.sq2Channels.size() +
                                          context.waveChannels.size() + context.noiseChannels.size());
            crc = Crc32Update(crc, pcm.data(), pcm.size() * sizeof(pcm[0]));
        }

        const size_t totalFrames = blockCount * framesPerBlock;
        std::printf("Item-get PCM: song=0x%03x player=%u, %zu blocks, %zu frames (%.3f s), CRC32 %08x\n",
                    kItemGetSong, player, blockCount, totalFrames,
                    static_cast<double>(totalFrames) / static_cast<double>(kSampleRate), ~crc);
        std::printf("Mixer duration: %.3f ms total, %.3f us/block average, %.3f us/block maximum; channels PCM/PSG %zu/%zu\n",
                    static_cast<double>(totalBlockNs) / 1000000.0,
                    static_cast<double>(totalBlockNs) / static_cast<double>(blockCount) / 1000.0,
                    static_cast<double>(maxBlockNs) / 1000.0, maxPcmChannels, maxPsgChannels);
        if (blockCount == kDefaultBlocks && ~crc != 0xD3B9DBD0u) {
            std::fprintf(stderr, "item-get PCM regression: expected CRC32 d3b9dbd0\n");
            return 1;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "audio benchmark failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
