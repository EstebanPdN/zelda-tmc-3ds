# tmc_vk_rt_experiment

Standalone Vulkan-1.3 + KHR ray-tracing scaffold for an experimental
pseudo-3D-projected render path. **Not linked into `tmc_pc`** — build
and run independently.

## Status

Foundational architecture only. Compiles against the Vulkan SDK +
SDL3 + glslang. Has not been executed on real hardware; the BLAS/
TLAS, SBT, and pipeline setup are spec-conformant per the headers,
but per-iteration debugging via Renderdoc / validation layers is
still needed before any of it is trusted.

## Files

| File                          | Purpose                                |
|-------------------------------|----------------------------------------|
| `Engine.{h,cpp}`              | Vulkan instance/device/swapchain/loop  |
| `RenderLayerManager.{h,cpp}`  | 2D draw-call → 3D quad batch + upload  |
| `RayTracingPipeline.{h,cpp}`  | BLAS, TLAS, pipeline, SBT, dispatch    |
| `shaders/raygen.rgen`         | Camera ray generation (orthographic)   |
| `shaders/miss.rmiss`          | Sky colour + shadow-miss               |
| `shaders/closesthit.rchit`    | Hit shading + soft shadow trace        |

## Build prerequisites

- Vulkan SDK 1.3+ (`vulkan-headers`, `vulkan-loader`,
  `glslangValidator`)
- SDL3 development headers
- Hardware exposing `VK_KHR_ray_tracing_pipeline` —
  NVIDIA RTX 20xx+, AMD RX 6000+, or Intel Arc with up-to-date drivers.
  **Will not run on Apple Silicon** (Vulkan RT not available on Metal).

## Compiling shaders

```sh
glslangValidator --target-env vulkan1.3 -V shaders/raygen.rgen     -o shaders/raygen.rgen.spv
glslangValidator --target-env vulkan1.3 -V shaders/miss.rmiss      -o shaders/miss.rmiss.spv
glslangValidator --target-env vulkan1.3 -V shaders/closesthit.rchit -o shaders/closesthit.rchit.spv
```

## Compiling the C++ side

The experiment is intentionally isolated from `xmake.lua`'s main
build to keep `tmc_pc` unaffected. A minimal manual build:

```sh
g++ -std=c++20 -O2 \
    -DVK_NO_PROTOTYPES=0 \
    Engine.cpp RenderLayerManager.cpp RayTracingPipeline.cpp \
    main.cpp \
    -lSDL3 -lvulkan
```

(`main.cpp` is the driver loop the user provides — instantiates
Engine, RenderLayerManager, RayTracingPipeline, then per frame
issues drawSprite/drawBgQuad → flushToBuffers → rebuildAS →
dispatchRays.)

## Integration with tmc_pc (future)

The eventual hook would be in `port/port_ppu.cpp`: instead of
calling `Port_GPU_PresentFrame(virtuappu_frame_buffer, ...)`, the
runtime would iterate the GBA's OAM table + BG layers, calling
`RenderLayerManager::drawSprite/drawBgQuad` per visible element with
the original 16×16 / 8×8 tile UVs into a pre-baked diffuse atlas.
The atlas is the existing extracted graphics under `assets/`; the
RT path would build it once at boot.

This is a multi-week integration project — not on the current
roadmap.
