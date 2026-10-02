package dev.picori.tmc;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

/**
 * Puts the panel on whichever screen the game is not using, and keeps it
 * there across display and lifecycle churn.
 *
 * Normally that means Thor's secondary panel (a real Android Display
 * flagged FLAG_PRESENTATION, normally owned by AYN's own launcher when this
 * app isn't targeting it), shown/hidden as the display attaches/detaches or
 * the activity's own lifecycle changes — and re-shown if the system ever
 * dismisses it behind our back.
 *
 * With "swap screens" on, {@link TMCLauncherActivity} has already launched
 * the game onto that secondary display, so the panel goes to the MAIN
 * display instead. A Presentation is not allowed there (the framework
 * refuses presentation windows on DEFAULT_DISPLAY), so that case is hosted
 * by {@link SecondScreenActivity}.
 */
public class SecondScreenManager implements DisplayManager.DisplayListener {
    private static final String TAG = "SecondScreenManager";

    private final Context mContext;
    private final DisplayManager mDisplayManager;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    /** The display the game's own window is on — the one screen the panel must stay off. */
    private final int mGameDisplayId;
    private SecondScreenPresentation mPresentation;
    // True outside a start()..stop() window: while stopped the panel must
    // stay down no matter what messages are still in flight. Only touched on
    // the main thread — activity lifecycle, our display listener (registered
    // with a null handler = caller's looper) and Dialog's dismiss messages
    // all land there, so no locking is needed.
    private boolean mStopped = true;

    public SecondScreenManager(Activity activity) {
        mContext = activity.getApplicationContext();
        mDisplayManager = (DisplayManager) mContext.getSystemService(Context.DISPLAY_SERVICE);
        mGameDisplayId = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        // Tell native which way round the screens actually ended up. The
        // launcher asks for a display; firmware is free to refuse and hand
        // back a normal launch, so this is the only honest answer, and the
        // panel's "swap screens" row needs it to know whether to read ON/OFF
        // or RESTART. Safe here: SDLActivity.onCreate has already loaded
        // libmain.so by the time this object is constructed.
        nativeSetGameOnSecondaryDisplay(mGameDisplayId != Display.DEFAULT_DISPLAY);
        Log.i(TAG, "game window is on display " + mGameDisplayId);
    }

    public void start() {
        if (mDisplayManager == null) {
            return;
        }
        mStopped = false;
        mDisplayManager.registerDisplayListener(this, null);
        showOnAttachedDisplays();
    }

    public void stop() {
        if (mDisplayManager == null) {
            return;
        }
        mStopped = true;
        mDisplayManager.unregisterDisplayListener(this);
        dismiss();
    }

    private void showOnAttachedDisplays() {
        if (mGameDisplayId != Display.DEFAULT_DISPLAY) {
            // Swapped: the game took the secondary display, so the panel
            // belongs on the main one — as an activity, not a Presentation.
            startPanelActivity();
            return;
        }
        for (Display display : mDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() == mGameDisplayId) {
                continue; // never cover the game with the panel
            }
            showOn(display);
        }
    }

    private void showOn(Display display) {
        if (mPresentation != null || mStopped) {
            return; // Thor only has the one secondary panel; already showing
                    // (or the activity is backgrounded and the panel is down
                    // on purpose).
        }
        Log.i(TAG, "showing second-screen panel on display " + display.getDisplayId());
        SecondScreenPresentation presentation = new SecondScreenPresentation(mContext, display);
        presentation.setOnDismissListener(dialog -> {
            // Dismiss recovery. Dialog delivers onDismiss as a *posted*
            // message, a looper pass after the dismissal itself, and every
            // path where we take the panel down on purpose (stop() ->
            // dismiss(), display removal) has already cleared mPresentation
            // by the time it arrives. So "mPresentation still points at this
            // presentation" reliably means the system dismissed the window
            // behind our back (display flicker, config churn), and without a
            // re-show here the bottom screen stays dead until an app
            // restart. Don't rework this into a "did we call dismiss()" flag
            // tested here — with the posted delivery such a flag describes
            // the wrong point in time, the ordering trap the zelda3-android
            // mod ran into before settling on this clear-the-reference-first
            // scheme.
            if (mPresentation != presentation) {
                return; // our own teardown; stay silent.
            }
            mPresentation = null;
            // Re-show from a fresh message rather than inside the dismiss
            // dispatch, re-scanning what's attached at that point: if the
            // panel's display itself is mid-flicker the scan simply finds
            // nothing, and the still-registered display listener brings the
            // panel back in onDisplayAdded once the display returns.
            mMainHandler.post(() -> {
                if (!mStopped) {
                    showOnAttachedDisplays();
                }
            });
        });
        try {
            presentation.show();
            mPresentation = presentation;
        } catch (Exception e) {
            // Presentation.show() can fail if the display is claimed
            // elsewhere (e.g. AYN's own Cocoon Shell launcher) — degrade
            // gracefully rather than crashing the whole app over a HUD panel.
            Log.w(TAG, "failed to show second-screen presentation", e);
        }
    }

    /**
     * Swapped layout: bring the panel up on the main display in its own
     * task (launching onto a specific display requires one). A refusal here
     * costs the panel, not the game — the game window is already up on the
     * other screen and keeps running without it.
     */
    private void startPanelActivity() {
        if (mStopped || SecondScreenActivity.sInstance != null || Build.VERSION.SDK_INT < 26) {
            return;
        }
        Intent intent = new Intent(mContext, SecondScreenActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(Display.DEFAULT_DISPLAY);
        try {
            mContext.startActivity(intent, options.toBundle());
            Log.i(TAG, "showing second-screen panel on the main display (swapped)");
        } catch (RuntimeException e) {
            Log.w(TAG, "failed to show the panel on the main display", e);
        }
    }

    private void dismiss() {
        if (mPresentation != null) {
            mPresentation.dismiss();
            // Cleared before the onDismiss message can arrive (delivery is
            // posted, never synchronous) — that is what tells the dismiss
            // listener this teardown was intentional.
            mPresentation = null;
        }
        SecondScreenActivity panel = SecondScreenActivity.sInstance;
        if (panel != null) {
            panel.finish();
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        Display display = mDisplayManager.getDisplay(displayId);
        if (display != null && (display.getFlags() & Display.FLAG_PRESENTATION) != 0
                && displayId != mGameDisplayId) {
            showOn(display);
        }
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (mPresentation != null && mPresentation.getDisplay().getDisplayId() == displayId) {
            // The display (and its window) is already gone — don't call
            // dismiss() on it, just drop our reference. The Presentation
            // also dismisses itself on display removal; its posthumous
            // onDismiss then finds the reference already cleared and the
            // recovery logic correctly stays quiet.
            mPresentation = null;
        }
    }

    @Override
    public void onDisplayChanged(int displayId) {
        // No-op for now; revisit if the secondary panel's resolution/state
        // can change while attached (not expected on Thor's fixed hardware).
    }

    private static native void nativeSetGameOnSecondaryDisplay(boolean onSecondary);
}
