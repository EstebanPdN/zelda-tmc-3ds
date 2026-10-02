package dev.picori.tmc;

import android.content.Context;
import android.graphics.PixelFormat;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * The panel itself: one SurfaceView whose native Surface is handed to
 * libmain.so via JNI so C++ can draw into it directly (see
 * port/port_second_screen.c and port/port_second_screen_jni.cpp). This is a
 * separate window/surface from SDLActivity's primary SDLSurface,
 * deliberately not routed through SDL's own event/render pipeline.
 *
 * The view is kept apart from its host because the host varies: normally
 * the panel lives in a {@link SecondScreenPresentation} on the secondary
 * display, but with "swap screens" on the game takes that display and the
 * panel is hosted by {@link SecondScreenActivity} on the main one instead.
 * Only the window setup differs between the two — the surface handoff and
 * the tap forwarding below are the same either way, and there is only ever
 * one of these alive at a time, so native sees the same single surface
 * come and go however it was hosted.
 */
class SecondScreenView extends SurfaceView {
    private static final long LONG_PRESS_MS = 350;

    private long mDownTime;

    SecondScreenView(Context context) {
        super(context);

        // Explicit format: an unset SurfaceHolder format is device/API-level
        // dependent (can resolve to RGB_565, 2 bytes/pixel), and the native
        // side writes fixed 4-bytes/pixel ARGB — a mismatch here overflows
        // the locked buffer. Pin it so ANativeWindow_lock's buf.format is
        // always what the native writer expects.
        getHolder().setFormat(PixelFormat.RGBA_8888);

        getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                // No-op: surfaceChanged always follows immediately with the
                // real dimensions, so do the native handoff there instead
                // of racing a not-yet-sized surface.
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSurfaceCreated(holder.getSurface(), width, height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSurfaceDestroyed();
            }
        });
    }

    // Tap-to-equip: a completed tap is forwarded to native with the press
    // duration collapsed to a boolean — short tap equips the item to A,
    // press-and-hold to B (mirrors the pause menu's two equip buttons).
    // Surface pixels == view pixels here (the view fills the display and the
    // native side draws at surface size), so event coordinates need no
    // transform before hit-testing in C.
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                mDownTime = event.getEventTime();
                return true;
            case MotionEvent.ACTION_UP:
                boolean longPress = event.getEventTime() - mDownTime >= LONG_PRESS_MS;
                nativeTap((int) event.getX(), (int) event.getY(), longPress);
                performClick();
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean performClick() {
        return super.performClick();
    }

    private static native void nativeSurfaceCreated(Surface surface, int width, int height);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTap(int x, int y, boolean longPress);
}
