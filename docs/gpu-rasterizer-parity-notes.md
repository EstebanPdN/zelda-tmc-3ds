# GPU Rasterizer — parity notes

Companion to `docs/gpu-rasterizer-design.md`. Records the parity status of the
GPU PPU rasterizer against the CPU software rasterizer (the golden reference),
including any documented, unavoidable driver deltas.

## Status: bit-exact

As of the initial implementation, the GPU rasterizer is **bit-exact** to the CPU
rasterizer on every tested scene and backend. There are **no documented deltas**.

## How it's verified

Two independent gates, both integer-only (so results are portable across
compilers/CPUs/GPUs):

1. **Synthetic harness** — `tools/ppu_gpu_parity` (xmake target
   `ppu_gpu_parity`). Renders 42 hand-built GBA memory states through BOTH the
   CPU rasterizer (`virtuappu_render_frame`) and the GPU rasterizer, then diffs
   the two framebuffers pixel-for-pixel. Covers: forced-blank/backdrop; text BGs
   (scroll, flip, 4bpp/8bpp, mosaic, 256/512 sizes, priority, transparency);
   OBJ (regular + affine, 1D/2D, double-size, XY wrap, mosaic, overlap order);
   windows (win0/win1/objwin, h/v wrap); blending (alpha/brighten/darken,
   semi-transparent OBJ, backdrop-as-2nd-target); affine BG2 (identity, scale,
   rotate, wrap, OOB-transparent); and — in a 384-wide build — the widescreen
   shadow reveal, sentinel force-black, HUD right-anchor, and message centering.

   Run:
   ```bash
   xmake build -y ppu_gpu_parity
   SDL_VIDEODRIVER=offscreen ./build/pc/ppu_gpu_parity        # native 240
   # widescreen (compile-time width, mirrors the CPU constant):
   g++ -O2 -std=c++17 -fopenmp -DMODE1_GBA_WIDTH=384 \
     -I port/ppu/include -I port $(pkg-config --cflags sdl3) \
     tools/ppu_gpu_parity.cpp port/port_gpu_raster.cpp \
     port/ppu/src/virtuappu.c port/ppu/src/mode1.c \
     $(pkg-config --libs sdl3) -lm -o /tmp/ppu_gpu_parity384
   SDL_VIDEODRIVER=offscreen /tmp/ppu_gpu_parity384
   ```

2. **Real-ROM end-to-end** — the live game's Tier-B frame hash (the FNV-1a
   checksum `TMC_PERFCAP` prints from `virtuappu_frame_buffer`) must match the
   CPU golden in `tools/ppu_golden_hashes.txt`. Because the GPU path reads the
   rendered frame back into `virtuappu_frame_buffer`, the standard perfcap hash
   exercises it directly — just run under a GPU-capable video driver:
   ```bash
   TMC_AUTOPLAY=1 TMC_PERFCAP=1 TMC_PERFCAP_AT_FRAME=150 \
     TMC_COLOR_CORRECTION=0 TMC_LCD_PERSISTENCE=0 \
     SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
     TMC_BASEROM=baserom.gba ./build/pc/tmc_pc --no-audio
   # intro_logo -> 0x7d7d9e2cde85b393 ; title -> 0x1dec0c73f65d112b (USA)
   ```

## Why bit-exactness holds

The GBA colour path is pure integer arithmetic. The shader reproduces it exactly:

- **Palette:** `rgb555 -> rgba` uses `(c & 0x1F) << 3` per channel; the `<<3`
  values are multiples of 8, so `/255.0` round-trips through `R8G8B8A8_UNORM`
  without rounding error.
- **Blending:** done directly in 5-bit rgb555 space. The CPU recovers 5-bit
  channels via `>>3` from its 8-bit framebuffer; the shader's rgb555 channels
  *are* those 5-bit values, so `(a*eva + b*evb) >> 4` (clamped to 31) is
  identical, as are the brighten/darken forms.
- **Addressing:** all divides/moduli are by powers of two (tile 8, map sizes,
  screenblocks), so they compile to masks/shifts — no divide-precision drift.

## Known non-parity behaviours (by design, CPU fallback)

- **Swamp-sink OBJ vertical clip** (`virtuappu_mode1_obj_clip_*`): not
  implemented in the shader. When active (Castor Wilds sinking), the frame uses
  the CPU rasterizer for that frame. Tracked as a possible future shader addition
  (a small per-OAM clip-mark uniform).

## Second backend: OpenGL ES 3.1 compute (no-Vulkan devices)

For GLES-3.1 devices without a usable Vulkan driver (Adreno 4xx / msm8952 — the
Moto G4), a second GPU backend (`port/port_gpu_raster_gl.cpp`) runs the SAME
shared logic (`port/shaders/ppu_core.glsl`) as a compute shader built at runtime
(prologue + core + `main`). It is **bit-exact** to the CPU rasterizer: all 42
harness scenes pass at 240 + 384 under Mesa GLES 3.2, diffed alongside the
Vulkan path (`ppu_gpu_parity` runs both). `packUnorm4x8` rounds identically to
the CPU LUT because the colours are exact multiples of `8/255`.

**It is OPT-IN (`gpu_raster_gles`, default off) — never auto-enabled.**
On-device benchmark, Moto G4 / Adreno 405, title screen (mode-2 affine + OBJ):

| Backend | render | present | fps |
|---|---|---|---|
| CPU (3-thread A53) | ~4.1 ms | ~14.7 ms | 59–60 |
| GLES compute (Adreno 405) | ~21 ms | ~32 ms | 27–30 |

The weak tiled GPU's compute throughput plus the readback stall lose ~5x to the
already-tuned CPU rasterizer, and the per-line OBJ cull doesn't close the gap.
So the default fallback chain is **Vulkan → CPU**; GLES is only tried when the
user opts in (F8 Display → "GLES raster (exp.)"), for the case of a stronger
GLES-only GPU where it might win. It needs re-benchmarking per device.

## Platform verification matrix

| Platform / backend | Shader | Status |
|---|---|---|
| Linux x86_64 / Vulkan | SPIR-V | Verified bit-exact (harness + live ROM) |
| Linux software (`--gpu_renderer=n`) | — | CPU path; parity gate passes |
| Android arm64 / Vulkan | SPIR-V | Same SPIR-V path; module compiles (NDK aarch64) — device runtime check pending (no Vulkan on the G4 to test) |
| Android arm64 / GLES 3.1 compute | GLSL ES (runtime) | Verified on Moto G4 / Adreno 405: renders correctly, bit-exact vs CPU in the harness; opt-in (slower than CPU there) |
| Windows / Vulkan | SPIR-V | Same SPIR-V path as Linux — runtime check pending on device |
| macOS·iOS / Metal | MSL (spirv-cross) | Format-aware path in place; MSL generated by `build.sh` when spirv-cross is available, else CPU fallback — runtime check pending on device |

Regenerate the committed shader blobs (SPIR-V always; MSL when spirv-cross is
installed) with `port/shaders/build.sh`. The GLES backend builds its compute
program from the committed `ppu_core.glsl` at runtime (no offline step).

## Readback optimization + performance findings

The GPU raster path reads its result back to `virtuappu_frame_buffer` each frame
(so the present/filter/screenshot/shm consumers are unchanged). That readback is
the cost centre. Measured on desktop (Intel Arc / Vulkan, 240×160, busy 2-BG
scene, 2000 iters):

| Path | cost | notes |
|---|---|---|
| separate Render + Readback (2 submits/fences) | 2.01 ms | original |
| **merged RenderReadback (1 submit / 1 fence)** | **1.77 ms** | −12%; current default (`Port_GpuRaster_RenderReadback`) |
| **deferred (submit N, poll N-1, no CPU stall)** | **0.77 ms CPU-side** | −62%; opt-in API (`Port_GpuRaster_RenderReadbackDeferred`), 1-frame stale |
| CPU rasterizer (reference, same scene) | ~0.40 ms | — |

**Conclusion (measured + externally corroborated): GPU rasterization of a
240×160 GBA frame cannot beat a competent CPU rasterizer**, because the frame is
tiny and the per-frame upload + submit + readback overhead exceeds the actual
rasterize cost. This is a known result for GBA emulation:

- mGBA and other GBA emulators default to *software* rasterization precisely
  because "the GBA's internal resolution (240×160) is extremely small... the
  entire GBA frame can be rasterized in software extremely quickly [and] avoids
  the latency of transferring data between system RAM and VRAM." GPU (OpenGL)
  rendering is introduced for *upscaling, clean rotation/scaling (Mode 7), and
  post-processing shaders* — not to make native-res rendering faster; it can be
  *slower* than software "despite the GPU's theoretical power." (mGBA docs /
  emulation-dev consensus.)
- The readback stall is a pipeline sync point: a texture download "forces the
  CPU to wait until the GPU has completely finished... serializ[ing] your engine,
  effectively killing your framerate regardless of how small the texture is."
  The prescribed fix is double/triple-buffered async readback (PBO
  ping-ponging), "access the data 2–3 frames later... CPU overhead drops
  significantly." Our deferred ring (`SDL_QueryGPUFence` over 2 download
  transfer buffers) is the SDL_GPU equivalent, and the 0.77 ms figure confirms
  the win.

### What this means for the defaults (as shipped)

- **The game's Vulkan raster path uses the DEFERRED readback in normal play**
  (submit frame N, return frame N-1 via `SDL_QueryGPUFence` — no per-frame CPU
  stall). Measured live (desktop autoplay): GPU-raster present **~2.4 ms → ~1.2
  ms**, render timer **~2.1 → ~1.1 ms**, 60 fps. Cost: 1 frame of display
  latency.
- **Under `TMC_PERFCAP` it switches to the synchronous merged path** so the
  byte-exact golden hash reflects the current frame — the parity gate and
  `tools/ppu_gpu_parity` stay current-frame-exact (intro_logo/title golden,
  76/76 scenes bit-exact). Screenshot/shm consumers tolerate the 1-frame lag in
  play.
- Even fully optimized, native-res GPU raster still costs more CPU than the CPU
  rasterizer alone (upload ~0.8 ms > CPU raster ~0.45 ms); deferred's value is
  removing the *stall*, not beating the CPU at 240×160. It hits 60 fps either
  way on desktop.
- The remaining structural win is **direct-present** (render straight into the
  swapchain via `SDL_WaitAndAcquireGPUSwapchainTexture` / `SDL_BlitGPUTexture`,
  no readback at all) — the only path that removes the bus transfer entirely,
  and the only one that could make GPU raster a net win. It requires moving the
  CPU-side frame consumers (colour-correction, LCD persistence, xBRZ,
  internal-scale, shm) onto the GPU first. Tracked as the next step if/when
  high-res GPU output is prioritised.
- GPU backends stay opt-in-safe (Vulkan auto where present, GLES opt-in).
