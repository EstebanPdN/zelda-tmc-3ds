#ifndef PORT_FILTER_H
#define PORT_FILTER_H

/*
 * Post-process CRT/LCD filters applied to the upscaled framebuffer
 * before SDL hands it to the texture for present.
 *
 * These are CPU-side approximations of the Sonkun shader pack effects.
 * The real Sonkun shaders are multi-pass GLSL designed for RetroArch's
 * shader pipeline; SDL_Renderer doesn't expose custom fragment shaders
 * without dropping to SDL_GPU / OpenGL, so the implementations here
 * trade fidelity for ship-now: each preset captures the most visually
 * distinctive properties of its namesake (colour cast, mask pattern,
 * scanline shape, signal-style smear) but won't be pixel-identical.
 *
 * All filters operate on ABGR8888 buffers at native 240*S × 160*S
 * (where S is the internal render scale). The mask patterns assume
 * S >= 3 to be visible — at 1× there aren't enough pixels per
 * "phosphor cell" for the pattern to read as a CRT.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    PORT_FILTER_NONE = 0,
    PORT_FILTER_CRT_WARM_COMPOSITE,  /* Sonkun warm composite aperture grill */
    PORT_FILTER_LCD_GRID,             /* GBA-style LCD pixel grid */
    PORT_FILTER_CRT_WARM_RF,          /* Sonkun warm RF aperture grill */
    PORT_FILTER_SCANLINES,            /* libretro scanline.glsl equivalent — alternating dim rows */
    PORT_FILTER_HANDHELD,             /* libretro handheld-grid equivalent — tight LCD cells + cool tint */
    PORT_FILTER_VIGNETTE,             /* libretro vignette equivalent — corner darkening */
    PORT_FILTER_COUNT
} PortFilterType;

const char* Port_Filter_Name(PortFilterType type);

/* Apply the filter in place over the framebuffer (ABGR8888 little-
 * endian: byte 0=R, 1=G, 2=B, 3=A). Width must equal stride; rows are
 * contiguous. No-op when filter == PORT_FILTER_NONE. */
void Port_Filter_Apply(uint32_t* fb, int width, int height, int internal_scale,
                       PortFilterType filter);

#ifdef __cplusplus
}
#endif

#endif
