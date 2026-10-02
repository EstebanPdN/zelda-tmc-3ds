package dev.picori.tmc;

import android.os.Bundle;
import android.view.View;
import android.view.Window;
import org.libsdl.app.SDLActivity;

/**
 * Project Picori — The Minish Cap PC port, Android shell.
 *
 * The entire game (engine + PPU + audio + ImGui menus + touch overlay) lives
 * in libmain.so, built by xmake (repo root: `xmake f -p android ...`). SDL3 is
 * statically linked into it, so the only library to load is "main"; SDL's
 * Java-side glue binds to the JNI_OnLoad exported from the static SDL inside.
 */
public class TMCActivity extends SDLActivity {
    // Same set the second-screen panel uses (see SecondScreenPresentation) and
    // the same set SDL's own glue would apply — kept here so the game window
    // never depends on SDL having dispatched COMMAND_CHANGE_WINDOW_STYLE.
    private static final int IMMERSIVE_FLAGS =
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;

    private SecondScreenManager mSecondScreen;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // SDL_WINDOW_FULLSCREEN at SDL_CreateWindow time (port/port_main.c) is
        // not enough on its own: SDL only pushes the immersive flags onto the
        // decor view from COMMAND_CHANGE_WINDOW_STYLE, and the window theme's
        // FLAG_FULLSCREEN alone leaves the status bar drawn solid over the top
        // of the game whenever there is notification state to show — which is
        // why the bar only went away with Do Not Disturb on (issue #4). Own the
        // system-bar state here instead, the way the second-screen panel and
        // the zelda3-android mod on the same hardware already do.
        applyImmersiveMode();
        mSecondScreen = new SecondScreenManager(this);
    }

    // The flags are dropped by the system every time the window loses focus
    // (notification shade, power menu, the panel's own display churn), and
    // nothing in SDL's glue puts them back. Re-assert on the way in.
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private void applyImmersiveMode() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        window.getDecorView().setSystemUiVisibility(IMMERSIVE_FLAGS);
        // Arm SDL's own re-hide watchdog (SDLActivity.onSystemUiVisibilityChange
        // ignores bar reveals unless this is set). IMMERSIVE_STICKY already
        // auto-hides a swipe-revealed bar, but a bar the system raises on its
        // own — a notification arriving mid-game — produces no focus change for
        // the override above to catch, so the watchdog is what covers it. Safe
        // to force on: there is no windowed mode on Android (port_main.c).
        mFullscreenModeActive = true;
    }

    // The panel tracks onStart/onStop, not onResume/onPause. onPause also
    // fires for transient interruptions — permission dialogs, system popups,
    // display config changes — where tearing the Presentation down just makes
    // the bottom screen flash to the launcher and back (a zelda3-android
    // lesson from the same hardware). onStop is the real "user left the app"
    // boundary, and it is also SDL's own: this SDLActivity glue pauses the
    // native thread from onStop/onStart when mHasMultiWindow is set (API 24+,
    // i.e. every device we target), so the panel's Surface comes and goes in
    // step with the game that feeds it. Keeping the panel up past onStop
    // would leave the native painter (a free-running ~20 Hz thread, see
    // port/port_second_screen.c) redrawing a frozen game in the background.
    @Override
    protected void onStart() {
        super.onStart();
        mSecondScreen.start();
    }

    @Override
    protected void onStop() {
        mSecondScreen.stop();
        super.onStop();
    }
}
