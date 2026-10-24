package me.hapke.opensmash;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
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
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The launcher. Everything the game needs is set up here, on the device:
 *  - the game folder from the user's own Switch files (keys, base game, update 13.0.4 / 13.0.5, add-on content) found in
 *    files the user picks one step at a time (the setup walkthrough): GameSetup with libswitchimport.so (android/tools/romfs_extract.c) makes romfs/, binaries/,
 *    state.json and config/dlc.txt; the game libraries (the game's code, recompiled on a PC from the same dump:
 *    android/tools/build_game_android.py --package) are imported from a .zip;
 *  - their copy into the app's private storage (Android runs no code from shared storage);
 *  - add-on content (DLC): its ids from .nsp / .rar / .zip names (or the names inside) into config/dlc.txt;
 *  - save data (a folder or .zip holding save_data, e.g. from JKSV / Checkpoint / an emulator) into <game>/saves/save,
 *    where android_main.c points SSBU_USERDATA (runtime/nnfs.c MountSaveData);
 *  - the GPU driver: the system's, or an AdrenoTools driver package (e.g. Mesa Turnip), android/runtime/vk_driver.c;
 *  - the settings passed to the game as SSBU_* variables.
 * Nothing of the game is in the APK.
 */
public class LauncherActivity extends Activity {
    static final String PREFS = "opensmash";
    static final int REQ_TREE = 1, REQ_DRIVER = 2, REQ_SAVE_TREE = 3, REQ_SAVE_ZIP = 4, REQ_DLC_FILES = 6, REQ_DLC_TREE = 7, REQ_LIBS_ZIP = 8,
            REQ_WIZ_BASE = 9, REQ_WIZ_PICK = 10, REQ_WIZ_KEYS = 11;      /* the setup walkthrough: main game, update or DLC, keys */

    private Ui u;
    private EditText extraEnv;
    private TextView folderText, device, status, driverInfo, saveInfo, dlcInfo, statePill, prepHint, libsNote;
    private LinearLayout checks;
    private Button start, copy;
    private ProgressBar progress;
    private Spinner touch, resolution, aspect;
    private CheckBox fps;
    private String folderPath;
    /* the game's render resolution: resolution_scale of graphics.ini against the Switch's 1920x1080 (SSBU_VK_SCALE) */
    private static final String[] RES_NAMES = {"From graphics.ini", "540p (0.5×)", "720p (0.67×)", "900p (0.83×)", "1080p (native)", "1440p (1.33×)", "2160p (2×)"};
    private static final String[] RES_SCALE = {null, "0.5", "0.6667", "0.8333", "1.0", "1.3333", "2.0"};
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean busy;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        u = new Ui(this);
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        folderPath = p.getString("folder", Environment.getExternalStorageDirectory().getAbsolutePath() + "/OpenSmash");
        getWindow().setStatusBarColor(Ui.BG);
        getWindow().setNavigationBarColor(Ui.BG);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Ui.BG);
        scroll.setFillViewport(true);
        LinearLayout page = u.column();
        page.setPadding(u.dp(18), u.dp(22), u.dp(18), u.dp(28));
        scroll.addView(page);

        /* ---- header: the name, the state, Play */
        LinearLayout hero = u.column();
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[] {0xFF2A1220, 0xFF181B22, 0xFF13202A});
        g.setCornerRadius(u.dp(24));
        hero.setBackground(g);
        hero.setPadding(u.dp(20), u.dp(20), u.dp(20), u.dp(20));
        page.addView(hero);
        LinearLayout top = u.row();
        TextView title = u.text("OpenSmash", 30, Ui.TEXT);
        title.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        top.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        statePill = u.pill("…", Ui.TEXT2);
        top.addView(statePill);
        hero.addView(top);
        TextView sub = u.text("Super Smash Bros. Ultimate, recompiled for Android. Uses your own game files; nothing of the game is in the app.", 13.5f, Ui.TEXT2);
        sub.setPadding(0, u.dp(4), 0, u.dp(14));
        hero.addView(sub);
        start = u.filled("▶  Play", v -> startGame());
        start.setTextSize(18);
        hero.addView(start, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, u.dp(56)));
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        progress.setProgressTintList(android.content.res.ColorStateList.valueOf(Ui.ACCENT));
        u.add(hero, progress, 10);
        status = u.text("", 13, Ui.TEXT2);
        status.setPadding(0, u.dp(8), 0, 0);
        status.setVisibility(View.GONE);
        status.addTextChangedListener(new android.text.TextWatcher() {          /* no empty line under Play */
            @Override public void beforeTextChanged(CharSequence c, int a, int b, int d) { }
            @Override public void onTextChanged(CharSequence c, int a, int b, int d) { }
            @Override public void afterTextChanged(android.text.Editable e) { status.setVisibility(e.length() > 0 ? View.VISIBLE : View.GONE); }
        });
        hero.addView(status);

        /* ---- game files */
        LinearLayout files = u.card(page, "Game files", null);
        LinearLayout frow = u.row();
        folderText = u.text(folderPath, 13, Ui.TEXT2);
        frow.addView(folderText, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        frow.addView(u.plain("Change", v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE)));
        files.addView(frow);
        checks = u.column();
        u.add(files, checks, 4);
        u.add(files, u.tonal("Set up from Switch files…", v -> askSetup()), 10);
        libsNote = u.text("", 12, Ui.TEXT2);
        libsNote.setPadding(0, u.dp(10), 0, 0);
        files.addView(libsNote);
        copy = u.tonal("Copy the game libraries into the app", v -> copyLibraries(false));
        u.add(files, u.pair(u.tonal("Import libraries (.zip)…", v -> pickFile(REQ_LIBS_ZIP, false)), copy), 8);

        /* ---- add-on content */
        LinearLayout dlc = u.card(page, "Add-on content (DLC)", "Fighters, costumes, spirits and music are part of the update; your add-on content packages unlock them.");
        dlcInfo = u.text("", 14, Ui.TEXT);
        u.add(dlc, dlcInfo, 6);
        u.add(dlc, u.pair(u.tonal("Import files…", v -> pickFile(REQ_DLC_FILES, true)), u.tonal("Import a folder…", v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_DLC_TREE))), 10);
        LinearLayout dlcRow = u.row();
        dlcRow.addView(u.plain("Show the list", v -> showDlc()));
        dlcRow.addView(u.plain("Remove all", v -> removeDlc()));
        u.add(dlc, dlcRow, 4);

        /* ---- save data */
        LinearLayout sv = u.card(page, "Save data", "From JKSV, Checkpoint, yuzu / Eden / Ryujinx: a folder or .zip that holds save_data.");
        u.add(sv, u.pair(u.tonal("Import a folder…", v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_SAVE_TREE)),
                         u.tonal("Import a .zip…", v -> pickFile(REQ_SAVE_ZIP, false))), 10);
        saveInfo = u.text("", 13, Ui.TEXT2);
        u.add(sv, saveInfo, 6);

        /* ---- display and controls */
        LinearLayout disp = u.card(page, "Display & controls", null);
        disp.addView(u.label("Render resolution (lower = faster)"));
        resolution = u.spinner(RES_NAMES, p.getInt("resolution", 0));
        disp.addView(resolution);
        /* the game's picture is 16:9; aspect = keep | stretch | wide of graphics.ini (SSBU_ASPECT, display.c display_fit; wide: nvn_vk_draw.c wide_init) */
        disp.addView(u.label("Picture shape"));
        aspect = u.spinner(new String[] {"From graphics.ini", "16:9 with black bars (the game's own shape)", "Fill the screen (" + screenShape() + ", stretched)", "Wide: the screen's shape (" + screenShape() + ", more seen at the sides)"}, p.getInt("aspect", 0));
        disp.addView(aspect);
        prepHint = u.text("", 12, Ui.WARN);
        prepHint.setPadding(0, u.dp(6), 0, 0);
        disp.addView(prepHint);
        android.widget.AdapterView.OnItemSelectedListener re = new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> a, View v, int pos, long id) { updatePrepHint(); }
            @Override public void onNothingSelected(android.widget.AdapterView<?> a) { }
        };
        resolution.setOnItemSelectedListener(re);
        aspect.setOnItemSelectedListener(re);
        disp.addView(u.label("On-screen controls"));
        touch = u.spinner(new String[] {"Auto: off while a controller is connected", "On", "Off"}, p.getInt("touch_mode", p.getBoolean("touch", true) ? 0 : 2));
        disp.addView(touch);
        fps = new CheckBox(this);
        fps.setText("Show the frame rate (top left)");
        fps.setTextColor(Ui.TEXT);
        fps.setButtonTintList(android.content.res.ColorStateList.valueOf(Ui.ACCENT));
        fps.setChecked(p.getBoolean("fps", false));
        u.add(disp, fps, 8);

        /* ---- GPU driver */
        LinearLayout drv = u.card(page, "GPU driver", null);
        driverInfo = u.text("", 13, Ui.TEXT2);
        u.add(drv, driverInfo, 4);
        u.add(drv, u.pair(u.tonal("Install a driver (.zip)…", v -> pickFile(REQ_DRIVER, false)),
                          u.tonal("Use the system driver", v -> { deleteTree(driverDir()); driverChanged("system driver selected"); })), 10);

        /* ---- advanced */
        LinearLayout adv = u.card(page, "Advanced", null);
        adv.addView(u.label("Extra settings (one NAME=VALUE per line)"));
        extraEnv = new EditText(this);
        extraEnv.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        extraEnv.setMinLines(2);
        extraEnv.setTypeface(Typeface.MONOSPACE);
        extraEnv.setTextSize(13);
        extraEnv.setTextColor(Ui.TEXT);
        extraEnv.setHintTextColor(Ui.TEXT2);
        extraEnv.setHint("e.g. SSBU_VK_MISSING=warn");
        extraEnv.setBackground(Ui.round(Ui.CARD_HI, u.dp(12)));
        extraEnv.setPadding(u.dp(12), u.dp(10), u.dp(12), u.dp(10));
        extraEnv.setText(p.getString("env", ""));
        adv.addView(extraEnv);
        LinearLayout advRow = u.row();
        advRow.addView(u.plain("Show the last game log", v -> showLog()));
        advRow.addView(u.plain("Re-check", v -> { refresh(); driverChanged(null); }));
        u.add(adv, advRow, 6);
        adv.addView(u.label("This device"));
        device = u.mono();
        adv.addView(device);

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
    private static void deleteTree(File f) { GameSetup.deleteTree(f); }
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

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        return ControllerPanel.onKey(this, ev) || super.dispatchKeyEvent(ev);
    }

    @Override
    protected void onDestroy() {
        ControllerPanel.close();
        super.onDestroy();
    }

    private void save() {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("folder", folderPath)
                .putString("env", extraEnv.getText().toString()).putInt("touch_mode", touch.getSelectedItemPosition())
                .putInt("resolution", resolution.getSelectedItemPosition())
                .putInt("aspect", aspect.getSelectedItemPosition()).putBoolean("fps", fps.isChecked()).apply();
    }

    /** The screen's shape held in landscape, e.g. "20:9" (2400x1080), the cut-out of a camera included. */
    private String screenShape() {
        android.graphics.Rect r = getWindowManager().getMaximumWindowMetrics().getBounds();
        int w = Math.max(r.width(), r.height()), h = Math.min(r.width(), r.height());
        if (h <= 0) return "the screen's shape";
        String n = String.format(Locale.ROOT, "%.2f", w * 9.0 / h);
        if (n.endsWith(".00")) n = n.substring(0, n.length() - 3);
        return n + ":9";
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

    private static boolean allFiles() { return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager(); }
    private void askAllFiles() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }
    /** All-files access is needed for everything below: asks for it and says so. */
    private boolean needFiles() {
        if (allFiles()) return true;
        new AlertDialog.Builder(this).setTitle("Allow access to files").setMessage("The game folder and your Switch files are on the shared storage: allow OpenSmash to manage all files, then come back.")
                .setPositiveButton("Allow…", (d, w) -> askAllFiles()).setNegativeButton("Cancel", null).show();
        return false;
    }
    private void pickFile(int req, boolean multiple) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        if (multiple) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        else i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        startActivityForResult(i, req);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req >= REQ_WIZ_BASE && req <= REQ_WIZ_KEYS) { wizardResult(req, res == RESULT_OK && data != null ? data.getData() : null); return; }
        if (res != RESULT_OK || data == null) return;
        if (req == REQ_DLC_FILES) {
            List<Uri> uris = new ArrayList<>();
            ClipData cd = data.getClipData();
            if (cd != null) for (int i = 0; i < cd.getItemCount(); i++) uris.add(cd.getItemAt(i).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            importDlcFiles(uris);
            return;
        }
        if (data.getData() == null) return;
        if (req == REQ_DRIVER) { installDriver(data.getData()); return; }
        if (req == REQ_SAVE_TREE) { importSave(new File(treePath(data.getData())), null); return; }
        if (req == REQ_SAVE_ZIP) { importSave(null, data.getData()); return; }
        if (req == REQ_DLC_TREE) { scanForDlc(new File(treePath(data.getData()))); return; }
        if (req == REQ_LIBS_ZIP) { importLibraries(data.getData()); return; }
        if (req != REQ_TREE) return;
        folderPath = treePath(data.getData());
        folderText.setText(folderPath);
        save();
        refresh();
    }
    /** The file system path of a folder chosen with ACTION_OPEN_DOCUMENT_TREE (read through the all-files access). */
    private static String treePath(Uri tree) { return idPath(DocumentsContract.getTreeDocumentId(tree)); }
    /** A document id of the shared storage as a path: "primary:OpenSmash" or "1234-ABCD:Games/OpenSmash". */
    private static String idPath(String id) {
        int colon = id.indexOf(':');
        String vol = colon >= 0 ? id.substring(0, colon) : id, rel = colon >= 0 ? id.substring(colon + 1) : "";
        String base = "primary".equalsIgnoreCase(vol) ? Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
        return rel.isEmpty() ? base : base + "/" + rel;
    }
    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) { }
        String s = uri.getLastPathSegment();
        return s != null ? s : uri.toString();
    }
    private long uriSize(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
        } catch (Exception ignored) { }
        return -1;
    }

    /* ---------------------------------------------------------------- busy state: one long job at a time */
    private boolean begin(String what) {
        if (busy) { toastStatus("busy: wait for the current job to end"); return false; }
        busy = true;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        progress.setIndeterminate(true);
        progress.setVisibility(View.VISIBLE);
        status.setText(what);
        refresh();
        return true;
    }
    private void end(String msg) {
        busy = false;
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        progress.setVisibility(View.GONE);
        if (msg != null) status.setText(msg);
        refresh();
    }
    private void toastStatus(String s) { status.setText(s); }
    private GameSetup.Progress uiProgress() {
        return new GameSetup.Progress() {
            long last;
            @Override public void status(String s) { ui.post(() -> status.setText(s)); }
            @Override public void progress(long done, long total) {
                long now = System.currentTimeMillis();
                if (now - last < 200 && done < total) return;
                last = now;
                final int pm = (int) (total > 0 ? Math.min(1000, done * 1000 / total) : 0);
                ui.post(() -> {
                    progress.setIndeterminate(false);
                    progress.setProgress(pm);
                    String s = status.getText().toString().replaceAll("  ·  [0-9.]+ / [0-9.]+ GB$", "");
                    status.setText(String.format(Locale.ROOT, "%s  ·  %.1f / %.1f GB", s, done / 1073741824.0, total / 1073741824.0));
                });
            }
        };
    }

    /* ---------------------------------------------------------------- setting up: the walkthrough */
    /** What the walkthrough has taken so far (keys, base game, update, add-on content). */
    private GameSetup.Found wizard;

    /** Step 1 of 3: the main game; step 2: the update or DLC; step 3: the keys. Every step can be skipped. */
    private void askSetup() {
        if (!needFiles()) return;
        wizard = new GameSetup.Found();
        wizardBase();
    }
    private void wizardBase() {
        new AlertDialog.Builder(this).setTitle("1 of 3 · The main game")
                .setMessage("Choose the base game of Super Smash Bros. Ultimate (.nsp or .xci).\n\n" + wizard.summary())
                .setPositiveButton("Choose file…", (d, w) -> pickWizardFile(REQ_WIZ_BASE))
                .setNeutralButton("Skip", (d, w) -> wizardUpdate())
                .setNegativeButton("Cancel", null).show();
    }
    private void wizardUpdate() {
        new AlertDialog.Builder(this).setTitle("2 of 3 · Update or add-on content")
                .setMessage("Choose the update (13.0.4 or 13.0.5) or a DLC package (.nsp or .xci). Choose as many packages as you have, then Next.\n\n" + wizard.summary())
                .setPositiveButton("Choose file…", (d, w) -> pickWizardFile(REQ_WIZ_PICK))
                .setNeutralButton("Next", (d, w) -> wizardKeys())
                .setNegativeButton("Cancel", null).show();
    }
    private void wizardKeys() {
        new AlertDialog.Builder(this).setTitle("3 of 3 · The keys")
                .setMessage("Choose prod.keys (a title.keys beside it is used too).\n\n" + wizard.summary())
                .setPositiveButton("Choose prod.keys…", (d, w) -> pickWizardFile(REQ_WIZ_KEYS))
                .setNeutralButton("Skip", (d, w) -> wizardReview())
                .setNegativeButton("Cancel", null).show();
    }
    private void wizardStep(int req) {
        if (req == REQ_WIZ_BASE) wizardBase();
        else if (req == REQ_WIZ_PICK) wizardUpdate();
        else wizardKeys();
    }
    private void pickWizardFile(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, req);
    }
    /** A file the walkthrough asked for came back (uri null: none chosen). The file is taken, then the walkthrough goes on. */
    private void wizardResult(int req, Uri uri) {
        if (uri == null) { wizardStep(req); return; }
        String path = filePath(uri);
        if (path == null) { wizardRefused(req, displayName(uri) + " is not on the shared storage: copy it to Download or to the game folder, then choose it again."); return; }
        File file = new File(path);
        String name = file.getName(), low = name.toLowerCase(Locale.ROOT);
        if (req == REQ_WIZ_KEYS) {
            if (!low.endsWith(".keys")) { wizardRefused(req, name + " is not a keys file (.keys)."); return; }
            wizard.prodKeys = file;
            File t = new File(file.getParentFile(), "title.keys");
            wizard.titleKeys = t.isFile() ? t : null;
            wizardReview();
            return;
        }
        if (low.endsWith(".nsz") || low.endsWith(".xcz")) { wizardRefused(req, name + " is compressed (.nsz / .xcz): decompress it first, then choose it."); return; }
        if (!SwitchFiles.isPackage(name)) { wizardRefused(req, name + " is not a .nsp or .xci package."); return; }
        List<Long> ids = SwitchFiles.titleIds(name);
        if (ids.isEmpty()) ids = GameSetup.idsInside(file, GameSetup.tool(this), wizard);
        if (ids.isEmpty()) { wizardRefused(req, name + ": the game cannot tell which title it is (its name has no title id)."); return; }
        if (req == REQ_WIZ_BASE) {
            if (!ids.contains(SwitchFiles.APP)) { wizardRefused(req, name + " is not the base game (title 0100…6E000)."); return; }
            wizard.bases.clear();
            wizard.bases.add(file);
        } else if (ids.contains(SwitchFiles.UPDATE)) {
            if (!wizard.updates.contains(file)) wizard.updates.add(file);
        } else {
            int before = wizard.dlc.size();
            SwitchFiles.addDlc(SwitchFiles.titleIds(name).isEmpty() ? String.format(Locale.ROOT, "[%016X]", ids.get(0)) : name, wizard.dlc);
            if (wizard.dlc.size() == before) { wizardRefused(req, name + " is neither the update nor add-on content of this game."); return; }
            wizard.dlcPackages++;
        }
        wizardStep(req == REQ_WIZ_BASE ? REQ_WIZ_PICK : req);
    }
    private void wizardRefused(int req, String why) {
        new AlertDialog.Builder(this).setTitle("Not this file").setMessage(why)
                .setPositiveButton("OK", (d, w) -> wizardStep(req)).show();
    }
    /** The file system path of a file chosen with ACTION_OPEN_DOCUMENT, if it is on the shared storage (or Downloads); else null. */
    private String filePath(Uri uri) {
        if (!DocumentsContract.isDocumentUri(this, uri)) return null;
        String id = DocumentsContract.getDocumentId(uri), path = null;
        if (id.startsWith("raw:")) path = id.substring(4);
        else if ("com.android.externalstorage.documents".equals(uri.getAuthority()) && id.indexOf(':') >= 0) path = idPath(id);
        return path != null && new File(path).isFile() ? path : null;
    }
    /** The last step: what was taken, and the setup if it can run (the main game, its update and the keys). */
    private void wizardReview() {
        GameSetup.Found f = wizard;
        File g = game();
        boolean can = f.prodKeys != null && !f.bases.isEmpty() && !f.updates.isEmpty();
        boolean dlcOnly = f.bases.isEmpty() && f.updates.isEmpty() && !f.dlc.isEmpty();
        long free = new StatFs(g.getParentFile() != null && g.getParentFile().exists() ? g.getParentFile().getPath() : Environment.getExternalStorageDirectory().getPath()).getAvailableBytes();
        boolean haveRomfs = new File(g, "romfs/data.arc").isFile();
        String msg = f.summary() + "\n\nInto: " + g + "\nFree space: " + (free >> 30) + " GB" + (!haveRomfs && free < (17L << 30) ? "  (about 16 GB are needed: free some space first)" : "");
        AlertDialog.Builder b = new AlertDialog.Builder(this).setTitle(can ? "Ready to set up" : dlcOnly ? "Add-on content only" : "Not everything was chosen")
                .setNegativeButton("Cancel", null);
        if (can) b.setMessage(msg).setPositiveButton("Set up", (d, w) -> runSetup(f));
        else if (dlcOnly) b.setMessage(msg).setPositiveButton("Add the DLC only", (d, w) -> confirmDlc(f.dlc, "the walkthrough", f.notes));
        else b.setMessage(msg + "\n\nA first setup needs the main game, its update and the keys. Start again to choose them.").setNeutralButton("Start again", (d, w) -> askSetup());
        b.show();
    }
    /** Searches dir for add-on content (a folder of DLC packages) and confirms what it finds. */
    private void scanForDlc(File dir) {
        if (!needFiles() || !begin("Searching " + dir + "…")) return;
        File g = game();
        new Thread(() -> {
            GameSetup.Found f = GameSetup.scan(dir, GameSetup.tool(this), g, uiProgress());
            ui.post(() -> { end(""); confirmDlc(f.dlc, dir.getName(), f.notes); });
        }).start();
    }
    private void runSetup(GameSetup.Found f) {
        if (!begin("Setting up the game folder…")) return;
        File g = game();
        new Thread(() -> {
            String msg;
            try {
                String v = GameSetup.setup(this, f, g, uiProgress());
                msg = "Game data " + v + " set up" + (f.dlc.isEmpty() ? "" : ", " + f.dlc.size() + " add-on contents unlocked") + "."
                        + (isAndroidElf(new File(g, "work/ssbu_game_eh_o1.dll")) ? "" : " Now import the game libraries (.zip).");
            } catch (IOException e) {
                msg = "Setup stopped: " + e.getMessage() + "\n(Start it again to go on where it stopped.)";
            }
            final String m = msg;
            ui.post(() -> end(m));
        }).start();
    }

    /* ---------------------------------------------------------------- the game libraries (.zip) */
    private void importLibraries(Uri zip) {
        if (!needFiles()) return;
        if (gameRunning()) { quitFirst(); return; }
        if (!begin("Importing the game libraries…")) return;
        File g = game();
        long size = uriSize(zip);
        new Thread(() -> {
            String msg;
            try (InputStream in = getContentResolver().openInputStream(zip)) {
                int n = GameSetup.importLibraries(in, g, uiProgress(), size);
                GameSetup.installFonts(this, g);
                msg = n + " game libraries imported";
            } catch (IOException e) {
                msg = "Import failed: " + e.getMessage();
            }
            final String m = msg;
            ui.post(() -> { end(m); if (allFiles() && isAndroidElf(new File(g, "work/ssbu_game_eh_o1.dll")) && !libsCurrent()) copyLibraries(false); });
        }).start();
    }

    /* ---------------------------------------------------------------- add-on content */
    private void importDlcFiles(List<Uri> uris) {
        if (!needFiles() || !begin("Reading " + uris.size() + " file(s)…")) return;
        new Thread(() -> {
            TreeMap<Integer, String> rows = new TreeMap<>();
            List<String> notes = new ArrayList<>();
            for (Uri uri : uris) {
                String name = displayName(uri);
                int before = rows.size();
                SwitchFiles.addDlc(name, rows);
                if (rows.size() == before && (SwitchFiles.isArchive(name) || SwitchFiles.isPackage(name))) {
                    try (InputStream in = getContentResolver().openInputStream(uri)) {
                        for (String n : SwitchFiles.namesInside(name, in)) SwitchFiles.addDlc(n, rows);
                    } catch (IOException e) { notes.add("◐ " + name + ": " + e.getMessage()); }
                }
                if (rows.size() == before) notes.add("◐ " + name + ": no add-on content of this game found");
            }
            ui.post(() -> { end(""); confirmDlc(rows, uris.size() == 1 ? displayName(uris.get(0)) : uris.size() + " files", notes); });
        }).start();
    }
    private void confirmDlc(TreeMap<Integer, String> rows, String from, List<String> notes) {
        StringBuilder sb = new StringBuilder();
        for (String n : notes) sb.append(n).append('\n');
        if (rows.isEmpty()) {
            new AlertDialog.Builder(this).setTitle("No add-on content found").setMessage(sb.append("Nothing in ").append(from).append(" carries an add-on content id of Super Smash Bros. Ultimate (0100…6F0xx).").toString())
                    .setPositiveButton("OK", null).show();
            return;
        }
        TreeMap<Integer, String> have = SwitchFiles.readDlcList(GameSetup.dlcFile(game()));
        int fresh = 0;
        for (Integer k : rows.keySet()) if (!have.containsKey(k)) fresh++;
        StringBuilder list = new StringBuilder();
        int shown = 0;
        for (Map.Entry<Integer, String> e : rows.entrySet()) { if (shown++ < 12) list.append("• ").append(e.getValue()).append('\n'); }
        if (rows.size() > 12) list.append("… and ").append(rows.size() - 12).append(" more\n");
        new AlertDialog.Builder(this).setTitle(rows.size() + " add-on contents found")
                .setMessage(sb + list.toString() + "\n" + (fresh == 0 ? "All of them are unlocked already." : fresh + " of them are new."))
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Unlock", (d, w) -> {
                    try {
                        int n = GameSetup.addDlc(game(), rows);
                        status.setText(n + " add-on contents added (" + (have.size() + n) + " in all). They take effect at the next start of the game.");
                    } catch (IOException e) { status.setText("could not write " + GameSetup.dlcFile(game()) + ": " + e.getMessage()); }
                    refresh();
                }).show();
    }
    private void showDlc() {
        TreeMap<Integer, String> have = SwitchFiles.readDlcList(GameSetup.dlcFile(game()));
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<Integer, String> e : have.entrySet()) sb.append(String.format(Locale.ROOT, "%03X  %s\n", e.getKey(), e.getValue()));
        TextView t = u.mono();
        t.setTextColor(Ui.TEXT);
        t.setText(have.isEmpty() ? "none" : sb.toString());
        t.setPadding(u.dp(20), u.dp(12), u.dp(20), u.dp(12));
        ScrollView s = new ScrollView(this);
        s.addView(t);
        new AlertDialog.Builder(this).setTitle(have.size() + " add-on contents").setView(s).setPositiveButton("Close", null).show();
    }
    private void removeDlc() {
        File f = GameSetup.dlcFile(game());
        if (!f.isFile()) return;
        new AlertDialog.Builder(this).setTitle("Remove all add-on content?").setMessage("The game then starts without the DLC unlocked (your packages and saves are not touched).")
                .setNegativeButton("Cancel", null).setPositiveButton("Remove", (d, w) -> { f.delete(); refresh(); }).show();
    }

    /* ---------------------------------------------------------------- importing save data */
    /** The game's own process (GameActivity, android:process=":game") is running. */
    private boolean gameRunning() {
        List<ActivityManager.RunningAppProcessInfo> ps = ((ActivityManager) getSystemService(ACTIVITY_SERVICE)).getRunningAppProcesses();
        if (ps != null) for (ActivityManager.RunningAppProcessInfo p : ps) if ((getPackageName() + ":game").equals(p.processName)) return true;
        return false;
    }
    private void quitFirst() {
        new AlertDialog.Builder(this).setTitle("The game is running").setMessage("Quit the game first.").setPositiveButton("OK", null).show();
    }
    /** The save_data folder below dir: the folder holding system_data.bin (as JKSV / Checkpoint / yuzu / Ryujinx keep
     *  it, at any depth up to 6); of several (Ryujinx's 0\ and 1\) the one written last. Null: none. */
    private static File findSaveData(File dir, int depth) {
        File sys = new File(dir, "system_data.bin"), best = sys.isFile() && sys.length() > 0 ? dir : null;
        File[] fs = depth > 0 ? dir.listFiles() : null;
        if (fs != null) for (File f : fs) {
            if (!f.isDirectory()) continue;
            File d = findSaveData(f, depth - 1);
            if (d != null && (best == null || new File(d, "system_data.bin").lastModified() > new File(best, "system_data.bin").lastModified())) best = d;
        }
        return best;
    }
    private static void copyTree(File from, File to) throws IOException {
        if (!to.isDirectory() && !to.mkdirs()) throw new IOException("cannot create " + to);
        File[] fs = from.listFiles();
        if (fs == null) throw new IOException("cannot read " + from);
        byte[] buf = new byte[1 << 16];
        for (File f : fs) {
            File t = new File(to, f.getName());
            if (f.isDirectory()) { copyTree(f, t); continue; }
            try (InputStream in = new FileInputStream(f); OutputStream out = new FileOutputStream(t)) {
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
        }
    }
    private static void unzip(Uri zip, File to, Activity a) throws IOException {
        String root = to.getCanonicalPath() + File.separator;
        byte[] buf = new byte[1 << 16];
        try (ZipInputStream in = new ZipInputStream(a.getContentResolver().openInputStream(zip))) {
            ZipEntry e;
            while ((e = in.getNextEntry()) != null) {
                File t = new File(to, e.getName());
                if (!t.getCanonicalPath().startsWith(root)) continue;                /* no paths out of the folder */
                if (e.isDirectory()) { t.mkdirs(); continue; }
                t.getParentFile().mkdirs();
                try (OutputStream out = new FileOutputStream(t)) {
                    int n;
                    while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                }
            }
        }
    }
    /** Replaces <game>/saves/save with the save_data found in folder (or in the .zip): the old save is renamed to
     *  saves/save_backup_<date>, and the session files of earlier runs (save.work.<pid>, save.journal.<pid>, and
     *  save.commit, which the next mount would otherwise apply onto the imported save) are removed. */
    private void importSave(File folder, Uri zip) {
        if (!needFiles()) return;
        if (gameRunning()) { quitFirst(); return; }
        File saves = new File(game(), "saves");
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File cur = new File(saves, "save"), backup = new File(saves, "save_backup_" + stamp);
        new AlertDialog.Builder(this).setTitle("Import save data")
                .setMessage("This replaces the game's save data in " + cur + (cur.isDirectory() ? ".\n\nThe current save is kept as " + backup.getName() + "." : "."))
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Import", (d, w) -> {
                    saveInfo.setText("importing the save data…");
                    new Thread(() -> {
                        String msg;
                        File raw = new File(saves, "save.import.zip"), imp = new File(saves, "save.import");
                        try {
                            saves.mkdirs();
                            deleteTree(raw);
                            deleteTree(imp);
                            File src = folder;
                            if (zip != null) { unzip(zip, raw, this); src = raw; }
                            File sd = findSaveData(src, 6);
                            if (sd == null) throw new IOException("no save_data (a folder holding system_data.bin) in " + (zip != null ? "the .zip" : folder));
                            copyTree(sd, new File(imp, "save_data"));
                            File[] old = saves.listFiles();
                            if (old != null) for (File f : old) {
                                String n = f.getName();
                                if (n.startsWith("save.work.") || n.startsWith("save.journal.") || n.equals("save.commit")) deleteTree(f);
                            }
                            if (cur.exists() && !cur.renameTo(backup)) throw new IOException("cannot rename " + cur);
                            if (!imp.renameTo(cur)) throw new IOException("cannot rename " + imp);
                            msg = "save data imported from " + sd + (backup.exists() ? "\nthe old save: " + backup : "");
                        } catch (IOException e) {
                            deleteTree(imp);
                            msg = "import failed: " + e.getMessage();
                        }
                        deleteTree(raw);
                        if (zip != null) msg = msg.replace(raw.getPath(), "the .zip");
                        final String m = msg;
                        ui.post(() -> saveInfo.setText(m));
                    }).start();
                }).show();
    }

    File game() { return new File(folderPath.trim()); }
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

    /* ---------------------------------------------------------------- the state of the game folder */
    private boolean ready;
    private void refresh() {
        File g = game();
        checks.removeAllViews();
        boolean ok = true, files = allFiles();
        if (!files) {
            LinearLayout r = u.check("File access not allowed yet", 0, "Needed for the game folder on the shared storage");
            r.setOnClickListener(v -> askAllFiles());
            checks.addView(r);
            Button b = u.tonal("Allow access to files…", v -> askAllFiles());
            u.add(checks, b, 6);
            ok = false;
        }
        String ver = version(g);
        boolean state = new File(g, "state.json").isFile(), bin = new File(g, "binaries/" + ver + "/main.elf").isFile();
        boolean romfs = new File(g, "romfs/data.arc").isFile();
        checks.addView(u.check("Game data " + (state ? ver : ""), state && bin && romfs ? 1 : 0,
                state && bin && romfs ? "romfs/, binaries/" + ver + "/" : "not set up yet: \"Set up from Switch files\" below"
                        + (!romfs && (state || bin) ? " (the RomFS is missing)" : "")));
        ok &= state && bin && romfs;
        File dll = new File(g, "work/ssbu_game_eh_o1.dll");
        boolean elf = isAndroidElf(dll), copied = elf && libsCurrent();
        checks.addView(u.check("Game libraries", copied ? 1 : elf ? -1 : 0,
                copied ? "built for Android and copied into the app"
                        : elf ? "found in work/, not copied into the app yet (or changed): \"Copy\" below, or just press Play"
                        : dll.isFile() ? "work/ssbu_game_eh_o1.dll is the Windows build: build the Android one" : "not imported yet: \"Import libraries (.zip)\" below"));
        ok &= copied || elf;
        boolean fonts = new File(g, "work/fonts_open").isDirectory() || new File(g, "sysdata/fonts").isDirectory();
        if (!fonts && files && new File(g, "work").isDirectory()) { try { GameSetup.installFonts(this, g); fonts = true; } catch (IOException ignored) { } }
        if (!fonts) checks.addView(u.check("System fonts", -1, "installed with the setup"));
        libsNote.setText(elf ? "" : "The game libraries are the game's code, recompiled ahead of time from your dump. They are made on a PC once "
                + "(android/tools/build_game_android.py --package) and imported here as a .zip; everything else is set up on this device.");
        libsNote.setVisibility(elf ? View.GONE : View.VISIBLE);
        int dlc = SwitchFiles.readDlcList(GameSetup.dlcFile(g)).size();
        dlcInfo.setText(dlc == 0 ? "No add-on content unlocked" : dlc + " add-on contents unlocked");
        ready = ok;
        start.setEnabled(!busy);
        start.setAlpha(ok && !busy ? 1f : 0.55f);
        copy.setEnabled(!busy && files && elf);
        copy.setVisibility(elf && !copied ? View.VISIBLE : View.GONE);
        u.setPill(statePill, busy ? "Working…" : ok ? "Ready" : "Setup needed", busy ? Ui.WARN : ok ? Ui.OK : Ui.WARN);
        updatePrepHint();
    }

    /** The first start with a set of picture settings prepares every shader (nvn_vk_m4.c, marker prepared_v*_s<N>_*): said
     *  here, so the progress screen of ~15-25 minutes is not taken for a freeze. N = 2 for a scaled or wide picture (one set
     *  for both on Android, nvn_vk_draw.c WIDE_TR), 0 at 1080p without wide. */
    private void updatePrepHint() {
        if (prepHint == null || resolution == null || aspect == null) return;
        File g = game();
        java.util.Properties ini = graphicsIni(g);
        String sc = RES_SCALE[resolution.getSelectedItemPosition()];
        float scale = 1f;
        try { scale = Float.parseFloat(sc != null ? sc : ini.getProperty("resolution_scale", "1.0")); } catch (NumberFormatException ignored) { }
        int as = aspect.getSelectedItemPosition();
        boolean wide = as == 3 || (as == 0 && ini.getProperty("aspect", "keep").trim().equalsIgnoreCase("wide"));
        int set = scale != 1f || wide ? 2 : 0;
        File[] fs = new File(g, "cache/vk").listFiles();
        boolean done = false;
        if (fs != null) for (File f : fs) if (f.getName().startsWith("prepared_v") && f.getName().contains("_s" + set + "_")) done = true;
        prepHint.setText(done ? "" : "◐ The first start with this resolution / shape prepares the shaders: about 15–25 minutes, with a progress screen. Later starts are quick.");
        prepHint.setVisibility(done ? View.GONE : View.VISIBLE);
    }
    private static java.util.Properties graphicsIni(File g) {
        java.util.Properties p = new java.util.Properties();
        try (InputStream in = new FileInputStream(new File(g, "config/graphics.ini"))) { p.load(in); } catch (IOException ignored) { }
        return p;
    }

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
        if (!begin("Copying the game libraries into the app…")) return;
        File work = new File(game(), "work");
        new Thread(() -> {
            String msg;
            try {
                List<String> libs = new ArrayList<>();
                findLibs(work, "", libs);
                long total = 0, done = 0;
                for (String r : libs) total += new File(work, r).length();
                long lastUi = 0;
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
                            long now = System.currentTimeMillis();
                            if (now - lastUi < 150) continue;
                            lastUi = now;
                            final int pm = (int) (total > 0 ? done * 1000 / total : 0);
                            final String now_ = r;
                            final long d = done, t = total;
                            ui.post(() -> { progress.setIndeterminate(false); progress.setProgress(pm); status.setText(String.format(Locale.ROOT, "Copying %s  ·  %d / %d MB", now_, d >> 20, t >> 20)); });
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
                end(m);
                if (thenStart && ready) startGame();
            });
        }).start();
    }

    private void startGame() {
        save();
        refresh();
        if (busy) return;
        if (!ready) {
            new AlertDialog.Builder(this).setTitle("Not ready yet").setMessage(notReadyText()).setPositiveButton("OK", null).show();
            return;
        }
        if (!libsCurrent()) { copyLibraries(true); return; }
        ArrayList<String> env = new ArrayList<>();
        File driver = driverLib();
        if (driver != null) env.add("SSBU_VK_DRIVER=" + driver.getAbsolutePath());     /* first: an extra setting can override it */
        String scale = RES_SCALE[resolution.getSelectedItemPosition()];
        if (scale != null) env.add("SSBU_VK_SCALE=" + scale);                       /* before the extra settings: one of them can override it */
        int as = aspect.getSelectedItemPosition();
        if (as > 0) env.add("SSBU_ASPECT=" + (as == 3 ? "wide" : as == 2 ? "stretch" : "keep"));
        if (fps.isChecked()) env.add("SSBU_FPS=1");
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
        status.setText(pad != null ? "Controller: " + pad : "");
        i.putStringArrayListExtra("env", env);
        startActivity(i);
    }
    private String notReadyText() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < checks.getChildCount(); i++) {
            View v = checks.getChildAt(i);
            if (!(v instanceof LinearLayout) || ((LinearLayout) v).getChildCount() < 2) continue;
            LinearLayout r = (LinearLayout) v;
            String mark = ((TextView) r.getChildAt(0)).getText().toString();
            if (mark.equals("●")) continue;
            LinearLayout c = (LinearLayout) r.getChildAt(1);
            sb.append("• ").append(((TextView) c.getChildAt(0)).getText());
            if (c.getChildCount() > 1) sb.append(": ").append(((TextView) c.getChildAt(1)).getText());
            sb.append('\n');
        }
        return sb.length() == 0 ? "The game folder is not complete." : sb.toString().trim();
    }

    private void showLog() {
        File f = new File(game(), "work/ssbu_android_log.txt");
        String text;
        try {
            long n = f.length();
            text = readTail(f, 64 * 1024);
            if (n > 64 * 1024) text = "… (last 64 KB of " + (n >> 10) + " KB)\n" + text;
        } catch (IOException e) { text = "no log yet (" + f + ")"; }
        TextView t = u.mono();
        t.setTextColor(Ui.TEXT);
        t.setTextSize(10);
        t.setText(text);
        t.setPadding(u.dp(8), u.dp(8), u.dp(8), u.dp(8));
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
