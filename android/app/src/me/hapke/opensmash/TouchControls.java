package me.hapke.opensmash;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

/**
 * On-screen controls drawn over the game: a floating left stick, a right stick (smash attacks), the four face buttons in
 * the Switch layout (A right, B bottom, X top, Y left), L / R / ZL / ZR, + / - and a d-pad. The state goes to a virtual SDL
 * gamepad (GameActivity.nativeTouchPad -> android_main.c), which nnpad_sdl.c sees like any other pad. The "◐" button in
 * the top middle hides them (a physical pad works with or without them).
 */
public class TouchControls extends View {
    /* bits = SDL_GamepadButton numbers; 16 / 17 = the triggers (axes) */
    static final int SOUTH = 0, EAST = 1, WEST = 2, NORTH = 3, BACK = 4, START = 6, LSTICK = 7, RSTICK = 8, LSHOULDER = 9, RSHOULDER = 10,
            DUP = 11, DDOWN = 12, DLEFT = 13, DRIGHT = 14, LTRIG = 16, RTRIG = 17;

    private static final class Btn { String label; int bit; float x, y, r; Btn(String l, int b) { label = l; bit = b; } }
    private final Btn[] btns = {
        new Btn("A", EAST), new Btn("B", SOUTH), new Btn("X", NORTH), new Btn("Y", WEST),
        new Btn("L", LSHOULDER), new Btn("R", RSHOULDER), new Btn("ZL", LTRIG), new Btn("ZR", RTRIG),
        new Btn("−", BACK), new Btn("+", START),
        new Btn("▲", DUP), new Btn("▼", DDOWN), new Btn("◀", DLEFT), new Btn("▶", DRIGHT),
    };
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float d;
    private boolean hidden;
    private float toggleX, toggleY, toggleR;
    /* sticks: pointer id, centre, knob */
    private int lId = -1, rId = -1;
    private float lcx, lcy, lx, ly, rcx, rcy, rx, ry, stickR;
    private final int[] owner = new int[24];        /* pointer id holding a button bit, -1 none */
    private int sent = -1;
    private float sLx, sLy, sRx, sRy;

    public TouchControls(Context c) {
        super(c);
        d = c.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2 * d);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        java.util.Arrays.fill(owner, -1);
        setFocusable(false);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float r = 30 * d, gap = 62 * d;
        float cx = w - 120 * d, cy = h - 120 * d;
        place(0, cx + gap, cy, r); place(1, cx, cy + gap, r); place(2, cx, cy - gap, r); place(3, cx - gap, cy, r);
        place(4, 70 * d, 40 * d, 30 * d); place(5, w - 70 * d, 40 * d, 30 * d);
        place(6, 70 * d, 110 * d, 30 * d); place(7, w - 70 * d, 110 * d, 30 * d);
        place(8, w / 2f - 60 * d, 34 * d, 22 * d); place(9, w / 2f + 60 * d, 34 * d, 22 * d);
        float px = 190 * d, py = 120 * d, dg = 36 * d;
        place(10, px, py - dg, 20 * d); place(11, px, py + dg, 20 * d); place(12, px - dg, py, 20 * d); place(13, px + dg, py, 20 * d);
        toggleX = w / 2f; toggleY = 34 * d; toggleR = 20 * d;
        stickR = 60 * d;
        lcx = lx = 150 * d; lcy = ly = h - 130 * d;
        rcx = rx = w - 300 * d; rcy = ry = h - 70 * d;
        text.setTextSize(18 * d);
    }
    private void place(int i, float x, float y, float r) { btns[i].x = x; btns[i].y = y; btns[i].r = r; }

    private int buttons() {
        int b = 0;
        for (int i = 0; i < owner.length; i++) if (owner[i] != -1) b |= 1 << i;
        return b;
    }
    private void send() {
        int b = buttons();
        float a = lId == -1 ? 0 : clamp((lx - lcx) / stickR), bb = lId == -1 ? 0 : clamp((ly - lcy) / stickR);
        float c = rId == -1 ? 0 : clamp((rx - rcx) / (stickR * 0.7f)), e = rId == -1 ? 0 : clamp((ry - rcy) / (stickR * 0.7f));
        if (b == sent && a == sLx && bb == sLy && c == sRx && e == sRy) return;
        sent = b; sLx = a; sLy = bb; sRx = c; sRy = e;
        try { GameActivity.nativeTouchPad(b, a, bb, c, e); } catch (UnsatisfiedLinkError ignored) { }
    }
    private static float clamp(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        if ((ev.getSource() & InputDevice.SOURCE_TOUCHSCREEN) == 0) return false;
        int act = ev.getActionMasked(), idx = ev.getActionIndex(), id = ev.getPointerId(idx);
        float x = ev.getX(idx), y = ev.getY(idx);
        switch (act) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
            if (dist(x, y, toggleX, toggleY) < toggleR * 1.5f) { hidden = !hidden; release(-2); invalidate(); return true; }
            if (hidden) return false;
            press(id, x, y);
            break;
        case MotionEvent.ACTION_MOVE:
            if (hidden) return false;
            for (int i = 0; i < ev.getPointerCount(); i++) {
                int pid = ev.getPointerId(i);
                float px = ev.getX(i), py = ev.getY(i);
                if (pid == lId) { lx = px; ly = py; }
                else if (pid == rId) { rx = px; ry = py; }
                else {                                         /* sliding between face buttons */
                    for (int b = 0; b < owner.length; b++) if (owner[b] == pid) owner[b] = -1;
                    Btn h = hit(px, py);
                    if (h != null) owner[h.bit] = pid;
                }
            }
            break;
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
        case MotionEvent.ACTION_CANCEL:
            release(act == MotionEvent.ACTION_CANCEL ? -2 : id);
            break;
        default:
            return true;
        }
        send();
        invalidate();
        return true;
    }
    private void press(int id, float x, float y) {
        Btn b = hit(x, y);
        if (b != null) { owner[b.bit] = id; return; }
        int w = getWidth(), h = getHeight();
        if (x < w * 0.42f && y > h * 0.35f && lId == -1) { lId = id; lcx = lx = x; lcy = ly = y; }       /* floating left stick */
        else if (x > w * 0.42f && dist(x, y, rcx, rcy) < stickR * 1.3f && rId == -1) { rId = id; rx = x; ry = y; }
    }
    private void release(int id) {
        for (int b = 0; b < owner.length; b++) if (owner[b] == id || id == -2) owner[b] = -1;
        if (lId == id || id == -2) { lId = -1; lx = lcx; ly = lcy; }
        if (rId == id || id == -2) { rId = -1; rx = rcx; ry = rcy; }
        send();
    }
    private Btn hit(float x, float y) {
        for (Btn b : btns) if (dist(x, y, b.x, b.y) < b.r * 1.25f) return b;
        return null;
    }
    private static float dist(float a, float b, float c, float e) { float x = a - c, y = b - e; return (float) Math.sqrt(x * x + y * y); }

    @Override
    protected void onDraw(Canvas c) {
        fill.setColor(Color.argb(hidden ? 60 : 110, 255, 255, 255));
        text.setColor(Color.argb(hidden ? 120 : 220, 0, 0, 0));
        c.drawCircle(toggleX, toggleY, toggleR, fill);
        c.drawText("◐", toggleX, toggleY + text.getTextSize() / 3, text);
        if (hidden) return;
        int b = buttons();
        for (Btn k : btns) {
            boolean on = (b >> k.bit & 1) != 0;
            fill.setColor(on ? Color.argb(170, 255, 200, 60) : Color.argb(80, 255, 255, 255));
            line.setColor(Color.argb(150, 255, 255, 255));
            c.drawCircle(k.x, k.y, k.r, fill);
            c.drawCircle(k.x, k.y, k.r, line);
            text.setColor(Color.argb(230, 255, 255, 255));
            c.drawText(k.label, k.x, k.y + text.getTextSize() / 3, text);
        }
        line.setColor(Color.argb(120, 255, 255, 255));
        fill.setColor(Color.argb(60, 255, 255, 255));
        c.drawCircle(lcx, lcy, stickR, fill);
        c.drawCircle(lcx, lcy, stickR, line);
        fill.setColor(Color.argb(140, 255, 255, 255));
        float kx = lcx + clamp((lx - lcx) / stickR) * stickR, ky = lcy + clamp((ly - lcy) / stickR) * stickR;
        c.drawCircle(kx, ky, stickR * 0.45f, fill);
        fill.setColor(Color.argb(50, 255, 255, 255));
        c.drawCircle(rcx, rcy, stickR * 0.7f, fill);
        c.drawCircle(rcx, rcy, stickR * 0.7f, line);
        fill.setColor(Color.argb(120, 255, 255, 255));
        c.drawCircle(rcx + clamp((rx - rcx) / (stickR * 0.7f)) * stickR * 0.7f, rcy + clamp((ry - rcy) / (stickR * 0.7f)) * stickR * 0.7f, stickR * 0.3f, fill);
        text.setColor(Color.argb(200, 255, 255, 255));
        c.drawText("C", rcx, rcy + text.getTextSize() / 3, text);
    }
}
