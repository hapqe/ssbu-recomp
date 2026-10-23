package me.hapke.opensmash;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.system.Os;
import android.text.InputFilter;
import android.text.InputType;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * The game: SDL3's activity runs SDL_main of libmain.so (android/runtime/android_main.c) with the game folder, the folder
 * of the copied game libraries and the environment as arguments. The SSBU_* environment is also set here, before the
 * libraries are loaded, because parts of the runtime read it in their constructors.
 */
public class GameActivity extends SDLActivity {
    private static GameActivity self;
    private String game, libdir;
    private ArrayList<String> env;
    private TouchControls touch;
    private InputManager.InputDeviceListener padWatch;

    @Override
    protected void onCreate(Bundle saved) {
        self = this;
        Intent i = getIntent();
        game = i.getStringExtra("game");
        libdir = i.getStringExtra("libdir");
        env = i.getStringArrayListExtra("env");
        if (env == null) env = new ArrayList<>();
        /* the on-screen pad exists from the start (android_main.c): it is player 1, the keyboard takes a later slot */
        if (i.getBooleanExtra("touch", true)) env.add("SSBU_TOUCH_PAD=1");
        for (String kv : env) {
            int eq = kv.indexOf('=');
            if (eq > 0) try { Os.setenv(kv.substring(0, eq), kv.substring(eq + 1), true); } catch (Exception ignored) { }
        }
        /* aspect = stretch / wide (SSBU_ASPECT, display.c): the picture fills the whole screen, the camera's cut-out included
         * (without this the window in landscape stops short of the cut-out's edge of the screen) */
        String as = Os.getenv("SSBU_ASPECT");
        if (as != null && (as.equalsIgnoreCase("stretch") || as.equalsIgnoreCase("fill") || as.equals("1") || as.equalsIgnoreCase("wide") || as.equals("2"))) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        /* 2026-10-23, input latency that "is sometimes more and sometimes less": Android hands a controller's sticks, d-pad
         * (hat) and analog triggers to the app as joystick MotionEvents, and those are batched until the UI thread's next
         * frame (Choreographer), so SDL's state (SDLControllerManager -> Android_OnJoy) took them 0..1 refresh late, by
         * chance (0-8.4 ms on the Marble's 120 Hz panel, 0-16.7 ms at 60 Hz). Buttons are KeyEvents and were never
         * batched. Unbuffered dispatch delivers every joystick sample to the focused surface as it arrives. */
        if (mSurface != null) mSurface.requestUnbufferedDispatch(InputDevice.SOURCE_CLASS_JOYSTICK);
        /* touch_mode (LauncherActivity): 0 auto = shown while no controller is connected, 1 on, 2 off. A controller that
         * connects takes player 1 from the on-screen pad (nnpad_sdl.c); one that was there at the start left it out (touch) */
        int mode = i.getIntExtra("touch_mode", i.getBooleanExtra("touch", true) ? 1 : 2);
        if (mode != 2 && mLayout != null) {
            touch = new TouchControls(this);
            mLayout.addView(touch, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            if (!i.getBooleanExtra("touch", true)) touch.setVisibility(View.GONE);
        }
        if (mode == 0 && touch != null) {
            padWatch = new InputManager.InputDeviceListener() {
                @Override public void onInputDeviceAdded(int id) { padsChanged(); }
                @Override public void onInputDeviceRemoved(int id) { padsChanged(); }
                @Override public void onInputDeviceChanged(int id) { padsChanged(); }
            };
            ((InputManager) getSystemService(Context.INPUT_SERVICE)).registerInputDeviceListener(padWatch, null);
        }
    }

    private void padsChanged() {
        if (touch != null) touch.setVisibility(LauncherActivity.controllerName() == null ? View.VISIBLE : View.GONE);
    }

    @Override
    protected String[] getLibraries() { return new String[] {"SDL3", "main"}; }

    @Override
    protected String[] getArguments() {
        ArrayList<String> a = new ArrayList<>();
        a.add(game);
        a.add(libdir);
        a.addAll(env);
        return a.toArray(new String[0]);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    protected void onDestroy() {
        if (padWatch != null) ((InputManager) getSystemService(Context.INPUT_SERVICE)).unregisterInputDeviceListener(padWatch);
        super.onDestroy();
        /* the runtime cannot be restarted inside a process: the game process ends with its activity */
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }

    /* ------------------------------------------------------------ called from native code (android_main.c), any thread */
    public static void showMessage(String title, String text) {
        GameActivity a = self;
        if (a == null) return;
        a.runOnUiThread(() -> new AlertDialog.Builder(a).setTitle(title).setMessage(text).setPositiveButton("OK", null).show());
    }

    /** The game's software keyboard: blocks the calling (game) thread until the user answers; null = cancelled. */
    public static String textInput(String header, String guide, String initial, int maxLen, int minLen, boolean password) {
        GameActivity a = self;
        if (a == null) return null;
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<String> result = new AtomicReference<>();
        a.runOnUiThread(() -> {
            LinearLayout col = new LinearLayout(a);
            col.setOrientation(LinearLayout.VERTICAL);
            int pad = (int) (16 * a.getResources().getDisplayMetrics().density);
            col.setPadding(pad, pad / 2, pad, 0);
            if (guide != null && !guide.isEmpty()) { TextView g = new TextView(a); g.setText(guide); col.addView(g); }
            EditText e = new EditText(a);
            e.setSingleLine(true);
            e.setInputType(password ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_CLASS_TEXT);
            if (maxLen > 0) e.setFilters(new InputFilter[] {new InputFilter.LengthFilter(maxLen)});
            e.setText(initial);
            e.setSelection(e.getText().length());
            col.addView(e);
            AlertDialog d = new AlertDialog.Builder(a).setTitle(header == null || header.isEmpty() ? "Enter text" : header).setView(col)
                    .setPositiveButton("OK", (x, y) -> { String t = e.getText().toString(); result.set(t.length() >= minLen ? t : null); })
                    .setNegativeButton("Cancel", null)
                    .setOnDismissListener(x -> done.countDown()).create();
            d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
            d.show();
        });
        try { done.await(); } catch (InterruptedException ignored) { }
        return result.get();
    }

    /** A message over the keyboard; question = yes / no. Blocks; true = OK / yes. */
    public static boolean askMessage(String text, boolean question) {
        GameActivity a = self;
        if (a == null) return true;
        CountDownLatch done = new CountDownLatch(1);
        AtomicBoolean yes = new AtomicBoolean(!question);
        a.runOnUiThread(() -> {
            AlertDialog.Builder b = new AlertDialog.Builder(a).setMessage(text).setOnDismissListener(x -> done.countDown());
            if (question) b.setPositiveButton("Yes", (x, y) -> yes.set(true)).setNegativeButton("No", null);
            else b.setPositiveButton("OK", null);
            b.show();
        });
        try { done.await(); } catch (InterruptedException ignored) { }
        return yes.get();
    }

    /* the on-screen controls (TouchControls) feed a virtual SDL gamepad (android_main.c) */
    static native void nativeTouchPad(int buttons, float lx, float ly, float rx, float ry);
}
