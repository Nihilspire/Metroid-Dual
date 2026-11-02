package com.jack.metroiddual;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/** On-screen NES pad, drawn in the game's colors. Multi-touch; the D-pad allows diagonals and sliding. */
final class TouchPadView extends View {
    interface Listener { void onMenu(); }
    volatile int bits;
    int opacity = 70;
    RomGfx gfx;
    Listener listener;

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float dcx, dcy, dr, bR, aX, aY, bX, bY, selX, stX, ssY, ssW, ssH, mX, mY, mR;
    private final int[] ptrBits = new int[16];

    TouchPadView(Context c) { super(c); }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        float u = Math.min(w, h) / 100f;
        if (w < h) u = w / 72f;
        dr = 17 * u;
        dcx = dr + 6 * u; dcy = h - dr - 8 * u;
        bR = 8.5f * u;
        aX = w - bR - 5 * u; aY = h - bR - 16 * u;
        bX = aX - 2.4f * bR; bY = aY + 9 * u;
        ssW = 15 * u; ssH = 5 * u; ssY = h - ssH - 4 * u;
        selX = w / 2f - ssW - 3 * u; stX = w / 2f + 3 * u;
        mR = 5 * u; mX = mR + 4 * u; mY = mR + 4 * u;
    }

    @Override protected void onDraw(Canvas c) {
        int a = opacity * 255 / 100;
        p.setStyle(Paint.Style.FILL);
        // D-pad: a cross like the NES pad
        float arm = dr * 0.36f;
        p.setColor(0x24188C | (a * 3 / 4 << 24));
        c.drawRoundRect(new RectF(dcx - arm, dcy - dr, dcx + arm, dcy + dr), arm / 3, arm / 3, p);
        c.drawRoundRect(new RectF(dcx - dr, dcy - arm, dcx + dr, dcy + arm), arm / 3, arm / 3, p);
        int on = bits;
        p.setColor(0x3CBCFC | (a << 24));
        if ((on & Nes.UP) != 0) c.drawRect(dcx - arm, dcy - dr, dcx + arm, dcy - arm, p);
        if ((on & Nes.DOWN) != 0) c.drawRect(dcx - arm, dcy + arm, dcx + arm, dcy + dr, p);
        if ((on & Nes.LEFT) != 0) c.drawRect(dcx - dr, dcy - arm, dcx - arm, dcy + arm, p);
        if ((on & Nes.RIGHT) != 0) c.drawRect(dcx + arm, dcy - arm, dcx + dr, dcy + arm, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(Math.max(2, dr / 20));
        p.setColor(0x3CBCFC | (a << 24));
        c.drawRoundRect(new RectF(dcx - arm, dcy - dr, dcx + arm, dcy + dr), arm / 3, arm / 3, p);
        c.drawRoundRect(new RectF(dcx - dr, dcy - arm, dcx + dr, dcy + arm), arm / 3, arm / 3, p);
        button(c, bX, bY, bR, "B", (on & Nes.B) != 0, a);
        button(c, aX, aY, bR, "A", (on & Nes.A) != 0, a);
        pill(c, selX, ssY, "SELECT", (on & Nes.SELECT) != 0, a);
        pill(c, stX, ssY, "START", (on & Nes.START) != 0, a);
        // Menu: three bars, like a pause icon
        p.setStyle(Paint.Style.FILL);
        p.setColor(0x000000 | (a / 2 << 24));
        c.drawCircle(mX, mY, mR, p);
        p.setColor(0xFC9838 | (a << 24));
        for (int i = -1; i <= 1; i++) c.drawRect(mX - mR * 0.5f, mY + i * mR * 0.35f - mR * 0.08f, mX + mR * 0.5f, mY + i * mR * 0.35f + mR * 0.08f, p);
    }

    private void button(Canvas c, float x, float y, float r, String label, boolean down, int a) {
        p.setStyle(Paint.Style.FILL);
        p.setColor((down ? 0xFC9838 : 0xA80010) | (a << 24));
        c.drawCircle(x, y, r, p);
        p.setStyle(Paint.Style.STROKE);
        p.setColor(0xFCD8A8 | (a << 24));
        c.drawCircle(x, y, r, p);
        label(c, label, x, y, r * 0.11f, a);
    }

    private void pill(Canvas c, float x, float y, String label, boolean down, int a) {
        p.setStyle(Paint.Style.FILL);
        p.setColor((down ? 0x3CBCFC : 0x24188C) | (a << 24));
        c.drawRoundRect(new RectF(x, y, x + ssW, y + ssH), ssH / 2, ssH / 2, p);
        label(c, label, x + ssW / 2, y + ssH / 2, ssH / 12f, a);
    }

    private void label(Canvas c, String s, float cx, float cy, float px, int a) {
        if (gfx != null && gfx.ready()) {
            gfx.text(c, s, cx - s.length() * 4 * px, cy - 4 * px, px, 0xFFFCFCFC & (0x00FFFFFF | (a << 24)));
        } else {
            p.setStyle(Paint.Style.FILL); p.setColor(0xFCFCFC | (a << 24)); p.setTextSize(px * 9);
            p.setTextAlign(Paint.Align.CENTER); c.drawText(s, cx, cy + px * 3, p);
        }
    }

    private int hit(float x, float y) {
        float dx = x - dcx, dy = y - dcy;
        if (dx * dx + dy * dy < dr * dr * 1.8f) {
            if (Math.hypot(dx, dy) < dr * 0.18f) return 0;
            int b = 0;
            double ang = Math.toDegrees(Math.atan2(dy, dx));
            if (ang > -157.5 && ang < -22.5) b |= Nes.UP;
            if (ang > 22.5 && ang < 157.5) b |= Nes.DOWN;
            if (ang > 112.5 || ang < -112.5) b |= Nes.LEFT;
            if (ang > -67.5 && ang < 67.5) b |= Nes.RIGHT;
            return b;
        }
        float rr = bR * bR * 1.7f;
        boolean inA = sq(x - aX) + sq(y - aY) < rr, inB = sq(x - bX) + sq(y - bY) < rr;
        if (inA && inB) return Nes.A | Nes.B;
        if (inA) return Nes.A;
        if (inB) return Nes.B;
        // between A and B presses both
        if (sq(x - (aX + bX) / 2) + sq(y - (aY + bY) / 2) < bR * bR * 0.5f) return Nes.A | Nes.B;
        if (y > ssY - ssH && y < ssY + ssH * 2) {
            if (x > selX - ssH && x < selX + ssW + ssH / 2) return Nes.SELECT;
            if (x > stX - ssH / 2 && x < stX + ssW + ssH) return Nes.START;
        }
        return 0;
    }

    private static float sq(float v) { return v * v; }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int act = e.getActionMasked();
        if (act == MotionEvent.ACTION_DOWN || act == MotionEvent.ACTION_POINTER_DOWN) {
            int i = e.getActionIndex();
            float x = e.getX(i), y = e.getY(i);
            if (sq(x - mX) + sq(y - mY) < mR * mR * 2.5f) { if (listener != null) listener.onMenu(); return true; }
        }
        for (int i = 0; i < ptrBits.length; i++) ptrBits[i] = 0;
        for (int i = 0; i < e.getPointerCount(); i++) {
            if ((act == MotionEvent.ACTION_POINTER_UP && i == e.getActionIndex()) || act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_CANCEL) continue;
            int id = e.getPointerId(i);
            if (id < ptrBits.length) ptrBits[id] = hit(e.getX(i), e.getY(i));
        }
        int b = 0;
        for (int v : ptrBits) b |= v;
        if (b != bits) { bits = b; invalidate(); }
        return true;
    }
}
