#ifndef PORT_SECOND_SCREEN_H
#define PORT_SECOND_SCREEN_H

/*
 * Second-screen panel (AYN Thor secondary display).
 *
 * This is a genuinely independent render target from the game's own PPU:
 * it never touches virtuappu_frame_buffer / mode1_memory / OAM (see the
 * cautionary note in port_softslots.h about an earlier native-framebuffer
 * UI attempt that corrupted pause-menu visuals). The Java side
 * (SecondScreenView.java) owns the SurfaceView this draws into and hands
 * its Surface to this module via port_second_screen_jni.cpp; what hosts
 * that view depends on which way round the screens are — a Presentation on
 * the secondary display normally, a plain Activity on the main one when
 * the game itself has been swapped onto the secondary display.
 *
 * The compositor itself (Port_SecondScreen_PaintInto + the tap handler) is
 * platform-agnostic C compiled on every platform this port targets — only
 * the ANativeWindow lock/post plumbing and the render thread are
 * Android-only. That split keeps the panel testable off-device: anything
 * that can hand over an RGBA8888 buffer can render and drive the layout.
 */

#include <stdint.h>

#include "port_second_screen_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void Port_SecondScreen_Init(void);

/* Paints one complete second-screen frame into `pixels` (RGBA8888 in the
 * same little-endian A|B|G|R u32 convention the theme/render modules emit),
 * `strideInPixels` u32s per row. `tick` advances the panel's animations
 * (the Android render thread runs at ~20 Hz; anything driving this should
 * step tick once per paint). Reads the UI state (active tab, map view,
 * pins) that Port_SecondScreen_OnTap mutates, and republishes the frame's
 * tap targets for the next hit test. */
void Port_SecondScreen_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                 const SecondScreenSnapshot* snap, uint32_t tick);
int Port_SecondScreen_IsDeveloperOverlayOpen(void);

/* Called from JNI when the Presentation's Surface becomes available (or is
 * resized). `window` is an ANativeWindow* on Android; kept as void* here so
 * this header doesn't require <android/native_window.h> to include. Takes
 * ownership of the one ANativeWindow reference the caller obtained via
 * ANativeWindow_fromSurface — releases it internally, exactly once, when
 * replaced or when the surface is lost. */
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height);

/* Called from JNI when the Presentation's Surface is destroyed (display
 * detached, app backgrounded, etc). Safe to call even if no surface is
 * currently held. */
void Port_SecondScreen_OnSurfaceLost(void);

/* Called from JNI on a completed tap on the second screen, in surface
 * pixel coordinates (the Java side resolves tap vs long press and calls
 * this once per gesture). Hit-testing happens against the layout of the
 * most recently painted frame: tabs switch panels, rings arm an equip
 * slot, plaques preview a dungeon floor, settings rows toggle, the map
 * toggles follow/whole view and zooms into the tapped map tile,
 * and an item cell files an equip request through
 * Port_SecondScreenState_RequestEquip (tap = A, hold = B — or whichever
 * slot an armed ring selected). Compiled on all platforms so a host
 * harness can drive the same layout the device shows. */
void Port_SecondScreen_OnTap(int x, int y, int longPress);

/* Which way round the two screens ended up this launch: nonzero when the
 * game's own window is on a secondary display and this panel therefore
 * owns the main one (the "swap screens" setting, applied by the Android
 * shell at launch — see SecondScreenManager.java, which reports the game's
 * display here as soon as it knows it). The settings row compares this
 * against the persisted flag to decide whether it can honestly say ON/OFF
 * or has to say RESTART: the display the shell asked for is not always the
 * display it got, since firmware may refuse a launch there outright. */
void Port_SecondScreen_SetGameOnSecondaryDisplay(int onSecondary);
int Port_SecondScreen_GameOnSecondaryDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_H */
