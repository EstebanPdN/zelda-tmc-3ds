# TMC PC-Port — Audio Pipeline Architectural Audit (2026-05-30)

_GBA MP2K → libs/agbplay_core software synth → port_m4a_backend bridge → port_audio SDL3. Read-only audit; the GBA audio hardware (FIFO/DMA/Timer/PSG MMIO) is already abstracted away — src/gba/m4a.c is compiled-out._

# Audio Pipeline Architectural Audit — The Minish Cap PC Port

## 1. Summary

The GBA MP2K ("m4a") sound driver is **present in the tree as a faithful decomp but bypassed on PC**: `src/gba/m4a.c` is commented out of the `tmc_pc` target (`xmake.lua:859`) and the hand-written ARM mixer `asm/lib/m4a_asm.s` is linked only by the GBA-ROM rebuild path (`xmake.lua:1349`), so none of its FIFO/DMA/Timer/SOUNDxCNT MMIO or its `SoundMain`/`CgbSound` synthesis is ever executed. The game-facing m4a C API is re-implemented as pure bookkeeping in `port/port_m4a_stubs.c` (its `m4aSoundMain` only does `info->clock++`) and forwards real synthesis to `port/port_m4a_backend.cpp`, which drives `libs/agbplay_core` — a from-scratch behavioral software re-implementation of MP2K, **not** an MMIO/register emulator. agbplay renders DirectSound PCM (`sndChannels`) and the four legacy PSG voices (`sq1/sq2/wave/noiseChannels`) directly to a 48 kHz float buffer; the SDL3 output stage (`port/port_audio.c`) opens a pull-model `SDL_AudioStream` at S16LE/48 kHz/stereo and applies a post-process DSP chain. The headline accuracy posture is **high at the sequencer/envelope/instrument level** (CGB sustain-bug, Ch3 quantization, MONO_STRICT PSG polyphony all reproduced) but **deliberately cleaner-than-hardware in the signal path** (SINC resampling instead of GBA nearest-neighbour, forced global reverb, an LP/HP/stereo-widen/soft-clip output chain the GBA never had, no DirectSound 8-voice cap). The single game-visible defect is `#22`: the stuck `IsPlayerActive` probe that forces the BGM-duck to fall back to a 180-frame timeout.

## 2. FILE MAP

| File | Layer | Core functions / structs | Role |
|---|---|---|---|
| `src/gba/m4a.c` | GBA MP2K driver (decomp) | `m4aSoundInit`, `m4aSoundMain`, `SoundInit`, `SampleFreqSet`, `m4aSoundVSyncOn/Off`, `MPlayExtender`, `CgbSound`, `MidiKeyToCgbFreq`, `SoundInfo`/`SoundChannel`/`CgbChannel` | Faithful decomp of the GBA DirectSound+PSG driver. **Compiled-but-unused** on PC (excluded at `xmake.lua:859`); reference + GBA-ROM build only. |
| `include/gba/m4a.h` | MP2K data model | `WaveData`, `ToneData`, `SongHeader`, `Song`, `SoundChannel`, `MusicPlayerInfo`, `MusicPlayerTrack`, `MusicPlayer`, `gMusicPlayers`, `gSongTable` | Struct + song-table declarations. Still used on PC as the **song-id/header data model** consumed by the stubs/backend; the driver protos it declares are provided by `port_m4a_stubs.c`. |
| `asm/lib/m4a_asm.s` | GBA low-level mixer (ARM) | `SoundMain`, `SoundMainRAM`, `SoundMainBTM`, `RealClearChain`, `MPlayJumpTableCopy`, `umul3232H32` | Hand-written ARM/Thumb hot path (RAM-copied/IWRAM-executed on GBA). GBA-ROM-only; **never linked into `tmc_pc`**. |
| `include/gba/io_reg.h` | GBA MMIO defs | `REG_SOUNDCNT_H/X/L`, `REG_FIFO_A/B`, `REG_DMA1/2`, `REG_TM0CNT`, `REG_SOUND1-4CNT`, `WAVE_RAM0-3`, `REG_SOUNDBIAS_H`, `DMA_START_SPECIAL` | Audio MMIO address/bit defs. Referenced **only** by the un-compiled `m4a.c` on PC. |
| `include/gba/macro.h` | DMA macro layer | `DmaSet→port_DmaTransfer`, `DmaStop=((void)0)`, `DmaWait=((void)0)` | On PC `DmaStop/DmaWait` are no-ops and `DmaSet` is a memcpy; no `DMA_START_SPECIAL` (sound/FIFO) handling. Audio DMA never flows through here. |
| `xmake.lua` | Build config | l.656 `port_audio.c`, l.695 `port_m4a_backend.cpp`, l.752 `port_m4a_stubs.c`, l.777 `src/sound.c`, l.760 `libs/agbplay_core/*.cpp`, l.859 commented `m4a.c`, l.1349 `os.files("asm/**.s")` | Proves which audio TUs are compiled and that `m4a.c`/`m4a_asm.s` are GBA-ROM-only. |
| `port/port_m4a_stubs.c` | m4a API re-impl (PC) | `m4aSoundInit:245`, `m4aSoundMain:269` (clock-only), `MPlayOpen:159` (#53 trackCount fix), `m4aSongNumStart:314`, `m4aSoundMode:293`, `m4aSoundVSync:308`, `m4aMPlayVolumeControl:444`, `m4aMPlayPanpotControl:482` | Replaces `src/gba/m4a.c`. Thin bookkeeping over `gMusicPlayers` + delegation to the backend; **no MMIO/DMA/timer/FIFO**. |
| `port/port_m4a_backend.cpp` | agbplay bridge | `SongHeaderToRomPos:138`, `MakeSoundMode:103`, `MakeAgbplayMode:113`, `LoadSongMapLocked:317`, `RebuildContextLocked:451`, `RenderChunkLocked:494`, `HasActivePlaybackLocked:479`, `IsPlayerActive:680`, `StartSongById:600`, `Render:703`, `sStateMutex:101` | The real bridge: owns one `MP2KContext`, song-id→ROM-offset map, per-track software mix, mutex; configures agbplay sound/accuracy modes; drives the engine tick from the audio thread. |
| `port/port_m4a_backend.h` | Bridge API | `Port_M4A_Backend_IsPlayerActive` doc ("tracks actively running") | Public API surface. Doc comment already describes the *intended* (tracks-based) active semantics. |
| `port/port_audio.c` | SDL3 output | `PORT_AUDIO_SAMPLE_RATE=48000:9`, `Port_Audio_PostProcess:89`, `Port_Audio_SoftClip:74`, `Port_Audio_Feed:125`, `Port_Audio_Init:152`, `Port_Audio_OnFifoWrite:193` (no-op) | Pull-model SDL3 `SDL_AudioStream` setup + 4-stage output DSP. Does no resampling. |
| `port/port_audio.h` | Output API | `Port_Audio_Init/Shutdown/Reset/OnFifoWrite` | Public API for the output stage. |
| `port/port_audio_mute.cpp` / `.h` | Accessibility filter | `sCategories[]:12`, `Port_AudioMute_ShouldSuppress:63`, `AudioMuteCategory` enum | Dispatch-time `SoundReq` SFX-category mute (Ezlo/NPC voice, low-health beep). Not a GBA-accuracy concern. |
| `port/port_gba_mem.c` | MMIO state buffer | `gIoMem[0x400]:12`, `gba_write8` FIFO hook:31, `gba_write32` FIFO hook:138 | Intercepts FIFO_A/B writes → `Port_Audio_OnFifoWrite`. **Vestigial**: no compiled code writes FIFO; `gba_write16` has no hook. |
| `port/port_main.c` | Init wiring | `Port_InitAudio:126`, SDL dummy-driver fallback → `gMain.muteAudio=1:137` | Initializes the output stage; sets mute on dummy-driver fallback. |
| `port/port_gameplay_stubs.c` | Deferred SFX queue | `EnqueueSFX:99` + `Port_AudioMute_ShouldSuppress` gate:106, `SoundReqClipped:116` | Frame-flushed SFX queue; mirrors `SoundReq`'s mute gate (duplicated policy). |
| `src/sound.c` | Game-facing audio API | `IS_BGM/IS_SFX:11`, `IsDuckingSfx:44`, `BGM_DUCK_TIMEOUT_FRAMES=180:42`, `SoundReq:80`, `AudioMain:195`, `fade:255`, `doPlaySound:269`, `InitVolume:297`, `gSongTable[0x222]:1006` | Decompiled, **compiled & used** command-dispatch + volume layer. Near byte-identical to upstream except the SFX mute hook, the #22 BGM-duck state machine, and the `!sBgmDuckActive` stop guard. |
| `src/interrupts.c` | IRQ handlers | `VBlankIntr:51` (`m4aSoundVSync`), `HBlankIntr:63` (`m4aSoundMain`) | Calls `m4aSoundVSync/Main` — but on PC these resolve to the clock-only stub, not synthesis. |
| `src/main.c` | Init/DMA | `InitDMA:238` (`SONG_VSYNC_OFF`), `DmaStop(0..3):164`, `SetVBlankDMA:257`, `DisableVBlankDMA:265` | Audio init issues `SONG_VSYNC_OFF`; `DmaStop` is a no-op; VBlank-DMA is repurposed for BG effects, not FIFO. |
| `libs/agbplay_core/MP2KContext.hpp/.cpp` | agbplay engine root | `m4aSoundMain` = `reader.Process()+mixer.Process()` (`.cpp:44`), channel `std::list`s (`.hpp:75-79`), `m4aSoundMode` parses DirectSound max-channel bits (`.cpp:55-57`), `m4aMPlayStart` priority steal (`.cpp:166`) | Top-level engine object; owns Rom ref, SequenceReader, SoundMixer, players, 5 channel pools, `masterAudioBuffer`. |
| `libs/agbplay_core/SequenceReader.cpp` | MP2K interpreter | `PlayerMain` tick/`finished`:93-139, `cmdPlayNote`:278, CGB dispatch:395-434, `cgbPolyphonySuppressFunc`:356, DirectSound cap/voice-steal before `sndChannels.emplace_back`, `cmdPlayFine`:647 | MP2K bytecode interpreter + tempo driver + note-on dispatcher; reads sequence/voice data straight from ROM. |
| `libs/agbplay_core/SoundMixer.cpp/.hpp` | Mixdown | `UpdateFixedModeRate`/`rateTable[5]=15768`:30-51, `Process`:53, `GetReverbLevel`:184, `GetBufferLengthSpeedCorrection`:154, `samplesPerBuffer = sr/(AGB_EXACT_FPS*INTERFRAMES)` (`.hpp:38`) | Per-AGB-frame mixdown; per-track reverb to PCM only; master fade; forced-reverb honoring. |
| `libs/agbplay_core/MP2KChnPCM.cpp` | DirectSound PCM voice | `sampleFetchCallback` (8-bit `/128`):433, fixed-mode `interStep`:97-98, `stepEnvelope`:204, `SetVol` pan:120 | Software FIFO/DAC replacement: ADSR + 8-bit ROM sample fetch + float resample + stereo vol/pan. |
| `libs/agbplay_core/MP2KChnPSG.cpp` | 4 PSG voices | `MP2KChnPSGSquare:364` (sweep `sweep2coeff:518`/`sweep2convergence:544`), `MP2KChnPSGWave:571` (ROM wave read:581, DC corr:591, Ch3 quant:720), `MP2KChnPSGNoise:796` (LFSR 0x6000/0x60:805), `stepEnvelope`/`applyVol` CGB sustain bug:208-216,349-353 | Software synthesis of the 2 square / 1 wave / 1 noise GameBoy channels. |
| `libs/agbplay_core/CGBPatterns.cpp` | Duty LUTs | `pat_sq12/25/50/75:5-8` | The 4 GBA square-wave duty-cycle patterns. |
| `libs/agbplay_core/Resampler.cpp` | Per-channel resamplers | `MakeResampler:17`, `NearestResampler:52`, `SincResampler:141`, `BlepResampler:274`, `BlampResampler:357`, `INTERP_FILTER_CUTOFF_FREQ 0.85` | Bandlimited resamplers replacing GBA nearest-neighbour fetch (port forces SINC for PCM). |
| `libs/agbplay_core/ReverbEffect.cpp` | Reverb | `ctor` (DMA-buffer-sized delay):14, `ProcessInternal` 2-tap comb:72, `MakeReverb:46`, `ReverbGS1/GS2`:104 | MP2K stock comb reverb (NORMAL) + Golden-Sun variants. |
| `libs/agbplay_core/Rom.cpp/.hpp` | ROM accessor | `LoadFromBufferRef:15`, `Load*/Verify` no-op:52-72, `ReadU8/U16/U32`, `ReadAgbPtrToPos` | Wraps the port's live mmap'd `gRomData` by reference; byte-wise little-endian reads, AGB-ptr→offset. |
| `libs/agbplay_core/Types.hpp` | Shared types | `sample{float,float}:162`, `MP2KSoundMode:94` (`dacConfig:114`), `AgbplaySoundMode:117` (accuracy flags:125-127), `ADSR:61`, `EnvState:11`, `ResamplerType` | All shared structs/enums incl. the accuracy toggles the port sets. |
| `libs/agbplay_core/Constants.hpp` | Timing constants | `AGB_EXACT_FPS≈59.7275:12`, `AGB_APPROX_FPS=60:13`, `INTERFRAMES=4:17`, `MAX_TRACKS=16:7`, `SONG_FINISH_TIME=1000:20` | Engine timing constants. |
| `libs/agbplay_core/MP2KPlayer.cpp` | Player init | `Init` (`playing=true`, `finished=(tracksUsed==0)`):14-50, `finished` default true (`.hpp:26`) | Defines the initial `playing`/`finished` state the duck probe reads. |
| `libs/agbplay_core/SoundData.hpp` | Voice type bytes | `BANKDATA_TYPE_SPLIT 0x40`, `RHYTHM 0x80`, CGB mask `0x07`, `FIX 0x08`, `PCM/SQ1/SQ2/WAVE/NOISE:8-18` | MP2K bank-data type bytes used by `cmdPlayNote` to dispatch PCM vs CGB voice types. |

## 3. GBA QUIRKS LOG

### DirectSound (FIFO / DMA / Timer)

| What | Where | PC handling | Accuracy risk |
|---|---|---|---|
| FIFO_A/B refill via Timer0-driven DMA1/2 (`SAD=pcmBuffer`, `DAD=&REG_FIFO_A`, `DMA_START_SPECIAL\|REPEAT\|32BIT`), re-armed every frame | `src/gba/m4a.c:581-582`, `:736`, `:758-762`; `io_reg.h:962` | **compiled-but-unused** — `m4a.c` excluded; agbplay synthesizes PCM in software (`MP2KChnPCM.cpp`); no FIFO/DMA. | Latent: if `m4a.c` were re-enabled, `port_DmaTransfer` (`macro.h:30`) models neither `DMA_START_SPECIAL` nor repeat → audio would be one synchronous copy with no refill. Currently none. |
| Timer0 sample clock `REG_TM0CNT_L = -(0x44940/pcmSamplesPerVBlank)`, VCOUNT VBlank sync | `src/gba/m4a.c:744-745`, `SampleFreqSet:604` | **abstracted** — no Timer0 handler in `port_bios.c`; pacing is SDL pull-callback + agbplay's per-AGB-frame buffer (`SoundMixer.hpp:38`). | Low — render rate honored inside agbplay; only the pacing *source* differs (callback-pull vs Timer-push). |
| `REG_FIFO_A/B` 8-bit signed PCM sample queue | `src/gba/m4a.c:582`; `io_reg.h:437`; trapped in `port_gba_mem.c:31,138` | **stubbed-noop** — `Port_Audio_OnFifoWrite` is empty (`port_audio.c:193`); no compiled code writes FIFO. | None (unreachable). Vestigial dead code; would silently drop audio if `m4a.c` re-enabled. |
| `SOUNDCNT_H` DirectSound routing (`FIFO_RESET\|TIMER_0\|L/R\|MIX_FULL`) | `src/gba/m4a.c:577`; `io_reg.h:427` | **emulated-in-agbplay** — DS/PSG mixing done in `SoundMixer::Process`; no register state. | None for content — TMC always uses full mix; ratio bits not literally honored but inaudible. |
| `SOUNDBIAS_H` 9-bit PWM DAC bias / output sampling (`0x40` = 65.5 kHz) | `src/gba/m4a.c:579`, `:663`; `CgbSound:1292` reads it | **abstracted** — fixed `dacConfig=0x09` (`port_m4a_backend.cpp:109`); SOUNDBIAS not modeled. | Low (cosmetic): GBA PWM/quantization noise floor & aliasing absent; output cleaner than hardware. |
| GBA DirectSound fixed render rate (`SOUND_MODE_FREQ_15768`, freq nibble 5) | `port_m4a_stubs.c:18,248`; `SoundMixer.cpp:30-51` `rateTable[5]=15768` | **emulated-in-agbplay** — drives `fixedModeRate` for FIX instruments only; everything else resampled to 48 kHz host rate. | Medium: GBA fetched 8-bit at ~15.7 kHz nearest-neighbour (aliased "crunch"); port resamples at 48 kHz SINC = intentionally cleaner/brighter. |
| DirectSound channel cap (`maxChannels=8`, hard cap `MAX_DIRECTSOUND_CHANNELS=12`) with voice-stealing | `m4a.c:85,100`; `MakeSoundMode maxChannels=0x08` (`port_m4a_backend.cpp:108`) | **inaccurate** — agbplay explicitly drops the cap (`MP2KContext.cpp:55-57`); uncapped `sndChannels.emplace_back` (`SequenceReader.cpp:475`). | Medium: dense passages play more/louder PCM voices than hardware would (GBA voice culling not reproduced). |

### PSG (SOUNDxCNT / WAVE_RAM)

| What | Where | PC handling | Accuracy risk |
|---|---|---|---|
| `SOUND1CNT_L/H/X` square ch1 + frequency sweep | `m4a.c:1146,1171,1312`; `io_reg.h:396-401` | **emulated-in-agbplay** — `MP2KChnPSGSquare` (`MP2KChnPSG.cpp:364`) with float sweep timer (`sweep2coeff:518`, `sweep2convergence:544`); voice/sweep byte read from ROM bankdata. | Low — continuous-float sweep vs discrete 11-bit hardware timer; sub-cent drift inaudible. |
| `SOUND2CNT_L/H` square ch2 (no sweep) | `m4a.c:545`; `io_reg.h:404-407` | **emulated-in-agbplay** — same channel spawned with `sweep=0` (`SequenceReader.cpp:409`); duty LUTs `pat_sq12/25/50/75`. | Low — duty patterns match standard MP2K CGB LUTs. |
| `SOUND3CNT_L/H/X` + `WAVE_RAM0-3` 32×4-bit wave | `m4a.c:1154-1157`; `io_reg.h:410-415,433-436` | **emulated-in-agbplay** — `MP2KChnPSGWave` reads the 16-byte/32-nibble pattern directly from the ROM voice ptr (`MP2KChnPSG.cpp:581`); DC correction:591; accurate Ch3 vol snap (`getVol:684`) + quantization:720, both port-enabled. | Low — faithful Ch3 model. GBA two-bank (64-sample) mode and mid-note WAVE_RAM reload not modeled, but TMC voices are single-bank static (not exercised). |
| `SOUND4CNT_L/H` noise (15/7-bit LFSR) | `m4a.c:549`; `gNoiseTable:285`; `io_reg.h:418-421` | **emulated-in-agbplay** — real Galois LFSR (state/mask `0x6000`/`0x60`, `MP2KChnPSG.cpp:805-908`); freq from continuous key curve (`SetPitch:814`). | Low-to-moderate: LFSR width/taps correct, but noise pitch uses a continuous formula instead of `m4a.c`'s discrete `gNoiseTable` NRx3-divisor steps → SFX pitch can differ slightly. |
| `SOUNDCNT_L/X` PSG L/R pan enables + master enable | `m4a.c:505-506,576-577`; `io_reg.h:424-428` | **abstracted** — no register; PSG 3-state L/CENTER/R pan per note (`applyVol:323`); master vol folded into `MP2KSoundMode.vol=0x0f`. | Moderate-by-design: 3-way quantized pan matches hardware; `SOUNDCNT_H` PSG:DS ratio not honored from a register (`vol=0x0f` assumed). TMC uses stock init → low. |
| CGB single-voice-per-channel constraint | `m4a.c:541,1095` (4-entry `gCgbChans`) | **emulated-in-agbplay** — `cgbPolyphony=MONO_STRICT` (`MakeAgbplayMode:130`), enforced by `cgbPolyphonySuppressFunc` (`SequenceReader.cpp:356`). | Low — matches GBA one-oscillator-per-channel. |
| MP2K CGB envelope "sustain applied ~1-in-7 frames" bug | GBA behavior `m4a.c:1200-1290` | **emulated-in-agbplay** — `emulateCgbSustainBug=true` (`MakeAgbplayMode:134`), in `stepEnvelope:208-216` + `applyVol:349-353`. | None — deliberate fidelity feature; enabled, so PSG sustain matches GBA. |

### MP2K driver / sequencer

| What | Where | PC handling | Accuracy risk |
|---|---|---|---|
| `SoundMainRAM` CpuCopy32'd to RAM & executed (IWRAM hot loop); 3 non-word-aligned thumb funcs | `m4a.c:352`; `m4a_asm.s:85` | **compiled-but-unused** — absent from `tmc_pc` (nm shows no symbol). | None for accuracy — pure GBA performance technique; the mixing math it did is agbplay's job. |
| `MusicPlayerJumpTableCopy` via BIOS `swi 0x2A`; `gMPlayJumpTable`/`ply_*` handlers | `m4a.c:556`, `gMPlayJumpTableTemplate:242` | **compiled-but-unused** — agbplay parses the MP2K command stream itself (`SequenceReader.cpp`). | None. |
| `SoundInfo.ident` non-volatile re-entrancy lock (main vs IRQ) | `m4a.c:89-94` | **abstracted** — replaced by `sStateMutex` (`port_m4a_backend.cpp:101`) + relaxed-atomic `muteAudio` (`port_audio.c:144`). | None — a real mutex is safer than the original soft lock. |
| Per-VBlank engine cadence (`m4aSoundMain` once/VBlank from IRQ) | `interrupts.c:53,65`; agbplay tick `MP2KContext.cpp:44` | **emulated-in-agbplay** — game's `m4aSoundMain` is clock-only (`port_m4a_stubs.c:269`); real tick pumped from the SDL audio thread (`RenderChunkLocked:504`), tempo via `GetBufferLengthSpeedCorrection` (`SoundMixer.cpp:154`). | Low-to-medium: audio advances on the audio-clock (self-corrects 60↔59.7275 fps), but controls cross threads → ~1 buffer (~4 ms) of jitter; decouples entirely during fast-forward/pause. |
| `player.finished`/`playing` as SFX-complete signal (no GBA equivalent — channel stealing was emergent) | `MP2KPlayer.hpp:25`; `SequenceReader.cpp:126-129`; `IsPlayerActive:680`; consumed `sound.c:200-208` | **inaccurate** — agbplay leaves a one-shot SFX player marked `playing` past its audible end. | **High (#22)** — probe sticks true; duck only lifts on `BGM_DUCK_TIMEOUT_FRAMES=180`. Jingle measures ~147f but probe sticks. Stale `stopBgm` could fully kill BGM (worked around in `InitVolume`). |
| Per-song reverb (TMC defaults dry; song header sets its own) | reference `m4a.c:356,853-854`; forced `MakeAgbplayMode:128-129`; `GetReverbLevel:184-187` | **inaccurate** — `reverbForce=32` overrides every header; `GetReverbLevel` returns the forced value whenever `REV_MASK_SET` is present. | Medium (audible, intentional): dry tracks gain a tail; tracks with intended reverb flattened to 32. |
| GBA nearest-neighbour PCM resampling | `MakeAgbplayMode:122-123` (SINC) | **inaccurate** — SINC bandlimited resampling (PCM); BLEP for square/wave, nearest→sinc for noise. | Intentional tonal divergence — removes GBA aliasing "crunch". |
| Output DSP chain (HP 30 Hz, LP 16 kHz Butterworth, mid/side +20%, soft-clip) | `port_audio.c:89-121` | **inaccurate** — non-GBA post-synthesis coloration, unconditional, no runtime bypass. `#115` fixed a wrong-sign `kLpA1`. | Moderate — the single largest perceptual departure; no path to bit-exact GBA output. |
| Hand-written ARM mixer / `MidiKeyToCgbFreq`/`SampleFreqSet` etc. | `asm/lib/m4a_asm.s`; `xmake.lua:1349` (ROM build only) | **compiled-but-unused** — wholesale C++ re-implementation in agbplay; no stubbed/partial ARM in the live path. | Low — agbplay's `MidiKeyToCgbFreq`/envelope math is a re-derivation, so PSG pitch/envelope edges can differ marginally from a cycle-accurate emulator. |

### Memory / alignment

| What | Where | PC handling | Accuracy risk |
|---|---|---|---|
| `SoundInfo` packs 12 `SoundChannel` + 1584-byte `s8 pcmBuffer`, DMA word-aligned; `SoundChannel`/`CgbChannel` pointer fields declared as `u32` (GBA 4-byte ptr assumption) | `m4a.c:89`, `:45`; `m4a.h:53` | **inert** — `m4a.c` not compiled; structs reused only as a data model. `MusicPlayerInfo/Track` hold real 8-byte pointers in the stubs/backend. No `PORT_STATIC_ASSERT_OFFSET` needed (driver that depends on exact offsets is excluded). | None — backend keys off `gMusicPlayers`/`gSongTable` + `SongHeaderToRomPos` (pointer-subtract vs `gRomData`), so MP2K struct ABI drift cannot crash the build. |
| `CpuCopy32` of `SoundMainRAM` into 0x380-byte RAM buffer; 3 `non_word_aligned_thumb_func_start` markers | `m4a.c:352`; `m4a_asm.s` | **inert** — ARM/Thumb instruction-alignment assumptions, irrelevant on PC. | None. |
| `ply_memacc` raw pointer arithmetic into `gMPlayMemAccArea[0x10]` | `m4a.c:1512` | **inert** — sequencer doesn't run on PC; agbplay has its own `cmdPlayMemacc` (`SequenceReader.cpp:659`). | None. |
| `gSongTable` element stride grows 8→16 bytes (embedded `SongHeader*` widened) | `sound.c:1006-1460`; `m4a.h:47` | **safe** — built and indexed entirely PC-side as a native C array, never read from ROM bytes; accessed only as `gSongTable[song].musicPlayerIndex`. | None — self-consistent stride growth. |
| agbplay core: portable C++20, no inline asm, no `#pragma pack`, no GBA-layout overlays; all ROM reads via byte-wise `ReadU8/U16/U32` | `Rom.hpp:46-66`; `AgbTypes.hpp:3` (`AGB_MAP_ROM=0x08000000` numeric subtraction only) | **portable** — endian/alignment-independent; never assumes 4-byte GBA pointers. `sample` is `{float,float}` (8 bytes), unrelated to GBA layout. | None. |

## 4. REFACTORING STRATEGY (as-implemented + gaps)

The MMIO→software migration is **already complete and structurally clean**; this section documents the as-built layering, then the residual gaps.

**As-implemented (how raw MMIO was replaced):**

1. **Driver excision.** `src/gba/m4a.c` (every `REG_SOUNDCNT*`/`REG_FIFO_*`/`REG_DMA*`/`REG_TM0CNT`/`WAVE_RAM*` write, plus `CgbSound`/`MidiKeyToCgbFreq`) and `asm/lib/m4a_asm.s` (the ARM `SoundMain`/`SoundMainRAM` mixer) are excluded from `tmc_pc` (`xmake.lua:859`, `:1349`). `nm` confirms `SoundMainRAM`/`SoundMain`/`MPlayMain`/`gMPlayJumpTable`/`CgbSound` are absent and `m4aSoundMain`/`MPlayOpen` resolve to the stub addresses.

2. **API re-implementation as bookkeeping.** `port/port_m4a_stubs.c` provides every public `m4a*`/`MPlay*` symbol. These touch **no** GBA register — they update the decomp `MusicPlayerInfo`/`MusicPlayerTrack` fields (status/track-mask/tempo/fade/clock) and forward to the backend. The game's `m4aSoundMain` is deliberately a near-no-op (`info->clock++`, `:269`); the `MPlayOpen:159` `#53` fix iterates `trackCount` rather than a hardcoded 16.

3. **Identity bridging by ROM offset, not struct reinterpretation.** `SongHeaderToRomPos` (`port_m4a_backend.cpp:138`) pointer-subtracts a `SongHeader*` against `gRomData` to recover a raw ROM byte offset, which agbplay re-reads with its own unaligned `ReadU8/U32/ReadAgbPtrToPos`. This is the key design choice that makes the audio bridge immune to the pointer-widening hazards that plague the entity structs — no GBA-layout struct is ever dereferenced across the 32/64-bit boundary for synthesis.

4. **State → agbplay sound-mode mapping.** 32-bit `m4aSoundMode` word (`DA_BIT_8 | FREQ_15768 | masvol 0xf | maxchn 8`) unpacked in `MakeSoundMode` into `MP2KSoundMode` (`rev/masterVol/freq/maxChannels/dacConfig`), and `MakeAgbplayMode` sets accuracy/quality flags. `MP2KContext` rebuilt over live ROM at SDL output rate.

5. **Threading & mix.** `RenderChunkLocked` (`:494`) drives the real `ctx->m4aSoundMain()` on the **SDL audio thread**, then performs a per-track gain/pan software mix to int16. All entry points and the render are serialized by `sStateMutex` (`:101`), replacing the GBA's `ident` soft lock. `Port_Audio_Feed` (`port_audio.c:125`) pulls in ≤1024-frame chunks, post-processes, and `SDL_PutAudioStreamData`s.

**Still raw / stubbed / inaccurate — concrete steps to finish:**

- **Dead FIFO hook.** `gba_write8/32` route FIFO_A/B → `Port_Audio_OnFifoWrite`, which is an empty no-op (`port_audio.c:193`); no compiled code writes FIFO. *Step:* delete the hook and the `port_gba_mem.c:31/138` interception, or comment them as intentional defense-in-depth — they are currently misleading.
- **Latent DMA re-enable hazard.** `port_DmaTransfer` (`macro.h:30`) models neither `DMA_START_SPECIAL` nor repeat. *Step:* no action needed while `m4a.c` is excluded, but if it is ever re-enabled, audio would break silently — keep `m4a.c` excluded and note this in the build comment.
- **Stub fade fields never consumed.** `MPlayFadeOut/FadeIn/FadeOutTemporarily` and `m4aMPlayTempoControl` write only `MusicPlayerInfo` bookkeeping (`fadeOI/fadeOC/fadeOV`) and never reach agbplay. *Step:* forward these to backend calls that drive agbplay's `StartFadeOut`/tempo so engine-issued fades (scripted fade-outs, boss-music tempo ramps) become audible — currently a real behavioral loss vs GBA.
- **[IMPLEMENTED 2026-06-14] `maxChannels=8` honored.** agbplay now parses the m4a DirectSound channel nibble and applies a priority-ordered cap before adding PCM `sndChannels`; lower-priority/releasing voices are culled, while PSG channels remain governed by their existing CGB polyphony logic.

## 5. PC EMULATION-LAYER DESIGN

**Routing of MP2K/PSG/DirectSound state into the mix.** The game calls the unchanged `src/sound.c` API (`SoundReq`→`m4aSongNumStart`, `doPlaySound`→`m4aMPlayVolumeControl`, etc.). `port_m4a_stubs.c` resolves `songId → gSongTable → musicPlayerIndex`, then forwards to `port_m4a_backend.cpp`, which calls `m4aMPlayStart(playerIdx, songPos)` on the `MP2KContext` with a ROM offset it computed itself (the port bypasses agbplay's own song-table scan, configured `POS_AUTO`/`count=0` at `:469`). Inside agbplay:

- **Sequencer:** `SequenceReader::PlayerMain`/`TrackMain`/`cmdPlayNote` walk MP2K bytecode from ROM, dispatch each voice by its bank-data type byte (`SoundData.hpp`) — PCM (DirectSound) → `MP2KChnPCM`, SQ1/SQ2/WAVE/NOISE → `MP2KChnPSG*` — and push them onto the five `std::list` channel pools.
- **PCM (DirectSound):** `MP2KChnPCM` fetches signed 8-bit ROM samples normalized `/128`, runs MP2K ADSR, resamples per channel (SINC), with FIX instruments locked to `fixedModeRate = rateTable[5] = 15768 Hz` (`SoundMixer.cpp:36`).
- **PSG:** square channels use the DC-centered duty LUTs (`CGBPatterns.cpp`) through a BLEP resampler with a float sweep timer; the wave channel reads the 16-byte ROM wave RAM with accurate Ch3 volume snapping/quantization; noise runs a real 15/7-bit Galois LFSR. CGB envelopes step in a 4-bit/frame model with the sustain bug reproduced.
- **Mixdown:** `SoundMixer::Process` sizes a per-AGB-frame buffer (`samplesPerBuffer = 48000/(59.7275·4) ≈ 200` frames, `INTERFRAMES=4`), mixes PCM into per-track buffers, applies the 2-tap comb reverb **to PCM only** (forced level 32), removes `DEAD` channels, master-fades, and sums to `masterAudioBuffer`. The port then **re-mixes the per-track buffers itself** with its own gain/pan in `RenderChunkLocked` rather than draining `masterAudioBuffer`.

**Out through SDL3.** `Port_Audio_Init` (`port_audio.c:152`) opens `SDL_OpenAudioDeviceStream` at **48000 Hz, `SDL_AUDIO_S16LE`, 2 channels**, with the `Port_Audio_Feed` pull callback and `SDL_ResumeAudioStreamDevice`. The backend produces float, clamps to [-1,1], rounds to int16; SDL3's internal stream then converts to the device's native rate/format. The output stage itself does **no resampling** — it is rate-clean. After render it runs the 4-stage DSP chain (DC-block HPF, 16 kHz Butterworth LPF, +20% mid/side widen with mid untouched for mono compatibility, tanh soft-clip) before `SDL_PutAudioStreamData`.

**Comparison to mGBA.** mGBA emulates the *hardware*: it runs the GBA CPU, the real `m4a` driver/ARM mixer execute, and it models the DirectSound FIFO/DAC and PSG registers cycle-approximately — so it inherits the GBA's 8-bit quantization and nearest-neighbour aliasing as a byproduct. agbplay instead emulates the *engine behaviorally*: it re-implements MP2K's sequencer/envelope/instrument logic in float DSP, reading the same ROM song data, and is therefore generally **more faithful to sequence/envelope behavior** (it even reproduces documented MP2K quirks: CGB sustain bug, Ch3 quantization, pseudo-echo) while being **less faithful to the analog signal character** (no 8-bit DAC crush, bandlimited rather than aliased, plus the port's added reverb/widen/LP). Net: more accurate *musically*, less accurate *sonically*, than a hardware emulator.

**Buffer / threading / drift / underrun.** Two-thread model: the **game thread** issues control changes under `sStateMutex`; the **SDL audio thread** advances the sequencer and synthesizes inside the pull callback. There is no hand-rolled ring buffer — buffering is SDL3's internal stream queue plus the backend's `pendingSamples`/`pendingFrameOffset` chunk cache. Tempo drift between 60 Hz and the exact 59.7275 Hz is compensated by `GetBufferLengthSpeedCorrection` (`SoundMixer.cpp:154`), so pitch/tempo are correct (arguably more so than hardware, which had MP2K's slightly-slow 60 Hz assumption). **Underrun handling is delegated entirely to SDL3** — there is no `SDL_GetAudioStreamQueued` watermarking, no latency hint, and no underrun logging; if no chunk is ready the backend zero-fills (`port_m4a_backend.cpp:725`). Control→audible latency is quantized to ~1 buffer (~4 ms / ~200 samples), and the audio clock fully decouples from the video clock during fast-forward/pause.

## 6. PRIORITISED ACCURACY FINDINGS

1. **[HIGH — game-visible, #22] Stuck `IsPlayerActive` probe → BGM duck only lifts on timeout.**
   *Location:* `Port_M4A_Backend_IsPlayerActive` (`port_m4a_backend.cpp:680`, returns `player.playing && !player.finished`); `finished` set only in `SequenceReader::PlayerMain` (`SequenceReader.cpp:126-129`); consumed in `AudioMain` (`src/sound.c:200-208`) with `BGM_DUCK_TIMEOUT_FRAMES=180` (`sound.c:42`).
   *Why:* agbplay leaves a one-shot SFX player marked `playing` past its audible end; `m4aMPlayStop`/`Continue` (`MP2KContext.cpp:182-201`) don't cleanly set `finished`, so `playing && !finished` is not an "is audible" predicate. The jingle measures ~147f but the probe sticks, leaving BGM fully ducked up to 3 s.
   *Fix:* implement the semantics the header doc already claims — under `sStateMutex`, return active iff any track in the player is still `trk.enabled` **or** the player still has live (non-`DEAD`) channels, rather than reading `finished`. This makes the 180-frame backstop redundant. (Note the port also over-ducks: it fades BGM fully to 0 vs the GBA's partial channel-arithmetic duck, and only `SFX_ITEM_GET` ducks — `IsDuckingSfx`, `sound.c:44` — so other GBA channel-stealing jingles won't duck.)
   **[IMPLEMENTED 2026-05-30, `d46c69c36`]** `IsPlayerActive` now returns `playing && any(trk.enabled || trk.activeNotes.any())`. At worst equivalent to `!finished` (a one-shot's `finished` *is* set at `PlayerMain` lines 126-129) and at best fixes the stuck looping/held-jingle case; the 180-frame timeout is retained purely as a backstop.

2. **[CORRECTED 2026-05-30 — the forced reverb is actually INERT, not active.]**
   *Location:* `MakeAgbplayMode reverbForce=32` (`port_m4a_backend.cpp:129`), `MakeSoundMode rev=0x80` (`:106`); `SoundMixer::GetReverbLevel` (`SoundMixer.cpp:184-189`).
   *Correction:* `GetReverbLevel` only honours `reverbForce` when `reverbForce & REV_MASK_SET (0x80)` is set. `32` (0x20) lacks that bit, so the function falls through to `mp2kSoundMode.rev & 0x7F`. And `MakeSoundMode` sets `rev=0x80` → value `0`, with no later `m4aSoundModeReverb` call carrying the SET bit (TMC's sound mode has a zero reverb byte). **Net: reverb is effectively off in both the enhanced and accurate paths, tracking the song exactly as on GBA — the "gentle ~32/255 reverb" comment never took effect.** So this was never a real departure from hardware; no fix needed. The GBA-accurate toggle (#4) sets `reverbForce=0` only for semantic clarity. (If forced reverb is ever *wanted* as an enhancement, it must be `0x80 | level`.)

3. **[IMPLEMENTED 2026-06-14] DirectSound channel cap / voice-stealing.**
*Location:* `MP2KContext::m4aSoundMode` parses max-channel bits into `MP2KSoundMode::maxChannels`; `SequenceReader::cmdPlayNote` enforces the cap before `sndChannels.emplace_back`.
*Why:* GBA culls lower-priority simultaneous PCM voices instead of playing unbounded DirectSound channels; the PC backend now clamps to the configured m4a cap, with 12 as the original driver's hard ceiling.
*Fix:* priority-ordered cull added before PCM voice allocation. Releasing voices are preferred victims, active voices only lose to equal-or-higher priority new notes.

4. **[MEDIUM — by design] Cleaner-than-hardware signal path + no bypass toggle.**
   *Location:* SINC resampling (`MakeAgbplayMode:122-123`); output DSP chain `Port_Audio_PostProcess` (`port_audio.c:89-121`); 48 kHz S16 float mix; absent SOUNDBIAS 8-bit DAC quantization.
   *Why:* removes GBA aliasing "crunch", brick-walls at 16 kHz, widens stereo, soft-clips — all unconditional, no runtime bypass. Largest perceptual departure from hardware.
   *Fix:* add an F8 "GBA-accurate audio" toggle that selects `NEAREST`/`LINEAR` resampling, `reverbForce=0`, and bypasses `Port_Audio_PostProcess` for A/B comparison against mGBA. Also `memset` the HPF/LPF biquad state (`sHp*`/`sLp*`) in `Port_Audio_Reset` (`port_audio.c:189`) — currently it carries stale history across a reset (benign one-buffer transient).
   **[IMPLEMENTED 2026-05-30, `0734c3109`]** F8 → Audio → "GBA-accurate audio". `Port_Audio_SetGbaAccurate` is the single front door: sets the audio-thread flag that bypasses `Port_Audio_PostProcess` and forwards to `Port_M4A_Backend_SetGbaAccurate` (NEAREST resampling, live-mutated + honoured by `MakeAgbplayMode` on rebuild). Biquad history now cleared in `Port_Audio_Reset`. Reverb dimension is moot per finding #2 (already inert). SOUNDBIAS 8-bit DAC quantization deliberately *not* emulated (out of scope; the resampler + DSP bypass are the dominant perceptual terms).

5. **[INVESTIGATED 2026-05-30 — inert for TMC; no action] Engine-driven fades/tempo are dropped, but TMC never drives them.**
   *Location:* `MPlayFadeOut/FadeIn/FadeOutTemporarily` and `m4aMPlayTempoControl` in `port_m4a_stubs.c` write only `MusicPlayerInfo` fields; nothing forwards them to agbplay (`SequenceReader.cpp:98` notes "TODO implement player based fadeout").
   *Why originally flagged:* scripted fade-outs / runtime tempo ramps issued via the m4a API (not via `src/sound.c`'s volume system) would be inaudible on PC.
   *Investigation result:* a full `src/`+`data/`+`include/` sweep found **zero live callers** of any fade function — the only references are in the compiled-but-unused `src/gba/m4a.c`. TMC fades music exclusively through `src/sound.c`'s `volumeBgmTarget` ramp (the path hardened for #22), which works. The only live tempo wiring is `SoundReq`'s `SONG_PLAY_TEMPO_CONTROL` case → `m4aMPlayTempoControl`, but `SONG_PLAY_TEMPO_CONTROL` (`0x800C0000`, `sound.h:593`) has **no sender** anywhere in the tree (no symbolic use, no raw `0x800c`, no script tempo). So the path is never exercised.
   *Why NOT forwarding it:* agbplay's tempo knob is a **single global** `SequenceReader::speedFactor` applied to *every* player (`SequenceReader.cpp:116` — `player.bpm * speedFactor`); per-player tempo is the unimplemented `player.bpmFactor`/line-98 TODO. Forwarding `m4aMPlayTempoControl` to it would scale BGM **and** SFX together — i.e. convert a benign silent-drop into latent *wrong* behavior — for a command the game never sends. Implementing correct per-player tempo would be a sizable agbplay change with zero live payoff.
   *Decision:* **no code change.** Left as a documented latent gap; revisit only if a romhack or future game-code change actually sends `SONG_PLAY_TEMPO_CONTROL`, at which point the right fix is per-player tempo in agbplay, not a global-reader hack.

6. **[LOW — by design] Timing/drift & control-latency from thread decoupling.**
   *Location:* clock-only `m4aSoundMain` (`port_m4a_stubs.c:269`) vs real tick on the audio thread (`RenderChunkLocked:504`); fps compensation `GetBufferLengthSpeedCorrection` (`SoundMixer.cpp:154`).
   *Why:* pitch/tempo are correct and self-correct 60↔59.7275 fps, but `SoundReq` effects are quantized to ~1 buffer (~4 ms) and audio fully decouples from video during fast-forward/pause — frame-tight SFX (footsteps) can drift.
   *Fix:* none required for music; if frame-tight SFX sync matters, drive control changes through a timestamp-ordered queue consumed at buffer boundaries.

7. **[LOW — minor] Noise pitch & DAC/PWM granularity hardcoded.**
   *Location:* noise pitch from continuous key curve (`MP2KChnPSG.cpp:814`) instead of `m4a.c`'s discrete `gNoiseTable` (`:285`); `dacConfig=0x09` hardcoded (`port_m4a_backend.cpp:109`) instead of SOUNDBIAS-derived.
   *Why:* noise SFX pitch and the noise upsample granularity differ slightly from quantized hardware steps; inaudible for stock TMC sound mode.
   *Fix:* optional — map noise pitch through the `gNoiseTable` divisor steps for bit-closer SFX, behind the GBA-accurate toggle from finding 4.

8. **[LOW — latent] Wave channel single-snapshot, vestigial FIFO hook, dead DMA path.**
   *Location:* `MP2KChnPSGWave` takes one 16-byte wave snapshot at note-on (`MP2KChnPSG.cpp:581-585`, no two-bank/mid-note reload); `Port_Audio_OnFifoWrite` no-op (`port_audio.c:193`) + `gba_write8/32` FIFO hook (`port_gba_mem.c:31,138`); `port_DmaTransfer` lacks `DMA_START_SPECIAL`.
   *Why:* none exercised by TMC's static wave voices or by any compiled FIFO write; the DMA gap is masked by `m4a.c` being excluded.
   *Fix:* low priority — remove or clearly comment the vestigial FIFO hook; document the DMA-re-enable hazard in the build comment near `xmake.lua:859`. No action needed for the wave snapshot unless a non-static wave voice is introduced.

## 7. AUDIO-QUALITY ENHANCEMENTS (shipped 2026-05-31)

Driven by a 5-lens design panel (spatial/reverb, tonal-EQ, stereo-imaging,
dynamics/loudness, purist-minimal), each proposal adversarially critiqued, then
synthesized. Guiding principle: **default-on-launch output stays byte-identical
to before** — every audible change is either a fix to an objective defect or an
opt-in control defaulted to the current value. All apply to the *enhanced*
(toggle-OFF) path only; GBA-accurate mode stays raw.

**Shipped:**
- **C1-continuous tanh soft-clip** (`1c2be2eb4`, `port_audio.c`). The atan bend had
  slope 1.0 below the 25000 knee but 2/π≈0.637 above it — a transfer-curve corner
  that injects odd harmonics (edgy fizz) on loud lead notes. `room*tanh(over/room)`
  matches slope 1 on both sides, asymptotes at exactly 32767, byte-identical below
  |25000|. The one always-on change; isolated to a single variable for A/B.
- **Click-free GBA-accurate toggle** (`1c2be2eb4`). `Port_Audio_ClearFilterState`
  (single source of truth), RELEASE/ACQUIRE on the flag, biquad history cleared on
  the false→true edge so re-engaging the chain doesn't click/ring.
- **F8 → Audio "Stereo width" slider** (`ebea05a24`), [1.00,1.50], default 1.20
  (= shipped). Mid never scaled → mono fold-down invariant. Read once/call, memoryless.
- **F8 → Audio "Reverb" slider** (`d7c394fae`), 0..24, default 0 (= dry). Engages the
  previously-inert forced reverb correctly (`0x80|level`), PCM-only (chiptune leads
  stay dry by mix order), applied **live** via `SoundMixer::UpdateReverb()` (no song
  restart — the live path the design synthesis had missed), persists across rebuilds,
  byte-identical at level 0.

**Deliberately REJECTED (do not re-propose without a listening test + a gate):**
- **Low-shelf "warmth" bass boost** — TMC has little real low end by design; a shelf
  amplifies synthesis noise/DC residue and pushes bass-heavy panned notes past the
  25000 knee (#115-class squash). No bass boost; the 30 Hz DC-blocker stays.
- **High-shelf de-tilt** — would dull TMC's deliberately bright top; taste-level
  character change with no objective defect behind it. Defer; gate if ever revisited.
- **Bus/glue compressor or limiter** — would pump on the dense ~60 Hz transient
  stream and smear staccato PSG snap; there's no headroom to compress into (upstream
  float mix is pre-bounded). The tanh soft-clip is the *only* dynamics stage by design.
- **Even-harmonic (DC-bias) warmth** — the textbook way to *muddy* a bright chiptune;
  fights the DC-blocker. Wrong tool for the source.
- **Output-stage reverb** — would reverberate the summed mix (dry PSG leads + SFX +
  UI blips); reverb MUST stay synth-side (PCM-only) where selectivity is free.
- **True-peak ceiling drop / widen makeup-gain trim** — both permanently soften
  loudness for the majority (already-48 kHz devices / near-mono content) to fix edge
  cases TMC's centre-clustered mix essentially never produces.
- **reverbType GS1/GS2/MGAT** — the hall algorithms are far too wet/smeary; NORMAL's
  short comb is the restrained, idiomatic MP2K choice.
- **Removing the dormant per-track pan stage in `RenderChunkLocked`** — correct and
  retained for romhack panpot; commented, not deleted, to avoid a future double-pan.
