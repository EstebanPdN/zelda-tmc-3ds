# Old 3DS PICA200 PPU Renderer Design

**Status:** Approved 2026-08-18

## Goal

Move the top-screen GBA PPU rasterization from the Old 3DS ARM11 cores to the PICA200 while preserving the current software PPU as the byte-exact reference and automatic fallback. New 3DS behavior remains unchanged until the Old 3DS path is proven.

The retained implementation must:

- cover every tiled, affine, sprite, window, mosaic, blend, and widescreen state used by TMC;
- produce no accepted visual mismatch against the software PPU;
- survive GPU initialization, command-capacity, and unsupported-state failures by rendering that frame in software;
- improve the deterministic Old 3DS item-get capture from its 43.75 FPS baseline by at least 10%; otherwise the GPU path is removed.

## Hardware constraint

PICA200 has programmable vertex shaders but no programmable fragment or compute shader. The existing integer fragment-shader rasterizer in `port/port_gpu_raster.*` therefore cannot run on this hardware. Raw GBA palette indices also cannot be resolved through a dependent palette lookup in the fixed fragment pipeline.

The GPU path must consequently decode changed tiles on the CPU, then offload visibility, transforms, rasterization, priority, windows, effects, composition, and final scaling to PICA200.

## Chosen architecture

### Frame preparation

`Port_PPU_PresentFrame()` keeps the existing DISPCNT mode mapping and HDMA hooks. On Old 3DS it calls `virtuappu_mode1_prepare_frame()` instead of the complete software renderer. That preserves the existing sequential behavior:

1. run the pre-line HDMA callback;
2. snapshot uniform or per-line IO state;
3. capture per-line DISPCNT;
4. precompute affine BG2 references.

This preparation is the GPU input. If GPU preflight later rejects the frame, the port calls `port_hdma_vblank_reset()` and runs the existing complete software renderer, matching the established desktop GPU-fallback pattern. This avoids a second prepared-frame CPU API and preserves the callback's frame-start semantics.

### Tile cache

A new 3DS-only renderer owns one persistent 512x512 `GPU_RGBA5551` atlas containing 4096 aligned 8x8 slots. Each slot is keyed by:

- BG or OBJ palette domain;
- VRAM tile address;
- 4bpp or 8bpp format;
- 4bpp palette bank where applicable.

The cache stores the source tile bytes and palette generation used for each slot. At frame start, the renderer compares the small BG and OBJ palette banks with their shadows and increments only the affected generations. At tile lookup it compares the source 32 or 64 bytes. A mismatch decodes that tile directly into the atlas's swizzled 8x8 slot and flushes only the changed range.

No VRAM write hooks, speculative invalidation system, or second copy of all VRAM is added. Slots referenced by the current frame are pinned until submission; unpinned slots use LRU replacement. If one frame needs more than 4096 live variants, preflight falls back instead of evicting a tile that queued geometry still references.

Palette index zero decodes with alpha zero. Other entries expand the same 5-bit channels as the software PPU and use alpha one.

### Geometry and state bands

The CPU walks the visible tilemaps and OAM once per frame and emits batched quads into a bounded linear-memory vertex buffer. Geometry covers:

- text BG0-BG3, scroll, map sizes, char/screen bases, 4bpp/8bpp, flips, and priorities;
- affine BG2 references, wrap/overflow, and transformed repeated map coverage;
- regular and affine OBJ, double-size, 1D/2D mapping, priority, flips, and semi-transparency;
- BG and OBJ mosaic by subdividing only the affected geometry at mosaic boundaries;
- the existing widescreen shadow columns, HUD anchoring, message shift, and sprite clipping.

Consecutive scanlines with identical relevant IO state form one state band. HDMA effects with different per-line scroll, affine, window, or blend state form smaller bands, down to one scanline when required. A band supplies its scissor and transform state without changing the prepared HDMA result.

Atlas-slot, vertex, and command capacities are checked during preflight, before `C3D_FrameBegin()`. Exhaustion returns `false` and invokes software fallback for that frame; memory is never grown during gameplay.

### Native render target and composition

PICA200 renders into a 256x256 `GPU_RGBA5551` texture using a 240x160 or widescreen viewport and a depth24/stencil8 attachment. The existing presenter samples the native subtexture and applies the selected top-screen scaling. The successful GPU path removes the current CPU framebuffer upload.

Layers are drawn back-to-front in exact GBA priority and tie-break order. Alpha test discards palette-index-zero texels, leaving the previous color and stencil state intact.

Window handling uses:

- scissored intervals for WIN0 and WIN1, including wrapped ranges and precedence;
- OBJ-window sprite geometry writing a dedicated stencil bit without color writes;
- separate region passes only where WININ/WINOUT layer or effect masks differ.

A second-target stencil bit records whether the currently visible destination pixel belongs to the active BLDCNT target-2 set. A target-1 or semi-transparent OBJ draw uses two complementary stencil passes: hardware blend where the destination bit is set, opaque replacement where it is clear. Each successful layer write replaces only the second-target bit for the next layer. Brighten and darken use fixed TEV constants and the same region gating.

The RGBA5551 target quantizes every stored channel to the GBA's 5-bit domain. PICA200 blend rounding is not assumed correct; it is a hardware parity gate.

### Presenter integration

`platform_gpu_3ds` gains only the low-level boundaries needed to share one Citro3D frame:

- begin a frame for custom top rendering;
- draw a supplied native top texture through the existing scale/aspect/FPS path;
- retain the current software-upload entry point;
- finish the unchanged bottom-screen path and frame submission.

The PPU renderer does not own global Citro3D initialization or bottom-screen rendering. New 3DS continues calling the current software upload path.

## Fallback and reversibility

The software renderer remains compiled and unchanged in responsibility. Before `C3D_FrameBegin()`, GPU preflight rejects any of these:

- resource or shader initialization failure;
- unimplemented or invalid PPU state;
- atlas, vertex, or command-capacity exhaustion;
- diagnostic parity mismatch.

Preflight rejection resets HDMA and runs the complete software renderer through the existing upload path in the same frame. A diagnostic parity mismatch additionally disables GPU rendering for the rest of the process so repeated bad frames cannot flicker between renderers.

Failure after `C3D_FrameBegin()` follows the presenter's existing dropped-frame behavior because no valid Citro3D frame remains for a same-frame software upload. It disables GPU rendering so the next frame uses software; it does not attempt a second submission against corrupted GPU state.

The work stays isolated on `old3ds-performance`; reverting the GPU renderer does not require reverting the established software PPU or presenter optimizations.

## Verification

### Host checks

Small deterministic tests cover behavior independent of PICA200:

- 4bpp and 8bpp tile decode, transparency, flip, and palette invalidation;
- exact BG/OBJ priority and draw-order generation;
- state-band grouping and wrapped window intervals;
- capacity failure returning fallback rather than truncating output;
- existing software PPU parity corpus remaining unchanged.

The existing captured PPU states and `port/port_gpu_raster.*` output remain the semantic oracle for command generation.

### Hardware parity mode

A diagnostic build renders selected captured frames through both paths from the same prepared state. It copies the PICA200 native target back to linear memory, compares all visible pixels with the software framebuffer, records the first differing coordinate and total differences, then disables GPU rendering on any mismatch.

Coverage must include title/file-select, normal field gameplay, item-get alpha effects, affine scenes, OBJ windows, mosaic, fades, and widescreen columns. Blend states are accepted only after this check proves the PICA200 RGBA5551 result matches the software 5-bit result. A failing blend state falls back to software; no tolerance threshold is used.

### Physical performance gate

Use the existing deterministic Gust Jar item-get reproduction and diagnostics on the same Old 3DS:

- baseline: 43.75 presented FPS, 13.511 ms average software PPU render;
- compare presented FPS, engine cadence, CPU preparation/build time, PICA drawing/processing time, missed 16/33 ms intervals, audio deadline misses, and underruns;
- confirm music and effects remain audible without crackling;
- retain the renderer only at 48.13 FPS or better with zero parity failures and no worse audio behavior.

Azahar is a visual and lifecycle smoke test only; its uninitialized NDSP path is not audio evidence.

## Rejected alternatives

- **Direct port of the desktop fragment shader:** impossible on PICA200 because there is no programmable fragment or compute stage.
- **CPU-rendered full layer textures with GPU-only composition:** safer but retains most tile/sprite raster work and adds multiple full-surface uploads, so the expected Old 3DS gain is too small.
- **Replace the software PPU:** removes the only byte-exact oracle and makes unsupported states destructive rather than recoverable.
- **Runtime configuration framework:** unnecessary. Hardware/model detection, parity, and failure determine the path automatically.
