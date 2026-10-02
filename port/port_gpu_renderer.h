#pragma once

/*
 * port_gpu_renderer.h — SDL_GPU-backed presentation path (Stage 1 scaffold).
 *
 * The default presentation goes through SDL_Renderer in port_ppu.cpp.
 * SDL_Renderer doesn't expose custom fragment shaders, so adding
 * libretro-style shader filters (CRT, scanlines, lcd-grid as
 * single/multi-pass GLSL) requires migrating to SDL_GPU.
 *
 * Stage 1 (current): scaffold only — init the device, compile the
 * passthrough vertex+fragment shaders from the SPIR-V blobs under
 * port/shaders/build/, log success. No frame is rendered through this
 * path yet; the SDL_Renderer presentation continues to drive the screen.
 *
 * Compile-time gate: pass `--gpu_renderer=y` to `xmake f` to enable.
 * Without the flag the entire module is a no-op linker stub.
 */

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Initialise the SDL_GPU device and load the passthrough shaders.
 * Returns true on success; logs failures to stderr and returns false
 * so the caller can fall back to the SDL_Renderer path. Safe to call
 * even when the binary was built without TMC_GPU_RENDERER — becomes
 * a no-op that returns true. */
bool Port_GPU_Init(SDL_Window* window);

/* Stage 2: claim the window for the GPU device, query the swapchain
 * texture format, and build the graphics pipeline + sampler + source
 * texture (240x160 RGBA). Call AFTER Port_GPU_Init has succeeded and
 * BEFORE Port_PPU_Init creates its SDL_Renderer. Returns true if the
 * GPU path is ready to take over presentation; false on any error so
 * the SDL_Renderer fallback can run. */
bool Port_GPU_ClaimWindow(SDL_Window* window, int fb_width, int fb_height);

/* Stage 2: present one frame through the GPU. Uploads `fb` to the
 * source texture, renders a fullscreen quad via the passthrough
 * shader, submits to the swapchain. Caller must have invoked
 * Port_GPU_ClaimWindow successfully first. Returns true on success;
 * false if the swapchain wasn't ready this frame (caller skips the
 * present, no fallback needed — the GBA frame just doesn't show). */
bool Port_GPU_PresentFrame(const uint32_t* fb, int fb_w, int fb_h, int fb_pitch_bytes);

/* Stage 6: paint the boot splash via SDL_GPU — a single render pass
 * that clears the swapchain to a dark "loading" colour. Replaces the
 * SDL_Renderer-based Port_PaintBootSplash on GPU builds, which
 * couldn't run because SDL_Renderer's Vulkan surface conflicted with
 * the SDL_GPU swapchain claim. Returns false if the swapchain isn't
 * ready (window minimised, mid-resize); caller can ignore. */
bool Port_GPU_PaintBootSplash(void);

/* Whether the GPU renderer was successfully initialised AND has
 * claimed the window. When true, Port_PPU_PresentFrame dispatches
 * to Port_GPU_PresentFrame instead of the SDL_Renderer path. */
bool Port_GPU_IsActive(void);

/* Set the swapchain present mode: vsync on = VSYNC, off = IMMEDIATE
 * (fallback MAILBOX when unsupported). Returns false when the GPU path
 * is inactive or the driver refuses both non-synced modes. */
bool Port_GPU_SetVSync(bool enabled);

/* True when the active GPU shader format can load runtime .glslp
 * presets. The current .glslp compiler emits SPIR-V, so Metal-backed
 * macOS GPU builds use stock filters but disable preset loading. */
bool Port_GPU_SupportsGlslpRuntime(void);
SDL_GPUDevice* Port_GPU_GetDevice(void);
SDL_GPUTextureFormat Port_GPU_GetSwapchainFormat(void);
/* Shader format the GPU device was created with (SPIRV on Vulkan, MSL on
 * Metal). SDL_GPU_SHADERFORMAT_INVALID when no GPU device is active. Used by
 * the PPU rasterizer to pick the matching shader blob + entrypoint. */
SDL_GPUShaderFormat Port_GPU_GetShaderFormat(void);

/* Prelaunch-screen present helper used before the main game loop takes over. */
bool Port_GPU_PresentPrelaunchFrame(void);

/* Available GPU-side shader filters. Maps loosely onto port_filter.c's
 * PortFilterType enum (Stage 3 ports a subset to real GLSL passes; the
 * rest still run on the SDL_Renderer build's CPU path). */
typedef enum {
    PORT_GPU_FILTER_NONE = 0,      /* passthrough — direct blit */
    PORT_GPU_FILTER_LCD_GRID,      /* GBA LCD grid (GLSL port of Apply_LcdGrid) */
    PORT_GPU_FILTER_SCANLINE,      /* alternating dim rows (Apply_Scanlines) */
    PORT_GPU_FILTER_HANDHELD,      /* LCD grid + cool tint (Apply_HandheldGrid) */
    PORT_GPU_FILTER_VIGNETTE,      /* radial corner darkening (Apply_Vignette) */
    PORT_GPU_FILTER_CRT_COMPOSITE, /* 3-tap blur + AG mask + warm tint */
    PORT_GPU_FILTER_CRT_RF,        /* 5-tap blur + AG mask + warm tint + scanlines */
    PORT_GPU_FILTER_COUNT
} PortGpuFilter;

void Port_GPU_SetFilter(PortGpuFilter f);
void Port_GPU_SetTextureScaleMode(SDL_ScaleMode mode);
PortGpuFilter Port_GPU_GetFilter(void);
const char* Port_GPU_FilterName(PortGpuFilter f);

/* Release device + shader objects. Idempotent. */
void Port_GPU_Shutdown(void);

#ifdef __cplusplus
}
#endif
