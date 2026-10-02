# Performance: render thread cap (2026-06-08)

## TL;DR

The software PPU rasterizer is the port's dominant CPU cost (**~80% of frame
time**). Its OpenMP scanline parallelism was **oversubscribing** on machines
where it used every physical core: a spin-wait barrier under full subscription
thrashes and the render gets **~4× *slower* than the optimum**. Fixing the
worker count (`omp_set_num_threads` in `Port_PPU_Init`) cut the in-game render
from **~4.5 ms/frame to ~1.07 ms/frame** (~4.2×) on an 8-core box, with a
**byte-identical framebuffer** (zero behavioural change).

## How it was measured

All from a real in-game scene (4 BGs + OBJ; `dispcnt=0x1f40`), no perf/valgrind
needed:

- **Phase timers** — `TMC_PROFILE=1` (gated, off by default; `port_bios.c`
  `VBlankIntrWait` + a render bracket in `port_ppu.cpp`). Prints a rolling
  average every 600 frames decomposing each frame into *game* (engine + entity
  update), *present* (all of `Port_PPU_PresentFrame`), and *render*
  (`virtuappu_render_frame`). Run uncapped (`config.json: frame_time_ns=0`) so
  pacing doesn't pollute the *game* bucket.
- **Snapshot harness** — `TMC_PERFCAP=1` (`port/port_repro_perfcap.c`) drives
  title→file-select→game, settles, and dumps a *complete* PPU snapshot
  (IO + VRAM + BG/OBJ palette + OAM — unlike `port_quicksave.c`, which omits
  palette/OAM) to `TMC_PERFCAP_DUMP` (default `/tmp/tmc_ppu_snapshot.bin`).
  `TMC_PERFCAP_WARP="area,room,x,y,layer"` targets a specific scene.
- **Microbench** — `tools/ppu_bench.c` links the real `libs/ViruaPPU/src/mode1.c`
  and replays the snapshot with no engine running, so render cost is isolated and
  optimizations are A/B'd against a framebuffer checksum (the parity guard).
  Build:
  ```
  gcc -O3 -mavx2 -mfma -fopenmp -I libs/ViruaPPU/include -DMODE1_GBA_WIDTH=240 \
      tools/ppu_bench.c libs/ViruaPPU/src/mode1.c -o /tmp/ppu_bench -lm
  /tmp/ppu_bench /tmp/tmc_ppu_snapshot.bin 4000
  ```

## The data

Microbench, single in-game frame, varying OpenMP threads on an 8-core (no HT)
i7-9700; checksum identical at every thread count:

| threads | render ms/frame | note |
|--------:|----------------:|------|
| 1 | 1.47 | serial baseline |
| 2 | 0.77 | 1.9× |
| 3 | 0.50 | |
| 4 | 0.39 | 3.8× |
| 5 | 0.30 | |
| 6 | **0.28** | best (5.3×) |
| 7 | 0.35 | |
| 8 | **5.02** | **collapse** (full subscription, spin barrier) |
| 8 + `OMP_WAIT_POLICY=passive` | 0.53 | collapse avoided (threads sleep at barrier) |

Scaling is near-linear up to 6; at 8/8 cores the spin-wait barrier starves a
straggler whenever the OS / main / audio thread needs a core, and the whole
parallel-for stalls. `passive` confirms the diagnosis (no collapse when finished
threads sleep instead of spinning).

## The fix

`port/port_ppu.cpp`, `Port_PPU_Init` (port-side; **no ViruaPPU patch** — the
mode1 render pragma has no `num_threads` clause, so the process `nthreads`
default governs it):

```c
#ifdef _OPENMP
    n = omp_get_num_procs() - 1;   // reserve a core for main/audio/OS
    if (n > 6) n = 6;              // 160 scanlines see no gain past ~6
    if (n >= 1) omp_set_num_threads(n);
#endif
```

- `TMC_RENDER_THREADS=N` forces a value (tuning/testing).
- An explicit `OMP_NUM_THREADS` is left untouched (power-user override).
- Floors to serial on 1–2 core hosts (a 2-core P4 runs 1 thread — full
  subscription on few cores is exactly the failure mode, and the GBA logic is
  light enough that serial render is the relevant path there anyway).

### In-game result (same scene, uncapped, default config)

| | render ms/frame | uncapped fps |
|---|---:|---:|
| before (8 threads) | ~4.5 | ~175 |
| after (6-thread cap) | ~1.07 | ~510 |

## Parity

The change is timing-only. `tools/ppu_bench` reports an FNV-1a checksum of the
full 240×160 framebuffer; it is **`0x85577c051954a49b` at 1, 6, and 8 threads**.
`schedule(static)` over independent scanlines is output-invariant in the worker
count by construction; the checksum confirms it.

## Why this matters for the legacy / low-power target

The collapse meant multicore parallelism was a *net loss* on full-core machines,
and the cap makes per-core scaling actually function — e.g. a 4-core Pi 3 gets a
real ~3× from its cores (3 threads) instead of thrashing. It also frees ~3
ms/frame of CPU on desktop, which directly buys headroom toward a locked 60 fps
on constrained hardware.

## Next opportunities (not yet done)

- **Single-core render** (relevant to a 1–2 core Pentium 4, where threading
  can't help): the serial split is composite 0.79 ms + bg×4 0.62 ms + obj
  0.045 ms. `virtuappu_mode1_composite_line`'s per-pixel priority×BG search is
  the largest serial item — a candidate for a parity-preserving rewrite, but it
  lives in ViruaPPU (must be a `port/patches/viruappu-*.patch`) and should be
  validated with `tools/ppu_bench` against the checksum before landing.
- **`io_snapshots` memcpy** in `virtuappu_mode1_render_frame` copies the 1 KB IO
  block per scanline even with no active HDMA; it can be skipped (point all
  lines at the shared IO) when `virtuappu_mode1_pre_line_callback == NULL`.
  Small (~tens of µs) but free; also a ViruaPPU patch.

## Unrelated bug noticed (not fixed here)

Clean process exit (`exit(0)`, e.g. a repro harness or normal Quit) triggers a
`std::terminate` (`std::length_error` from a `std::string` op in a static
destructor / atexit handler) → SIGABRT. Harmless to the perf work (all data is
emitted before exit) and pre-existing, but it would also abort a real
"Save & Quit". Worth a separate look.
