#include "bottom_map_anim_3ds.h"

#include <math.h>

/*
 * Every term below mirrors a specific expression in the draw code. The test
 * re-derives each one independently and asserts the signature changes exactly
 * when the drawn tuple changes, so a future edit to either side that drifts
 * from the other fails on the host rather than freezing a screen on hardware.
 */
uint32_t BottomMapAnim_Signature(uint32_t tick, int isDungeon, int32_t width, int32_t height) {
    uint32_t sig = 1u;

    /* Marker blink, shared by the overworld player dot, the region markers and
     * the dungeon own-floor dot. `tick & 8` in the draw code, i.e. bit 3:
     * port_second_screen.c:913, :1164, :1331 and
     * port_second_screen_dungeonmap.c:499. */
    sig = sig * 31u + ((tick >> 3) & 1u);

    if (isDungeon) {
        /* Room-palette rotation. The fastest MAP animation at 3 changes per
         * 8 ticks, which is what caps the dungeon skip ratio near 3x.
         * port_second_screen_dungeonmap.c:424. */
        sig = sig * 31u + (((tick * 3u) >> 3) & 7u);
    } else {
        /* Player-marker pulse radius. Quantise the drawn integer rather than
         * the 32-tick phase: u is min(w,h)/720, so on the 320x240 bottom
         * screen u = 1/3 and the 6.5 +- 1.5 float collapses to just {1, 2}.
         * Quantising the phase instead would change every tick and save
         * nothing. port_second_screen.c:1161. */
        const float u = (float)(width < height ? width : height) / 720.0f;
        const float pulse = 6.5f + 1.5f * sinf((float)(tick % 32u) * (6.28318f / 32.0f));
        sig = sig * 31u + (uint32_t)(int32_t)(pulse * u);
    }

    return sig;
}

int BottomMapAnim_NeedsPaint(uint32_t tick, uint32_t paintedTick, int isDungeon, int32_t width,
                             int32_t height) {
    if (BottomMapAnim_Signature(tick, isDungeon, width, height) !=
        BottomMapAnim_Signature(paintedTick, isDungeon, width, height)) {
        return 1;
    }
    /* Unsigned difference so a tick wrap does not stop forcing repaints. */
    return (uint32_t)(tick - paintedTick) >= BOTTOM_MAP_ANIM_FORCE_TICKS;
}
