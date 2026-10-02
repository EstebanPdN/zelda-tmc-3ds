#pragma once

#include <SDL3/SDL.h>

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drives the cold-launch / first-launch asset extraction. Now linked
 * directly against the extractor (no shell-out to asset_extractor)
 * and shares the engine's already-loaded ROM buffer.
 *
 * `rom_data`/`rom_size` are the bytes returned by Port_LoadRom — pass
 * NULL/0 if the ROM hasn't been loaded yet and the bootstrap should
 * fall back to reading baserom.gba from disk. */
void Port_EnsureAssetsReadyWithDisplay(SDL_Window* window, const u8* rom_data, u32 rom_size);

/* Paint a single "loading" splash frame to the window's existing
 * renderer (the one the progress bar used, or the one PPU created
 * if extraction was skipped). Call this between Port_PPU_Init and
 * AgbMain so the user sees a continuous "Starting..." card instead
 * of a blank window during audio init / AgbMain warmup. Safe to
 * call with a NULL renderer — it becomes a no-op. */
void Port_PaintBootSplash(SDL_Window* window, const char* message);

/* When set to true, Port_EnsureAssetsReadyWithDisplay will skip
 * mounting any *.pak archives even if they are present in the assets
 * directory, forcing the engine to read loose files via std::ifstream
 * exactly as before paks existed. Set this from CLI arg parsing
 * before calling Port_EnsureAssetsReadyWithDisplay. Default false. */
extern int Port_LooseAssetsRequested;

#ifdef __cplusplus
}
#endif
