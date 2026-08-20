# Old 3DS Performance — Handoff

Branch `perf/old3ds-performance`, worktree `.worktrees/old3ds-performance`.
Branch is unmerged; `main` has none of this. Two code commits on base
`3415b6235`:

- **`e753a32c5`** — the measured state (25 modified files, 8 new sources,
  ~3600 insertions). Every figure in this file was measured on this tree.
- **`fe4077478`** — MAP-tab repaint skipping, added after that dump.
  Host-tested only; **not yet run on hardware**, so no figure here reflects it.

All eight host gates pass at `fe4077478`. The 3DS link is unverified in this
checkout: there is no devkitARM here (`/usr/bin/arm-none-eabi-gcc` is bare-metal
with no libctru), so `platform/3ds/build.sh` cannot run and the two 3DS-only
translation units changed by `fe4077478` — `port_ppu_3ds.c` and
`port_second_screen_3ds.c` — have never been through a compiler. Build before
trusting them.

The detailed engineering log — every trap below with full derivations — is in
`docs/old3ds-pica200-parity-notes.md`. This file is the summary.

## State

From `dump-20260820-200845` (13,376 presented frames, ~244 s), the run that
finally measured the core-1 painter move.

| | value | from |
|---|---|---|
| Frame rate | **54.89 FPS** | was 49.10 |
| Average frame interval | **18.217 ms** vs 16.67 target | **the whole deficit is 1.55 ms/frame** |
| Frames over 16.67 / 33.33 ms | 10270 (76.8%) / 135 (1.0%) | almost all frames are *slightly* over |
| Audio mix CPU | **0.354 ms/buffer** | was 1.785 (−80%) |
| Underruns | **2 / 15864** | was 8 / 2422 |
| Audio deadline misses | 714 (5.3% of buffers) | steady ~5.5% in every dump |
| Bottom paint | 11.679 ms avg, 85.079 max, **2230 paints, 0 skips** | was 13.2 avg |
| Main-thread render max | 213.2 ms, of which **top presentation 197.6 ms** | the spike, now localised |
| Debt clamps | 127 | was 81 |

Scene-dependent good/busy splits are gone from this table: this dump is a
single 4-minute run, so the numbers are one mixed average rather than two
hand-picked scenes.

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
- Host gates, all eight green at `fe4077478`: `bottom_map_anim_3ds_test`,
  `bottom_frame_state_3ds_test`, `bottom_idle_3ds_test`,
  `port_ppu_gpu_3ds_model_test`, `port_second_screen_slicemap_test` (7,274,904
  pixel mappings identical), `old3ds_frame_pacer_test`,
  `platform_gpu_layout_3ds_test`, `mode1_native_fast_path_test` (4096
  deterministic scenes). Plus `port_ppu_gpu_3ds_bench <dump-dir>`, which needs a
  dump and reports timings rather than passing.
- Two ways to get `error: invalid argument: <target>` and they look identical.
  Run from a worktree without `-P .` and xmake resolves the **main repo's**
  `xmake.lua`, where these targets do not exist. Pass more than one target to
  one `xmake build` and it rejects the second. So: one target per invocation,
  always `-P .` — `for t in ...; do xmake build -P . -y "$t" && ./build/pc/$t; done`.
- FTP `192.168.1.48:5000`, gone while the game runs — a failed listing means
  "not yet", never "nothing there". Verify uploads by SHA-256; the ftpd
  truncates transfers and still reports success.
- Dumps: `/3ds/The Minish Cap 3DS/dumps/<dump-*>/info.txt`. **Not** `/3ds/tmc3ds/`.

## Measured: the painter move is falsified

The three in-flight changes are now measured. The A/B is clean because the
console config was rewritten at 20:03, so `dump-20260820-195959` is the last
run without the painter move and `dump-20260820-200845` the run with it.

| | 195959 (`bottom_core=0`) | 200845 (`bottom_core=1`) |
|---|---|---|
| FPS | 54.99 | 54.89 |
| Bottom paint avg | 11.690 ms | 11.679 ms |
| Audio deadline misses | 657 / 11799 frames | 714 / 13376 frames |

**Moving the painter to core 1 changed nothing.** Not a small win — no win. The
prediction in the previous revision of this file (13.80 → 11.59 ms, and 60 FPS
following from it) was half right and wholly misleading: the painter *does* cost
about what was predicted, and the frame rate did not move. So the painter was
never blocking on core affinity. Do not re-litigate `bottom_core`; it is a
neutral knob.

`BlitMapRegion` fixed-point and the thread-creation retry are both in and
neither regressed anything. The slicemap fixed-point is visible and real: the
`panel` paint phase went **6.871 → 3.504 ms average**, almost exactly halved.

## The 60 Hz blocker: 1.55 ms/frame, and a 197 ms spike in presentation

The framing in earlier revisions — "variance, not load", scenes leaving 30%
headroom — was built on scene-picked dumps and overstated the problem. The
single-run number is simpler: **average frame interval 18.217 ms against a
16.67 ms target.** 76.8% of frames are over budget but only 1.0% take two full
periods. This is not a variance problem with a few catastrophic frames; it is a
broad, shallow **1.55 ms/frame** overrun.

That reframes the target. 1.55 ms is a small enough number that the bottom
painter alone covers it: 2230 paints x 11.679 ms = 26.0 s over a 244 s run, or
**1.95 ms per presented frame**, with **0 skips**. Hence the MAP-tab work below.

Separately, the spike is now localised. In every dump, `Top presentation CPU
work` maximum tracks `Main-thread render/presentation` maximum within 3–8%:

| dump | main-thread render max | top presentation max |
|---|---|---|
| 174625 | 218.791 ms | 214.323 ms |
| 185752 | 177.339 ms | 170.799 ms |
| 195959 | 219.647 ms | 216.781 ms |
| 200845 | 213.246 ms | 197.601 ms |

So ~90–97% of the 200 ms stall is inside top presentation, not PPU render and
not the painter. It is also reproducible across every run, including the old
49 FPS ones. Instrument *inside* presentation next; the atlas flush (5284 calls,
220 MB) and GSP contention are the candidates, but they are candidates, not
findings.

Paint phases from 200845 (avg/max ms): `backdrop 4.967/13.820`,
`panel 3.504/34.721`, `tabbar 2.129/11.230`, `sidebar 1.869/10.471`. Backdrop is
now the largest phase, and it went *up* (3.892 → 4.967) while panel halved.

## Traps

- **The repaint tick was self-referential — now fixed, keep it that way.**
  `sBottomWorkerTick = sBottomTick++` used to sit inside `if (schedulePaint)`
  and every MAP animation derives from it, so any skip signature froze the tick
  and killed the animation *permanently*. `port_ppu_3ds.c` now advances
  `sBottomTick` on the cadence instead, whether or not a paint follows.
  `bottom_map_anim_3ds_test` pins the hazard with a loop that never paints again
  when fed a paint-gated tick.
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

1. **Measure the MAP-tab skip on hardware.** Implemented this session and host-
   tested, never run on a console — and this repo's own rule is that the host can
   falsify but not confirm. Expect paints to drop from 2230 to roughly 420 on
   overworld (5.33x) and 840 in dungeons (2.67x). What to check in `info.txt`:
   `static skips` must stop being 0, `Bottom paint scheduling` should show the
   drop, and the MAP screen must still animate — the player dot blink, the
   dungeon room-palette rotation, and the region bracket retiring after a tap.
   **A frozen bottom screen is the failure mode**; if it happens, the signature
   missed an input and the first suspects are the three live engine globals named
   in the parity notes.
2. **Instrument inside top presentation.** The 200 ms spike is ~90–97% there and
   reproduces in every dump. Atlas flush (5284 calls, 220 MB) and GSP contention
   are candidates; nothing is confirmed. Worth ~14 lost frames per occurrence.
3. **The backdrop is now the largest paint phase** at 4.967 ms and it grew from
   3.892. Find out why it grew before optimising it — and note the standing trap
   below that caching it is strictly worse.
4. **Audio deadline misses: 714, a steady ~5.3–5.6% across every dump.** Only
   2 underruns, so it is not yet audible, but `Audio render` averages 6.002 ms
   while the mix is 0.354 ms — 5.6 ms per buffer is unaccounted for and nobody
   has looked.
5. **Region-zoom `calloc` inside a paint** — 256 KiB + 65536-px decode on tap.
6. **Split the map build across cores** (~1.1 ms). Structurally ready; the tile
   cache is the shared resource. Pipelining moves ~3 ms but costs a frame of
   input latency.

Deliberately skipped: deleting agbplay's mixer (recovers only 0.354 ms),
top-screen RGB565 and the VRAM present quad (report items written before
hardware data; measurements do not support them), the last 340 CGB
rate-declines (uncorrectable without changing timbre, worth ~nothing), and
`bottom_core` (measured neutral — see above).
