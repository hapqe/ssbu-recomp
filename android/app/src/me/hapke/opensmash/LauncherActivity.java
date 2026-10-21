package me.hapke.opensmash;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The launcher: where the game folder is, whether it is complete, whether this device can run the port, and the copy of
 * the game libraries into the app's private storage (Android runs no code from shared storage). Nothing of the game is
 * in the APK: the folder is made on a PC from the user's own dump (android/tools/export_for_android.py).
 * Also the GPU driver: the system's, or a custom one from an AdrenoTools driver package (a .zip with meta.json and the
 * driver .so, e.g. Mesa Turnip for Adreno) installed into private storage and loaded by android/runtime/vk_driver.c.
 */
public class LauncherActivity extends Activity {
    static final String PREFS = "opensmash";
    static final int REQ_TREE = 1;
    static final int REQ_DRIVER = 2;

    private EditText folder, extraEnv;
    private TextView checks, device, status, driverInfo;
    private Button start, access, copy;
    private ProgressBar progress;
    private Spinner touch, resolution;
    /* the game's render resolution: resolution_scale of graphics.ini against the Switch's 1920x1080 (SSBU_VK_SCALE) */
    private static final String[] RES_NAMES = {"From graphics.ini", "540p (0.5x)", "720p (0.67x)", "900p (0.83x)", "1080p (native)", "1440p (1.33x)", "2160p (2x)"};
    private static final String[] RES_SCALE = {null, "0.5", "0.6667", "0.8333", "1.0", "1.3333", "2.0"};
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean copying;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        int pad = dp(16);
        ScrollView scroll = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);
        scroll.addView(col);

        TextView title = new TextView(this);
        title.setText("OpenSmash for Android");
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(title);
        TextView intro = new TextView(this);
        intro.setText("Super Smash Bros. Ultimate (13.0.4 / 13.0.5) recompiled for this phone. The APK contains no game code and no game data: "
                + "build the game folder on your PC from your own dump (see ANDROID.md: scripts/setup.py, then android/tools/build_game_android.py "
                + "and android/tools/export_for_android.py) and copy it to this device.");
        intro.setPadding(0, dp(6), 0, dp(12));
        col.addView(intro);

        access = new Button(this);
        access.setText("Allow access to all files (needed for the game folder)");
        access.setOnClickListener(v -> askAllFiles());
        col.addView(access);

        col.addView(label("Game folder"));
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        folder = new EditText(this);
        folder.setSingleLine(true);
        folder.setText(p.getString("folder", Environment.getExternalStorageDirectory().getAbsolutePath() + "/OpenSmash"));
        row.addView(folder, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Button pick = new Button(this);
        pick.setText("Choose…");
        pick.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE));
        row.addView(pick);
        col.addView(row);
        Button recheck = new Button(this);
        recheck.setText("Check the folder");
        recheck.setOnClickListener(v -> refresh());
        col.addView(recheck);

        checks = mono();
        col.addView(checks);

        copy = new Button(this);
        copy.setText("Copy the game libraries into the app");
        copy.setOnClickListener(v -> copyLibraries(false));
        col.addView(copy);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        col.addView(progress);
        status = new TextView(this);
        col.addView(status);

        col.addView(label("On-screen controls (touch)"));
        touch = spinner(new String[] {"Auto: off while a controller is connected", "On", "Off"}, p.getInt("touch_mode", p.getBoolean("touch", true) ? 0 : 2));
        col.addView(touch);
        col.addView(label("Render resolution (lower = faster)"));
        resolution = spinner(RES_NAMES, p.getInt("resolution", 0));
        col.addView(resolution);
        col.addView(label("Extra settings (one NAME=VALUE per line, e.g. SSBU_VK_MISSING=warn)"));
        extraEnv = new EditText(this);
        extraEnv.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        extraEnv.setMinLines(2);
        extraEnv.setTypeface(Typeface.MONOSPACE);
        extraEnv.setText(p.getString("env", ""));
        col.addView(extraEnv);

        start = new Button(this);
        start.setText("Start the game");
        start.setTextSize(18);
        start.setOnClickListener(v -> startGame());
        col.addView(start);
        Button log = new Button(this);
        log.setText("Show the last game log");
        log.setOnClickListener(v -> showLog());
        col.addView(log);

        col.addView(label("GPU driver"));
        driverInfo = new TextView(this);
        col.addView(driverInfo);
        Button installDriver = new Button(this);
        installDriver.setText("Install a driver (AdrenoTools .zip, e.g. Turnip)…");
        installDriver.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
            startActivityForResult(i, REQ_DRIVER);
        });
        col.addView(installDriver);
        Button systemDriver = new Button(this);
        systemDriver.setText("Use the system driver");
        systemDriver.setOnClickListener(v -> { deleteTree(driverDir()); driverChanged("system driver selected"); });
        col.addView(systemDriver);

        col.addView(label("This device"));
        device = mono();
        col.addView(device);
        setContentView(scroll);
        driverChanged(null);
    }

    /* ---------------------------------------------------------------- the GPU driver */
    File driverDir() { return new File(getFilesDir(), "gpu_driver"); }
    /** The installed custom driver's .so (meta.json "libraryName"), or null: the system's driver. */
    File driverLib() {
        try {
            JSONObject m = new JSONObject(readText(new File(driverDir(), "meta.json"), 1 << 16));
            File f = new File(driverDir(), new File(m.getString("libraryName")).getName());
            return f.isFile() ? f : null;
        } catch (Exception e) { return null; }
    }
    private String driverName() {
        try {
            JSONObject m = new JSONObject(readText(new File(driverDir(), "meta.json"), 1 << 16));
            return m.optString("name", "?") + " (" + m.optString("driverVersion", "?") + ", " + m.optString("author", "?") + ")";
        } catch (Exception e) { return "?"; }
    }
    private void driverChanged(String msg) {
        File lib = driverLib();
        driverInfo.setText((msg != null ? msg + "\n" : "") + (lib != null ? "Custom driver: " + driverName() : "System driver"));
        device.setText("checking the Vulkan driver…");
        final String path = lib != null ? lib.getAbsolutePath() : null;
        new Thread(() -> {
            String r;
            try { r = DeviceCheck.report(path); } catch (Throwable t) { r = "device check failed: " + t; }
            final String text = r;
            ui.post(() -> device.setText(text));
        }).start();
    }
    private static void deleteTree(File f) {
        File[] fs = f.listFiles();
        if (fs != null) for (File c : fs) deleteTree(c);
        f.delete();
    }
    /** An AdrenoTools driver package: meta.json (libraryName = the driver .so) and the files beside it, at its top level. */
    private void installDriver(Uri zip) {
        driverInfo.setText("installing the driver…");
        new Thread(() -> {
            String msg;
            File tmp = new File(getFilesDir(), "gpu_driver.new");
            deleteTree(tmp);
            try (ZipInputStream in = new ZipInputStream(getContentResolver().openInputStream(zip))) {
                tmp.mkdirs();
                ZipEntry e;
                byte[] buf = new byte[1 << 16];
                while ((e = in.getNextEntry()) != null) {
                    if (e.isDirectory()) continue;
                    String name = new File(e.getName()).getName();                 /* top level only: no paths out of the folder */
                    if (name.isEmpty() || name.startsWith(".")) continue;
                    try (OutputStream out = new FileOutputStream(new File(tmp, name))) {
                        int n;
                        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                    }
                }
                JSONObject m = new JSONObject(readText(new File(tmp, "meta.json"), 1 << 16));
                String lib = new File(m.getString("libraryName")).getName();
                if (!new File(tmp, lib).isFile()) throw new IOException("the package has no " + lib);
                if (!isAndroidElf(new File(tmp, lib))) throw new IOException(lib + " is not a 64-bit ARM library");
                deleteTree(driverDir());
                if (!tmp.renameTo(driverDir())) throw new IOException("cannot rename " + tmp);
                msg = "driver installed";
            } catch (Exception ex) {
                deleteTree(tmp);
                msg = "not an AdrenoTools driver package (meta.json + driver .so): " + ex.getMessage();
            }
            final String m = msg;
            ui.post(() -> driverChanged(m));
        }).start();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    @Override
    protected void onPause() {
        super.onPause();
        save();
    }

    private void save() {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("folder", folder.getText().toString().trim())
                .putString("env", extraEnv.getText().toString()).putInt("touch_mode", touch.getSelectedItemPosition())
                .putInt("resolution", resolution.getSelectedItemPosition()).apply();
    }

    private Spinner spinner(String[] items, int sel) {
        Spinner sp = new Spinner(this);
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(a);
        sp.setSelection(sel >= 0 && sel < items.length ? sel : 0);
        return sp;
    }
    /** A game controller (not the phone's own buttons) is connected now; its name, or null. */
    static String controllerName() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            int s = d.getSources();
            if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) return d.getName();
        }
        return null;
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }
    private TextView label(String s) { TextView t = new TextView(this); t.setText(s); t.setTypeface(Typeface.DEFAULT_BOLD); t.setPadding(0, dp(14), 0, dp(2)); return t; }
    private TextView mono() { TextView t = new TextView(this); t.setTypeface(Typeface.MONOSPACE); t.setTextSize(12); t.setPadding(0, dp(6), 0, dp(6)); return t; }

    private static boolean allFiles() { return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager(); }
    private void askAllFiles() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        if (req == REQ_DRIVER) { installDriver(data.getData()); return; }
        if (req != REQ_TREE) return;
        String id = DocumentsContract.getTreeDocumentId(data.getData());       /* "primary:OpenSmash" or "1234-ABCD:Games/OpenSmash" */
        int colon = id.indexOf(':');
        String vol = colon >= 0 ? id.substring(0, colon) : id, rel = colon >= 0 ? id.substring(colon + 1) : "";
        String base = "primary".equalsIgnoreCase(vol) ? Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
        folder.setText(rel.isEmpty() ? base : base + "/" + rel);
        save();
        refresh();
    }

    File game() { return new File(folder.getText().toString().trim()); }
    File libDir() { return new File(getFilesDir(), "gamelib"); }

    /** The game folder's version (state.json "game_version"), default 13.0.4. */
    static String version(File game) {
        try {
            String s = readText(new File(game, "state.json"), 1 << 16);
            int i = s.indexOf("\"game_version\"");
            if (i >= 0) { int a = s.indexOf('"', s.indexOf(':', i) + 1), b = s.indexOf('"', a + 1); if (a > 0 && b > a) return s.substring(a + 1, b); }
        } catch (IOException ignored) { }
        return "13.0.4";
    }

    private boolean ready;
    private void refresh() {
        access.setVisibility(allFiles() ? View.GONE : View.VISIBLE);
        File g = game();
        StringBuilder sb = new StringBuilder();
        boolean ok = true;
        if (!allFiles()) { sb.append("✗ file access not allowed yet (button above)\n"); ok = false; }
        String ver = version(g);
        ok &= line(sb, new File(g, "work").isDirectory(), "work/ (the runtime's folder)");
        ok &= line(sb, new File(g, "state.json").isFile(), "state.json (game version " + ver + ")");
        ok &= line(sb, new File(g, "binaries/" + ver + "/main.elf").isFile(), "binaries/" + ver + "/main.elf");
        boolean romfs = new File(g, "romfs/data.arc").isFile();
        ok &= line(sb, romfs, "romfs/data.arc (the game's RomFS)");
        File dll = new File(g, "work/ssbu_game_eh_o1.dll");
        boolean elf = isAndroidElf(dll);
        ok &= line(sb, elf, dll.isFile() && !elf ? "work/ssbu_game_eh_o1.dll is the Windows build: build the Android one (android/tools/build_game_android.py)"
                : "work/ssbu_game_eh_o1.dll (the lifted game, built for Android)");
        line(sb, new File(g, "work/fonts_open").isDirectory() || new File(g, "sysdata/fonts").isDirectory(), "fonts (work/fonts_open or sysdata/fonts)");
        boolean copied = libsCurrent();
        ok &= line(sb, copied, copied ? "game libraries copied into the app" : "game libraries not copied into the app yet (or changed): button below");
        checks.setText(sb.toString());
        ready = ok;
        start.setEnabled(!copying);
        copy.setEnabled(!copying && allFiles() && elf);
    }
    private static boolean line(StringBuilder sb, boolean ok, String what) { sb.append(ok ? "✓ " : "✗ ").append(what).append('\n'); return ok; }

    static boolean isAndroidElf(File f) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            byte[] h = new byte[20];
            r.readFully(h);
            return h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[18] == (byte) 183;   /* EM_AARCH64 */
        } catch (IOException e) { return false; }
    }

    /* every *.dll below work/ (the game library, nro_bin*\, fighters_bin\, ...), relative paths */
    private static void findLibs(File dir, String rel, List<String> out) {
        File[] fs = dir.listFiles();
        if (fs == null) return;
        for (File f : fs) {
            String r = rel.isEmpty() ? f.getName() : rel + "/" + f.getName();
            if (f.isDirectory()) { if (!f.getName().startsWith("obj")) findLibs(f, r, out); }
            else if (f.getName().toLowerCase(Locale.ROOT).endsWith(".dll") && isAndroidElf(f)) out.add(r);
        }
    }
    private boolean libsCurrent() {
        File work = new File(game(), "work");
        if (!work.isDirectory()) return false;
        List<String> libs = new ArrayList<>();
        findLibs(work, "", libs);
        if (libs.isEmpty()) return false;
        for (String r : libs) {
            File a = new File(work, r), b = new File(libDir(), r);
            if (!b.isFile() || b.length() != a.length() || b.lastModified() < a.lastModified()) return false;
        }
        return true;
    }
    private void copyLibraries(boolean thenStart) {
        if (copying) return;
        copying = true;
        refresh();
        progress.setVisibility(View.VISIBLE);
        File work = new File(game(), "work");
        new Thread(() -> {
            String msg;
            try {
                List<String> libs = new ArrayList<>();
                findLibs(work, "", libs);
                long total = 0, done = 0;
                for (String r : libs) total += new File(work, r).length();
                for (String r : libs) {
                    File a = new File(work, r), b = new File(libDir(), r);
                    if (b.isFile() && b.length() == a.length() && b.lastModified() >= a.lastModified()) { done += a.length(); continue; }
                    b.getParentFile().mkdirs();
                    File tmp = new File(b.getPath() + ".part");
                    try (InputStream in = new FileInputStream(a); OutputStream out = new FileOutputStream(tmp)) {
                        byte[] buf = new byte[1 << 20];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            out.write(buf, 0, n);
                            done += n;
                            final int pm = (int) (total > 0 ? done * 1000 / total : 0);
                            final String now = r;
                            final long d = done, t = total;
                            ui.post(() -> { progress.setProgress(pm); status.setText(String.format(Locale.ROOT, "copying %s  (%d / %d MB)", now, d >> 20, t >> 20)); });
                        }
                    }
                    if (!tmp.renameTo(b)) throw new IOException("cannot rename " + tmp);
                    b.setLastModified(Math.max(a.lastModified(), System.currentTimeMillis()));
                }
                msg = String.format(Locale.ROOT, "%d game libraries ready (%d MB)", libs.size(), total >> 20);
            } catch (IOException e) {
                msg = "copy failed: " + e.getMessage();
            }
            final String m = msg;
            ui.post(() -> {
                copying = false;
                progress.setVisibility(View.GONE);
                status.setText(m);
                refresh();
                if (thenStart && ready) startGame();
            });
        }).start();
    }

    private void startGame() {
        save();
        refresh();
        if (!ready) {
            if (allFiles() && isAndroidElf(new File(game(), "work/ssbu_game_eh_o1.dll")) && !libsCurrent()) { copyLibraries(true); return; }
            new AlertDialog.Builder(this).setTitle("The game folder is not complete").setMessage(checks.getText())
                    .setPositiveButton("OK", null).show();
            return;
        }
        ArrayList<String> env = new ArrayList<>();
        File driver = driverLib();
        if (driver != null) env.add("SSBU_VK_DRIVER=" + driver.getAbsolutePath());     /* first: an extra setting can override it */
        String scale = RES_SCALE[resolution.getSelectedItemPosition()];
        if (scale != null) env.add("SSBU_VK_SCALE=" + scale);                       /* before the extra settings: one of them can override it */
        for (String l : extraEnv.getText().toString().split("\n")) { l = l.trim(); if (l.contains("=") && !l.startsWith("#")) env.add(l); }
        env.add("SSBU_LOCALE=" + Locale.getDefault().toLanguageTag());
        env.add("SSBU_REGION=" + Locale.getDefault().getCountry());
        env.add("SSBU_TZ_NAME=" + TimeZone.getDefault().getID());
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra("game", game().getAbsolutePath());
        i.putExtra("libdir", libDir().getAbsolutePath());
        /* Auto: a connected controller is player 1, so the on-screen pad (which would take that slot) is left out */
        String pad = controllerName();
        int tm = touch.getSelectedItemPosition();
        i.putExtra("touch", tm == 1 || (tm == 0 && pad == null));
        i.putExtra("touch_mode", tm);
        if (pad != null) status.setText("Controller: " + pad);
        i.putStringArrayListExtra("env", env);
        startActivity(i);
    }

    private void showLog() {
        File f = new File(game(), "work/ssbu_android_log.txt");
        String text;
        try {
            long n = f.length();
            text = readTail(f, 64 * 1024);
            if (n > 64 * 1024) text = "… (last 64 KB of " + (n >> 10) + " KB)\n" + text;
        } catch (IOException e) { text = "no log yet (" + f + ")"; }
        TextView t = new TextView(this);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextSize(10);
        t.setTextIsSelectable(true);
        t.setText(text);
        t.setPadding(dp(8), dp(8), dp(8), dp(8));
        ScrollView s = new ScrollView(this);
        s.addView(t);
        s.post(() -> s.fullScroll(View.FOCUS_DOWN));
        new AlertDialog.Builder(this).setTitle("ssbu_android_log.txt").setView(s).setPositiveButton("Close", null).show();
    }

    static String readText(File f, int max) throws IOException {
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(max, f.length())];
            int n = 0, r;
            while (n < b.length && (r = in.read(b, n, b.length - n)) > 0) n += r;
            return new String(b, 0, n, "UTF-8");
        }
    }
    static String readTail(File f, int max) throws IOException {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            long len = r.length(), from = Math.max(0, len - max);
            byte[] b = new byte[(int) (len - from)];
            r.seek(from);
            r.readFully(b);
            return new String(b, "UTF-8");
        }
    }
}
