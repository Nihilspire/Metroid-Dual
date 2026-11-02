package com.jack.metroiddual;

import android.graphics.Bitmap;

/**
 * Builds the 16:9 side-border texture from what the game is drawing right now: the most common solid
 * background block in the current nametables, in its own area palette. Brinstar gets blue rock,
 * Norfair its bubbles, Tourian its tech panels, and so on, with no art shipped in the APK.
 */
final class BezelTheme {
    Bitmap block;          // 16x16 metatile
    int accent = 0xFF3CBCFC, shade = 0xFF0000A8, key, area = -1;

    static BezelTheme build(byte[] ppu, int area) {
        int[] counts = new int[256 * 4];
        int best = -1, bestN = 0, bestNt = 0, bestPos = 0;
        for (int nt = 0; nt < 2; nt++) {
            int base = 0x2000 + nt * 0x400;
            for (int r = 2; r < 30; r++) for (int col = 0; col < 32; col++) {
                int t = ppu[base + r * 32 + col] & 255;
                int pal = palOf(ppu, base, r, col);
                if (coverage(ppu, t) < 40) continue;
                int k = pal * 256 + t;
                if (++counts[k] > bestN) { bestN = counts[k]; best = k; bestNt = base; bestPos = r * 32 + col; }
            }
        }
        if (best < 0 || bestN < 6) return null;
        BezelTheme th = new BezelTheme();
        int pal = best >> 8;
        int r0 = (bestPos / 32) & ~1, c0 = (bestPos % 32) & ~1;
        int[] cols = new int[4];
        cols[0] = Nes.color(ppu[0x3F00]);
        for (int i = 1; i < 4; i++) cols[i] = Nes.color(ppu[0x3F00 + pal * 4 + i]);
        th.block = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888);
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            int t = ppu[bestNt + (r0 + dy) * 32 + c0 + dx] & 255;
            int p = palOf(ppu, bestNt, r0 + dy, c0 + dx);
            int[] cc = new int[4]; cc[0] = cols[0];
            for (int i = 1; i < 4; i++) cc[i] = Nes.color(ppu[0x3F00 + p * 4 + i]);
            for (int y = 0; y < 8; y++) {
                int lo = ppu[0x1000 + t * 16 + y] & 255, hi = ppu[0x1000 + t * 16 + y + 8] & 255;
                for (int x = 0; x < 8; x++) {
                    int v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
                    th.block.setPixel(dx * 8 + x, dy * 8 + y, cc[v]);
                }
            }
        }
        th.accent = brightest(cols);
        th.shade = cols[1];
        th.key = best * 8 + area;
        th.area = area;
        return th;
    }

    private static int palOf(byte[] ppu, int base, int r, int c) {
        int a = ppu[base + 0x3C0 + (r / 4) * 8 + c / 4] & 255;
        int shift = ((r & 2) << 1) | (c & 2);
        return (a >> shift) & 3;
    }

    private static int coverage(byte[] ppu, int t) {
        int n = 0;
        for (int y = 0; y < 8; y++) n += Integer.bitCount((ppu[0x1000 + t * 16 + y] | ppu[0x1000 + t * 16 + y + 8]) & 255);
        return n;
    }

    private static int brightest(int[] c) {
        int best = c[1], bl = -1;
        for (int i = 1; i < 4; i++) {
            int l = ((c[i] >> 16) & 255) * 3 + ((c[i] >> 8) & 255) * 6 + (c[i] & 255);
            if (l > bl) { bl = l; best = c[i]; }
        }
        return best;
    }
}
