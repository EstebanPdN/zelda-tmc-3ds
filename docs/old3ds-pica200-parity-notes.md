# Old 3DS PICA200 PPU — parity and performance notes

Companion to `docs/superpowers/specs/2026-08-18-old3ds-pica200-ppu-renderer-design.md`.
Records what the emulator-based testing established about the GPU PPU path, and
the two limits that shape the remaining work.

## Test harness (emulator)

Azahar 2126.0 runs the 3DSX and reproduces the Old 3DS profile well enough to
exercise the whole GPU path, including the parity harness.

```sh
# Emulator must report an Old 3DS, or PpuGpu3DS_ShouldUse() picks the software path.
#   ~/.config/azahar-emu/qt-config.ini:  is_new_3ds=false
# SD card layout it reads (Azahar's user dir):
#   ~/.local/share/azahar-emu/sdmc/3ds/The Minish Cap 3DS/<any>.gba
QT_QPA_PLATFORM=xcb azahar -w build-3ds/game/tmc-3ds-v1.2-E1.3dsx
```

Input has to be injected with XTEST (`xdotool keydown q w a`) rather than
`xdotool --window`: Qt ignores synthetic `XSendEvent` keys. `q`/`w`/`a` are the
default bindings for L/R/A, i.e. the quick-dump combo.

**Take two dumps.** The quick dump writes `info.txt` immediately, but the parity
readback only lands two frames later, so the first dump always reports
`parity checks 0`. The second dump reports the first one's verdict.

Dumps appear under `.../The Minish Cap 3DS/dumps/dump-*/`. On a mismatch the
parity path also writes `parity-cpu.bin`, `parity-gpu.bin` (raw 512x256 RGBA5551
surfaces) and `parity-diff.txt` into the most recent dump directory.

Results so far: an early-boot scene came back **bit-exact** (0 differing
pixels), and a 160-band HDMA title-screen frame came back with **0 failures and
0 structural differences** — its 3,259 differing pixels are all one-step blend
deltas, the floor described below. That is two scenes under one emulator, not
proof; see the caveats at the end.

## Limit 1: alpha blending cannot be bit-exact on PICA200

The GBA folds two layers with `out5 = min(31, (top*eva + bottom*evb) >> 4)`
(`port/ppu/src/mode1.c`), i.e. four fractional bits below the stored 5-bit unit.
The PICA200 blender computes `src*C/255 + dst*C'/255` in 8 bits and the RGBA5551
colour buffer truncates to 5, leaving three fractional bits. The missing bit is
not recoverable by any choice of constant:

> With `eva = evb = 1`, every `(s,d)` with `s+d = 15` must produce 0 and every
> `s+d = 16` must produce 1. Monotonicity of the write quantizer then forces the
> destination contribution to grow by at least one 8-bit step for each of the 16
> increments, so `contribution(16) >= 16`; the same holds for the source. But
> `s = d = 16` must produce 2, which caps the sum at 23. Contradiction.

An exhaustive search over all 65536 8-bit constant pairs confirms it under
per-term-truncate, sum-then-truncate and sum-then-round models. Of the 289
possible `(eva,evb)` pairs only 119 are even theoretically satisfiable, and the
pairs Minish Cap actually programs — `(15,10)` on file select, `(9,9)` and its
neighbours on the title screen, the `(16-t, t)` ramp in `mazaalBossObject.c` —
are provably not among them.

**Policy, as implemented:** the parity check classifies each differing pixel. A
difference of at most one step per 5-bit channel is the documented floor and is
counted but tolerated; anything larger is a renderer defect and retires the GPU
path. `info.txt` reports `differing` and `structural` separately.

**Still open — needs real hardware:** which constant table minimises the residual
depends on whether silicon expands 5-bit to 8-bit by bit replication or by
shifting, and whether it truncates or rounds each term. An emulator cannot answer
this: Azahar executes the blend in host OpenGL at host precision. A calibration
probe (render known `(source, destination, constant)` triples through the real
blender, read back, solve offline) was drafted and then removed — its geometry
did not land where intended under the probe's own viewport, and debugging that
on an emulator that cannot answer the question anyway was not worth the code.
Rebuild it against hardware, reusing the render path's viewport/scissor
convention rather than a fresh one.

An empirical fudge (`BlendCoefficient8`, subtracting 5 from every coefficient
and a further 3 from red) had been added while chasing this on hardware. It was
tuned to one observed scene, has no mechanism behind it — nothing in the pipeline
treats red differently — and raises the overall mismatch rate. It has been
reverted.

## Limit 2 (resolved): per-band emission did not fit the frame budget

A *band* is a run of scanlines whose PPU registers match. The builder emitted
per-tile quads **per band, per layer**, so cost scaled with
`bands x columns x layers`. On the title screen that meant 160 bands, >49,000
vertices demanded against a 32,768 capacity, and every GPU fallback measured on
device attributed to that capacity failure.

**What actually creates the bands was not what it looked like.** The per-line
registers are identical across the whole frame; what changes per line is the
**affine reference point** of BG2. In mode 1 the game uses BG2 as an ordinary
scrolled layer whose reference advances exactly one source line per scanline
(`PA/PD = 1.0`, `PB/PC = 0`, `refY` stepping `0x100`). `line_states_equal`
splits on the reference, so the frame became 160 one-line bands, and the general
affine path re-sampled all 240 screen pixels of every one of them: ~38,400
per-pixel samples to draw a static scrolled background.

Finding this needed a faithful replay. The quick dump now also records
`io-per-line.bin`, `dispcnt-per-line.bin` and `affine-ref.bin`, so the host
benchmark replays the exact frame the console built. Two traps on the way:
the per-line array is only populated when HDMA is active, so a frame without it
must have row 0 replicated; and the flag for that has to be captured during the
build, because `port_hdma_vblank_reset()` has already cleared the HDMA channels
by the time the dump runs.

### What was done

1. **OAM prepass.** `read_obj` ran 640 times per band — over 100,000 decodes on a
   160-band frame. Objects are now decoded once per frame and every band walks
   the result.
2. **Per-layer band merging with priority-major emission.** Bands are merged
   independently per layer when the inputs that layer depends on are equal
   across the run, and emission is reordered priority-major so merging cannot
   disturb the order a pixel sees its layers in. Merging is deliberately
   conservative: an object window, or any active window whose registers differ,
   blocks it.
3. **Map-space background geometry.** A background's tiles are emitted once per
   frame in map space; each band then draws the index range covering its rows,
   with its scroll supplied as a vertex-shader offset (`uOffset`, added to every
   position — legacy emitters pass zero and are bit-for-bit unchanged). A
   single-row band narrows to just the columns it can see.
4. **Affine backgrounds that are really scrolled layers** take the same path:
   identity matrix, integer reference, linear per-line advance and wrapping
   enabled are recognised, and BG2 is then emitted once like any other layer.
   Everything else keeps the general affine sampler.

### Results

Host benchmark (`port_ppu_gpu_3ds_bench`), replaying the console's own
160-band title-screen frame:

| | build time | vertices | tile lookups |
|---|---|---|---|
| Before this work | 0.498 ms | 25,140 | 6,264 |
| After | **0.075 ms** | 8,340 | 2,064 |

On the console profile under the emulator, the same frame:

| | before | after |
|---|---|---|
| Captured frame build | 33.4 ms | **4.7 ms** |
| PPU render, average | 30.3 ms | **5.0 ms** |
| Frame interval, average | 35.1 ms | **16.76 ms** |
| Frames over 33.3 ms | 1,193 | **6** |
| Frames rendered on GPU | 250 of 1,050 | **2,889 of 2,894** |

The host/device ratio measured on identical work is about 67x, which is what a
268 MHz in-order ARM11 costs against this desktop; the earlier 20-40x assumption
was optimistic and had made the budget look easier than it was.

### How the output is proven unchanged

The benchmark replays the built command buffer the way the Citro3D backend does
and records, per pixel, the winning layer, its priority, its effect and the
atlas texel sampled. Every optimization above was accepted only when that record
stayed byte-identical. `PpuGpu3DS_SetMapSpaceEnabled(false)` forces the legacy
per-band walk, so the two paths can be diffed directly:

| case | map space on | map space off | match |
|---|---|---|---|
| synthetic, 1 band | 3434331802c9a6b7 | 3434331802c9a6b7 | yes |
| synthetic, 8 bands | 4c9a33a717ec411f | 4c9a33a717ec411f | yes |
| synthetic, 40 bands | ff2659f5f23b3dd7 | ff2659f5f23b3dd7 | yes |
| synthetic, 160 bands | 5c0907526b870614 | 5c0907526b870614 | yes |
| real 160-band device frame | d45e4a2f8e1c579d | d45e4a2f8e1c579d | yes |

On device, the parity check on a 160-band HDMA frame reports **0 failures and 0
structural differences**; the 3,259 differing pixels are all one-step blend
deltas, and the best alignment of the two surfaces is exactly (0,0) — four times
better than any shifted alignment, which rules out a placement error.

## Correctness fixes made while testing

- The parity comparator indexed `readback + y*512 + 256`, a 256-**column** shift
  on a 512-wide row, comparing the frame against never-rendered backdrop. Every
  check would have failed and permanently disabled the GPU path.
- The full-target scissor was left in the pre-offset coordinate space after the
  viewport moved to the top of the render target.
- The backdrop stencil quad still sampled the atlas on the old V axis.
- `C3D_FrameSplit(C3D_FRAME_SYNCDRAW)` passed a `C3D_FrameBegin` flag to a
  function that takes `GX_CMDLIST_*` flags, where that bit means
  `GX_CMDLIST_UPDATE_GAS_ACC`. It is now `C3D_FrameSplit(0)`.
  Note: calling `C3D_FrameSplit` repeatedly inside a frame hung the game.
- The depth mapping the negated OBJ depth relies on is now stated with
  `C3D_DepthMap` instead of inherited from whatever Citro2D last configured.
- Three synchronous `fopen("sdmc:/...")` framebuffer dumps ran inside the parity
  check on the render thread; they now write into the quick-dump directory and
  only when a difference is found.
- A parity request no longer cancels when the frame falls back to software — it
  waits for a frame the GPU can build (bounded), so a verdict is actually
  reachable.

## Instrumentation added

`info.txt` in every quick dump now reports build-failure attribution
(`args/unsupported/capacity/atlas/geometry`), peak bands, peak and demanded
vertices, peak batches, and the parity `differing`/`structural` split. This is
what turned "84% of frames fall back" into "100% of fallbacks are command-buffer
capacity, caused by HDMA banding".

## Two bug classes the emulator cannot catch

Both were found only on hardware, and both are invisible under Azahar for
structural reasons. Worth knowing before trusting a clean emulator run.

### The GPU reads the command buffers asynchronously

`PlatformGpu3DS_BeginCustomTop()` used `C3D_FrameBegin(0)`, which does not wait
for the previous frame's drawing to retire, and the builder wrote the vertex,
index and atlas buffers *before* that call. The CPU was therefore overwriting
geometry the GPU was still reading. An emulator whose GPU completes instantly
never shows it; on an Old 3DS it is a flicker. The GPU frame is now claimed with
`C3D_FRAME_SYNCDRAW` before any buffer is touched.

The related lesson on cache maintenance: **the cost of `GSPGPU_FlushDataCache`
is the GSP round trip, not the bytes**. Measured on an Old 3DS at roughly
330 us per call, so 60 freshly decoded tiles flushed individually cost 19.7 ms
per frame while one call covering the whole span costs a fraction of that. Atlas
uploads are coalesced into at most two ranges and geometry into one call for
vertices and one for indices. Counting calls, not bytes, is the rule here.

### Per-frame state the GPU path forgot to advance

`virtuappu_mode1_prepare_frame()` walks the HBlank DMA channels exactly as
rendering does, so they must be rewound with `port_hdma_vblank_reset()` each
frame or the next frame resumes past the end of the per-scanline table. The
software path did this; the GPU path did not, so any channel outliving its frame
fed the builder garbage registers -- seen as a blue background behind intro
cutscene text.

It looked self-healing: taking a quick dump made the screen correct again,
because the parity frame performs the rewind. That symptom -- "capturing the bug
fixes the bug" -- is the signature of missing per-frame state maintenance, and
it is worth recognising quickly.

## Builder cost, by phase

Measured by the host benchmark replaying the console's own 160-band frame
(`PPU_GPU3DS_PROFILE`, printed as ms/frame):

| phase | at first measurement | now |
|---|---|---|
| bands | 0.0020 | 0.0018 |
| merge | 0.0060 | 0.0059 |
| maps | 0.0181 | 0.0075 |
| scene | 0.0102 | 0.0104 |
| **total median** | **0.033** | **0.023** |

`maps` fell by replacing a digest of the character data with a snapshot taken
when the geometry was built and a straight `memcmp` against it: exact, and a
sequential compare rather than a hash over every word. `merge` fell by computing
the state common to all layers once instead of re-reading the same registers for
each of the six.

The host/device ratio on identical work is about 133x, so the remaining 0.023 ms
is roughly 3 ms of an Old 3DS frame.

## Caveats on emulator results

- Azahar's timing is instruction-count based and models no cache misses, so
  frame costs measured there are optimistic for real hardware.
- Its PICA200 blending runs through host OpenGL, so a bit-exact parity result
  under Azahar is **not** hardware proof, and a small non-zero count in blended
  regions is not necessarily a bug.
- Everything geometric — viewport placement, V-flip conventions, tile addressing,
  OBJ depth ordering, window/scissor bands, tile-cache eviction — does reproduce
  faithfully and can be settled on the emulator.

## Next optimisation, and why it is not done yet

Presentation is the largest remaining per-frame cost (~3.4 ms on hardware, ~0.07 ms
under an emulator), which places it in the GPU rather than the CPU. The frame
also now waits for the previous one to retire before the builder rewrites the
command buffers -- correct, because the GPU reads them asynchronously, but a
stall by construction.

The way to keep the correctness without the wait is to double-buffer the
geometry: alternate the per-band region between two halves each frame so the
CPU never writes what the GPU is reading. The obstacle is the retained map
slices, which are written in place when a layer changes:

- 16-bit indices cap the buffer at 65536 vertices.
- Slices need 4 x 1024 quads = 16384 vertices; shrinking them below ~600 quads
  per layer forces layers back onto the per-band path, which is far worse.
- That leaves 24576 vertices per dynamic half (6144 quads) against a measured
  worst case of ~4800 quads -- workable.
- What it does not solve is the frame where a slice *is* rebuilt: that write
  still races unless it waits, or the slices are double-buffered too, which
  does not fit.

So this needs either a per-layer "rebuild into the other half and swap"
scheme, or a cheap mid-frame sync that is known-safe on hardware
(`C3D_FrameSplit` is not: calling it repeatedly inside a frame hung the
console). It should not be attempted until the presentation split now recorded
in the quick dump says how much of that 3.4 ms is actually the wait.

## The oracle's blind spots, and the stencil fix they were hiding

The host bench compared the GPU command stream against the software rasterizer
and reported "0 differing pixels" on every captured hardware frame, including
the ones that come out wrong on the console. That agreement was worth much less
than it looked, because the comparator skipped three classes of pixel:

- `got.effect != PPU_GPU3DS_EFFECT_NONE` -- the GPU applies brightness and
  blend effects, so the replay cannot predict the final colour. On the frames
  that actually fail this is **15360 px, exactly 240x64**: rows 96-159, which is
  precisely where the hardware parity surface first differs.
- `got.blendInvolved` -- a blended layer is drawn twice, and which draw survives
  depends on stencil state the replay did not model.
- `got.texel == 0` -- an empty pixel was read as "no data" and skipped. But an
  empty pixel is not absent, it is the *clear colour*, which is what the console
  shows there. Content rejected by the stencil lands here and nowhere else, so
  missing content was invisible to the comparator by construction.

`record_frame` also replayed only geometry: it walked the batch list once and
never consulted `windowMask` or the stencil at all. So the comparison exercised
geometry, bands, scissor and texture sampling -- none of which were ever in
doubt -- and was blind to the one subsystem the symptoms pointed at.

The bench now models the stencil directly, mirroring the three passes in
`port_ppu_gpu_3ds.c` (OBJWIN writes bit 0x04; the backdrop pass writes bit 0x08
with the alpha test *off*; the colour pass applies `SetColorStencil`), and
compares empty pixels against the clear colour rather than skipping them
(`UNCOVERED` in the output). All 18 captured frames still pass, so the command
stream really is correct -- the fault is in how the device executes it.

That narrowed things to one arithmetic fact. Both halves of an alpha pair carry
`inputMask = 0x0c` with bit 0x04 clear in `ref` when the batch is in region
OUTSIDE. If stencil bit 0x04 is set at those pixels, the blended batch *and* its
complement are both rejected, nothing is drawn, and the clear colour survives --
a blue backdrop behind text, or a black screen where the clear colour is black.
Only the OBJWIN pass writes bit 0x04, and the failing frames (`dispcnt=2640`,
mode 0 with WIN0 enabled) emit **zero** OBJWIN batches, so the bit they test is
one nothing in the frame ever establishes.

`BatchRegion` now collapses OUTSIDE and OBJWIN onto "no window" whenever the
frame contains no object-window batch. This is correct by construction -- with
no OBJ window every pixel is outside it -- and removes a test that can only ever
reject work that should have drawn. It also drops a stencil state change from
the common case.

Note that the regions are *not* WIN0/WIN1: those are horizontal scissor spans
plus band splits. Region kinds are `OUTSIDE=0, OBJWIN=1, WIN1=2, WIN0=3`, and
only the first two involve the stencil.

## What the hardware counters say to optimise (and what not to)

A quick dump carries enough to rank the costs without guessing. From
`dump-20260819-135754` (5050 presented frames, measured cadence 37.67 FPS):

| item | measured |
| --- | --- |
| PICA200 frames attempted / rendered / **disabled** | 1273 / 1270 / **3777** |
| software PPU render | 15.172 ms average |
| GPU path: preflight / draw | 5.451 / 0.927 ms per attempted frame |
| GPU path: build / flush (both inside preflight) | 4.104 / 1.335 ms |
| top presentation CPU work | 7.256 ms average |
| top transfers (512 KB upload each) | 3780 |
| audio render / deadline misses | 7.694 ms average / 942 |

The single largest optimisation is keeping the GPU path enabled. It was retired
by the parity check for **3777 of 5050 frames**, so the measured 37.67 FPS is
mostly the software rasterizer. Note `3780` top transfers against `3777`
disabled frames: the 512 KB CPU upload happens *only* on the software path
(`gpuReady` presents straight from the GPU texture), so a retired path costs the
15.172 ms render *and* a half-megabyte upload with its cache flush *and* the
core-1 PPU worker that competes with audio at priority 47 -- which is where the
942 audio deadline misses and the reported crackling come from.

Two optimisations were considered and rejected on the numbers:

- **Splitting the geometry flush.** The flush spans from the lowest dirty map
  slice, so a dirty slice 0 re-flushes all four (~404 KB). But
  `GSPGPU_FlushDataCache` costs ~330 us per call against roughly 1 us/KB, so one
  extra call is worth ~330 KB. Splitting to save ~196 KB loses. This is the same
  trap as the earlier span-flush regression, where bytes were optimised and the
  hardware charged per syscall.
- **Shrinking the vertex from 24 bytes.** Dropping `w` and packing UVs as 16-bit
  texel coordinates would cut geometry by a third, but the flush is call-bound
  rather than byte-bound, so it buys ~134 us/frame for a format change across
  the builder, the shader and the attribute loaders. Not worth it yet.

One optimisation was made: `build_oam_set` now buckets entries by priority.
The draw loops run priority-major and walked all 128 entries inside every band,
which on a 160-band HDMA frame is 81920 filter iterations. The buckets keep the
descending order the loops used, so output is bit-identical -- verified by the
raster-record hash matching on all 18 captured frames. Measured on host it is
0.026 -> 0.022 ms on the 160-band frame and within noise elsewhere; the win
should be larger on ARM11, where that walk misses a much smaller cache, but that
is an expectation and not a measurement.

The remaining question is where the 4.104 ms build actually goes on hardware.
`PICA200 PPU build phases` is printed from `gPpuGpu3DSPhase[]` under
`PPU_GPU3DS_PROFILE` (defined for the 3DS build) but is absent from every dump
captured so far, because those predate it. The next dump will carry it. Until
then the host profile (obj ~50%, maps ~38%) is not a safe guide: the bench
replays one static frame, so its tile cache warms up after the first iteration
(624 decodes over 400 frames) while hardware sustains ~95 decodes per frame.

## Optimising the decode path, and fixing the bench that hid it

The host bench replays one static frame, so its tile cache warms up after the
first iteration: 624 decodes across 400 frames, about 1.5 per frame, against the
~95 per frame the console sustains. Every profile taken from it therefore
understated decoding to the point of invisibility. `PPU_BENCH_COLD=1` now empties
the cache between frames, which exposes the path:

| frame | warm | cold (before) | cold (after) |
| --- | --- | --- | --- |
| dump-20260819-130924 | 0.018 ms | 0.201 ms | **0.105 ms** |
| dump-20260819-132255 | 0.021 ms | 0.271 ms | **0.112 ms** |

Isolating decoding alone, 0.293 -> 0.147 us per tile, almost exactly 2x. Three
changes, none of which alter output:

- **4bpp reads each source byte once.** The old loop indexed `source[pixel / 2]`
  and chose a nibble with a shift computed per pixel, reading every byte twice.
  It now reads the byte once and takes both nibbles.
- **The palette pack runs 16 times, not 64.** Every pixel of a 4bpp tile shares
  one 16-colour bank, so the bank is packed into a lookup table and the inner
  loop is a table read. Index 0 maps to 0, which keeps transparency exact.
- **Morton position is a table.** `PpuGpu3DS_MortonIndex` was a call and six
  shift-mask pairs per pixel; `kTileMorton[64]` replaces it.

A fourth change memoises the packed bank in the cache (`bankLut`, keyed by the
per-bank generation that already existed), so tiles decoded back to back share
one pack instead of repeating sixteen. Worth a further ~5%, consistently across
three frames.

8bpp keeps its per-pixel pack: a 256-entry table would cost more to build than
the 64 packs it saves, and the format is rare here.

Verification for all four: the raster-record hash is identical to the original
on all 18 captured frames, `port_ppu_gpu_3ds_model_test` passes, all 18 software
comparisons stay clean, and `PPU_BENCH_MUTATE` at offsets that land in live tile
data (0x0, 0x8000) still forces re-decodes and still matches the original
byte for byte -- so the in-place tile animation path is exercised, not bypassed.

A fifth change bucketed OAM by priority (see above).

Neither the warm nor the cold bench is the case the console runs: hardware sits
between them at ~95 decodes against ~520 hits per frame. `PPU_BENCH_CHURN=<n>`
dirties one byte in each of n consecutive tiles per frame, the way the game
animates tile data in place, so the bench can replay that mix. Only about a
quarter of churned tiles are referenced by a given frame, so n=400 lands at 91
decodes per frame -- near the console's 94.6:

| frame | build before | build after | |
| --- | --- | --- | --- |
| dump-20260819-130924 | 0.072 ms | **0.055 ms** | -24% |
| dump-20260819-134710 | 0.072 ms | **0.056 ms** | -22% |

So at the console's own decode rate the builder is about 23% faster, and output
is identical on all 18 frames under that churn as well. Scaling that to the
4.104 ms hardware build suggests roughly 0.9 ms per frame, which is still a
scaling assumption -- but the decode *mix* is now right rather than measured at
one of two extremes. The deployed build prints `PICA200 PPU build phases`, so
the next dump replaces the assumption with the split.

## The on-target profile, and what it said about the host one

Azahar runs the same ARM binary, so a quick dump taken under it carries the
counters and the `PICA200 PPU build phases` line even when the console is not
available to run. From an emulator dump of the build carrying the stencil fix
(4629 frames):

```
attempted/rendered/fallback/disabled: 4629/4621/8/0
parity checks/failures/differing/structural: 1/0/3471/0
build phases (ms/frame): bands 0.0625 merge 0.2293 maps 1.3586 scene 0.5589
                         (objwin 0.0016 regions 0.0053 bg 0.0904 obj 0.4928)
```

Two things follow. First, **parity passes and no frame is disabled** -- against
1/1 failures, 15078 structural pixels and 3777 disabled frames on the console
before the fix. Second, the phase split is not the one the host bench reports:

| phase | host bench | on target |
| --- | --- | --- |
| maps | ~38% | **61%** |
| obj | ~50% | 22% |

The host bench was pointing at the wrong phase. `maps` is 1.36 ms of a 2.21 ms
build, and the reason is not the retention compare -- doubling that memcmp on
the host moved the maps phase by 0.0012 ms of 0.0502, about 2.4%. It was the
pinning:

- A retained layer pins 600 atlas slots, and four layers pin 2400 of 4096.
- Pinned entries were never `cache_touch`ed, so they sank to the LRU tail, and
  every eviction walked past all of them to find a victim.
- `retain_touch` stamped `lastUseFrame` across all 2400 every frame -- 2400
  scattered writes into a ~330 KB entry array, so 2400 cache-line misses -- and
  it existed only to keep the stale-slot refresh in the lookup off pinned slots,
  because that path tested `lastUseFrame` and not `pinned`.

A pinned slot can never be an eviction victim, so it should not be in the list
at all. It now leaves the LRU on pin and is pushed back on release, the lookup's
refresh path tests `pinned` directly, and `retain_touch` is gone. The eviction
walk no longer sees pinned entries and the per-frame stamp disappears.

On host this is worth 18-29% of the maps phase, which understates it: scattered
writes and list walks are cheap on a machine with fast memory and large caches,
and that difference is exactly why the host called maps 38% and the target 61%.

Verified against the pre-optimisation baseline: raster records identical on all
18 frames in warm, `PPU_BENCH_CHURN=400` and both `PPU_BENCH_MUTATE` modes; the
retention sweep reports 3072 tiles checked and 0 stale; the model test passes;
all 18 software comparisons stay clean.

### Measured on target

Rerunning the same emulator scenario with the pinning change in, against the
dump above (comparable workloads: 51.2 vs 51.3 decodes per frame, 4629 vs 4625
frames):

| phase | before | after | |
| --- | --- | --- | --- |
| maps | 1.3586 | **1.2385** | -8.8% |
| scene | 0.5589 | 0.5414 | -3.1% |
| obj | 0.4928 | 0.4842 | -1.7% |
| total build | 2.209 | **2.072 ms** | **-6.2%** |

The cumulative counter agrees independently: 10704.446 -> 10102.091 ms of build
CPU over those frames, 2.313 -> 2.184 ms per frame. Parity stays at 0 failures
and 0 disabled frames.

This is the ARM binary, but under an emulator, not an Old 3DS. Azahar runs it on
host caches, so the part of this win that comes from cache misses -- which is
most of it -- is understated here relative to the console. The console figure
still needs a console dump; what is no longer assumed is the direction and the
phase it lands in.

## The actual fault: the stencil plane was never zeroed on hardware

The console showed a black top screen on file select before the region collapse
above, and a white one after. Both are the same fault, and the bit arithmetic
identifies it exactly. Both halves of an alpha pair in region OUTSIDE carry
`inputMask = 0x0c`, with `ref = 0x00` on the complement and `0x08` on the
blended half. Suppose the stencil holds `0x0c` rather than `0`:

- Before the collapse, `(0x0c & 0x0c)` is neither `0x00` nor `0x08`, so **both**
  halves are rejected, nothing is drawn, and the clear colour survives -- black.
- After the collapse the mask is `0x08`, so the complement (`ref 0`) fails and
  the blended half (`ref 0x08`) passes everywhere -- a uniformly blended screen,
  white.

One cause, two appearances, and neither is visible under an emulator, which
zeroes the plane properly. The window and blend design reads the stencil as
though it starts at zero every frame, and the only thing guaranteeing that was
`C3D_RenderTargetClear`. That call clears colour and depth/stencil separately:
`C3D_FrameBufClear` skips the depth/stencil fill entirely when the attachment is
missing while still clearing colour, which is exactly the observed signature --
a correctly cleared background with every gated batch resolving against garbage.

Two changes, because two things can produce it:

1. **`ClearStencilPlane`** resets the plane with a draw at the top of every
   frame: a backend-owned full-screen quad living in the tail of the vertex and
   index buffers (`PPU_GPU3DS_CLEAR_FIRST_VERTEX`, written and flushed once at
   init, with the builder's capacity reduced to match), drawn with
   `GPU_ALWAYS`/`REPLACE`, `ref 0`, write mask `0xff`, and a zero colour write
   mask so it touches nothing but the stencil. It costs one quad and depends on
   no clear semantics at all.
2. **An init guard** refuses to enable the PICA200 path when the target comes
   back without a real stencil attachment (`depthBuf`, `depthFmt`, `depthMask`),
   logging the pointer, format, mask and free VRAM. Without a stencil the
   renderer cannot be merely imperfect -- it is uniformly wrong -- so the frame
   belongs to the software rasterizer instead.

Under the emulator the guard reports `depthBuf 0x1f103400 fmt=3 mask=3, vram
free 3419648`, so the attachment is present with room to spare there; that makes
the uncleared plane the live explanation on the console and the missing
attachment the one held in reserve. Either way both now render correctly rather
than showing a blank screen.

## The parity check only ever ran on a quick dump

`PortPpuGpu3DS_RequestParityCheck` had exactly one caller:
`Port_PPU_3DS_WriteQuickDump`. That is the whole explanation for "taking a dump
before the file select screen fixes it" -- the dump triggered the comparison,
the comparison failed, the path retired, and the software rasterizer drew the
screen correctly. In normal play the GPU output was never once checked against
the software renderer, so a divergence that only appears on hardware stayed on
screen indefinitely. Every bad screen reported in this session was a fault the
existing safety net could have caught and never got the chance to.

Each distinct display configuration is now checked once, plus a sweep every 600
frames, rate limited to one check per 30 frames. Measured on target over 4647
frames that is **16 checks, 0.34% of frames**: at the console's ~15 ms software
render, roughly 240 ms across 77 seconds of play, about 0.3%. In exchange, a
divergence -- from a fault that is understood or one that is not -- retires the
path within about half a second and leaves a correct picture rather than a blank
one.

`ClearStencilPlane` does not register: the maps phase reads 1.2378 ms with it
against 1.2385 ms without, and the cadence holds at 59.18 FPS.

## Sampling the render target without retiring the draws

The observation that broke this open was not a rendering artefact at all. With
`gpu_frame_sync=1` the console showed a white top screen and a **black bottom
screen**, with sound still running. The bottom screen is drawn by citro2d from a
separate worker and cannot be touched by the PPU's GPU path, so a fault that
reaches it is not in the PPU frame -- it is in the frame structure around it.

`PortPpuGpu3DS_DrawPrepared` renders into `sOutputTexture` via its own render
target, and `DrawTopTexture` then samples that same texture with `C2D_DrawImage`
in the same frame, with nothing in between. Reading a render target that was
just written needs the queued draws retired and the texture cache dropped first.
The software path already did exactly that before its transfer:

    /* Retire the queued PPU draws before the transfer reads the target. */
    C3D_FrameSplit(0);

The GPU path never did. Without it the sampler takes whatever the texture cache
already held -- a uniformly white or black screen -- and the disturbed command
list ordering reaches the bottom screen as well.

None of this reproduces under Azahar, which completes GPU work instantly and
keeps no texture cache of the kind that goes stale here. That is the reason
every emulator confirmation in this session was worthless, including two fixes
reported as verified: the emulator cannot exhibit the class of fault involved.
Where a hypothesis is about ordering, caching or timing, the emulator can only
falsify it, never confirm it.

`PlatformGpu3DS_DrawTopTexture` now issues one `C3D_FrameSplit(0)` before the
sample -- the same single split per frame the software path issues. It is
deliberately one and not more: repeated splits inside a frame previously hung
the console.

Two theories died on the way here and should not be revived. The render target
does have a real stencil attachment on hardware: the console logs
`gpu depth buffer 0x1f103400 fmt=3 mask=3 vram free 3419648`, identical to the
emulator, so the missing-attachment guard can never fire. And scheduling the
parity check outside a quick dump crashes the console on boot; that machinery
has only ever run with the game paused, and it was reverted.

## Removing the retained-slot walk

Taking pinned slots off the LRU list made a second check redundant without
anyone noticing. `retain_tiles_current` still walked every retained slot -- 600
per layer, 2400 across four -- testing `valid && pinned` before the snapshot
compare. Those are scattered reads through a ~330 KB entry array, so 2400 cache
misses a frame, and they confirmed something that cannot change:

- `pinned` is cleared only by `retain_release`, which empties `slotCount` with it.
- `valid` is cleared only by `CacheInit`'s memset, which does the same.
- A pinned slot is not on the LRU list, so it can never be an eviction victim.
- The stale-slot refresh in the lookup skips pinned entries.

So if `slotCount` is non-zero, every slot it names is valid and pinned by
construction. The walk is gone.

Host: the maps phase drops 31-36% (0.0059 -> 0.0041, 0.0062 -> 0.0040,
0.0053 -> 0.0034 ms). On target the same change reads:

| phase | before | after |
| --- | --- | --- |
| maps | 1.2378 | **1.1675 ms** |
| total build | 2.0616 | **2.0007 ms** |

Against the pre-optimisation baseline that is maps 1.3586 -> 1.1675 (-14.1%)
and build 2.209 -> 2.001 ms per frame (-9.4%), with the cumulative build CPU
counter agreeing: 2.313 -> 2.113 ms per frame. Verification unchanged: all 18
frames raster-identical in warm, churn-400 and both mutate modes, model test
passing, 18 of 18 software comparisons clean, retention sweep 0 stale.

## The real-hardware phase breakdown

A quick dump taken on the console finally carried `PICA200 PPU build phases`:

```
bands 0.0370  merge 0.0541  maps 2.4968  scene 0.1750
                            (objwin 0.0017 regions 0.0073 bg 0.6495 obj 0.0053)
```

`maps` is 2.4968 ms of a ~2.77 ms build -- **90% on real hardware**, against 58%
under the emulator and 38% on the host bench. Both proxies understated it, the
host worst. Anything measured only on the host should be treated as indicative
of correctness, not of cost.

That is consistent with what the phase actually does: per-frame comparison of
tens of kilobytes of VRAM against snapshots, plus scattered reads through a
~330 KB cache-entry array. Those are memory-bound on ARM11 with a 16 KB L1, and
close to free on a desktop with megabytes of cache and SIMD memcmp -- which is
why the host called this phase cheap for the entire session.

The two optimisations aimed at it are therefore worth far more than their host
numbers suggested: removing the retained-slot walk (2400 scattered reads a frame)
and skipping the compare for a range already verified this frame (37-39% of the
bytes, since layers' character ranges are routinely subsets of one another).

Also confirmed from the same dump: the title screen renders **correctly** on
hardware, from the captured `top-screen.bmp` of the displayed framebuffer. The
GPU path is not broken in general -- only certain screens are.

## It was a deadlock, not a rendering fault

Every "black screen" reported on hardware was the console **hung**, not drawing
wrongly. The tell was in the report: audio kept playing, both screens held their
last contents, and no quick dump could be taken -- the dump runs on the main
thread, so a dump that never appears means a main thread that never got there.

A watchdog polled from the audio thread (which survives, being on core 1 and
woken by DSP callbacks) named the spot:

```
WATCHDOG: main thread stopped at stage 20, frame 462
```

Stage 20 sits immediately before `PlatformGpu3DS_BeginCustomTop`, whose
`C3D_FrameBegin` performs `gxCmdQueueWait(queue, -1)` -- an unbounded wait.
`BeginCustomTop` returns early when a frame is already open, so reaching that
wait proves the previous frame had ended and its command list had not retired.

`C3D_FRAME_NONBLOCK` fixes it: "Return false instead of waiting if the GPU is
busy." A frame that would have blocked is skipped instead, and the screen comes
up correctly. That the console recovers rather than merely surviving says the
queue does drain -- but only while the main thread keeps running. Blocking on it
prevented the progress it was waiting for. A deadlock, not a hardware stall, and
invisible under an emulator whose GPU work completes before the wait is ever
reached.

What this cost: the fault was diagnosed for hours as a pixel problem. Stencil bit
arithmetic, clear colours, a missing barrier -- all analysed against captures of
screens that looked fine, because a hung console cannot produce a capture of
itself. Two conclusions follow for next time.

- A symptom that includes "audio still works" and "the dump does not happen" is a
  hang. Check liveness before analysing pixels.
- The emulator cannot confirm anything about ordering, caching or timing. It can
  only falsify. Every emulator "confirmation" in this session was worthless, and
  two fixes were reported as verified on that basis.

The watchdog and the stage markers are cheap -- a volatile store per phase -- and
are kept. The automatic dump on each new DISPCNT was diagnostic scaffolding and
is removed; it paused the game on every new screen.

## Correction: the deadlock had a cause, and the first fix was not it

The section above concluded that `C3D_FRAME_NONBLOCK` fixed the freeze. It did
not fix it; it stopped the freeze being fatal. The console's own counters said
so on the next run:

```
emptyDraws=146  disabled=1  beginFail=4129  att=1340 rend=1340
```

The renderer still retired, and the screen only looked right because the
software path was drawing it -- the same way taking a quick dump used to
"fix" it.

**The cause.** The builder emits batches whose index range is empty whenever a
layer's tiles are all transparent, and `DrawBatch` turned each into a
`C3D_DrawElements` with a count of zero. That command never signals completion
on a PICA200, so the GX queue's interrupt count never reaches the number of
entries queued, and the next `C3D_FrameBegin` -- an unbounded `gxCmdQueueWait`
-- waits on it forever. The evidence is a clean split across captures taken from
the console:

| screen | zero-count draws |
| --- | --- |
| file select, five captures, froze | 16, 16, 4, 16, 4 |
| title screen, three captures, fine | 0, 0, 0 |

and the hardware counter afterwards: `emptyDraws=146`.

**A bug introduced while fixing it.** Making `C3D_FrameBegin` non-blocking meant
"GPU busy" started returning false, and three call sites treated that as fatal
and called `PortPpuGpu3DS_Disable()`: the main frame begin, the parity check's
begin, and a failed parity readback. A busy GPU is ordinary -- the software path
alone transfers 512 KB a frame -- so the renderer retired itself after ~1340
good frames. All three now skip or defer; only a failed `DrawPrepared`, a failed
cache invalidate, and a real parity mismatch still retire the path.

A busy begin also drops the frame rather than re-rendering it in software. The
first attempt rendered it, which costs ~15 ms and makes the next begin more
likely to fail as well -- a spiral, and `beginFail` was 4129.

**What found it.** Not analysis. A watchdog polled from the audio thread, which
survives a stalled main thread, printing the last stage the main thread reached.
Stage 20 named `C3D_FrameBegin`; splitting the stages narrowed it from "somewhere
in a frame" to one call in two runs. Hours of pixel-level reasoning before that
produced five wrong theories, because a frozen console cannot capture itself and
every capture available came from a screen that looked fine.

**Still open.** Whether the GPU path now survives file select on hardware
(`disabled=0`, `att`/`rend` climbing). Also a stall at stage 9 seen once at frame
5331 -- multi-second, recovered -- for which stages 10-13 now cover the
end-of-frame, lifecycle pump and VBlank wait.

**Available, not applied.** Dropping empty batches where they are built rather
than at submission takes file select from 54 batches to 38. It is written and
verified against the captures, but it fails `port_ppu_gpu_3ds_model_test`, which
asserts an object batch exists with a given scissor -- a batch that draws nothing
under the test's all-transparent tiles. Rendered output is identical on all 18
frames, so the test over-specifies an internal descriptor, but it should be
updated to use non-transparent tiles rather than relaxed to fit the change.

## Confirmed on hardware

File select on an Old 3DS, 1440 consecutive frames:

```
f=1440 gpuReady=1 init=1 disabled=0 beginFail=0 topXfer=3 att=1440 rend=1437 fb=3 dispcnt=1f40
```

`dispcnt=1f40` is the file select screen -- the configuration that froze the
console. The PICA200 path renders it, has not retired, and fails no frame
begins. Three software fallbacks in 1440 frames, against one software upload
per frame before.

`emptyDraws=20056` in the same session: twenty thousand zero-count draws
intercepted, any one of which would previously have wedged the GX queue.

The fix that mattered was one line -- refusing to submit a draw with no indices
-- and it took an evening to find because the failure presented as wrong pixels.
Three things made the difference, none of them analysis:

1. The report "audio works, both screens black, dump doesn't happen". A dump
   runs on the main thread; a dump that never appears means a main thread that
   never got there. That is a liveness symptom, and it should have redirected
   the investigation immediately.
2. A watchdog polled from the audio thread, which survives a stalled main
   thread. Two runs took it from "somewhere in a frame" to one call.
3. Counters instead of inference. `emptyDraws` proved the mechanism was present
   on hardware; `busyDrops=0` alongside `beginFail=2736` proved the failures were
   at the frame-start begin and not where a fix had been aimed.

What went wrong for hours: reasoning about stencil bits, clear colours and
barriers from captures of screens that looked correct, because a frozen console
cannot capture itself; and treating emulator agreement as confirmation. Azahar
completes GPU work before any wait is reached, so it cannot exhibit a stall, a
stale cache or an ordering fault. For this class of bug it can only falsify.

Two fixes made along the way were also real, and are independent of the stall:
the RGBA5551 clear colour (a black backdrop cleared to 0x00ff, saturated blue --
the blue behind the intro cutscene text, on hardware and emulator alike), and
the restored `C2D_TargetClear`, whose internal C3D_FrameSplit had been removed
by a conditional-clear optimisation.

Still open: a stall at stage 9 seen once at frame 5331, multi-second and
self-recovering, not observed since. Stages 10-13 now cover that region.

## Baseline with the PICA200 path actually running

First measurement on hardware with the GPU path alive for a whole session
(8336 attempted, 8332 rendered, 0 disabled), in-game at DISPCNT 0x1740:

| | software path | PICA200 path |
| --- | --- | --- |
| measured cadence | 37.67 FPS | **48.11 FPS** |
| top presentation CPU | 7.256 ms | **2.442 ms** |
| bottom paint worker | 26.5 ms | **17.2 ms** |
| frames the GPU gave up on | 3777 | **0** |
| audio deadline misses | 942 | **2475** |
| audio underruns | 5 | **102** |

Per-frame CPU on the GPU path: preflight 5.60 ms (build 4.32, flush 1.27),
draw 0.90, presentation 2.44. Build splits as maps 1.89, scene 2.23 (bg 1.82,
obj 0.37), merge 0.02, bands 0.03.

**Rendering improved and audio got worse, for a structural reason.** GSP services
every GX command and cache flush on core 1, and the audio worker runs on core 1
at priority 47. Moving rendering onto the GPU moved work onto the core the audio
thread lives on. The frame rate rose 28% and the audio budget fell.

The underrun mechanism is arithmetic rather than speculation: buffers are
4 x 256 frames at 16364 Hz, so the whole queue is 62.6 ms, and a single audio
render was measured at 61.97 ms. One slow render can drain everything queued.
BUFFER_COUNT is now 8, making the queue 125 ms, so a worst-case render costs
half of it. The price is queue latency.

Remaining distance to 60 Hz is about 4 ms: the average frame interval is
20.788 ms against a 16.67 ms budget. The candidates, in order of size, are the
bottom-screen paint worker (17.2 ms per paint, every third presentation, on
core 0 with the main thread), per-band background emission on HDMA frames
(bg 1.82 ms, with peak bands at 160), and the flush at 1.27 ms. Heavy frames
also reach 49500 wanted vertices against a 49148 capacity, which is why four
frames fell back.

## Optimisation pass, and the fact that reframed it

`docs/OLD3DS-OPTIMIZATION-REPORT.md` supplies the constraint that explains every
measurement in this file: **an Old 3DS has 32 KiB of L1 and no L2 at all.** An L1
miss stalls directly on 134 MHz FCRAM for 100-200 cycles. That is why the build
phase runs roughly 300x slower here than on a desktop, why `maps` reads as 90% of
build time on hardware and 2% on the host bench, and why every phase that
dominates is one that walks a large array. It also means host timings
systematically understate anything that reduces memory traffic, so they are
useful for proving correctness and near useless for ranking these changes.

Applied this pass, all verified output-identical (raster records match the
pre-optimisation baseline on 18 captured frames in warm, churn and both mutation
modes; model test passing; 18 of 18 software comparisons clean; retention sweep
0 stale):

| change | what it removes per frame |
| --- | --- |
| tile decode rewritten (byte pairs, 16 packs not 64, Morton table) | decode 2x faster |
| packed palette bank memoised per generation | 16 packs per tile |
| OAM bucketed by priority | 81920 filter iterations on a 160-band frame |
| pinned slots taken off the LRU list | 2400 scattered writes, plus long eviction walks |
| retained-slot walk removed | 2400 scattered reads |
| subset ranges skipped in the retention compare | 37-39% of the compared bytes |
| static index buffer | ~52 KB of writes and one GSP round trip |
| tile cache probes 88 -> 32 bytes | `entries[]` 360 KB -> 128 KB; one cache line per probe |
| vertex 24 -> 20 bytes | 186 KB of streaming stores at peak |
| direct-SVC cache clean (probed, GSP fallback) | a GSP round trip and a core-1 wakeup per flush |
| PLD prefetching compare | FCRAM stalls across the ~50 KB snapshot |
| vertex capacity to the 16-bit ceiling | software fallbacks on heavy frames |
| idle bottom repaints halved on Old 3DS | ~85 KB/frame of GSP transfer traffic |
| audio queue 4 -> 8 buffers | a whole-queue drain when one render runs long |

The two audio changes are measured on hardware: underruns fell from 0.93% of
buffers to 0.24%, deadline misses from 22.6% to 13.9%, and audio render cost
itself fell 19% -- which buffering cannot explain and the halved bottom-screen
traffic can, since GSP serves those transfers on core 1 where the audio worker
lives. Cadence went 37.67 -> 48.11 FPS once the PICA200 path stayed enabled.

Rejected with reasons, so they are not tried again: atlas into VRAM (the CPU
writes 45 tiles a frame into it), an NDSP channel per M4A voice (no precedent on
3DS, and it targets a figure that is mostly scheduling latency rather than CPU),
bottom screen RGB565 (512 KB in 17.2 ms is 30 MB/s -- the painter is
compute-bound, so the format will not halve it), and splitting the geometry
flush (one GSP call costs about as much as 330 KB, so splitting to save 196 KB
loses).

Two things the research corrected. The audio thread's 10.337 ms is wall clock on
a core-1 thread the OS quota-limits, not 10 ms of CPU -- so its 61.97 ms maximum
is a scheduling stall. And moving audio to core 0 was already tried and reverted
in 9eef48e13; core 0 is fuller than core 1.

Still open: whether the direct SVC is permitted under the homebrew launcher (the
dump now prints `Cache clean path:`), the ~2-4 ms still needed for 60 Hz, and a
single unexplained stall at stage 9 seen once at frame 5331.

## Report items: what was implemented, and what the attempts found

**Task 4, ARMv6 memory routines.** `Arm11FastMemcpy`/`Arm11FastMemset` copy a
cache line per iteration and prefetch three lines ahead, guarded to
`__3DS__ && __ARM_ARCH_6__`. Used for the retention snapshot, the largest copy
the builder makes. Below 64 bytes the library call wins and is used instead.

**Task 2, static quad presenter.** Done and verified. The frame is presented
with four vertices in linear memory, rebuilt only when the layout changes,
reusing the PPU's own vertex shader -- `PresentVertex` matches its attribute
info exactly once `w` was dropped. Getting the orientation right took four wrong
renders before deriving it from what `Mtx_OrthoTilt` actually does: positions map
`clipX = (screenY/240)*2-1`, `clipY = (screenX/400)*2-1`, and **both** u and v
inverted, because the display rotation reverses each axis. It calls `C2D_Flush`
before switching programs -- citro2d has vertices buffered from the clear, and
swapping the shader under them submits a batch mid-flight. Switchable via
`gpu_static_quad`, off by default: citro2d still draws the bottom screen, so its
per-frame vertex flush remains either way and caps the saving near 0.2-0.3 ms.

**Task 3, bottom screen RGB565: does not work here.** GX converts formats in
hardware, so the texture can be RGB565 while the painter keeps writing 32-bit --
no painter rewrite needed. But the painter writes **ABGR**, and
`ConfigureAbgrTextureEnv` un-swizzles it at sample time by reading *alpha as
red*. RGB565 has no alpha, so the conversion drops the channel carrying red and
the screen comes out solid red. Verified on an emulator. Making it work needs
the painter emitting true RGBA8 across 9000 lines and 85 signatures, and even
then only the transfer shrinks: at 512 KB in 17.2 ms the painter is
compute-bound at ~30 MB/s.

**Task 1, DSP audio offload: first slice done.** The GBA's CGB voices are
periodic waveforms, which an NDSP channel plays in hardware while the Teak DSP
otherwise sits idle at 134 MHz and the CPU synthesises them in floats on a
non-pipelined VFP. `ndsp_psg_offload.c` generates the four duty tables as
looping PCM16 (64 periods each, pitch carried by the sample rate: eight samples
per period, so rate = frequency * 8) and hands square voices to hardware
channels 1..4. `MP2KChnPSGSquare::Process` returns early when a channel is
claimed, so the software synthesis is skipped entirely; when none is free, or
`audio_dsp` is off, the original path runs unchanged.

All three CGB voice kinds are now handed over. Wave voices rebuild their table
from the game's 16 bytes of wave RAM only when that memory actually changes --
resetting the channel every buffer would restart the waveform sixty times a
second. Noise uses two pre-generated LFSR sequences, 15-bit and 7-bit, selected
by `noiseLfsrMask`; one step per output sample, so the rate is the voice
frequency directly.

Not offloaded: the DirectSound PCM voices. They need per-note sample pointers,
ADSR and pitch bend mapped onto channel state, and they carry MP2K's clipping
character, which is audible and is what a parity-focused port would be judged
on. Also unresolved: offloaded voices bypass MP2K's reverb, which applies to the
software mix -- on a game using ReverbType::NORMAL the CGB parts will lose their
tail. Both are reasons this stays behind `audio_dsp` with the software mixer as
the reference.

**research #4, smaller vertices.** 24 -> 20 bytes done by dropping `w`. The
further step to 16 was attempted: atlas UVs are exactly representable as
`(2n+1)/1024` since the atlas samples texel centres, so 16-bit costs no
precision. It touches six UV-producing sites and breaks
`port_ppu_gpu_3ds_model_test` at `affine_source_at`, which reads UVs back out of
the command buffer. Reverted rather than edit the test to match, for ~186 KB a
frame. Positions must stay float: quantising clip space shifts geometry about
0.012 px, which can flip a rasterisation boundary and cost pixel-exact parity.

## CGB/PSG DSP offload and reverb (settled by reading the mixer, not by ear)

Offloading the four CGB voices to NDSP raised an obvious parity worry: MP2K
applies reverb, and a hardware voice bypasses the software reverb entirely. That
worry does not survive contact with `SoundMixer::Process`:

- The reverb pass runs over each track's `audioBuffer` at roughly L91-99.
- The CGB channels (`sq1`, `sq2`, `wave`, `noise`) mix at L100-103, i.e. **after**
  that pass, and are the only `mixFunc` calls passing `feedsReverb = false`.
- `ReverbEffect::Process` only captures what is already in the track buffer when
  it runs, so PSG content added afterwards never enters the delay line either.

So PSG output is dry in the software mixer as well, and offloading it costs no
reverb tail. An earlier `NdspPsg_Available()` gate on reverb level was removed
for this reason. Two further points worth keeping:

- `Port_M4A_Backend_GetReverbLevel()` reports only the **forced override**
  (`sState.reverbForceByte`), not the song's effective reverb, which MP2K reads
  per song from `songHeaderPos + 3`. It is not a general "is reverb on" query.
- On 3DS `mode.reverbForce = 0x80` — `REV_MASK_SET` with level 0 — so reverb is
  already forced fully dry here to protect the audio budget. That also removes
  the reverb objection to a future DirectSound PCM offload; the remaining PCM
  risks are envelope interpolation (MP2K ramps volume within a block, NDSP would
  step it per block), MP2K's clipping character, and per-note sample/loop/pitch
  state. Resampler parity is available: 3DS already selects `NEAREST`, which
  corresponds to `NDSP_INTERP_NONE`.

`NdspPsg_VoicesOffloaded()` is reported in the quick dump so whether the offload
actually engages is measured rather than assumed.

## DirectSound (PCM) DSP offload — `audio_dsp_pcm=1`, default off

Only the per-sample fetch/resample/accumulate loop moves to NDSP. MP2K keeps
its envelope, volume and pan and pushes the result down as a channel mix once
per block, so the envelope *shape* is unchanged — what the ARM11 stops doing is
the inner loop that costs it the audio budget.

Scope is deliberately narrow: `Type::PCM` only. The DPCM/ADPCM and the three
synth types (PWM/saw/triangle) each need their own decode or generated table
and stay in software, as does any voice that finds no free channel or no linear
sample slot — `NdspPcm_Play` returns false and the original path runs.

Things that had to be right, each of which would have been a silent fault:

- **`pos`, not `sInfo.samplePos`.** `samplePos` is a ROM file offset kept for
  range validation; `pos` is the live playback index. A voice that spent its
  first blocks in software (all channels busy) must resume where it got to.
- **A finished non-looping sample must `Kill()` the voice**, mirroring
  `processNormal`'s `if (!running) Kill()`. It cannot fall back to software,
  because `pos` did not advance while the DSP owned the voice — the sample
  would restart. `NdspPcm_Play` reports this through its `finished` out-param.
- **Master volume and fades.** The software path applies these to the *track
  buffers* after mixing, a stage an offloaded voice never reaches. The mixer
  now publishes the level (`NdspPcm_SetMasterLevel` / `NdspPsg_SetMasterLevel`)
  and both offloads fold it into the hardware mix. The fade computation in
  `SoundMixer::Process` was hoisted above the mix calls to make the level
  available; nothing in `mixFunc` reads fade state, and the software ramp is
  still applied below unchanged. **This was already wrong for the shipped CGB
  offload** — with `audio_dsp=1` the volume slider and every song fade had no
  effect on the CGB voices.
- **A destructor releases the channel.** A voice can be destroyed without a
  final `Process` (context reset, song change clears the channel lists).
- Samples are copied into linear memory (the DSP cannot read the app heap) and
  reference counted, so an in-use sample is never evicted. Rate is checked once
  at claim time against what NDSP will honour.

Parity risk that remains, and it is the one only a listener can settle: MP2K
sums voices in float and clips, while NDSP sums in hardware. A part-software,
part-hardware sum is not bit-identical, and loud passages are where that shows.
`NdspPcm_VoicesOffloaded` / `Declined` are in the quick dump so engagement is
measured rather than assumed.

## The CGB offload played static on hardware — why, and what it costs

First hardware test of `audio_dsp=1` produced continuous static. The cause was
not a wiring mistake but a wrong assumption about what an NDSP channel can do.

NDSP outputs at 32728 Hz and resamples each channel to it. A playback rate far
above that does not give a high note, it gives garbage. The table-driven design
asks for exactly that:

| voice  | rate asked for      | worst case |
|--------|---------------------|------------|
| square | `freq * 8`          | 64 kHz     |
| wave   | `freq * 32`         | 256 kHz    |
| noise  | `freq` (LFSR clock) | **524 kHz**|

`MP2KChnPSG.cpp` derives the noise frequency as the LFSR *clock* — up to
524288 Hz — and the software path handles it by generating at that clock and
resampling down (`interStep = freq / noiseFreq`). Handing that number to
`ndspChnSetRate` asks the DSP for a 16x ratio. Noise is percussion, so it plays
almost continuously: hence static rather than an occasional glitch.

Fixes: a `PSG_MAX_RATE` ceiling checked **before** a channel is claimed (voices
above it stay in software rather than being clamped, which would retune them),
and the same ceiling applied to the PCM offload, whose 2x limit had been chosen
on no evidence. A second bug fell out of the same read: `NdspPsg_PlayWave`
called the combined claim+configure helper as `psg_begin(slot, NULL, 0, false)`,
which queued a **looping wave buffer with a NULL pointer and zero samples** on
every fresh wave note. Claim and configure are now separate.

What this costs: with the ceiling, only low-pitched voices offload — square
below ~4 kHz, wave below ~1 kHz, and noise only at its slowest settings. Much
of the CGB work therefore stays on the CPU by design. `NdspPsg_VoicesOffloaded`
vs `VoicesRateDeclined` in the quick dump measures the split. Covering the high
pitches would need per-note tables rendered at a fixed 32728 Hz rather than
per-period tables played at a variable rate — a redesign, not a tweak.

Worth keeping in mind for the wider effort: a dump measured the software mix at
**1.785 ms per 15.64 ms buffer (11.4% of realtime)**, ~4.3 s of core time across
a run, against the PICA build's 5.5 s and the bottom painter's 3.9 s. The audio
mix is one of three comparable costs, not the dominant one, and the underruns
(8 in 2422 buffers) coincide with a 13 ms-average, 88 ms-peak bottom painter
sharing core 1 with the audio worker — contention, not mix cost.

### Correction: the static was a units error, not just an out-of-range rate

The rate-ceiling entry above identified a real fault but named the wrong primary
cause. An adversarial review found five confirmed high-severity defects; the
decisive one is that **MP2K's PSG `freq` is the pattern-FETCH rate, not the tone
frequency**. The software path consumes exactly one table entry per source
sample (`interStep = freq * sampleRateInv`), so the audible tone is
`freq / entries-per-period`. The offload multiplied by the period length
instead, which played:

- square voices at `freq * 8 / 8` = `freq` — **three octaves sharp**
- wave voices at `freq * 32 / 32` = `freq` — **five octaves sharp**

The correct channel rate is the fetch rate *itself*. With that fixed the rates
land far below the ceiling, so the ceiling now only bites for noise (whose
`freq` genuinely is the LFSR clock, up to 524288 Hz, and whose units were right
all along).

The other confirmed defects:

- **PSG voices leaked their hardware channel on destruction.**
  `~MP2KChnPSG() = default` and the only release path was the `DEAD` check
  inside `Process()`. `SequenceReader.cpp:430` `channels.clear()` destroys a
  voice outright under `CGBPolyphony::MONO_STRICT`, which
  `port_m4a_backend.cpp:229` forces unconditionally — so essentially every
  repeated CGB note leaked a slot. After four notes all four channels were
  claimed forever, every CGB voice fell back to software, and up to four
  channels droned their last waveform at their last volume. That is both why it
  sounded wrong *and* why offload appeared to do nothing afterwards. Fixed by
  hoisting `ndspSlot` into `MP2KChnPSG` (it had been duplicated across all
  three subclasses — three chances to forget) and giving the base a destructor.
- **Declining mid-note while still holding the channel** made a voice play in
  hardware and software simultaneously — e.g. vibrato pushing the rate past the
  ceiling. Declines now release the channel first (`psg_decline`).

Method note: the review ran four independent lenses and then attacked each
finding with two refuters. It produced 30 raw findings; only the top 5 were
verified and the remaining 25 were explicitly dropped unverified, so this list
is not exhaustive.

## The bottom painter's 89 ms spike: software division per pixel

A four-lens analysis of the painter found one dominant cost, and it is not in
the painter's own code but in the theme's nine-slice blit.

`DrawSliced` (port_second_screen_theme.c) called `SliceMap` **once per
destination pixel**. `SliceMap` computes `d / scale`, `(dstLen - 1 - d) / scale`,
`(span / snap) * snap` and `(sd - c) % tspan` on runtime values, and ARM11 has
no divide instruction — each becomes a call into libgcc. `DrawPlate` runs this
over the whole 242x203 panel (313x203 on the settings tab) and `DrawWell` over
each of 16 cells.

Fix: every quantity `SliceMap` derives from `d` is monotonic in `d`, so `sd`,
`se` and the tiled-middle modulus all carry incrementally. **No division
survives in the inner loop**, and the `sy * srcStride` multiply is hoisted out
of the row. `port_second_screen_slicemap_test` sweeps widths, source lengths,
corners, snaps and scales and proves 7,274,904 pixel mappings identical to the
original.

Two corrections worth recording, both from the verification pass rather than the
proposal:

- The reviewers' suggested fix was a per-call lookup table declared as
  `int16_t xmap[320]`. This file is shared with the Android target, where the
  same code runs with `w` around 2049 — a stack overflow. Carrying the state
  needs no storage at all and cannot overflow.
- The claimed saving (45 ms) rested on arithmetic that was wrong in both
  directions: libgcc's `__divsi3` early-exits for power-of-two divisors and for
  dividend < divisor, and every divisor here is 1, 2, 8 or 16, so the slow
  shift-subtract body never runs. The real figure is ~37-42 ms on ITEMS/QUEST.

**Scope, which the headline number hides:** `DrawPlate`/`DrawWell` are reached
only from the ITEMS, QUEST and SETTINGS tabs. The default tab is `SS_TAB_MAP`
and the map/dungeon/overworld paths never call `DrawSliced`. So this removes the
~89 ms outlier — the spike that coincides with missed VBlanks — but should NOT
be expected to move the 10.5-15.6 ms average or the frame rate on its own.

## Bottom-screen repaint skipping: the self-referential tick trap

The bottom screen repaints every 6th presentation and never skips — a dump reads
`1370 paints requested; 1369 periodic checks; 0 static skips`. At 13.2 ms a paint
that is ~12% of a core redrawing a usually-identical picture, because
`Port_SecondScreen_3DS_NeedsPeriodicRefresh` (port_second_screen_3ds.c:235)
returns 1 unconditionally on the MAP tab.

The obvious fix is a signature: skip the paint when nothing that affects the
picture has changed. Before writing one, four lenses enumerated every input that
can change the MAP tab, and three adversarial agents tried to find a visual that
changes while all of them hold still. That produced 200 distinct inputs and one
finding that invalidates the naive design outright:

**`tick` only advances when a paint happens.** `sBottomWorkerTick = sBottomTick++`
is inside `if (schedulePaint)` (port_ppu_3ds.c:1100). Every MAP-tab animation is
derived from that tick. So a signature that quantises `tick` is self-referential:
skip a paint, the tick freezes, the quantised value can never change again, and
the animation is dead **permanently** — not stale for a moment, dead. The counter
must be free-running (e.g. `sFrameNumber / bottomInterval`) or `sBottomTick` must
advance on every cadence check whether or not a paint is scheduled.

The animation inventory, with safe quantisations:

| animation | expression | period | signature term |
|---|---|---|---|
| world-map player blink | `tick & 8` | 16 ticks | `(tick >> 3) & 1`, overworld only |
| player marker pulse | `sinf(tick % 32 ...)` | 32 ticks | fold the computed integer radius; on 3DS `u` collapses it to 2 values |
| dungeon room palette | `((tick * 3) >> 3) & 7` | 64 ticks | exact expression — fastest MAP animation, changes 3 ticks in 8 |
| dungeon player blink | `tick & 8` | 16 ticks | `(tick >> 3) & 1`, own-floor only |
| armed ring breath | `sinf(tick % 48 ...)` | 48 ticks | ~151 levels: changes every tick, so force the paint while `armedRing != 0` |
| region bracket beat | `tick - sUi.regionTick` vs 7 | deadline | force the paint while `regionState == SS_REGION_BRACKET` |

Three inputs survived the attack phase as genuine gaps, all in the "looks static
but is not" class:

- `gAreaRoomHeaders[]` read live by `GetRegionGeometry` for regions 4 and 7:
  only the region *artwork* is cached, the marker *geometry* is recomputed every
  paint from a mutable engine global.
- `GetRoomProperty` / `gAreaTable[area]` for dungeon compass markers — the
  self-healing `Port_RefreshAreaData` argument for excluding it does not hold.
- `Port_GetSpriteSizeTable()` / `sSizeTableLoaded`, a lazy latch gating every
  dungeon and region marker glyph.

Conclusion for whoever implements this: a *complete* content signature over 200
inputs, several of which are live engine globals sampled during the paint, is
both expensive to compute and fragile — any future painter change breaks it
silently, and the failure mode is a frozen screen. Prefer bounded staleness: the
quantised animation terms above, plus the existing snapshot memcmp, plus a
**forced repaint every N ticks** as a safety net. That way an unenumerated input
costs at most N ticks of staleness instead of freezing forever.

## MAP-tab repaint skipping, as implemented

`bottom_map_anim_3ds.{c,h}` plus `bottom_map_anim_3ds_test`. The design is the
one the section above settled on — quantised animation terms, snapshot memcmp,
forced repaint every N ticks — with three things worth recording because they
differ from the plan or were only settled by reading the code.

**The tick fix is in the scheduler, not the signature.** `sBottomTick` now
advances wherever `cadenceDue` is computed in `port_ppu_3ds.c`, not inside the
`schedulePaint` branch. Advancing on the cadence rather than per paint is not a
behaviour change in practice: the dump shows 2230 paints against 2229 periodic
checks over 13376 frames at a 6-frame cadence (13376/6 = 2229), so paints
already tracked the cadence 1:1 and the animation rate is preserved. Deriving
the tick from `sFrameNumber / bottomInterval` instead — the other option the
earlier section floated — was rejected because `bottomInterval` changes from 6
to 30 when the developer overlay opens, which would make the tick jump
*backwards* by 5x and corrupt every deadline compared against it.

**The pulse term is quantised on the drawn integer, not the phase.** `u` is
`min(w,h)/720`, so on the 320x240 bottom screen `u = 1/3` and
`(int32_t)((6.5 + 1.5*sin(2*pi*(tick%32)/32)) * u)` takes exactly two values,
1 and 2, switching at tick 18 and 31. Quantising the 32-tick phase instead
would change every tick and save nothing. The test asserts the collapse to two
values, so if the layout ever scales `u` up the test fails rather than the
optimisation silently evaporating.

**Two of the notes' inventory entries were wrong.** The `(tick & 8)` cursor and
`sinf(tick % 48)` breath at `port_second_screen.c:1495`/`:1501` are in
`PaintItemsPanel`, i.e. the ITEMS tab, which already forces a repaint
unconditionally — they are not MAP terms and are not in the signature. The
dungeon room-palette term is real and is the binding constraint: it lives in
`port_second_screen_dungeonmap.c:424` as `8 + (((tick*3)>>3) & 7)`, changing 3
times per 8 ticks, which caps the dungeon skip ratio at 2.67x against the
overworld's 5.33x.

Also folded in, because skipping paints would otherwise stall them: the region
bracket (`port_second_screen.c:1201`) and the floor preview (`:1377`) are state
machines that *retire inside the paint*. Both force a repaint while live rather
than being encoded in the signature.

What the test actually proves, beyond the counts: it compares the signature
against independently re-derived reference expressions over all 256x256 tick
pairs and asserts the signature partitions ticks *exactly* as the drawn picture
does. That catches both a missed animation (stale screen) and a hash collision
(skipped real change) — the latter being the failure the polynomial hash could
otherwise hide. Measured on the host: overworld 768/4096 paints with a maximum
gap of 8 ticks, dungeon 1536/4096 with a maximum gap of 3.

Unverified on hardware. The host can falsify this but not confirm it.
