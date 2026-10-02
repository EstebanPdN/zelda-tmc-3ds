#ifndef PORT_WIDESCREEN_H
#define PORT_WIDESCREEN_H

/*
 * port_widescreen.h — single source of truth for the rendered viewport
 * width, shared by the ViruaPPU renderer and the engine-side on-screen /
 * culling tests in the port.
 *
 * Why this exists (see docs/widescreen-*.md):
 *   The GBA decomp hardcodes a 240-px playfield into its visibility math
 *   (CheckOnScreen's 0x16E, CheckRectOnScreen's 0xF0, per-enemy
 *   `scroll_x + 0x108` bounds, ...). When the PPU renders a wider frame
 *   but the engine still culls/parks against 240, entities the engine
 *   treats as "off-screen" (parked, or AI-gated past col 240) leak into
 *   the revealed area — the sprite-edge flicker that has blocked Phase 2.
 *
 *   The two reference GBA ports that actually ship widescreen both fix
 *   this the same way — by keying every cull/camera bound off one
 *   display-width constant instead of a literal 240:
 *     - SAT-R/sa2:      include/gba/defines.h (DISPLAY_WIDTH) +
 *                       include/game/shared/stage/camera.h (IS_OUT_OF_RANGE,
 *                       IS_OUT_OF_DISPLAY_RANGE, CAM_BOUND_X).
 *     - pret/pokeemerald: src/event_object_movement.c cull at
 *                       `x >= DISPLAY_WIDTH + 16`; src/sprite.c parks
 *                       off-screen sprites at `DISPLAY_WIDTH + 64`.
 *
 * PORT_VIEW_WIDTH is that constant for this port. It tracks
 * MODE1_GBA_WIDTH, which xmake injects from `--widescreen_width=N`
 * (default 240) so the renderer and the engine cull tests can never
 * disagree. At 240 every derived bound collapses to its GBA-original
 * value, so the default build is behaviourally identical to native.
 */

/* Mirror of the ViruaPPU compile-time width. Kept #ifndef-guarded so
 * this header is self-contained when included by a TU that does not pull
 * in <cpu/mode1.h>; xmake always defines MODE1_GBA_WIDTH for tmc_pc, so
 * in a real build the injected value wins and this fallback is inert. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif

/* Rendered viewport, in screen pixels. Height is intentionally NOT
 * widened: the engine's main loop assumes 160-line frames for timing and
 * BG preload (see docs/widescreen-phase2-design.md, Step C-3), so only
 * the horizontal extent generalises. */
#define PORT_VIEW_WIDTH (MODE1_GBA_WIDTH)
#define PORT_VIEW_HEIGHT 160

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime state for a wide-width build. These collapse to native no-ops when
 * MODE1_GBA_WIDTH == 240 or the WIP widescreen option is disabled.
 *
 * FallbackNative == 1 means this scene renders the GBA-native 240px canvas
 * and the presenter fits it at its correct 3:2 aspect (pillarbox bars) —
 * fixed canvases are NEVER stretched to the widescreen target.
 *
 * EffectiveViewWidth is additionally capped by the current room's width:
 * rooms narrower than the window-aspect target but wider than 240 (256 and
 * 272px rooms — most dungeons/interiors with any extra world) run true
 * widescreen at their full room width, pillarboxing only the remainder. */
int Port_Widescreen_FallbackNative(void);
int Port_Widescreen_IsActive(void);
int Port_Widescreen_EffectiveViewWidth(void);
int Port_Widescreen_HudRightAnchor(void);
/* True while the map BGs are what the PPU renders (>=1 shadow registered);
 * overlay screens (storybook, pause) drop this to 0 -> present native 240. */
int Port_Widescreen_ShadowsLive(void);

/* True widescreen: the view width tracks the WINDOW's aspect each frame —
 * viewW = clamp(floor(window_width * 160 / window_height), 240,
 * MODE1_GBA_WIDTH) — so a 16:9 monitor fills exactly (~284 px) with a
 * constant world scale, instead
 * of letterboxing a fixed 2.4:1 frame. MODE1_GBA_WIDTH is the framebuffer
 * capacity / hard cap, no longer the presented width.
 *  - Port_Widescreen_SetWindowPixels: present path publishes the live
 *    window client size (pixels) once per frame.
 *  - Port_Widescreen_TargetViewWidth: the aspect-fit width (config/task
 *    independent). TMC_WS_VIEW_WIDTH=<px> overrides for headless captures.
 * EffectiveViewWidth == min(TargetViewWidth, room width) while active, else
 * 240 — always use EffectiveViewWidth for camera/layout math, never the raw
 * target (see the room-cap note above). */
void Port_Widescreen_SetWindowPixels(int w, int h);
int Port_Widescreen_TargetViewWidth(void);

/* Camera rest position for a target at absolute world x (origin included):
 * target centered in the live view width, clamped so the view stays inside
 * the room. THE single formula for "where the camera stops" — used by the
 * follow camera (Scroll1), the snap cameras (InitializeCamera,
 * sub_08080974, sub_080809D4) and the script wait
 * (WaitForCameraTouchRoomBorder, which busy-waits for scroll_x EQUALITY:
 * any drift between producers and that check deadlocks a cutscene).
 * Reduces to the GBA `x - 120` clamped to [origin, origin+width-240] at an
 * effective view width of 240. */
int Port_Widescreen_CameraRestX(int target_x);

#ifdef __cplusplus
}
#endif

#endif /* PORT_WIDESCREEN_H */
