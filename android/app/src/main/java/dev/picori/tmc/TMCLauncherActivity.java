package dev.picori.tmc;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * Launcher trampoline: decides which display the game gets, then starts
 * {@link TMCActivity} there and gets out of the way. It draws nothing —
 * onCreate finishes it.
 *
 * This exists because the choice cannot be made from inside the game. SDL's
 * Android backend only syncs a window's system-bar/immersive state from the
 * flags passed to the SDL_CreateWindow that made it (see the comment in
 * port/port_main.c), so moving the game to the other screen means creating
 * that window against the other display, which means creating the activity
 * against it — a launch-time decision, before any of the game is up. Hence
 * the panel's SWAP SCREENS row reads RESTART until this code has run again.
 *
 * The setting is read straight out of the port's own config.json rather
 * than duplicated into SharedPreferences, so the panel row that writes it
 * and the launcher that applies it are looking at one value.
 */
public class TMCLauncherActivity extends Activity {
    private static final String TAG = "TMCLauncher";
    private static final String CONFIG_NAME = "config.json";
    private static final String SWAP_KEY = "second_screen_swap";
    /* config.json is small (a few KB of settings); cap the read anyway so a
     * corrupt or hostile file can't be slurped into memory at startup. */
    private static final int CONFIG_MAX_BYTES = 1 << 20;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent intent = new Intent(this, TMCActivity.class);
        int display = swapDisplayId();
        if (display != -1 && Build.VERSION.SDK_INT >= 26) {
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(display);
            try {
                startActivity(intent, options.toBundle());
                finish();
                return;
            } catch (RuntimeException e) {
                // Some firmwares refuse app launches on a secondary display
                // outright. Fall through to a normal launch: the swap is a
                // preference, not a reason to fail to start the game. The
                // panel then reports the setting as RESTART rather than ON,
                // because ON would be a lie about the screens in front of
                // the player.
                Log.w(TAG, "launch on display " + display + " refused", e);
            }
        }
        startActivity(intent);
        finish();
    }

    /**
     * @return the display to launch the game on for the swapped layout, or
     *         -1 to launch normally (setting off, no config yet, no second
     *         display attached, or an API level without launch-display
     *         targeting).
     */
    private int swapDisplayId() {
        if (!readSwapFlag()) {
            return -1;
        }
        DisplayManager dm = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        if (dm == null) {
            return -1;
        }
        for (Display d : dm.getDisplays()) {
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY) {
                return d.getDisplayId();
            }
        }
        return -1;
    }

    /** The port's persisted "second_screen_swap" flag, false if unreadable. */
    private boolean readSwapFlag() {
        for (File dir : configDirs()) {
            if (dir == null) {
                continue;
            }
            File config = new File(dir, CONFIG_NAME);
            if (!config.isFile()) {
                continue;
            }
            try {
                return new JSONObject(readText(config)).optBoolean(SWAP_KEY, false);
            } catch (Exception e) {
                // A config we can't parse is not worth failing a launch
                // over; the game rewrites it from defaults anyway.
                Log.w(TAG, "couldn't read " + config, e);
                return false;
            }
        }
        return false;
    }

    /**
     * Where the port keeps its state, in the order port_main.c picks it:
     * the app's external files dir first, the private files dir as the
     * fallback for when external storage isn't writable (SDL's pref path on
     * Android is that same private files dir).
     */
    private File[] configDirs() {
        return new File[] { getExternalFilesDir(null), getFilesDir() };
    }

    private static String readText(File file) throws IOException {
        long size = file.length();
        if (size <= 0 || size > CONFIG_MAX_BYTES) {
            throw new IOException("implausible config size " + size);
        }
        byte[] raw = new byte[(int) size];
        FileInputStream in = new FileInputStream(file);
        try {
            new java.io.DataInputStream(in).readFully(raw);
        } finally {
            in.close();
        }
        return new String(raw, "UTF-8");
    }
}
