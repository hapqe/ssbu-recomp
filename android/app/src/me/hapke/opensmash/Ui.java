package me.hapke.opensmash;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

/** The launcher's look, built in code (the APK is made without Gradle or support libraries): a dark page of rounded
 *  cards, filled / tonal / text buttons, status rows with a coloured mark, small pills. */
final class Ui {
    static final int BG = 0xFF0E1014, CARD = 0xFF181B22, CARD_HI = 0xFF20242D, LINE = 0xFF2A2F3A;
    static final int TEXT = 0xFFF1F3F7, TEXT2 = 0xFF9AA2B1, ACCENT = 0xFFFF4D6A, ACCENT_DARK = 0xFFD93355, ACCENT_SOFT = 0x33FF4D6A;
    static final int OK = 0xFF3DDC97, WARN = 0xFFFFB547, BAD = 0xFFFF5C5C;

    private final Context c;
    Ui(Context c) { this.c = c; }

    int dp(float v) { return (int) (v * c.getResources().getDisplayMetrics().density + 0.5f); }

    static GradientDrawable round(int color, float radiusPx) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(radiusPx);
        return g;
    }
    private RippleDrawable ripple(int color, float radius) {
        return new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), round(color, radius), round(Color.WHITE, radius));
    }

    LinearLayout column() { LinearLayout l = new LinearLayout(c); l.setOrientation(LinearLayout.VERTICAL); return l; }
    LinearLayout row() { LinearLayout l = new LinearLayout(c); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }

    /** A card: a rounded panel with a title (and an optional line under it); returns the column to fill. */
    LinearLayout card(LinearLayout page, String title, String subtitle) {
        LinearLayout card = column();
        card.setBackground(round(CARD, dp(20)));
        card.setPadding(dp(18), dp(16), dp(18), dp(16));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(14);
        page.addView(card, lp);
        if (title != null) {
            TextView t = text(title, 17, TEXT);
            t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            card.addView(t);
        }
        if (subtitle != null) {
            TextView s = text(subtitle, 13, TEXT2);
            s.setPadding(0, dp(2), 0, dp(4));
            card.addView(s);
        }
        return card;
    }

    TextView text(String s, float sp, int color) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setLineSpacing(0, 1.12f);
        return t;
    }
    TextView label(String s) {
        TextView t = text(s.toUpperCase(java.util.Locale.ROOT), 11, TEXT2);
        t.setLetterSpacing(0.08f);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        t.setPadding(0, dp(14), 0, dp(6));
        return t;
    }
    TextView mono() {
        TextView t = text("", 11.5f, TEXT2);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextIsSelectable(true);
        return t;
    }

    /** filled (the accent), tonal (a lighter panel) or plain text */
    Button filled(String s, View.OnClickListener l) { return button(s, l, ACCENT, Color.WHITE, 52, 16); }
    Button tonal(String s, View.OnClickListener l) { return button(s, l, CARD_HI, TEXT, 46, 14); }
    Button plain(String s, View.OnClickListener l) { Button b = button(s, l, 0x00000000, ACCENT, 40, 14); b.setPadding(dp(8), 0, dp(8), 0); return b; }
    private Button button(String s, View.OnClickListener l, int bg, int fg, int heightDp, float sp) {
        Button b = new Button(c);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(sp);
        b.setTextColor(fg);
        b.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        b.setStateListAnimator(null);
        b.setBackground(ripple(bg, dp(heightDp / 2f)));
        b.setMinHeight(dp(heightDp));
        b.setMinimumHeight(dp(heightDp));
        b.setPadding(dp(18), 0, dp(18), 0);
        b.setOnClickListener(l);
        b.setSingleLine(false);
        b.setEllipsize(TextUtils.TruncateAt.END);
        return b;
    }
    /** add a view to a column with a top gap, full width */
    View add(LinearLayout col, View v, int gapDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(gapDp);
        col.addView(v, lp);
        return v;
    }
    /** two buttons side by side */
    LinearLayout pair(View a, View b) {
        LinearLayout r = row();
        LinearLayout.LayoutParams la = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1), lb = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        lb.leftMargin = dp(10);
        r.addView(a, la);
        r.addView(b, lb);
        return r;
    }

    /** A pill: a small rounded label (the state at the top). */
    TextView pill(String s, int color) {
        TextView t = text(s, 12.5f, color);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        t.setPadding(dp(12), dp(5), dp(12), dp(5));
        setPill(t, s, color);
        return t;
    }
    void setPill(TextView t, String s, int color) {
        t.setText(s);
        t.setTextColor(color);
        GradientDrawable g = round((color & 0x00FFFFFF) | 0x26000000, dp(14));
        t.setBackground(g);
    }

    /** One line of a checklist: a coloured mark and the text. */
    LinearLayout check(String what, int state /* 1 ok, 0 missing, -1 warning */, String detail) {
        LinearLayout r = row();
        r.setGravity(Gravity.TOP);
        r.setPadding(0, dp(5), 0, dp(5));
        TextView mark = text(state > 0 ? "●" : state == 0 ? "○" : "◐", 13, state > 0 ? OK : state == 0 ? BAD : WARN);
        mark.setPadding(0, dp(1), dp(10), 0);
        r.addView(mark);
        LinearLayout col = column();
        col.addView(text(what, 14, TEXT));
        if (detail != null && !detail.isEmpty()) col.addView(text(detail, 12, TEXT2));
        r.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        return r;
    }

    Spinner spinner(String[] items, int sel) {
        Spinner sp = new Spinner(c, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> a = new ArrayAdapter<String>(c, android.R.layout.simple_spinner_item, items) {
            @Override public View getView(int pos, View v, ViewGroup parent) { TextView t = (TextView) super.getView(pos, v, parent); t.setTextColor(TEXT); t.setTextSize(14); return t; }
            @Override public View getDropDownView(int pos, View v, ViewGroup parent) { TextView t = (TextView) super.getDropDownView(pos, v, parent); t.setTextColor(TEXT); t.setTextSize(14); t.setPadding(dp(16), dp(12), dp(16), dp(12)); return t; }
        };
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(a);
        sp.setSelection(sel >= 0 && sel < items.length ? sel : 0);
        sp.setBackground(round(CARD_HI, dp(14)));
        sp.setPopupBackgroundDrawable(round(CARD_HI, dp(14)));
        sp.setPadding(dp(6), dp(4), dp(6), dp(4));
        sp.setMinimumHeight(dp(46));
        return sp;
    }
}
