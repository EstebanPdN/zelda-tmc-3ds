#pragma once

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build the SDL window icon.
 *
 * Order of preference:
 *   1. If the user's ROM is loaded and Port_ExtractEzloFromRom() succeeds,
 *      use the runtime-decoded Ezlo sprite. Ships zero Nintendo data —
 *      the bytes only ever live in mmap'd `gRomData` (the user's own ROM)
 *      until the moment they are converted to RGBA on the heap.
 *   2. Otherwise fall back to a tiny programmatic silhouette so the
 *      window still has a recognisable, copyright-free icon.
 *
 * Caller owns the returned surface and must SDL_DestroySurface() it (or
 * pass it to SDL_SetWindowIcon, which takes a deep copy).
 * Returns NULL on allocation failure.
 */
SDL_Surface* Port_CreateAppIcon(void);

/*
 * Future hook: decode SPRITE_EZLOCAP frame 0 from gRomData via the
 * existing sprite-pointer / gfx-pool / palette-group plumbing.
 *
 * Returns NULL today (scaffolding); swapping in the real extractor is a
 * single-function change without touching port_main.c.
 */
SDL_Surface* Port_ExtractEzloFromRom(void);

#ifdef __cplusplus
}
#endif
