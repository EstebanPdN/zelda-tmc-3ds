# Old 3DS Performance — Handoff

Branch `perf/old3ds-performance`, worktree `.worktrees/old3ds-performance`.
Branch is unmerged; `main` has none of this. The code is one commit on base
`3415b6235`: **`e753a32c5`** (25 modified files, 8 new sources, ~3600
insertions), with this file and the parity notes committed on top. The five host
gates below pass at `e753a32c5`, and every figure in this file was measured on
that tree.

The detailed engineering log — every trap below with full derivations — is in
`docs/old3ds-pica200-parity-notes.md`. This file is the summary.

## State

| | value | from |
|---|---|---|
| Frame rate | **54.20 FPS** (busy scene 49.25) | was 49.10 |
| Audio mix CPU | **0.377 ms/buffer** | was 1.785 (−79%) |
| Underruns | **1 / 9902** | was 8 / 2422 |
| Frames missing VBlank | **10.1% good / 21.4% busy** | the 60 Hz blocker |
| Bottom paint | 13.2 ms avg, 77.6 ms max, 1370 paints | max was 88.8 |
| Core split | core 0 ~8.5 ms, core 1 ~2.65 ms | 76 / 24 |

**The measured numbers above are not what a default build does.** They come from
a console config of `gpu_renderer=1 audio_dsp=1 audio_dsp_pcm=1 bottom_core=1`.
In `port_config_3ds.c` only `gpu_renderer` defaults on; `audio_dsp`,
`audio_dsp_pcm`, `gpu_static_quad`, `bottom_rgb565` and `gpu_short_vertices`
default **off** and `bottom_core` defaults to `-1` (core 0). Build, run without
that file, and you measure the software audio path on core 0 — a different
system from the one this table describes. Check the flags in `info.txt` before
comparing any dump against these figures.

## Which docs to trust

Three files on `main` — untracked there, written 2026-08-20 before any hardware
data existed — describe this same work and are wrong in ways that will send you
down dead ends: `docs/OLD3DS-OPTIMIZATION-REPORT.md` and the
`2026-08-20-old3ds-full-optimization-{plan,design}` pair under
`docs/superpowers/`. Specifically:

| their claim | what hardware said |
|---|---|
| audio mix is 10.34 ms/buffer, the dominant cost | 1.785 ms measured, now 0.377. Underruns were core-1 contention with the bottom painter, not mix cost. |
| bottom screen → RGB565 saves 71% | painter writes ABGR and the TEV reads alpha-as-red; RGB565 has no alpha, so the screen comes out solid red. Painter is compute-bound at ~30 MB/s regardless. |
| static VRAM quad: 47,612 → 4 vertices | true, implemented, and worth 0.2–0.3 ms at most, because citro2d still draws the bottom screen and flushes its vertices either way. Off by default. |
| offload 24 voices, delete agbplay's mixer | NDSP outputs 32728 Hz; the rate ceiling keeps most CGB voices in software by design. Deleting the mixer recovers 0.377 ms. |
| the blocker is CPU load | the blocker is variance. Worst scene uses 70% of the budget with a whole core idle; the loss is a 234.9 ms render tail plus 81 debt clamps. |

Trust instead: this file, `docs/old3ds-pica200-parity-notes.md`, and
`docs/superpowers/{specs,plans}/2026-08-18-old3ds-pica200-ppu-renderer*`.

## Measure before you build

The emulator cannot exhibit stalls, cache staleness or ordering faults — it can
falsify a change, never confirm one. Treat any claim without a dump as a
hypothesis. Two bugs shipped today specifically because changes were stacked
without a measurement in between.

- Build: `bash platform/3ds/build.sh` (devkitARM env; prints `Ready:`).
- Host gates, all five green at `e753a32c5`: `port_ppu_gpu_3ds_model_test`,
  `port_second_screen_slicemap_test` (7,274,904 pixel mappings identical),
  `old3ds_frame_pacer_test`, `platform_gpu_layout_3ds_test`,
  `mode1_native_fast_path_test` (4096 deterministic scenes). Plus
  `port_ppu_gpu_3ds_bench <dump-dir>`, which needs a dump and reports timings
  rather than passing.
- Two ways to get `error: invalid argument: <target>` and they look identical.
  Run from a worktree without `-P .` and xmake resolves the **main repo's**
  `xmake.lua`, where these targets do not exist. Pass more than one target to
  one `xmake build` and it rejects the second. So: one target per invocation,
  always `-P .` — `for t in ...; do xmake build -P . -y "$t" && ./build/pc/$t; done`.
- FTP `192.168.1.48:5000`, gone while the game runs — a failed listing means
  "not yet", never "nothing there". Verify uploads by SHA-256; the ftpd
  truncates transfers and still reports success.
- Dumps: `/3ds/The Minish Cap 3DS/dumps/<dump-*>/info.txt`. **Not** `/3ds/tmc3ds/`.

## In flight — deployed, unmeasured

One run with an F8 dump settles all three.

1. **Bottom painter on core 1** (`bottom_core=0→1`). It cannot preempt the main
   thread on core 0, so it only gets the 9.8 ms VBlank idle while needing
   13.2 ms, and spills across the boundary. Core 1 is ~2.5% since audio left.
   Busy scene should go 13.80 → 11.59 ms against a 16.67 ms budget. **This is
   the direct test of the 60 FPS hypothesis.**
2. **BlitMapRegion fixed-point** (`port_second_screen.c`). Removed a per-pixel
   VFP add + float→int convert on the default MAP tab. Also a correctness fix:
   over 3.5 M drawn pixels the new path matches an exact double mapping on 100%,
   the float accumulation it replaced was wrong on 0.126%.
3. **Painter thread creation retries** (`platform_3ds.c`). A latched bool meant
   one transient `threadCreate` failure sent every paint to the synchronous
   main-thread path permanently and silently.

## The 60 Hz blocker is variance, not load

| scene | work/frame | % of 16.67 ms | frames taking 2 periods |
|---|---|---|---|
| busy (49.25 FPS) | 11.59 ms | 70% | 21.4% |
| good (54.20 FPS) | 8.52 ms | 51% | 10.1% |

Even the worst scene measured leaves 30% headroom. An entire core is idle, the
GPU never stalls the CPU (0 begin-failures / 8222 frames), and game logic is
0.66–1.92 ms. The loss is the tail: main-thread render max **234.9 ms**, PPU
render 86.5 ms, paint 77.6 ms — and 81 debt clamps mean that time is never
repaid.

Measured paint phases (instrumentation added this session, printed every dump):
`panel 6.871/35.24`, `backdrop 3.892/6.34`, `tabbar 2.356/9.43`,
`sidebar 1.901/25.25` (avg/max ms).

## Traps

- **The repaint tick is self-referential.** `sBottomWorkerTick = sBottomTick++`
  is inside `if (schedulePaint)` (`port_ppu_3ds.c:1100`) and every MAP animation
  derives from it. A skip signature quantising `tick` freezes the tick, so the
  animation dies *permanently*. Drive from a free-running counter.
- **MP2K's PSG `freq` is the pattern-fetch rate, not the tone.** Multiplying by
  the period length played squares three octaves sharp, wave five.
- **PSG voices leaked their hardware channel on destruction.**
  `~MP2KChnPSG() = default` plus `channels.clear()` under the unconditionally
  forced `MONO_STRICT` meant essentially every repeated CGB note leaked a slot.
- **NDSP will not play far above its 32728 Hz output.** Noise `freq` is the LFSR
  clock (to 524288 Hz) — a 16× ratio, which produced continuous static. Fixed
  with decimated tables; the LFSR periods are odd so power-of-two decimation
  keeps the full period.
- **Offloaded voices bypass master volume and fades**, which the software path
  applies to track buffers after mixing.
- **Do not cache the backdrop.** At 3.9 ms it is bandwidth-bound on the write;
  caching would read 300 KB *and* write 300 KB — strictly worse.
- **Shared `port/` code also builds for Android**, where `w ≈ 2049`. A proposed
  `int16_t[320]` per-call table would have smashed the stack.

## Next, ranked

1. **Take one dump.** Blocks everything else.
2. **Hunt the 234.9 ms render spike.** Unexplained; 14 lost frames each.
   Instrument atlas flush (3934 calls, 187 MB), GSP contention, first-touch.
3. **MAP-tab repaint skipping.** 1370 paints, zero skipped, ~12% of a core.
   Design settled — quantised animation terms + snapshot memcmp + forced repaint
   every N ticks. Prefer bounded staleness to a complete signature: 200 inputs
   were enumerated, several are live engine globals, and a missed one freezes
   the screen.
4. **Region-zoom `calloc` inside a paint** — 256 KiB + 65536-px decode on tap.
5. **Split the map build across cores** (~1.1 ms) only if the painter move is
   insufficient. Structurally ready; the tile cache is the shared resource.
   Pipelining moves ~3 ms but costs a frame of input latency.

Deliberately skipped: deleting agbplay's mixer (recovers only 0.377 ms),
top-screen RGB565 and the VRAM present quad (report items written before
hardware data; measurements do not support them), and the last 340 CGB
rate-declines (uncorrectable without changing timbre, worth ~nothing).
