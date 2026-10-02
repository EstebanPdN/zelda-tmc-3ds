#ifndef PORT_SECOND_SCREEN_RENDER_H
#define PORT_SECOND_SCREEN_RENDER_H

/*
 * Second-screen minimap/item-icon compositor (Phase 3d).
 *
 * Deliberately independent of the live PPU: reads item-icon tile graphics
 * straight from the ROM/asset layer (Port_GetSpritePtr + the frame OBJ
 * piece lists) and palettes through the theme's pause-time OBJ bank
 * resolver (Port_SecondScreenTheme_ObjPalette), not from
 * virtuappu_frame_buffer/mode1_memory or the live gPaletteBuffer. See
 * port_second_screen.h's file comment and port_softslots.h's cautionary
 * note about an earlier native-framebuffer/OAM UI attempt that corrupted
 * pause-menu visuals — this module exists to avoid repeating that
 * mistake.
 *
 * No Android-specific types in this header on purpose: callers hand in a
 * plain pixel buffer (already locked/owned by whoever's presenting it), so
 * this compositor has no dependency on ANativeWindow and could in principle
 * run on any platform this port targets.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws a single item icon (native 16x16px, sprite 322 — see
 * src/menu/pauseMenu.c's GetSpriteAnimation322) at (x,y), scaled up by
 * `scale` (each source pixel becomes a scale x scale block — nearest-
 * neighbor, matching the GBA's own blocky look rather than smoothing it),
 * into `pixels`, a `stride`-pixels-per-row RGBA8888 buffer of size
 * bufWidth x bufHeight. itemId 0 (unassigned) draws nothing. Out-of-bounds
 * destination pixels are clipped silently. Palette banks resolve the way
 * the pause menu's item screen resolves them. */
void Port_SecondScreenRender_DrawItemIcon(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, uint8_t itemId);

/* Same, for a screen whose OAM command carries a different palette bank.
 * Nearly every icon piece names its bank absolutely and ignores the
 * command's, but not all of them do — and the pause menu's screens do not
 * all command the same bank: the item screen sends 3, the quest screen 14,
 * which is what colors the element crystal there. */
void Port_SecondScreenRender_DrawItemIconBank(uint32_t* pixels, int32_t bufWidth, int32_t bufHeight,
                                              int32_t stride, int32_t x, int32_t y, int32_t scale,
                                              uint8_t itemId, uint32_t cmdPalBank);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_RENDER_H */
