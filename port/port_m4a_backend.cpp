#include "port_m4a_backend.h"
#include "port_config.h"
#include "port_sounds_embed.hpp"
#include "sound.h"
#include "port_debug_verbose.h"
#include "port_pcm_quantize.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef Stop
#undef Stop
#endif

#ifndef TMC_3DS
#include <filesystem>
#include <optional>
#include "port_exe_path.hpp"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

extern "C" {
typedef struct SongHeader SongHeader;
typedef struct MusicPlayerInfo MusicPlayerInfo;
typedef struct MusicPlayerTrack MusicPlayerTrack;
typedef struct MusicPlayer {
    MusicPlayerInfo* info;
    MusicPlayerTrack* tracks;
    uint8_t nTracks;
    uint16_t unk_A;
} MusicPlayer;

extern const MusicPlayer gMusicPlayers[];
}

#ifdef PACKED
#undef PACKED
#endif

#include "AgbTypes.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include "Types.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#ifdef TMC_ANDROID_PORT
extern "C" {
extern const unsigned char kEmbeddedSoundsJson[];
extern const std::size_t kEmbeddedSoundsJsonSize;
}
#endif

namespace {

constexpr uint32_t kPlayerCount = 32;
constexpr uint32_t kMaxTracks = 16;
constexpr uint32_t kSongCount = SFX_221 + 1;
constexpr size_t kCommandReserve = 64;

struct BackendState {
    bool initialized = false;
    bool vsyncEnabled = true;
    uint32_t sampleRate = 48000;
    uint32_t soundMode = 0;
    bool songMapLoaded = false;
    std::unique_ptr<Rom> rom;
    std::unique_ptr<MP2KContext> ctx;
    std::vector<int16_t> pendingSamples;
    std::vector<float> mixAccL;
    std::vector<float> mixAccR;
    size_t pendingFrameOffset = 0;
    std::array<size_t, kSongCount> songHeaderOffsets;
    uint8_t trackVolumes[kPlayerCount][kMaxTracks];
    int8_t trackPans[kPlayerCount][kMaxTracks];
    /* Track the currently-playing song per player so room transitions that
     * re-issue the same songId can be no-ops (real GBA's MPlayStart was
     * effectively idempotent for repeated calls; agbplay's m4aMPlayStart
     * restarts unconditionally and that's audible as music resetting). */
    uint16_t currentSongId[kPlayerCount];
    /* GBA-accurate audio toggle (F8 → Audio). false = enhanced (SINC); true =
     * NEAREST sample-and-hold + no forced reverb. Read by MakeAgbplayMode when
     * the context is (re)built, and mutated live by SetGbaAccurate. */
    bool gbaAccurate = false;
    /* Forced PCM-reverb level byte (F8 → Audio "Reverb"). 0x00 = OFF (dry —
     * ships unchanged). When enabled it is (0x80 | level), level 1..0x7F; the
     * 0x80 REV_MASK_SET bit is what actually engages SoundMixer::GetReverbLevel
     * (plain 32 lacked it, which is why the original forced reverb was inert).
     * NORMAL reverb is a PCM-only comb (CGB/PSG voices stay dry by mix order). */
    uint8_t reverbForceByte = 0x00;
    /* Game master volume [0,1] applied to the final mixed output (F8 -> Audio
     * "Master volume"). 1.0 = unchanged. */
    float masterVolume = 1.0f;
};

BackendState sState;
std::mutex sStateMutex;

/* --- Main-thread → audio-thread command queue (item-get freeze fix) ---------
 *
 * The game engine calls the m4a mutators (start/stop/continue song, set track
 * volume/pan) from the MAIN thread, several per frame. They used to take
 * sStateMutex directly — the SAME lock the audio thread holds while it renders.
 * On a contended low-end SoC (Moto G4: 4 fast cores, 3 doing the software PPU),
 * an item-get fires a storm of these calls in one frame (duck BGM, stop, start
 * fanfare, per-track volume) while the audio thread is mid-render; each call
 * then blocks on the lock, and std::mutex is unfair, so the main thread lost the
 * race repeatedly and the game tick froze for up to ~500 ms (issue: "fps drops
 * when we get an item").
 *
 * Fix: the main thread NEVER touches sStateMutex for these hot mutators. It
 * pushes a command onto sCmdQueue under sCmdMutex (held for microseconds — no
 * rendering ever happens under it) and returns immediately. The audio thread
 * drains the queue and applies the commands under sStateMutex at the top of its
 * render, in FIFO order, so playback semantics are unchanged (just applied at
 * the next audio callback — a few ms later, inaudible). */
enum class M4ACmdType { StartSong, Stop, Continue, SetVolume, SetPan };
struct M4ACommand {
    M4ACmdType type;
    uint8_t playerIndex;
    uint16_t songId;    /* StartSong */
    uint16_t trackBits; /* SetVolume / SetPan */
    uint16_t volume;    /* SetVolume */
    int8_t pan;         /* SetPan */
};
std::vector<M4ACommand> sCmdQueue;
std::vector<M4ACommand> sCmdDrainQueue;
std::mutex sCmdMutex;

static void PushCommand(const M4ACommand& c) {
    std::lock_guard<std::mutex> lock(sCmdMutex);
    sCmdQueue.push_back(c);
}

/* Lock-free per-player "audibly active" bitmask (bit N = player N live). The
 * audio thread publishes it each chunk under sStateMutex; the main thread reads
 * it WITHOUT any lock in Port_M4A_Backend_IsPlayerActive. This existed because
 * the BGM duck (#22) polls IsPlayerActive EVERY frame while the item-get jingle
 * plays — a per-frame sStateMutex acquisition that, like the mutators above,
 * blocked the game tick on the contended audio lock (the residual half of the
 * item-get freeze). kPlayerCount==32 fits a u32 exactly. */
std::atomic<uint32_t> sActivePlayerMask{ 0 };

static MP2KSoundMode MakeSoundMode(void) {
    MP2KSoundMode mode;
    mode.vol = 0x0f;
    mode.rev = 0x80;
    mode.freq = 0x05;
    mode.maxChannels = 0x08;
    mode.dacConfig = 0x09;
    return mode;
}

static AgbplaySoundMode MakeAgbplayMode(void) {
    AgbplaySoundMode mode;
    /* Use SINC resampling for both pitch-bent and fixed-rate PCM samples.
     * The GBA's hardware uses a no-interpolation nearest-neighbour fetch,
     * giving its characteristic aliased "crunch" — agbplay's LINEAR (the
     * previous default) softens that, but SINC is the bandlimited
     * resampler proper and removes virtually all imaging artefacts at
     * the cost of a few hundred extra MAC ops per audio frame. CPU
     * overhead is negligible on PC. */
    /* GBA-accurate mode (F8 → Audio toggle) uses NEAREST — the hardware's
     * no-interpolation sample-and-hold fetch and its characteristic aliased
     * "crunch". The default enhanced path uses SINC, the bandlimited resampler
     * proper, which removes virtually all imaging artefacts for a few hundred
     * extra MAC ops per audio frame (negligible on PC). */
    /* Resampler cost on weak in-order ARM: SINC is a few-hundred-MAC bandlimited
     * filter per audio frame — negligible on desktop, but MEASURED at ~17-20 ms
     * of synth work per 20 ms buffer on a Moto G4 (Cortex-A53) once several PCM
     * channels are live (e.g. the item-get fanfare), i.e. ~85-100% of realtime,
     * so any scheduling jitter underran the audio (audible crackle/lag). LINEAR
     * is a 2-tap interpolation — a fraction of SINC's cost — and still avoids
     * NEAREST's raw aliasing, so the enhanced path stays smooth on Android while
     * fitting the CPU budget. Desktop keeps SINC (the cleanest resampler; cost is
     * irrelevant there). GBA-accurate mode is NEAREST everywhere (hardware exact). */
#if defined(TMC_3DS)
    /* The ARM11 cannot sustain the desktop 32-tap SINC path. NEAREST matches
     * the GBA's sample-and-hold hardware and leaves CPU time for game/PPU work. */
    const ResamplerType enhancedRs = ResamplerType::NEAREST;
#elif defined(__ANDROID__)
    const ResamplerType enhancedRs = ResamplerType::LINEAR;
#else
    const ResamplerType enhancedRs = ResamplerType::SINC;
#endif
    const ResamplerType rs = sState.gbaAccurate ? ResamplerType::NEAREST : enhancedRs;
    mode.resamplerTypeNormal = rs;
    mode.resamplerTypeFixed = rs;
    mode.reverbType = ReverbType::NORMAL;
    /* Forced PCM reverb. reverbForce must carry REV_MASK_SET (0x80) to engage
     * (sState.reverbForceByte is 0x00 by default = OFF/dry, or 0x80|level when
     * the user opts in via the F8 slider — see Port_M4A_Backend_SetReverbLevel).
     * GBA-accurate mode forces it fully dry. Honoured here so a context rebuild
     * (ROM reload / area change) preserves the user's chosen level. */
#if defined(TMC_3DS)
    /* Force a dry mix on 3DS. Processing a software reverb tail for every
     * active MP2K track consumes the remaining real-time audio budget. */
    mode.reverbForce = 0x80;
#else
    mode.reverbForce = sState.gbaAccurate ? 0 : sState.reverbForceByte;
#endif
    mode.cgbPolyphony = CGBPolyphony::MONO_STRICT;
    mode.dmaBufferLen = 0x630;
    mode.accurateCh3Quantization = true;
    mode.accurateCh3Volume = true;
    mode.emulateCgbSustainBug = true;
    return mode;
}

static const char* GetCurrentVariantName(void) {
    switch (gRomRegion) {
        case ROM_REGION_EU:
            return "EU";
        case ROM_REGION_JP:
            return "JP";
        case ROM_REGION_USA:
        default:
            return "USA";
    }
}

#ifndef TMC_3DS
static std::string LoadTextFile(const char* path) {
    std::ifstream stream(path, std::ios::binary);

    if (!stream.good()) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

static std::optional<std::filesystem::path> ExeDirForSounds() {
    return port::ExecutableDir();
}
#endif

static std::string LoadSoundsJson(void) {
#ifdef TMC_3DS
    return std::string(reinterpret_cast<const char*>(PortSoundsEmbed::kData), PortSoundsEmbed::kSize);
#else
    /* Search beside the binary first (release-tarball layout), then walk a
     * couple of cwd-relative dev locations. Mirrors the asset loader's
     * lookup pattern so the release zip's sounds.json is found regardless
     * of the user's current directory. */
    std::vector<std::string> paths;
    if (auto dir = ExeDirForSounds(); dir.has_value()) {
        paths.push_back((*dir / "sounds.json").string());
        paths.push_back((*dir / "assets" / "sounds.json").string());
    }
    paths.push_back("assets/sounds.json");
    paths.push_back("../assets/sounds.json");
    paths.push_back("../../assets/sounds.json");

    for (const std::string& path : paths) {
        std::string text = LoadTextFile(path.c_str());
        if (!text.empty()) {
            std::fprintf(stderr, "[AUDIO] loaded %s\n", path.c_str());
            return text;
        }
    }

#ifdef TMC_ANDROID_PORT
    if (kEmbeddedSoundsJsonSize > 0) {
        return std::string(reinterpret_cast<const char*>(kEmbeddedSoundsJson), kEmbeddedSoundsJsonSize);
    }
#else
    /* Compile-time fallback: every tmc_pc build embeds a copy of
     * assets/sounds.json so a bare `tmc_pc + baserom.gba` install
     * still has audio. kSize is 0 only when assets/sounds.json was
     * missing at build time (in which case we fall through to the
     * silent-songs warning below). */
    if (PortSoundsEmbed::kSize > 0) {
        std::fprintf(stderr, "[AUDIO] using embedded sounds.json (%zu bytes)\n", PortSoundsEmbed::kSize);
        return std::string(reinterpret_cast<const char*>(PortSoundsEmbed::kData), PortSoundsEmbed::kSize);
    }
#endif

    std::fprintf(stderr, "[AUDIO] sounds.json not found — songs will be silent\n");
    return {};
#endif
}

static bool ParseIntAfterKey(const std::string& text, size_t keyPos, long long& outValue) {
    size_t pos = text.find(':', keyPos);
    size_t end = 0;

    if (pos == std::string::npos) {
        return false;
    }

    pos++;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        pos++;
    }

    outValue = std::strtoll(text.c_str() + pos, nullptr, 10);
    end = pos;
    if (end >= text.size() || (!std::isdigit(static_cast<unsigned char>(text[end])) && text[end] != '-')) {
        return false;
    }

    return true;
}

static size_t FindObjectEnd(const std::string& text, size_t objectStart) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = objectStart; i < text.size(); i++) {
        char c = text[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '{') {
            depth++;
            continue;
        }
        if (c == '}') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::string::npos;
}

static bool ObjectMatchesVariant(const std::string& objectText, const char* variantName) {
    size_t variantsPos = objectText.find("\"variants\"");

    if (variantsPos == std::string::npos) {
        return true;
    }

    return objectText.find(std::string("\"") + variantName + "\"", variantsPos) != std::string::npos;
}

static void LoadSongMapLocked(void) {
    std::string jsonText;
    const char* variantName = GetCurrentVariantName();
    long long variantOffset = 0;
    size_t searchPos = 0;

    sState.songHeaderOffsets.fill(0);
    sState.songMapLoaded = true;

    jsonText = LoadSoundsJson();
    if (jsonText.empty()) {
        return;
    }

    {
        size_t offsetsPos = jsonText.find("\"offsets\"");
        if (offsetsPos != std::string::npos) {
            size_t variantPos = jsonText.find(std::string("\"") + variantName + "\"", offsetsPos);
            if (variantPos != std::string::npos) {
                ParseIntAfterKey(jsonText, variantPos, variantOffset);
            }
        }
    }

    while (true) {
        size_t pathPos = jsonText.find("\"path\": \"sounds/", searchPos);
        size_t labelStart;
        size_t labelEnd;
        size_t objectStart;
        size_t objectEnd;
        std::string objectText;
        std::string label;
        long long startOffset = -1;
        long long headerOffset = -1;

        if (pathPos == std::string::npos) {
            break;
        }

        labelStart = pathPos + strlen("\"path\": \"sounds/");
        labelEnd = jsonText.find(".bin\"", labelStart);
        objectStart = jsonText.rfind('{', pathPos);
        if (labelEnd == std::string::npos || objectStart == std::string::npos) {
            break;
        }

        objectEnd = FindObjectEnd(jsonText, objectStart);
        if (objectEnd == std::string::npos) {
            break;
        }

        objectText = jsonText.substr(objectStart, objectEnd - objectStart + 1);
        label = jsonText.substr(labelStart, labelEnd - labelStart);
        searchPos = objectEnd + 1;

        if (!ObjectMatchesVariant(objectText, variantName)) {
            continue;
        }

        {
            size_t startsPos = objectText.find("\"starts\"");
            if (startsPos != std::string::npos) {
                size_t variantPos = objectText.find(std::string("\"") + variantName + "\"", startsPos);
                if (variantPos != std::string::npos) {
                    ParseIntAfterKey(objectText, variantPos, startOffset);
                }
            } else {
                size_t startPos = objectText.find("\"start\"");
                if (startPos != std::string::npos) {
                    if (ParseIntAfterKey(objectText, startPos, startOffset)) {
                        startOffset += variantOffset;
                    }
                }
            }
        }

        {
            size_t headerPos = objectText.find("\"headerOffset\"");
            if (headerPos != std::string::npos) {
                ParseIntAfterKey(objectText, headerPos, headerOffset);
            }
        }

        if (startOffset < 0 || headerOffset < 0) {
            continue;
        }

        for (uint32_t i = 0; i < kSongCount; i++) {
            const char* songLabel = Port_GetSongLabel((uint16_t)i);

            if (songLabel != nullptr && label == songLabel) {
                sState.songHeaderOffsets[i] = static_cast<size_t>(startOffset + headerOffset);
                break;
            }
        }
    }
}

static size_t SongIdToRomPosLocked(uint16_t songId) {
    if (gRomOffsets != nullptr && gRomOffsets->songTable != 0 && songId < kSongCount) {
        const size_t entryPos = static_cast<size_t>(gRomOffsets->songTable) + static_cast<size_t>(songId) * 8u;

        if (entryPos + sizeof(u32) <= gRomSize) {
            const u8* raw = gRomData + entryPos;
            const u32 gbaAddress = static_cast<u32>(raw[0]) | (static_cast<u32>(raw[1]) << 8) |
                                   (static_cast<u32>(raw[2]) << 16) | (static_cast<u32>(raw[3]) << 24);
            if (gbaAddress >= 0x08000000u && gbaAddress < 0x08000000u + gRomSize) {
                return static_cast<size_t>(gbaAddress - 0x08000000u);
            }
        }
    }

    if (!sState.songMapLoaded) {
        LoadSongMapLocked();
    }

    if (songId >= kSongCount) {
        return 0;
    }

    return sState.songHeaderOffsets[songId];
}

static void ResetTrackMixControlsLocked(void) {
    for (uint32_t i = 0; i < kPlayerCount; i++) {
        sState.currentSongId[i] = 0;
        for (uint32_t j = 0; j < kMaxTracks; j++) {
            sState.trackVolumes[i][j] = 0xff;
            sState.trackPans[i][j] = 0;
        }
    }
}

static PlayerTableInfo BuildPlayerTable(void) {
    PlayerTableInfo playerTable;
    playerTable.reserve(kPlayerCount);

    for (uint32_t i = 0; i < kPlayerCount; i++) {
        PlayerInfo info;
        info.maxTracks = gMusicPlayers[i].nTracks;
        info.usePriority = gMusicPlayers[i].unk_A != 0;
        playerTable.push_back(info);
    }

    return playerTable;
}

/* Throttled log when an agbplay exception is contained at a C boundary.
 * agbplay throws Xcept (: std::exception) on malformed song data — a bad
 * sounds.json offset or corrupt ROM — and several callers run on the SDL
 * audio thread, where letting it unwind across the extern "C" boundary is
 * UB / std::terminate. The guards below convert that into silence + a warning. */
static void AudioGuardWarn(const char* where, const char* what) {
    static int warned = 0;
    if (warned < 8) {
        fprintf(stderr, "[AUDIO] contained exception in %s: %s\n", where, what ? what : "unknown");
        ++warned;
    }
}

static void RebuildContextLocked(void) {
    std::span<uint8_t> romSpan;
    SongTableInfo songTableInfo;

    /* Drop any commands queued against the OLD context — the engine re-issues
     * the room BGM after a reset, so a stale start/volume must not leak onto the
     * fresh context. Caller holds sStateMutex; taking sCmdMutex here keeps the
     * sStateMutex→sCmdMutex order used by DrainCommandsLocked (no deadlock). */
    {
        std::lock_guard<std::mutex> cmdLock(sCmdMutex);
        sCmdQueue.clear();
        sCmdDrainQueue.clear();
    }
    sState.pendingSamples.clear();
    sState.mixAccL.clear();
    sState.mixAccR.clear();
    sState.pendingFrameOffset = 0;
    sState.ctx.reset();
    sState.rom.reset();
    sState.songMapLoaded = false;
    sState.songHeaderOffsets.fill(0);

    if (gRomData == nullptr || gRomSize == 0 || !sState.initialized) {
        return;
    }

    try {
        romSpan = std::span<uint8_t>(gRomData, gRomSize);
        sState.rom = std::make_unique<Rom>(Rom::LoadFromBufferRef(romSpan));

        songTableInfo.pos = SongTableInfo::POS_AUTO;
        songTableInfo.count = 0;
        songTableInfo.tableIdx = 0;

        sState.ctx = std::make_unique<MP2KContext>(sState.sampleRate, -1, *sState.rom, MakeSoundMode(),
                                                   MakeAgbplayMode(), songTableInfo, BuildPlayerTable());
        sState.ctx->m4aSoundMode(sState.soundMode);
        const size_t sampleCount = sState.ctx->mixer.GetSamplesPerBuffer();
        sState.pendingSamples.resize(sampleCount * 2);
        sState.mixAccL.resize(sampleCount);
        sState.mixAccR.resize(sampleCount);
    } catch (const std::exception& e) {
        /* Malformed song table / ROM: leave the backend silent rather than
         * letting agbplay's Xcept cross the extern "C" boundary. */
        AudioGuardWarn("RebuildContextLocked", e.what());
        sState.ctx.reset();
        sState.rom.reset();
    }
}

static bool HasActivePlaybackLocked(void) {
    if (!sState.ctx) {
        return false;
    }

    for (const auto& player : sState.ctx->players) {
        if (player.playing || !player.finished) {
            return true;
        }
    }

    return !sState.ctx->sndChannels.empty() || !sState.ctx->sq1Channels.empty() || !sState.ctx->sq2Channels.empty() ||
           !sState.ctx->waveChannels.empty() || !sState.ctx->noiseChannels.empty();
}

/* Recompute + publish the lock-free active-player mask (caller holds
 * sStateMutex). Same liveness test the old locking IsPlayerActive used: a
 * player is audibly active while it's playing AND some track is still
 * sequencing (enabled) or holding ringing notes (activeNotes). */
static void PublishActivePlayerMaskLocked(void) {
    uint32_t mask = 0;
    if (sState.ctx) {
        const size_t n = std::min<size_t>(kPlayerCount, sState.ctx->players.size());
        for (size_t p = 0; p < n; ++p) {
            const auto& player = sState.ctx->players[p];
            if (!player.playing) {
                continue;
            }
            for (const auto& trk : player.tracks) {
                if (trk.enabled || trk.activeNotes.any()) {
                    mask |= (1u << p);
                    break;
                }
            }
        }
    }
    sActivePlayerMask.store(mask, std::memory_order_relaxed);
}

static void RenderChunkLocked(void) {
    const size_t sampleCount = sState.ctx->mixer.GetSamplesPerBuffer();

    sState.pendingSamples.resize(sampleCount * 2);
    std::fill(sState.pendingSamples.begin(), sState.pendingSamples.end(), static_cast<int16_t>(0));
    sState.pendingFrameOffset = 0;

    if (!sState.vsyncEnabled || !HasActivePlaybackLocked()) {
        return;
    }

    try {
        sState.ctx->m4aSoundMain();

        // Track-outer / sample-inner: gain/pan are per-track invariants, so they
        // (and the muted/silent skip) are computed once per track instead of once
        // per sample. Track summation order is preserved, so the float result is
        // bit-identical to the old per-sample loop (pan factor folds to *1.0f when
        // inactive, which is exact in IEEE754).
        sState.mixAccL.resize(sampleCount);
        sState.mixAccR.resize(sampleCount);
        std::fill(sState.mixAccL.begin(), sState.mixAccL.end(), 0.0f);
        std::fill(sState.mixAccR.begin(), sState.mixAccR.end(), 0.0f);
        float *const accL = sState.mixAccL.data();
        float *const accR = sState.mixAccR.data();

        auto mixTrack = [&](uint32_t playerIndex, size_t trackIndex, const MP2KTrack& track) {
#ifdef TMC_3DS
            if (!track.audioBufferActive) {
                return;
            }
#endif
            const uint8_t volume = sState.trackVolumes[playerIndex][trackIndex];
            const float gain = static_cast<float>(volume) / 255.0f;

            if (track.muted || gain <= 0.0f) {
                return;
            }

            const int8_t panValue = sState.trackPans[playerIndex][trackIndex];
            const size_t n = std::min(sampleCount, track.audioBuffer.size());
            const auto* buf = track.audioBuffer.data();
            if (volume == 0xFF && panValue == 0) {
                for (size_t sampleIndex = 0; sampleIndex < n; sampleIndex++) {
                    accL[sampleIndex] += buf[sampleIndex].left;
                    accR[sampleIndex] += buf[sampleIndex].right;
                }
                return;
            }

            const float pan = static_cast<float>(panValue) / 64.0f;
            float panL = 1.0f;
            float panR = 1.0f;
            if (pan > 0.0f) {
                panL = 1.0f - std::min(pan, 1.0f);
            } else if (pan < 0.0f) {
                panR = 1.0f - std::min(-pan, 1.0f);
            }

            for (size_t sampleIndex = 0; sampleIndex < n; sampleIndex++) {
                accL[sampleIndex] += buf[sampleIndex].left * gain * panL;
                accR[sampleIndex] += buf[sampleIndex].right * gain * panR;
            }
        };

#ifdef TMC_3DS
        for (const MP2KTrack *track : sState.ctx->mixer.GetActiveTracks()) {
            if (track->playerIdx < kPlayerCount && track->trackIdx < kMaxTracks)
                mixTrack(track->playerIdx, track->trackIdx, *track);
        }
#else
        const uint32_t playerCount =
            std::min<uint32_t>(kPlayerCount, static_cast<uint32_t>(sState.ctx->players.size()));
        for (uint32_t playerIndex = 0; playerIndex < playerCount; playerIndex++) {
            const auto& player = sState.ctx->players[playerIndex];
            const size_t trackCount = std::min<size_t>(kMaxTracks, player.tracks.size());
            for (size_t trackIndex = 0; trackIndex < trackCount; trackIndex++)
                mixTrack(playerIndex, trackIndex, player.tracks[trackIndex]);
        }
#endif

        const float masterVolume = sState.masterVolume;
        for (size_t sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
            float left = accL[sampleIndex];
            float right = accR[sampleIndex];
            if (masterVolume != 1.0f) {
                left *= masterVolume;
                right *= masterVolume;
            }

            sState.pendingSamples[sampleIndex * 2 + 0] = Port_QuantizePcm16(left);
            sState.pendingSamples[sampleIndex * 2 + 1] = Port_QuantizePcm16(right);
        }
    } catch (const std::exception& e) {
        AudioGuardWarn("RenderChunkLocked", e.what());
        std::fill(sState.pendingSamples.begin(), sState.pendingSamples.end(), static_cast<int16_t>(0));
    }
}

} // namespace

bool Port_M4A_Backend_Init(uint32_t sampleRate) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.initialized = true;
    sState.sampleRate = sampleRate;
    sState.soundMode = 0;
    sState.vsyncEnabled = true;
    ResetTrackMixControlsLocked();
    {
        std::lock_guard<std::mutex> cmdLock(sCmdMutex);
        sCmdQueue.reserve(kCommandReserve);
        sCmdDrainQueue.reserve(kCommandReserve);
    }
    return true;
}

void Port_M4A_Backend_Shutdown(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.pendingSamples.clear();
    sState.mixAccL.clear();
    sState.mixAccR.clear();
    sState.pendingFrameOffset = 0;
    sState.ctx.reset();
    sState.rom.reset();
    sState.initialized = false;
}

void Port_M4A_Backend_Reset(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    ResetTrackMixControlsLocked();
    /* RebuildContextLocked destroys and reconstructs every MP2KTrack and its
       ReverbEffect (fresh zero-filled reverb buffer) and re-derives reverbForce
       from sState.reverbForceByte via MakeAgbplayMode — so a reverb tail can
       never bleed across an area/song change, and the user's chosen level
       persists. If a lighter soft-reset path is ever added that keeps the
       context alive, call trk.reverb->Reset() to flush the comb ring. */
    RebuildContextLocked();
}

void Port_M4A_Backend_SoundInit(uint32_t soundMode) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.soundMode = soundMode;
    sState.vsyncEnabled = true;
    ResetTrackMixControlsLocked();
    RebuildContextLocked();
}

void Port_M4A_Backend_SetSoundMode(uint32_t soundMode) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.soundMode = soundMode;
    if (sState.ctx) {
        sState.ctx->m4aSoundMode(soundMode);
    }
}

void Port_M4A_Backend_SetVSyncEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState.vsyncEnabled = enabled;
}

/* ---- *Locked helpers: run on the audio thread with sStateMutex already held,
 * applied from DrainCommandsLocked. Return value of the start helper is unused
 * (the public entry point returns optimistically after a synchronous validity
 * check). ---- */
static void StartSongLocked(uint8_t playerIndex, uint16_t songId) {
    try {
        const size_t songPos = SongIdToRomPosLocked(songId);
        if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
            return;
        }
        if (songPos == 0 || songPos >= gRomSize) {
            sState.ctx->m4aMPlayStop(playerIndex);
            if (playerIndex < kPlayerCount)
                sState.currentSongId[playerIndex] = 0;
            return;
        }
        /* Idempotent restart for BGM only (song IDs 1..99): if this player is
         * already running this exact BGM, leave it alone. The engine commonly
         * re-issues the room BGM on every room transition; restarting the
         * playback is audible as music resetting between rooms. SFX (>=100)
         * legitimately re-trigger the same id and must NOT be skipped. */
        if (songId >= 1 && songId <= 99 && playerIndex < kPlayerCount && sState.currentSongId[playerIndex] == songId) {
            return;
        }
        sState.ctx->m4aMPlayStart(playerIndex, songPos);
        if (playerIndex < kPlayerCount)
            sState.currentSongId[playerIndex] = songId;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[AUDIO] rejected song player=%u id=%u ROM offset=0x%zX\n", playerIndex, songId,
                     SongIdToRomPosLocked(songId));
        AudioGuardWarn("StartSongLocked", e.what());
    }
}

static void StopPlayerLocked(uint8_t playerIndex) {
    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return;
    }
    sState.ctx->m4aMPlayStop(playerIndex);
    if (playerIndex < kPlayerCount)
        sState.currentSongId[playerIndex] = 0;
}

static void ContinuePlayerLocked(uint8_t playerIndex) {
    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return;
    }
    sState.ctx->m4aMPlayContinue(playerIndex);
}

static void SetTrackVolumeLocked(uint8_t playerIndex, uint16_t trackBits, uint16_t volume) {
    const uint8_t clamped = volume > 0xff ? 0xff : static_cast<uint8_t>(volume);
    if (playerIndex >= kPlayerCount) {
        return;
    }
    for (uint32_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
        if (((trackBits >> trackIndex) & 1u) != 0) {
            sState.trackVolumes[playerIndex][trackIndex] = clamped;
        }
    }
}

static void SetTrackPanLocked(uint8_t playerIndex, uint16_t trackBits, int8_t pan) {
    if (playerIndex >= kPlayerCount) {
        return;
    }
    for (uint32_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
        if (((trackBits >> trackIndex) & 1u) != 0) {
            sState.trackPans[playerIndex][trackIndex] = pan;
        }
    }
}

/* Apply one command with sStateMutex already held (audio-thread drain or the
 * main-thread try-lock fast path below). */
static void ApplyCommandLocked(const M4ACommand& c) {
    switch (c.type) {
        case M4ACmdType::StartSong:
            StartSongLocked(c.playerIndex, c.songId);
            break;
        case M4ACmdType::Stop:
            StopPlayerLocked(c.playerIndex);
            break;
        case M4ACmdType::Continue:
            ContinuePlayerLocked(c.playerIndex);
            break;
        case M4ACmdType::SetVolume:
            SetTrackVolumeLocked(c.playerIndex, c.trackBits, c.volume);
            break;
        case M4ACmdType::SetPan:
            SetTrackPanLocked(c.playerIndex, c.trackBits, c.pan);
            break;
    }
}

/* Swap the pending command list out under the short sCmdMutex, then apply each
 * in order. Always entered with sStateMutex held (audio-thread render, or the
 * main-thread fast path), so the reused drain vector is single-threaded. */
static void DrainCommandsLocked(void) {
    {
        std::lock_guard<std::mutex> lock(sCmdMutex);
        if (sCmdQueue.empty()) {
            return;
        }
        sCmdDrainQueue.swap(sCmdQueue);
    }
    for (const M4ACommand& c : sCmdDrainQueue) {
        ApplyCommandLocked(c);
    }
    sCmdDrainQueue.clear();
}

/* Main-thread submit. The item-get freeze was the main thread BLOCKING on
 * sStateMutex (the audio render lock) — several mutators per frame, std::mutex
 * unfair, tick stalled ~500 ms. try_lock never blocks: if the audio thread is
 * not mid-render (true for most of each 20 ms callback), apply synchronously so
 * the sound arms THIS tick (no audible item-get lag); if contended, enqueue and
 * the audio thread drains it next callback (no freeze). FIFO holds because we
 * drain anything already queued before applying ours, all under sStateMutex —
 * the same lock/order the audio drain uses (state→cmd), so no reorder, no
 * deadlock. */
static void SubmitCommand(const M4ACommand& c) {
    std::unique_lock<std::mutex> lock(sStateMutex, std::try_to_lock);
    const bool fast = lock.owns_lock();
    if (Port_DebugVerbose && c.type == M4ACmdType::StartSong && c.songId >= 100) {
        fprintf(stderr, "[m4a] SFX start id=%u %s\n", c.songId, fast ? "FAST(sync)" : "QUEUED(defer)");
    }
    if (fast) {
        DrainCommandsLocked();
        ApplyCommandLocked(c);
        return;
    }
    PushCommand(c);
}

bool Port_M4A_Backend_StartSongById(uint8_t playerIndex, uint16_t songId) {
    /* Apply now if the audio lock is free (fanfare arms this tick, no lag),
     * else enqueue — never block on the render lock. Validity is checked at
     * apply; an invalid song just doesn't play. Return true so the m4a stub
     * keeps its track bookkeeping consistent with what will play. */
    M4ACommand c{};
    c.type = M4ACmdType::StartSong;
    c.playerIndex = playerIndex;
    c.songId = songId;
    SubmitCommand(c);
    return true;
}

void Port_M4A_Backend_StopPlayer(uint8_t playerIndex) {
    M4ACommand c{};
    c.type = M4ACmdType::Stop;
    c.playerIndex = playerIndex;
    SubmitCommand(c);
}

void Port_M4A_Backend_ContinuePlayer(uint8_t playerIndex) {
    M4ACommand c{};
    c.type = M4ACmdType::Continue;
    c.playerIndex = playerIndex;
    SubmitCommand(c);
}

void Port_M4A_Backend_SetTrackVolume(uint8_t playerIndex, uint16_t trackBits, uint16_t volume) {
    M4ACommand c{};
    c.type = M4ACmdType::SetVolume;
    c.playerIndex = playerIndex;
    c.trackBits = trackBits;
    c.volume = volume;
    SubmitCommand(c);
}

bool Port_M4A_Backend_IsPlayerActive(uint8_t playerIndex) {
    /* Lock-free: read the mask the audio thread publishes each chunk. The BGM
     * duck (#22) polls this EVERY frame while the item-get jingle plays; taking
     * sStateMutex here (as it used to) blocked the game tick on the audio render
     * lock and was the residual half of the item-get freeze. The liveness test
     * (playing && some track enabled/ringing) now lives in
     * PublishActivePlayerMaskLocked. */
    if (playerIndex >= kPlayerCount) {
        return false;
    }
    return (sActivePlayerMask.load(std::memory_order_relaxed) >> playerIndex) & 1u;
}

void Port_M4A_Backend_SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState.masterVolume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

float Port_M4A_Backend_GetMasterVolume(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    return sState.masterVolume;
}

void Port_M4A_Backend_SetGbaAccurate(bool accurate) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState.gbaAccurate = accurate;
    if (!sState.ctx) {
        return;
    }
    /* Mutate the live mode so the change takes effect without a context
     * rebuild. The resampler type is re-read whenever a PCM channel is
     * (re)allocated — i.e. on the next note-on, so it audibly switches within a
     * fraction of a second. reverbForce, however, is NOT re-read per frame: it
     * is captured into each ReverbEffect's cached intensity, refreshed only by
     * SoundMixer::UpdateReverb() — so set the field AND call UpdateReverb() to
     * apply it live. MakeAgbplayMode also honours these on a later rebuild. */
#if defined(TMC_3DS)
    const ResamplerType rs = ResamplerType::NEAREST;
#elif defined(__ANDROID__)
    const ResamplerType rs = accurate ? ResamplerType::NEAREST : ResamplerType::LINEAR;
#else
    const ResamplerType rs = accurate ? ResamplerType::NEAREST : ResamplerType::SINC;
#endif
    sState.ctx->agbplaySoundMode.resamplerTypeNormal = rs;
    sState.ctx->agbplaySoundMode.resamplerTypeFixed = rs;
#if defined(TMC_3DS)
    sState.ctx->agbplaySoundMode.reverbForce = 0x80;
#else
    sState.ctx->agbplaySoundMode.reverbForce = accurate ? 0 : sState.reverbForceByte;
#endif
    sState.ctx->mixer.UpdateReverb();
}

bool Port_M4A_Backend_GetGbaAccurate(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    return sState.gbaAccurate;
}

void Port_M4A_Backend_SetReverbLevel(int level) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    /* 0 (or below) → fully dry (byte 0x00); otherwise 0x80|level engages the
     * forced reverb. Clamp to the slider's safe ceiling (24) — NORMAL's
     * single-tap comb flutters/rings well below the 0x7F maximum. */
    if (level < 0) {
        level = 0;
    } else if (level > 24) {
        level = 24;
    }
    sState.reverbForceByte = (level == 0) ? 0x00 : (uint8_t)(0x80 | (level & 0x7F));
    if (!sState.ctx) {
        return;
    }
    /* Apply live: GetReverbLevel reads reverbForce, and UpdateReverb() pushes
     * the new level onto every existing ReverbEffect via SetLevel() — no
     * context rebuild, so the currently-playing song is not restarted. */
#if defined(TMC_3DS)
    sState.ctx->agbplaySoundMode.reverbForce = 0x80;
#else
    sState.ctx->agbplaySoundMode.reverbForce = sState.gbaAccurate ? 0 : sState.reverbForceByte;
#endif
    sState.ctx->mixer.UpdateReverb();
}

int Port_M4A_Backend_GetReverbLevel(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    return (sState.reverbForceByte & 0x80) ? (sState.reverbForceByte & 0x7F) : 0;
}

void Port_M4A_Backend_SetTrackPan(uint8_t playerIndex, uint16_t trackBits, int8_t pan) {
    M4ACommand c{};
    c.type = M4ACmdType::SetPan;
    c.playerIndex = playerIndex;
    c.trackBits = trackBits;
    c.pan = pan;
    SubmitCommand(c);
}

void Port_M4A_Backend_Render(int16_t* outSamples, uint32_t frameCount, bool mute) {
    uint32_t framesRemaining = frameCount;

    if (outSamples == nullptr) {
        return;
    }

    /* The main thread no longer takes sStateMutex for song/volume changes (they
     * go through the command queue) NOR for IsPlayerActive (it reads the atomic
     * mask published below). Per-chunk locking is retained only for the rare
     * remaining setters/queries (SetMasterVolume, SetReverbLevel, F8 menu). */
    while (framesRemaining > 0) {
        size_t availableFrames;
        size_t copyFrames;
        std::lock_guard<std::mutex> lock(sStateMutex);

        DrainCommandsLocked();

        if (!sState.ctx) {
            memset(outSamples, 0, sizeof(int16_t) * framesRemaining * 2);
            return;
        }

        if (sState.pendingFrameOffset >= sState.pendingSamples.size() / 2) {
            RenderChunkLocked();
            /* A chunk advanced player state — republish the lock-free liveness
             * mask so the main thread's duck sees the jingle end promptly. */
            PublishActivePlayerMaskLocked();
        }

        availableFrames = (sState.pendingSamples.size() / 2) - sState.pendingFrameOffset;
        if (availableFrames == 0) {
            memset(outSamples, 0, sizeof(int16_t) * framesRemaining * 2);
            return;
        }

        copyFrames = std::min<size_t>(availableFrames, framesRemaining);
        if (mute) {
            memset(outSamples, 0, sizeof(int16_t) * copyFrames * 2);
        } else {
            memcpy(outSamples, &sState.pendingSamples[sState.pendingFrameOffset * 2], sizeof(int16_t) * copyFrames * 2);
        }

        outSamples += copyFrames * 2;
        framesRemaining -= static_cast<uint32_t>(copyFrames);
        sState.pendingFrameOffset += copyFrames;
    }
}
