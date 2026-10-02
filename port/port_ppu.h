#ifndef PORT_PPU_H
#define PORT_PPU_H

#ifndef TMC_3DS
#include <SDL3/SDL.h>
#else
typedef void SDL_Window;
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
#ifdef launcher
void Port_SetBootstrapWindow(SDL_Window* window);
#endif

// Initialize the PPU renderer (call after SDL_CreateWindow)
void Port_PPU_Init(SDL_Window* window);

// Read GBA DISPCNT mode bits and render the current frame via ViruaPPU,
// then present it to the SDL window. Call once per VBlank.
void Port_PPU_PresentFrame(void);

// Decoupled pacing: whether the next PresentFrame is the first present of
// the current game tick. Temporal effects (LCD persistence) fold their
// accumulators only then; repeat presents of the same tick read them
// without writing.
void Port_PPU_SetPresentIsFirstOfTick(bool first);

// Update the SDL window title used by the port.
void Port_PPU_SetWindowTitle(const char* title);

// Toggle borderless desktop fullscreen on the SDL window.
void Port_PPU_ToggleFullscreen(void);
bool Port_PPU_IsFullscreen(void);
void Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);
void Port_PPU_ApplyWindowScale(void);

// Toggle nearest-neighbor (sharp pixels) ↔ linear (smooth) upscale filter.
void Port_PPU_ToggleSmoothing(void);
void Port_PPU_CyclePresentationMode(int direction);
const char* Port_PPU_PresentationModeName(void);

// CRT/LCD post-process filter cycling (off / warm-composite-AG /
// LCD-grid / warm-RF-AG). Applied at the upscaled resolution before
// SDL upload — needs internal-scale >= 2 to be visible.
void Port_PPU_CycleFilter(int direction);
const char* Port_PPU_FilterName(void);

// Toggle SDL vsync on the renderer. Used by VBlankIntrWait to disable the
// display-refresh cap when fast-forwarding or when target FPS exceeds the
// monitor refresh rate; without this, fast-forward (#26) and FPS presets
// > 60 are limited by the display, not by the busy-wait timer.
void Port_PPU_SetVSync(bool enabled);
bool Port_PPU_VSyncEnabled(void);

// Current refresh rate (Hz, rounded) of the display the window is on;
// 0 when unknown (headless/dummy driver, no window yet). Cached, re-queried
// every ~2 s so moving the window between monitors is tracked.
unsigned Port_PPU_DisplayRefreshRate(void);

// GBA reflective-LCD display transforms applied at present time to the final
// composited frame (see port_ppu.cpp). Colour correction defaults on
// (TMC_COLOR_CORRECTION=0 disables); LCD temporal persistence is opt-in
// (TMC_LCD_PERSISTENCE=1, TMC_LCD_PERSISTENCE_RHO=0..0.99). rho is the
// fraction of the previous frame retained; values outside [0,1) are ignored.
void Port_PPU_SetColorCorrection(bool enabled);
bool Port_PPU_ColorCorrectionEnabled(void);
void Port_PPU_SetPersistence(bool enabled, float rho);

// Cleanup
void Port_PPU_Shutdown(void);

void Port_OpenInGameSettingsModal(void);
bool Port_InGameSettingsModalIsOpen(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_PPU_H
