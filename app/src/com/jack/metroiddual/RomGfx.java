package com.jack.metroiddual;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;

import java.io.File;
import java.io.FileInputStream;

/** Graphics read from the player's own ROM at runtime (nothing from the game ships in the APK). */
final class RomGfx {
    byte[] rom;
    private final Bitmap[] glyphs = new Bitmap[64];   // white-on-transparent font tiles
    private final Paint paint = new Paint();
    private final Rect src = new Rect(0, 0, 8, 8), dst = new Rect();

    boolean load(File f) {
        try {
            if (!f.exists()) return false;
            byte[] b = new byte[(int) f.length()];
            FileInputStream in = new FileInputStream(f);
            int n = 0; while (n < b.length) { int r = in.read(b, n, b.length - n); if (r < 0) break; n += r; }
            in.close();
            if (b.length < Nes.ROM_FONT + 0x400) return false;
            rom = b;
            for (int t = 0; t < 64; t++) glyphs[t] = tile(Nes.ROM_FONT + t * 16, new int[] { 0, -1, -1, -1 });
            paint.setFilterBitmap(false);
            return true;
        } catch (Exception e) { return false; }
    }

    boolean ready() { return rom != null; }

    /** Decodes one 2bpp tile at a ROM offset with ARGB colors per pixel value. */
    Bitmap tile(int off, int[] colors) {
        Bitmap bm = Bitmap.createBitmap(8, 8, Bitmap.Config.ARGB_8888);
        for (int y = 0; y < 8; y++) {
            int lo = rom[off + y] & 255, hi = rom[off + y + 8] & 255;
            for (int x = 0; x < 8; x++) {
                int v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
                bm.setPixel(x, y, colors[v]);
            }
        }
        return bm;
    }

    static int code(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        if (c >= 'a' && c <= 'z') return c - 'a' + 36;
        if (c == '?') return 62;
        if (c == '-') return 63;
        return -1;
    }

    /** Draws text in the game's font; px is the size of one NES pixel on screen. */
    void text(Canvas c, String s, float x, float y, float px, int color) {
        if (!ready()) return;
        paint.setColorFilter(new android.graphics.PorterDuffColorFilter(color, android.graphics.PorterDuff.Mode.SRC_IN));
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '.') { Paint dot = new Paint(); dot.setColor(color); float bx = x + i * 8 * px; c.drawRect(bx + 3 * px, y + 5 * px, bx + 5 * px, y + 7 * px, dot); continue; }
            if (ch == '%') {
                Paint dot = new Paint(); dot.setColor(color); float bx = x + i * 8 * px;
                c.drawRect(bx + px, y + px, bx + 3 * px, y + 3 * px, dot); c.drawRect(bx + 5 * px, y + 5 * px, bx + 7 * px, y + 7 * px, dot);
                for (int k = 0; k < 6; k++) c.drawRect(bx + (6 - k) * px, y + (k + 1) * px, bx + (7 - k) * px, y + (k + 2) * px, dot);
                continue;
            }
            if (ch == ':' || ch == '/') {
                Paint dot = new Paint(); dot.setColor(color);
                float bx = x + i * 8 * px;
                if (ch == ':') { c.drawRect(bx + 3 * px, y + 2 * px, bx + 5 * px, y + 4 * px, dot); c.drawRect(bx + 3 * px, y + 5 * px, bx + 5 * px, y + 7 * px, dot); }
                else for (int k = 0; k < 6; k++) c.drawRect(bx + (6 - k) * px, y + k * px, bx + (7 - k) * px, y + (k + 1) * px, dot);
                continue;
            }
            int t = code(ch);
            if (t >= 0) {
                dst.set(Math.round(x + i * 8 * px), Math.round(y), Math.round(x + (i + 1) * 8 * px), Math.round(y + 8 * px));
                c.drawBitmap(glyphs[t], src, dst, paint);
            }
        }
        paint.setColorFilter(null);
    }
}
