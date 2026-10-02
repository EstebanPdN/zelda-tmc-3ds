# Old 3DS Exact-Hybrid Performance Design

## Goal

Improve original Nintendo 3DS performance as far as measured, safe changes allow while preserving game timing, audio, rendering output, widescreen, both screens, and New 3DS behavior.

The final acceptance run is one real Old 3DS hardware test. Before that run, changes must pass host-side correctness and performance gates.

## Current architecture

`platform/3ds/source/port_ppu_3ds.c` renders the GBA frame with the software VirtuaPPU directly into linear memory. `platform/3ds/source/platform_gpu_3ds.c` then uses the 3DS display engine and PICA200 through Citro3D/Citro2D to tile, scale, filter, composite, convert to the physical RGB565 displays, and retain an unchanged bottom target.

The current renderer already uses the Old 3DS application core, scanline workers, common-scene fast paths, adaptive presentation skipping, static bottom-screen reuse, and GPU presentation. The remaining dominant work is the accuracy-sensitive GBA PPU.

## Decision

Use an exact hybrid:

1. Keep the software GBA PPU as the correctness oracle.
2. Reduce CPU-to-GPU memory traffic and let GSP/PICA continue handling operations they express exactly.
3. Optimize only measured software-PPU hot paths, retaining the generic renderer as fallback.
4. Leave New 3DS behavior unchanged unless a shared change is byte-exact and benchmark-neutral or better there.

Do not implement a second PICA GBA renderer. PICA200 has a programmable vertex stage but a fixed-function fragment combiner; arbitrary GBA windows, per-line HDMA, OBJ ordering, palette lookup, and blend rules would require substantial CPU-generated geometry and fallback churn. With only one hardware measurement at the end, that path has unacceptable correctness and performance risk.

## Presenter changes

### Compact Old 3DS upload surfaces

Use aligned linear source pitches based on visible content:

- Top: 272 pixels by 160 rows. This accommodates the maximum 266-pixel widescreen frame and keeps an 8-pixel-aligned GSP source pitch.
- Bottom: 320 pixels by 240 rows.

Keep both tiled PICA textures at 512 by 256 because texture dimensions remain power-of-two. Pass the compact source dimensions to `C3D_SyncDisplayTransfer`; GSP remains responsible for converting the linear source into the tiled texture.

Compared with the current 512-pixel source pitch, this reduces top cache-flush and display-transfer input from 327,680 to 174,080 bytes per presented frame, a 46.9% reduction. A changed bottom frame falls from 491,520 to 307,200 bytes, a 37.5% reduction.

The renderer must continue writing directly into the upload surface; no conversion or copy pass is allowed.

### GPU command and cache traffic

Retain the existing asynchronous in-frame GSP transfer ordering and unchanged-bottom target reuse. Tighten Citro2D cache flushing only if both vertex and index buffer ranges can be proven from public state; otherwise retain the current bounded 64 KiB flush. Do not replace it with Citro3D's whole-linear-heap flush.

Do not add frame latency, deferred readback, additional presentation skipping, or a reduced-resolution mode.

## Software PPU changes

Use the existing Old 3DS path counters and `tools/ppu_bench.c` to identify the next dominant exact path. Candidate work is restricted to:

- `mode1_render_old3ds_field_alpha_line`;
- `mode1_render_native_direct_no_effect_line`;
- `mode1_render_native_compact_line`;
- scanline work distribution and frame-constant preparation used by those paths.

Every candidate must reuse the generic renderer as the parity oracle. No scene-specific output substitution is allowed. A candidate is retained only when repeated benchmark medians improve by at least 3% for its workload and do not regress another representative workload by more than 1%.

No new renderer abstraction, dependency, runtime setting, or format is introduced.

## Data flow

1. Engine updates GBA memory and IO state.
2. VirtuaPPU snapshots per-line state and renders scanlines on the available Old 3DS CPU cores.
3. VirtuaPPU writes visible pixels directly into the compact top linear upload surface.
4. The bottom worker writes into a compact double-buffered bottom upload surface only when its generation requires repainting.
5. GSP display transfer tiles the compact sources into the existing PICA textures.
6. PICA scales, filters, channel-swizzles, composites, and converts render targets to physical RGB565 displays.
7. Existing frame pacing skips presentation only when accumulated Old 3DS debt requires it; engine timing is unchanged.

## Correctness and fallback

- Allocation or GPU initialization failure retains the existing initialization failure behavior.
- Compact pitches are Old 3DS-only until hardware acceptance proves them; New 3DS keeps the established 512-pixel source pitch.
- All render modes, HDMA, affine backgrounds and objects, windows, mosaic, blending, color correction, and widescreen continue through existing code paths.
- Bottom-screen generation, visible-generation promotion, and touch hitbox synchronization remain unchanged.
- No silent visual downgrade is permitted.

## Verification

Before the hardware run:

1. Run `mode1_native_fast_path_test` with generic-vs-fast-path parity, including the Old 3DS profile.
2. Run the deterministic PPU parity gate for every locally available corpus scene.
3. Run focused 3DS host tests for frame pacing and bottom frame state.
4. Cross-build the 3DS target and inspect size/link failures.
5. Benchmark each retained PPU change with repeated medians; record before and after values.
6. Verify New 3DS code paths retain their established pitch, worker count, and presentation behavior through focused tests or compile-time assertions.

Final Old 3DS acceptance:

- Capture the existing diagnostics dump after representative field gameplay.
- Compare average/current visual FPS, presentation skips, frame intervals over 16/33 ms, PPU total and per-core timing, GPU drawing/processing time, bottom paints/transfers, audio deadline misses, and logic cadence.
- Accept only if logic remains 60 Hz, audio deadline misses do not increase, visuals are unchanged, and presentation cadence or PPU time improves without a material regression in another recorded subsystem.

## Scope exclusions

- Full PICA200 GBA rasterizer.
- Reduced effects, reduced width, reduced resolution, or extra frame skipping.
- New dependencies or public configuration.
- Unrelated engine, UI, audio, or save-system refactoring.
