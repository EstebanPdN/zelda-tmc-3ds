package dev.picori.tmc;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

/**
 * Hosts the panel ({@link SecondScreenView}) on the device's MAIN display
 * for the swapped layout, i.e. when the game itself was launched onto the
 * secondary display ("swap screens" in the panel's settings, applied by
 * {@link TMCLauncherActivity} at launch).
 *
 * A Presentation can't do this job: the framework refuses presentation
 * windows on DEFAULT_DISPLAY (InvalidDisplayException), so the swapped
 * layout needs a real activity on that display.
 */
public class SecondScreenActivity extends Activity {
    private static final String TAG = "SecondScreenActivity";

    // The game activity starts and finishes this one, and both live in the
    // same process, so a static reference is all the handle that's needed.
    static volatile SecondScreenActivity sInstance;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            // Unlike the bottom-screen Presentation, this window is
            // FOCUSABLE — deliberately, and it must stay that way. It owns
            // the default display, and a default display left with no
            // focusable window makes anything routed there ANR with "no
            // focused window" (a zelda3-android finding on this hardware).
            // The game runs on a separate display with its own per-display
            // focus, so touching this screen does not take the gamepad away
            // from it.
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            getWindow().getDecorView().setBackgroundColor(Color.BLACK);
            // Same per-window immersive flags the Presentation sets: system
            // UI visibility does not propagate between windows, let alone
            // between displays.
            getWindow().getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            setContentView(new SecondScreenView(this));
        } catch (Throwable e) {
            // Shares the game's process, so dying here would take the game
            // down with it. Drop the panel instead and let the game run.
            Log.e(TAG, "panel activity failed to come up", e);
            finish();
            return;
        }
        sInstance = this;
        Log.i(TAG, "panel activity up on display "
                + getWindowManager().getDefaultDisplay().getDisplayId());
    }

    @Override
    public void onBackPressed() {
        // Swallow BACK. This window is focusable (it has to be, above), so
        // BACK lands here — and gamepad B falls back to KEYCODE_BACK at the
        // system level, so the default behaviour would finish the panel on
        // a normal B press mid-game and leave the player with no way to get
        // it back short of restarting the app. Same reasoning as the
        // Presentation's setCancelable(false). Leaving the game is still
        // home/recents, and the game activity takes the panel down itself
        // when it stops.
    }

    @Override
    protected void onDestroy() {
        if (sInstance == this) {
            sInstance = null;
        }
        super.onDestroy();
    }
}
