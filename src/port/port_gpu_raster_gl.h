#pragma once
/*
 * port_gpu_raster_gl.h — GLES 3.1 compute PPU rasterizer (second GPU backend).
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SDL_GPU (port_gpu_raster.*) covers Vulkan/D3D12/Metal. This backend adds
 * OpenGL ES 3.1 compute for the large population of GLES-3.1 devices without a
 * usable Vulkan driver (e.g. Adreno 4xx / msm8952 — the Moto G4). Same shared
 * shader logic (port/shaders/ppu_core.glsl), same per-line OBJ cull, same
 * bit-exact target vs the CPU rasterizer; see docs/gpu-rasterizer-design.md.
 *
 * Runs on an isolated EGL context (created here, current SDL context saved and
 * restored around each render), renders one GBA frame with a compute shader
 * into an output SSBO, and reads it back to a caller buffer — the present path
 * (SDL_Renderer, since these devices have no Vulkan) consumes it unchanged.
 */

#include "port_gpu_raster.h" /* PortGpuRasterFrame */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortGpuRasterGl PortGpuRasterGl;

/* Create the GLES compute rasterizer from the shared core GLSL (the bytes of
 * port/shaders/ppu_core.glsl). Creates an isolated EGL/GLES-3.1 context and
 * builds the compute program. Returns NULL on any failure (no EGL, no GLES 3.1,
 * too few compute SSBOs, shader/program build) so the caller falls back to CPU.
 * Safe to call with no current GL context. */
PortGpuRasterGl* Port_GpuRasterGl_Create(const char* core_glsl, int core_len);

void Port_GpuRasterGl_Destroy(PortGpuRasterGl* r);

/* Render frame `f` on the GPU and read it back into `dst` (ABGR8888, `pitch`
 * pixels per row). Returns false on any GL error so the caller uses the CPU
 * rasterizer. Saves/restores the caller's EGL context. */
bool Port_GpuRasterGl_RenderReadback(PortGpuRasterGl* r, const PortGpuRasterFrame* f, uint32_t* dst, int pitch);

#ifdef __cplusplus
}
#endif
