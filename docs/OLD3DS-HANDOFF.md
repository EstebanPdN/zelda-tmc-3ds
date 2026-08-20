# Old 3DS Performance — Handoff

Branch `perf/old3ds-performance`, worktree `.worktrees/old3ds-performance`.
Branch is unmerged; `main` has none of this. Base `3415b6235`, then 34 commits.
`e753a32c5` is the tree the early figures were measured on; everything after it
is this session.

**devkitARM is installed at `/home/sian/devkitpro-root/opt/devkitpro`** (not
`/opt/devkitpro`; a previous session used `/tmp/dkp-root`, which `/tmp` cleanup
then deleted). Build with:

```sh
DEVKITPRO=/home/sian/devkitpro-root/opt/devkitpro bash platform/3ds/build.sh
```

gcc 16.1.0. `makerom`/`bannertool` are absent so no CIA is produced; the 3DSX is
what the console boots. Two non-obvious packages: `devkitarm-crtls` ships
`3dsx.specs` without which cmake's compiler probe fails, and `citro3d`/`citro2d`
are separate.

The detailed engineering log — every trap below with full derivations — is in
`docs/old3ds-pica200-parity-notes.md`. This file is the summary.

## State

Last long run: `dump-20260820-222226`, 6812 presented frames, ~128 s. It carries
the audio cache fix but **not** the four frame-path fixes that followed it.

| | value | note |
|---|---|---|
| Frame rate | **53.03 FPS** | target 59.73 (GBA); 59.83 is the LCD ceiling |
| Frame interval | 18.858 ms | per *logic* iteration 17.733 ms vs 16.7427 target |
| Engine logic cadence | **55.81 ticks/s** | the loop itself, not just presentation |
| Adaptive skips / debt clamps | 432 / 79 | caused by the 60 Hz pacer bug, now fixed |
| Audio render | **0.841 ms/buffer** = 5.4% of a core | was 5.367 ms = 34.3% |
| Underruns / deadline misses | **0** / 26 | was 2 / 241 |
| Pump (aptMainLoop + audio) | 0.255 ms avg, 18.2 ms max | `aptMainLoop` is 0.003 of it |
| Bottom paint | 377 paints, **758 static skips** | MAP-skip working; irrelevant to FPS |
| Top presentation | 2.033 ms avg, **196.0 ms max** | frequency unknown until next dump |

`vblank_phase_lock` is 0: it lost its A/B (54.61 -> 53.03 FPS, frames over two
periods 1.07% -> 2.83%).

**These numbers are not what a default build does.** The console config is
`gpu_renderer=1 audio_dsp=1 audio_dsp_pcm=1 bottom_core=1 bottom_map_skip=1
audio_dsp_interp_linear=1 speaker_eq=1 speaker_eq_hz=280.0 gpu_static_quad=1`.
In `port_config_3ds.c` most of those default off. `SaveConfig` now round-trips
all of them — it used to parse but not write them, so any settings change or
exit silently erased the config of the run being measured. Check the flags in
`info.txt`, not the ini, before comparing any dump against this table.

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

## The target is 59.7275 Hz, not 60 -- and the pacer was wrong

The GBA draws a frame every 280896 cycles of a 16.777216 MHz clock: **59.7275 Hz
/ 16.7427 ms**. The 3DS LCD delivers ~59.83 Hz / 16.7151 ms. Nothing runs at 60.

`old3ds_frame_pacer.c` had `OLD3DS_TARGET_HZ = 60`, a 16.6667 ms period shorter
than *both*. So a perfectly on-time frame was booked as 0.048 ms late, and the
bias is one-directional by construction: credit is capped at one period while
debt accumulates to four. It reached the skip threshold roughly every 310 frames
and forced a presentation skip for no reason -- 432 adaptive skips and 79 debt
clamps on an otherwise healthy loop. The period is now derived from the GBA's
own timing; the bias flips to -0.0276 ms/frame of (clamped) credit.

**Ceiling is 59.7 FPS. 59.83 is the hard LCD limit.**

## No service IPC on the frame or audio path

Four instances of one defect were found in a single session, each a blocking
sysmodule round trip on a hot path, and in every case a local alternative
already existed in-tree. `APT_SetAppCpuTimeLimit(80)` amplifies all of them by
starving the very sysmodule being waited on.

| call | where | cost | replaced with |
|---|---|---|---|
| `DSP_FlushDataCache` | once per audio buffer | audio **34.3% -> 5.4%** of a core; underruns 2 -> 0; deadline misses 241 -> 26 | `Platform3DS_CleanDataCache` (`svcStoreProcessDataCache`) |
| `GSPGPU_FlushDataCache` | once per presented frame (bounded C2D flush) | ~0.33 ms/frame | same helper |
| `DSP_GetHeadphoneStatus` | once per frame (speaker EQ) | 0.244 ms/frame; ~8.2 ms per call | `osIsHeadsetConnected()` (shared-config read) |
| `fopen`/`fwrite`/`fclose` | per 120 frames, *inside* the presentation span | est. 0.17-0.42 ms/frame, and the likely source of the ~196 ms presentation maximum | gated behind `frame_log`, default off |

`platform_3ds.c:638` had measured the GSP round trip at ~330 us and documented
the fix years before three of these four were found. Grep for service calls
before adding anything to the frame path.

## Statistics that measured nothing

Three separate figures were quoted as evidence and none of them meant what they
said. Check the derivation before trusting a counter here.

- **`Frames over 16.67 ms`** used `intervalTicks * 60 > ticksPerSecond`. The LCD
  period is 16.7151 ms, so *every on-time frame counted as over* and the figure
  read ~80%. Now compared against the real GBA period and relabelled
  `Frames over 1 / 2 GBA periods`.
- **`platform_gpu_layout_3ds_test: PASS`** asserts 512x256 for *both* profiles,
  because `PlatformGpu3DS_GetUploadLayout` is a stub with `(void)old3dsProfile`.
  The compact Old 3DS upload surface from the 2026-08-17 plan was never
  implemented. Worth ~0.05 ms/frame; carries bottom-screen corruption risk.
- **`Promote/input after wait`** average is polluted in any second-of-pair dump:
  the *first* dump's quick-dump SD write costs ~17.7 s, which amortises to
  ~2.6 ms/frame and looks like a per-frame cost. It is not.

## Falsified this session

Kept because re-deriving them costs another hardware run each.

- **Bottom painter is not the limiter.** Three independent interventions --
  core migration, 2.58x fewer paints via MAP-skip, and the mixdown rewrite --
  each moved the frame rate by exactly 0.00.
- **`vblank_phase_lock`** (waiting for the next VBlank rather than accepting a
  pending one): 54.61 -> 53.03 FPS, and frames over two periods went 1.07% ->
  2.83%. Reverted to 0.
- **The audio post-mix arithmetic was not the cost.** A bit-exact rewrite of the
  gain/pan/quantise stage produced no measurable change, because the ~5 ms was
  the per-buffer `DSP_FlushDataCache` IPC, not arithmetic. Real compute is
  ~0.75 ms/buffer.
- **The 196 ms presentation spike is not a quick-dump artifact.** `205654`
  reports `quick dump 0` and already shows a 171.6 ms maximum.

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

## The run reports itself now -- read the log, not just dumps

A run reaching frame 11160 happened with no quick dump written, losing that
whole session. So every 1800 frames (~30 s) the build appends one line to
`tmc3ds.log`:

```
[tmc3ds] CADENCE f=N fps=X logic=X interval=Xms skips=N clamps=N
         lostVblank=N/N over1=N over2=N xfer=X/Xms n=N
```

One SD write per 30 s is ~0.017 ms/frame amortised, three orders below the
per-120-frame line it replaced (`frame_log`, now default off). Fetch
`tmc3ds.log` over FTP; no L+R+A needed.

## Emulator reference: the pacing machinery is sound

Azahar 2126.0, `is_new_3ds=false`, current build, `compact_upload=1`:

```
CADENCE f=7200 fps=59.48 logic=59.55 interval=16.810ms skips=35 clamps=6
        lostVblank=0/7199 over1=15 over2=9 xfer=0.029/0.029ms n=1201
```

Timings are NOT hardware-representative -- Azahar's GPU completes instantly, so
`xfer=0.029ms` says nothing about the real synchronous transfer. What it does
establish is structural:

- **`skips=35` over 7200 frames (0.49%)** against hardware's 432/7244 (6.0%).
  With no real overruns present the pacer barely skips, which supports the
  corrected reading that most hardware skips are genuine frame overruns rather
  than the phantom-debt bug (that was only ~70 of them).
- **`lostVblank=0/7199`** -- the counter runs and reads zero when nothing is
  missed, so a non-zero value on hardware means something real.
- FPS converges upward, 59.17 -> 59.38 -> 59.48, interval closing on 16.7427 ms.

So the pacing and skip machinery can deliver ~59.5+ when the CPU is not the
constraint. Whether the ARM11 keeps frame work under the period often enough is
the only remaining unknown.

`compact_upload=1` is also **visually verified** here: both screens render
correctly at 272/320 pitch over 577 bottom transfers, and a pitch mismatch would
shear the image diagonally. It is safe to leave enabled.

## The whole goal reduces to two numbers: `over1` and `skips`

The emulator reported `lostVblank=0/7199` with `interval=16.810 ms`. Together
those mean the loop is vblank-locked: when it misses nothing, its interval *is*
the display period. Azahar emulates ~59.49 Hz, which is exactly why
`logic=59.55` there -- an emulator property, not a code shortfall.

Apply that to real hardware:

| | period | rate |
|---|---|---|
| real 3DS LCD | 16.7151 ms | **59.83 Hz** |
| GBA logic target | 16.7427 ms | 59.73 Hz |

**The display is faster than the target.** A vblank-locked loop that misses
nothing presents at 59.83 Hz, above 59.73. The pacer then only needs to skip
~0.16% of frames to hold logic at the GBA rate.

So success is not "shave N ms of work" -- work is already 7.8 ms of a 16.74 ms
period. It is:

- **`over1` small** -- presented intervals exceeding one GBA period
- **`skips` ~= 0.2%** (hardware baseline: 432/7244 = 6.0%)

**Do not use `lostVblank` as the criterion.** With `vblank_phase_lock=0` the
wait is `gspWaitForEvent(VBlank0, nextEvent=false)`, and `false` means an
already-pending VBlank satisfies it immediately. So a frame whose work crosses a
boundary makes the *next* wait return in ~0 ms rather than blocking for another
period: waits are either ~0 or ~(period - work) and essentially never exceed one
period. `lostVblank` therefore reads ~0 whether or not frames are being lost,
and the emulator's `0/7199` would look identical on an unhealthy loop. It is
still worth emitting -- a non-zero value means a genuine pathology -- but it
cannot confirm health. `over1` is the counter that measures the failure, and it
only became trustworthy once its threshold was corrected from 16.667 ms to the
real GBA period.

Every remaining lever -- `compact_upload`, `bottom_core=0`, the async bottom
transfer -- aims at the same thing: stop individual frames overrunning the
period. Nothing else moves the number.

## Next, ranked

**1. Take one long dump.** Five changes are deployed and unmeasured, and the
whole path to 59.7 rests on estimates below the first row. Play ~4 minutes, then
dump twice -- a 1800-frame run is warm-up noise (`engine work` reads 1.70 ms in
short runs against 0.588 ms in long ones).

The accounting, measured baseline plus estimates:

| step | ms/iter | Hz |
|---|---|---|
| measured baseline (`dump-20260820-222226`) | 17.733 | 56.39 |
| - headphone IPC | 17.489 | 57.18 |
| - per-frame C2D GSP flush | 17.159 | 58.28 |
| - static quad presenter (`gpu_static_quad=1`) | ~16.909 | ~59.14 |
| - periodic SD log (`frame_log` off) | ~16.659 | ~60.03 |
| **GBA target** | **16.7427** | **59.73** |

Only the first row is observed; the rest are derived.

**The pacer fix is worth far less than first claimed.** An earlier revision said
the 432 adaptive skips "cost 3.4 FPS on their own", which assumed all of them
were phantom. They were not. Phantom debt accrued 0.0484 ms/frame against a
15.068 ms skip threshold, so it took ~311 frames to force a skip: over 7244
frames that is ~23 bursts of at most 3 consecutive skips, i.e. **~70 phantom
skips, and ~362 caused by genuine frame overruns**. The pacer fix alone is worth
about **+0.5 FPS**, not 3.4.

That makes presented FPS depend on how many real overruns the 1.07 ms/frame of
work removal eliminates:

| real overruns remaining | presented FPS |
|---|---|
| all 362 | ~57.0 |
| half | ~58.5 |
| none | ~60.0 |

**Reaching 59.73 needs the real overruns cut by ~85%.** Logic cadence should
reach ~60 Hz regardless; presented cadence is the one at risk, and the gap
between the two is exactly `adaptive-skipped presentations`.

What the dump decides: `waits exceeding 1 / 2 GBA periods` (is the residual gap
missed vblanks at all -- the interval counters and the wait arithmetic currently
disagree), `Measured engine logic cadence` (55.81 -> needs ~59.7),
`adaptive-skipped presentations` and `debt clamps`, and the `presentation` and
`PPU render spans over 4/16/50 ms` buckets -- the first frequency data on the
196 ms and 86.8 ms maxima. If `over 50 ms` is near zero, the SD log was the
spike.

**2. If the spike survives:** `C3D_FrameBegin(0)` blocks on the GX queue with no
timeout (`platform_gpu_3ds.c:479`) and GSP retires that queue on core 1 with the
app's 20% quota. Test by moving the painter off core 1 and watching the *spike
count*, not the average -- `bottom_core` is neutral for the average, which is a
different question.

**3. Residual audio-pump tail.** Average is fixed (8.308 -> 0.255 ms) but the
maximum is still ~18 ms. `pump split` already attributes it to the audio pump,
not `aptMainLoop` (0.003 ms).

**4. NDSP buffer geometry.** 8 x 256-frame wavebufs with 4.9% queue-recovery
churn. RetroArch uses a single 2048-frame looping wavebuf with zero requeues,
mGBA 1280x4. Fewer, larger buffers means fewer flushes and fewer wakeups.

**5. PPU `maps` build, 2.2 ms of a 2.9 ms frame build.** Largest remaining
per-frame CPU item, but the loop is not work-bound (~7 ms of a 16.74 ms period),
so this buys headroom rather than frame rate.

**6. Compact Old 3DS upload surface.** Never implemented; the contract is a stub
and its test asserts the stub. ~0.05 ms/frame, with bottom-screen corruption
risk if the pitch and the painter disagree.

Deliberately skipped: deleting agbplay's mixer (~0.31 ms/buffer and it is the
parity reference), top-screen RGB565 (falsified -- painter writes ABGR and the
TEV reads alpha as red), the last 340 CGB rate-declines (uncorrectable without
changing timbre), `bottom_core` (measured neutral), and Thumb compilation
(ARMv6K predates ARMv6T2, so `-mthumb` is Thumb-1 with no conditional
execution -- verified against the local toolchain).
