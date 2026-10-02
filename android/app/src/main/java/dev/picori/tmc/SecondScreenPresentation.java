package dev.picori.tmc;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * Hosts the game's second-screen panel ({@link SecondScreenView}) on Thor's
 * secondary display, which is the normal way round: the game owns the main
 * screen and this Presentation owns the panel below it. With "swap screens"
 * on the roles trade places and {@link SecondScreenActivity} hosts the same
 * view on the main display instead — a Presentation cannot do that job, the
 * framework refuses presentation windows on DEFAULT_DISPLAY.
 */
public class SecondScreenPresentation extends Presentation {
    public SecondScreenPresentation(Context outerContext, Display display) {
        super(outerContext, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // A Presentation is a Dialog, and a Dialog's window is focusable by
        // default. Focusable is fatal here: Android gives key input to one
        // focused window, so the first touch on the bottom screen would move
        // focus to this panel and the gamepad would stop reaching SDL's
        // window on the game screen. Worse, gamepad B has a system-level
        // fallback to KEYCODE_BACK, and BACK cancels a focused dialog — the
        // bottom screen would vanish mid-game on a normal B press (both
        // shipped as user-reported bugs in the zelda3-android mod on the same
        // hardware). Non-focusable windows still receive touch, so the
        // tap-to-equip listener keeps working. Must be set before show()
        // attaches the window; onCreate runs inside show(), which is early
        // enough. Note this is exactly what SecondScreenActivity must NOT do
        // on the main display — see the comment there.
        Window window = getWindow();
        if (window != null) {
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            // Hide the system bars on the panel: Thor's bottom display
            // otherwise reserves a ~70px navigation-bar inset that shows up
            // as a dead black band beside the surface. System-UI visibility
            // is per-window, so the game activity going immersive does
            // nothing for us — the flags have to go on this window's own
            // decor view. setSystemUiVisibility is deprecated but is the
            // mechanism that exists at minSdk 21 (WindowInsetsController
            // needs API 30).
            window.getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        // Belt to FLAG_NOT_FOCUSABLE's suspenders: even if a BACK ever
        // reaches this dialog through a path the flag doesn't cover, refuse
        // to be cancelled by it.
        setCancelable(false);

        setContentView(new SecondScreenView(getContext()), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }
}
