package me.hapke.opensmash;

import android.Manifest;
import android.app.Activity;
import android.app.Dialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.hardware.input.InputManager;
import android.os.Build;
import android.util.SparseArray;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.animation.OvershootInterpolator;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * The controller panel: a pop-up over the launcher and over the game. Both volume buttons pressed together open it, and
 * the same pair closes it. It lists the controllers that are connected, and a press on a controller makes its entry
 * bounce. The two Switch pads are connected from here, without the Bluetooth settings: Procon 2 (Switch 2 Pro) is
 * started through the Procon2 Bridge app (Bluetooth LE, its own service), Procon 1 (Switch 1 Pro, Bluetooth Classic) is
 * searched for and paired here.
 */
final class ControllerPanel {
    /* the uinput devices the Procon2 bridge makes (Procon2Droid): 2069 = Switch 2 Pro, 2098 = the Switch 1 Pro proxy
     * (rumble_pro1), 2009 = the Switch 1 Pro as the kernel has it (grabbed by the proxy, but still listed) */
    static final int NINTENDO = 0x057e, PRO2 = 0x2069, PRO1_PROXY = 0x2098, PRO1_STOCK = 0x2009;
    static final String BRIDGE_PKG = "com.procon2droid.gatt", BRIDGE_SERVICE = BRIDGE_PKG + ".BridgeService";
    static final String BRIDGE_START = BRIDGE_PKG + ".START", BRIDGE_STOP = BRIDGE_PKG + ".STOP";
    /* the name the Switch 1 Pro gives itself over Bluetooth Classic */
    static final String PRO1_NAME = "Pro Controller";
    static final int REQ_BT = 9;

    private static boolean volUp, volDown, chord;
    private static Panel shown;

    private ControllerPanel() {}

    /** Activity.dispatchKeyEvent: true = the key was used. Both volume keys pressed together open or close the panel. */
    static boolean onKey(Activity a, KeyEvent ev) {
        int k = ev.getKeyCode();
        if (k != KeyEvent.KEYCODE_VOLUME_UP && k != KeyEvent.KEYCODE_VOLUME_DOWN) return false;
        if (ev.getAction() != KeyEvent.ACTION_DOWN && ev.getAction() != KeyEvent.ACTION_UP) return false;
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN;
        if (k == KeyEvent.KEYCODE_VOLUME_UP) volUp = down;
        else volDown = down;
        if (!volUp && !volDown) chord = false;
        else if (volUp && volDown && !chord) {
            chord = true;
            if (shown != null) shown.dismiss();
            else {
                shown = new Panel(a);
                shown.show();
            }
        }
        return chord;
    }

    /** The activity is going away: so does the panel. */
    static void close() {
        if (shown != null) shown.dismiss();
        volUp = volDown = chord = false;
    }

    /** A short squash, then an overshoot back to full size. */
    static void bounce(View v) {
        v.animate().cancel();
        v.animate().scaleX(0.93f).scaleY(0.93f).setDuration(70).withEndAction(() ->
                v.animate().scaleX(1f).scaleY(1f).setDuration(340).setInterpolator(new OvershootInterpolator(3f)).start()).start();
    }

    static boolean isPad(InputDevice d) {
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static final class Panel extends Dialog {
        private final Activity owner;
        private final Ui ui;
        private final InputManager im;
        private final BluetoothAdapter bt;
        private final SparseArray<View> bounceByDevice = new SparseArray<>();
        private final SparseIntArray held = new SparseIntArray();
        private final Set<String> found = new HashSet<>();
        private final InputManager.InputDeviceListener devices;
        private final BroadcastReceiver radio;
        private LinearLayout procon2Card, procon1Card, otherList, nearby;
        private TextView procon2State, procon2Hint, procon1State, procon1Hint;

        Panel(Activity a) {
            super(a);
            owner = a;
            ui = new Ui(a);
            im = (InputManager) a.getSystemService(Context.INPUT_SERVICE);
            BluetoothManager bm = (BluetoothManager) a.getSystemService(Context.BLUETOOTH_SERVICE);
            bt = bm == null ? null : bm.getAdapter();
            requestWindowFeature(Window.FEATURE_NO_TITLE);
            setContentView(sheet());
            getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            getWindow().setLayout(Math.min(a.getResources().getDisplayMetrics().widthPixels * 9 / 10, ui.dp(540)), ViewGroup.LayoutParams.WRAP_CONTENT);
            setCanceledOnTouchOutside(true);
            setOnDismissListener(d -> {
                if (shown == this) shown = null;
                leave();
            });

            devices = new InputManager.InputDeviceListener() {
                @Override public void onInputDeviceAdded(int id) { refresh(); }
                @Override public void onInputDeviceRemoved(int id) { refresh(); }
                @Override public void onInputDeviceChanged(int id) { refresh(); }
            };
            im.registerInputDeviceListener(devices, null);

            radio = new BroadcastReceiver() {
                @Override public void onReceive(Context c, Intent i) {
                    String act = i.getAction();
                    if (BluetoothDevice.ACTION_FOUND.equals(act)) {
                        BluetoothDevice d = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                        String name = i.getStringExtra(BluetoothDevice.EXTRA_NAME);
                        if (d != null && name != null && name.startsWith(PRO1_NAME) && d.getType() != BluetoothDevice.DEVICE_TYPE_LE) addFound(d, name);
                    } else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(act)) {
                        procon1Hint.setText(nearby.getChildCount() == 0
                                ? "Nothing found. Hold the sync button on the Switch 1 Pro, then search again."
                                : "Tap the controller to pair it.");
                    } else if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(act)) {
                        if (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE) == BluetoothDevice.BOND_BONDED)
                            procon1Hint.setText("Paired. Press a button on the controller to connect it.");
                        refresh();
                    }
                }
            };
            IntentFilter f = new IntentFilter();
            f.addAction(BluetoothDevice.ACTION_FOUND);
            f.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
            f.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
            if (Build.VERSION.SDK_INT >= 33) a.registerReceiver(radio, f, Context.RECEIVER_NOT_EXPORTED);
            else a.registerReceiver(radio, f);

            refresh();
        }

        private View sheet() {
            LinearLayout page = ui.column();
            page.setPadding(ui.dp(18), ui.dp(18), ui.dp(18), ui.dp(18));
            page.setBackground(Ui.round(Ui.BG, ui.dp(26)));
            TextView title = ui.text("Controllers", 20, Ui.TEXT);
            title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            page.addView(title);
            page.addView(ui.text("Press both volume buttons again to close. A press on a controller makes its entry bounce.", 13, Ui.TEXT2));

            procon2Card = ui.card(page, "Procon 2 · Switch 2 Pro", "Bluetooth LE, through the Procon2 Bridge app");
            procon2State = ui.pill("", Ui.TEXT2);
            procon2Card.addView(procon2State);
            procon2Hint = ui.text("", 13, Ui.TEXT2);
            ui.add(procon2Card, procon2Hint, 6);
            ui.add(procon2Card, ui.pair(ui.filled("Start", v -> bridge(true)), ui.tonal("Stop", v -> bridge(false))), 12);

            procon1Card = ui.card(page, "Procon 1 · Switch 1 Pro", "Bluetooth Classic: searched for and paired here");
            procon1State = ui.pill("", Ui.TEXT2);
            procon1Card.addView(procon1State);
            procon1Hint = ui.text("Hold the sync button on the controller until its lights flash, then search.", 13, Ui.TEXT2);
            ui.add(procon1Card, procon1Hint, 6);
            ui.add(procon1Card, ui.filled("Search", v -> search()), 12);
            nearby = ui.column();
            ui.add(procon1Card, nearby, 6);

            LinearLayout others = ui.card(page, "Other controllers", "Any other game controller connected to this device");
            otherList = ui.column();
            others.addView(otherList);

            ScrollView scroll = new ScrollView(owner);
            scroll.addView(page);
            return scroll;
        }

        /** The connected controllers, and the state of the two Switch pads (on every change of the input devices). */
        private void refresh() {
            bounceByDevice.clear();
            otherList.removeAllViews();
            String pro2 = null, pro1 = null;
            for (int id : InputDevice.getDeviceIds()) {
                InputDevice d = InputDevice.getDevice(id);
                if (d == null || d.isVirtual() || !isPad(d)) continue;
                boolean nintendo = d.getVendorId() == NINTENDO;
                if (nintendo && d.getProductId() == PRO2) {
                    bounceByDevice.put(id, procon2Card);
                    pro2 = d.getName();
                } else if (nintendo && (d.getProductId() == PRO1_PROXY || d.getProductId() == PRO1_STOCK)) {
                    bounceByDevice.put(id, procon1Card);
                    pro1 = d.getName();
                } else {
                    View row = padRow(d);
                    bounceByDevice.put(id, row);
                    ui.add(otherList, row, 8);
                }
            }
            if (otherList.getChildCount() == 0) otherList.addView(ui.text("None", 13, Ui.TEXT2));

            if (pro2 != null) ui.setPill(procon2State, "Connected", Ui.OK);
            else ui.setPill(procon2State, "Not connected", Ui.TEXT2);
            procon2Hint.setText(pro2 != null ? pro2 : "Tap Start, then press a button on the controller.");

            if (pro1 != null) ui.setPill(procon1State, "Connected", Ui.OK);
            else if (pairedProcon1()) ui.setPill(procon1State, "Paired, not connected", Ui.WARN);
            else ui.setPill(procon1State, "Not paired", Ui.TEXT2);
        }

        private View padRow(InputDevice d) {
            LinearLayout r = ui.row();
            r.setBackground(Ui.round(Ui.CARD_HI, ui.dp(14)));
            r.setPadding(ui.dp(14), ui.dp(12), ui.dp(14), ui.dp(12));
            LinearLayout col = ui.column();
            col.addView(ui.text(d.getName(), 15, Ui.TEXT));
            col.addView(ui.text(String.format(Locale.ROOT, "%04x:%04x · connected", d.getVendorId(), d.getProductId()), 12, Ui.TEXT2));
            r.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            return r;
        }

        /* a press: a key of a pad, or a stick-less pad input (d-pad hat, triggers) that was not held before */
        @Override public boolean dispatchKeyEvent(KeyEvent ev) {
            if (onKey(owner, ev)) return true;
            if (ev.getAction() == KeyEvent.ACTION_DOWN && ev.getRepeatCount() == 0 && fromPad(ev.getSource())) pressed(ev.getDeviceId());
            return super.dispatchKeyEvent(ev);
        }

        @Override public boolean dispatchGenericMotionEvent(MotionEvent ev) {
            if ((ev.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                float hx = ev.getAxisValue(MotionEvent.AXIS_HAT_X), hy = ev.getAxisValue(MotionEvent.AXIS_HAT_Y);
                int m = 0;
                if (hx < -0.5f) m |= 1;
                if (hx > 0.5f) m |= 2;
                if (hy < -0.5f) m |= 4;
                if (hy > 0.5f) m |= 8;
                if (ev.getAxisValue(MotionEvent.AXIS_LTRIGGER) > 0.5f) m |= 16;
                if (ev.getAxisValue(MotionEvent.AXIS_RTRIGGER) > 0.5f) m |= 32;
                int id = ev.getDeviceId();
                int fresh = m & ~held.get(id);
                held.put(id, m);
                if (fresh != 0) pressed(id);
            }
            return super.dispatchGenericMotionEvent(ev);
        }

        private static boolean fromPad(int src) {
            return (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                    || (src & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
        }

        private void pressed(int device) {
            View v = bounceByDevice.get(device);
            if (v != null) bounce(v);
        }

        /* Procon 2: the bridge app runs its own service (BLE to the Switch 2 Pro); null = the app is not installed */
        private void bridge(boolean on) {
            Intent i = new Intent(on ? BRIDGE_START : BRIDGE_STOP).setComponent(new ComponentName(BRIDGE_PKG, BRIDGE_SERVICE));
            ComponentName started;
            try {
                started = owner.startForegroundService(i);
            } catch (RuntimeException e) {
                started = null;
            }
            procon2Hint.setText(started == null ? "The Procon2 Bridge app is not installed."
                    : on ? "Starting. Press a button on the controller." : "Bridge stopped.");
        }

        /* Procon 1: Bluetooth Classic discovery; the sync button puts the Switch 1 Pro into pairing mode */
        private void search() {
            String[] need = missingPermissions();
            if (need.length > 0) {
                owner.requestPermissions(need, REQ_BT);
                procon1Hint.setText("Allow Bluetooth for the panel, then tap Search again.");
                return;
            }
            if (bt == null) {
                procon1Hint.setText("This device has no Bluetooth.");
                return;
            }
            if (!bt.isEnabled()) {
                owner.startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
                procon1Hint.setText("Turn Bluetooth on, then tap Search again.");
                return;
            }
            found.clear();
            nearby.removeAllViews();
            stopSearch();
            boolean go = bt.startDiscovery();
            procon1Hint.setText(go ? "Searching. Hold the sync button on the Switch 1 Pro until its lights flash."
                    : "The search could not start. Try again.");
        }

        private void addFound(BluetoothDevice d, String name) {
            if (!found.add(d.getAddress())) return;
            Button b = ui.tonal("Pair " + name + " · " + d.getAddress(), v -> pair(d));
            ui.add(nearby, b, 8);
        }

        private void pair(BluetoothDevice d) {
            stopSearch();
            try {
                procon1Hint.setText(d.createBond() ? "Pairing. Accept on the phone if it asks." : "Pairing could not start.");
            } catch (SecurityException e) {
                procon1Hint.setText("Allow Bluetooth for the panel, then try again.");
            }
        }

        private boolean pairedProcon1() {
            if (bt == null) return false;
            try {
                for (BluetoothDevice d : bt.getBondedDevices()) {
                    String n = d.getName();
                    if (n != null && n.startsWith(PRO1_NAME) && d.getType() != BluetoothDevice.DEVICE_TYPE_LE) return true;
                }
            } catch (SecurityException ignored) {
            }
            return false;
        }

        private String[] missingPermissions() {
            String[] want = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                    ? new String[] {Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT}
                    : new String[] {Manifest.permission.ACCESS_FINE_LOCATION};
            ArrayList<String> out = new ArrayList<>();
            for (String p : want) if (owner.checkSelfPermission(p) != android.content.pm.PackageManager.PERMISSION_GRANTED) out.add(p);
            return out.toArray(new String[0]);
        }

        private void stopSearch() {
            if (bt == null) return;
            try {
                bt.cancelDiscovery();
            } catch (SecurityException ignored) {
            }
        }

        private void leave() {
            im.unregisterInputDeviceListener(devices);
            try {
                owner.unregisterReceiver(radio);
            } catch (IllegalArgumentException ignored) {
            }
            stopSearch();
        }
    }
}
