package com.jack.metroiddual;

import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.Arrays;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Experimental 3D view (Dev settings). Rebuilds each emulated frame from memory: background tiles below $A0
 * (Metroid's solid tiles) become blocks, other tiles a back wall, sprites stand as cut-outs in the middle of the
 * block depth, HUD sprites stay flat in front. A dimmed wall of the area's own rock blocks (read from the ROM)
 * fills the empty space behind.
 * Same logic as tools/viewer3d/metroid3d.html.
 */
final class Renderer3D implements GLSurfaceView.Renderer {
    static final int SNAP = 0x3820, NT = 0x800, PAL = 0x1800, CHR = 0x1820;

    // options (set from the UI thread)
    volatile String camPreset = "tilt", back = "near";
    volatile int depth = 16;
    volatile boolean cave = true, light = true;
    volatile byte[] rom;     // the player's ROM: the backdrop is built from the area's own blocks

    private final Object lock = new Object();
    private byte[] pending = new byte[SNAP], current = new byte[SNAP];
    private boolean fresh;

    void submit(byte[] snap) {
        synchronized (lock) { System.arraycopy(snap, 0, pending, 0, SNAP); fresh = true; }
    }

    // ------------------------------------------------------------------ GL state
    private int uMask, maskTex;
    private final ByteBuffer mask = ByteBuffer.allocateDirect(256 * 256);
    private int uAlpha, transStart, transCount;
    private int prog, aPos, aUv, aShade, aKind, uMvp, uTex, uCave, uBg, uEdge, uLight, uLightOn, uMode, uTint, uFog;
    private int atlasTex, caveTex, vbo;
    private final ByteBuffer atlas = ByteBuffer.allocateDirect(512 * 256 * 4).order(ByteOrder.nativeOrder());
    private final byte[] lastChr = new byte[0x2000], lastPal = new byte[0x20];
    private boolean atlasValid;
    private int W = 1, H = 1;
    private float pitch = 16, yaw = 0, zoom = 1;
    private float[] verts = new float[1 << 17];
    private int nv;
    private FloatBuffer fb = ByteBuffer.allocateDirect(verts.length * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();

    private static final String VS =
        "attribute vec3 aPos; attribute vec2 aUv; attribute float aShade; attribute float aKind;\n" +
        "uniform mat4 uMvp; varying vec2 vUv; varying float vShade; varying float vKind; varying vec3 vWorld;\n" +
        "void main(){ vUv=aUv; vShade=aShade; vKind=aKind; vWorld=aPos; gl_Position=uMvp*vec4(aPos,1.0); }";
    private static final String FS =
        "precision mediump float;\n" +
        "varying vec2 vUv; varying float vShade; varying float vKind; varying vec3 vWorld;\n" +
        "uniform sampler2D uTex; uniform sampler2D uCave; uniform vec3 uBg; uniform int uEdge; uniform vec2 uLight;\n" +
        "uniform float uLightOn; uniform int uMode; uniform vec3 uTint; uniform vec3 uFog;\n" +
        "uniform sampler2D uMask; uniform float uAlpha;\n" +
        "void main(){\n" +
        "  if (uMode == 1) {\n" +
        "    float k = 0.2;\n" +
        "    k = mix(k, mix(0.4, 0.08, smoothstep(20.0, 150.0, distance(vWorld.xy, uLight))), uLightOn);\n" +
        "    gl_FragColor = vec4(texture2D(uCave, vUv).rgb * k, 1.0); return;\n" +
        "  }\n" +
        "  vec4 c = texture2D(uTex, vUv);\n" +
        "  if (uMode == 2) { if (c.a < 0.5) discard; gl_FragColor = vec4(c.rgb, 1.0); return; }\n" +
        "  if (c.a < 0.5) { if (vKind < 0.5) c = vec4(uBg * 0.55, 1.0); else discard; }\n" +
        "  if (vKind > 4.5 && vKind < 5.5 && texture2D(uMask, vec2((vWorld.x + 0.5) / 256.0, (-vWorld.y + 0.5) / 256.0)).r > 0.5) discard;\n" +
        "  float shade = vShade;\n" +
        "  shade *= mix(1.0, mix(1.15, 0.3, smoothstep(24.0, 160.0, distance(vWorld.xy, uLight))), uLightOn);\n" +
        "  vec3 col = c.rgb * shade; float a = 1.0;\n" +
        "  if (uEdge != 2) {\n" +
        "    float dx = max(max(-vWorld.x, vWorld.x - 256.0), 0.0), dy = max(max(vWorld.y + 8.0, -232.0 - vWorld.y), 0.0);\n" +
        "    float o = max(dx, dy);\n" +
        "    if (uEdge == 1 && o > 0.0) discard;\n" +
        "    if (uEdge == 0) { a = 1.0 - smoothstep(0.0, 24.0, o); if (a < 0.02) discard; }\n" +
        "  }\n" +
        "  gl_FragColor = vec4(mix(uFog, col, a), uAlpha);\n" +
        "}";

    @Override public void onSurfaceCreated(GL10 unused, EGLConfig cfg) {
        prog = GLES20.glCreateProgram();
        GLES20.glAttachShader(prog, shader(GLES20.GL_VERTEX_SHADER, VS));
        GLES20.glAttachShader(prog, shader(GLES20.GL_FRAGMENT_SHADER, FS));
        GLES20.glLinkProgram(prog);
        aPos = GLES20.glGetAttribLocation(prog, "aPos"); aUv = GLES20.glGetAttribLocation(prog, "aUv");
        aShade = GLES20.glGetAttribLocation(prog, "aShade"); aKind = GLES20.glGetAttribLocation(prog, "aKind");
        uMvp = GLES20.glGetUniformLocation(prog, "uMvp"); uTex = GLES20.glGetUniformLocation(prog, "uTex");
        uCave = GLES20.glGetUniformLocation(prog, "uCave"); uBg = GLES20.glGetUniformLocation(prog, "uBg");
        uEdge = GLES20.glGetUniformLocation(prog, "uEdge"); uLight = GLES20.glGetUniformLocation(prog, "uLight");
        uLightOn = GLES20.glGetUniformLocation(prog, "uLightOn"); uMode = GLES20.glGetUniformLocation(prog, "uMode");
        uTint = GLES20.glGetUniformLocation(prog, "uTint"); uFog = GLES20.glGetUniformLocation(prog, "uFog"); uAlpha = GLES20.glGetUniformLocation(prog, "uAlpha");
        uMask = GLES20.glGetUniformLocation(prog, "uMask");
        int[] t = new int[4];
        GLES20.glGenTextures(2, t, 0);
        atlasTex = t[0]; caveTex = t[1];
        GLES20.glGenTextures(1, t, 3);
        maskTex = t[3];
        GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, maskTex);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE, 256, 256, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, mask);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glGenBuffers(1, t, 2);
        vbo = t[2];
        atlasValid = false; bdArea = -1;
    }

    private static int shader(int type, String src) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, src);
        GLES20.glCompileShader(s);
        return s;
    }

    private static void tex(int id, int w, int h, ByteBuffer data, int wrap) {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, data);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, wrap);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, wrap);
    }

    @Override public void onSurfaceChanged(GL10 unused, int w, int h) { W = Math.max(1, w); H = Math.max(1, h); GLES20.glViewport(0, 0, W, H); }

    // ------------------------------------------------------------------ helpers
    private static int u8(byte b) { return b & 255; }
    private static int rgb(int i) { return Nes.PALETTE[i & 63]; }

    private int ox, oy, bgBase, spBase;
    private void view(byte[] f) {
        int ctrl = u8(f[0xFF]);
        ox = u8(f[0xFD]) + (ctrl & 1) * 256;
        oy = u8(f[0xFC]) + ((ctrl >> 1) & 1) * 240;
        bgBase = (ctrl & 0x10) != 0 ? 0x1000 : 0;
        spBase = (ctrl & 0x08) != 0 ? 0x1000 : 0;
    }
    private int ntT, ntP;
    private void ntTile(byte[] f, int wx, int wy) {
        wx = ((wx % 512) + 512) % 512; wy = ((wy % 480) + 480) % 480;
        int tbl = (wx >= 256 ? 1 : 0) + (wy >= 240 ? 2 : 0), x = (wx % 256) >> 3, y = (wy % 240) >> 3;
        ntT = u8(f[NT + tbl * 0x400 + y * 32 + x]);
        int a = u8(f[NT + tbl * 0x400 + 0x3C0 + (y >> 2) * 8 + (x >> 2)]);
        ntP = (a >> (((y & 2) << 1) | (x & 2))) & 3;
    }
    static final int[] STATUE_TILES = { 0xFA, 0xFB, 0xC5, 0xC6, 0xC7, 0xD5, 0xD6, 0xD7, 0xE5, 0xE6, 0xE7,
                                        0xC8, 0xC9, 0xEB, 0xD8, 0xD9, 0xEA, 0xE8, 0xE9 };
    static boolean isStatueTile(int t) { for (int v : STATUE_TILES) if (v == t) return true; return false; }
    private final boolean[] slab = new boolean[64];
    /** Marks OAM entries that are solid objects: door bubbles, the elevator platform (a row of 4 sprites
     *  $28 $38 $38 $28 on one line) and the Kraid/Ridley statues in Tourian. */
    private void findSlabs(byte[] f, int area) {
        for (int s = 0; s < 64; s++) {
            int o = 0x200 + s * 4, y = u8(f[o]), t = u8(f[o + 1]);
            slab[s] = y < 0xEF && (isDoor(t) || (area == 3 && isStatueTile(t)));
        }
        for (int s = 0; s < 64; s++) {
            int o = 0x200 + s * 4, y = u8(f[o]), t = u8(f[o + 1]), x = u8(f[o + 3]);
            if (y >= 0xEF || t != 0x28) continue;
            int run = 0;                                       // count $28/$38 sprites to the right on the same line
            for (int k = 0; k < 64 && run < 3; k++) {
                int ok = 0x200 + k * 4;
                if (u8(f[ok]) == y && u8(f[ok + 3]) == x + 8 * (run + 1) && (u8(f[ok + 1]) == 0x38 || u8(f[ok + 1]) == 0x28)) { run++; k = -1; }
            }
            if (run == 3) for (int k = 0; k < 64; k++) {
                int ok = 0x200 + k * 4, kx = u8(f[ok + 3]);
                if (u8(f[ok]) == y && kx >= x && kx <= x + 24 && (u8(f[ok + 1]) == 0x38 || u8(f[ok + 1]) == 0x28)) slab[k] = true;
            }
        }
    }
    static boolean isDoor(int t) { return t == 0x0F || t == 0x1F || t == 0x2F || t == 0x6A || t == 0x6B || t == 0x6C; }
    static boolean isHud(int y, int t, int x) {
        // Rows of the status display: tanks $17, EN/health $21, missiles $2B (OAM y, +1 here). Exact rows so a
        // missile fired near the top-left (same tiles as the missile icon) isn't mistaken for the HUD.
        if ((y != 0x18 && y != 0x22 && y != 0x2C) || x >= 0x50) return false;
        return t == 0x5E || t == 0x5F || t == 0x76 || t == 0x7F || t == 0x3A || t == 0x6E || t == 0x6F || t == 0xFF
                || t == 0x58 || t == 0x59 || (t >= 0xA0 && t <= 0xA9);
    }

    private void buildAtlas(byte[] f) {
        boolean same = atlasValid;
        if (same) for (int i = 0; i < 0x20 && same; i++) same = f[PAL + i] == lastPal[i];
        if (same) for (int i = 0; i < 0x2000 && same; i++) same = f[CHR + i] == lastChr[i];
        if (same) return;
        System.arraycopy(f, PAL, lastPal, 0, 0x20);
        System.arraycopy(f, CHR, lastChr, 0, 0x2000);
        atlasValid = true;
        // Passable tiles that look exactly like a solid one ($A0+ with the pattern of a tile below $A0): hidden passages
        // and pipes Samus goes through. Drawn as see-through blocks so they're easy to spot.
        for (int t = 0; t < 256; t++) {
            passable[t] = false;
            if (t < 0xA2 || t == 0xFF || t == 0xF0 || t == 0xF1) continue;
            boolean blank = true;
            for (int i = 0; i < 16 && blank; i++) blank = f[CHR + bgBase + t * 16 + i] == 0;
            if (blank) continue;
            for (int u = 0; u < 0xA0 && !passable[t]; u++) {
                boolean eq = true;
                for (int i = 0; i < 16 && eq; i++) eq = f[CHR + bgBase + t * 16 + i] == f[CHR + bgBase + u * 16 + i];
                passable[t] = eq;
            }
        }
        for (int set = 0; set < 8; set++) {
            boolean spr = set >= 4; int pal = set & 3, base = spr ? spBase : bgBase;
            for (int t = 0; t < 256; t++) {
                int idx = set * 256 + t, ax = (idx % 64) * 8, ay = (idx / 64) * 8;
                for (int y = 0; y < 8; y++) {
                    int lo = u8(f[CHR + base + t * 16 + y]), hi = u8(f[CHR + base + t * 16 + y + 8]);
                    for (int x = 0; x < 8; x++) {
                        int c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1), k = ((ay + y) * 512 + ax + x) * 4;
                        int col = rgb(u8(f[PAL + (spr ? 16 : 0) + pal * 4 + c]));
                        atlas.put(k, (byte) (col >> 16)); atlas.put(k + 1, (byte) (col >> 8)); atlas.put(k + 2, (byte) col);
                        atlas.put(k + 3, (byte) (c != 0 ? 255 : 0));
                    }
                }
            }
        }
        atlas.position(0);
        tex(atlasTex, 512, 256, atlas, GLES20.GL_CLAMP_TO_EDGE);
        bdDirty = true;
    }

    // ------------------------------------------------------------------ backdrop
    // The empty space behind the room is a dimmed wall of the area's own blocks: the 2x2-tile blocks ("macros") the
    // area's room structures are built from, taken from the ROM and weighted by how often the rooms use them, laid
    // out in 32 px clumps by a fixed hash. Drawn with the live pattern table and palette, so it always matches the
    // area. Scrolls at half speed, following only the smooth scroll movement: the scroll registers jump by half a
    // screen when the game swaps nametables at a room change, and following those made the old backdrop jump.
    private int bdArea = -1; private boolean bdDirty = true, bdOk; private byte[] bdRom, bdChr;
    private int[] bdPool = new int[0];         // per entry: t0 t1 t2 t3 pal weight
    private final ByteBuffer bd = ByteBuffer.allocateDirect(256 * 256 * 4).order(ByteOrder.nativeOrder());
    private float accX, accY; private int lastOx = Integer.MIN_VALUE, lastOy;
    private static final int[] AREA_BANK = { 1, 2, 4, 3, 5 };

    private static int romOff(int bank, int addr) { return 16 + bank * 0x4000 + ((addr - 0x8000) & 0x3FFF); }
    private static int romW(byte[] r, int o) { return u8(r[o]) | u8(r[o + 1]) << 8; }
    /** Solid blocks the area's rooms are built from, with how often each (block, palette) pair is placed. */
    static int[] macroPool(byte[] rom, int area) {
        if (rom == null || rom.length < 0x20010 || area < 0 || area > 4) return new int[0];
        try {
            int b = AREA_BANK[area], hdr = romOff(b, 0x9598);
            int rooms = romW(rom, hdr + 2), structs = romW(rom, hdr + 4), macros = romW(rom, hdr + 6), nRooms = (structs - rooms) >> 1;
            int[] count = new int[256 * 4];
            for (int n = 0; n < nRooms && n < 128; n++) {
                int p = romOff(b, romW(rom, romOff(b, rooms) + n * 2)) + 1;
                for (int g = 0; g < 200; g++) {
                    int c = u8(rom[p]);
                    if (c == 0xFF || c == 0xFD) break;
                    if (c == 0xFE) { p++; continue; }
                    int si = u8(rom[p + 1]), pal = u8(rom[p + 2]) & 3, sp = romOff(b, romW(rom, romOff(b, structs) + si * 2));
                    for (int row = 0; row < 32; row++) {
                        int len = u8(rom[sp]); if (len == 0xFF) break;
                        int cnt = (len & 15) != 0 ? len & 15 : 16;
                        for (int k = 0; k < cnt; k++) count[u8(rom[sp + 1 + k]) * 4 + pal]++;
                        sp += 1 + cnt;
                    }
                    p += 3;
                }
            }
            int[] out = new int[256 * 4 * 6]; int n = 0;
            for (int key = 0; key < 1024; key++) {
                if (count[key] == 0) continue;
                int mp = romOff(b, macros) + (key >> 2) * 4; boolean ok = true;
                for (int i = 0; i < 4 && ok; i++) ok = u8(rom[mp + i]) < 0xA0;
                if (!ok) continue;
                for (int i = 0; i < 4; i++) out[n * 6 + i] = u8(rom[mp + i]);
                out[n * 6 + 4] = key & 3; out[n * 6 + 5] = count[key]; n++;
            }
            return Arrays.copyOf(out, n * 6);
        } catch (RuntimeException e) { return new int[0]; }
    }
    // The pattern tables an area loads (MRoom.chr): the backdrop is only drawn while its blocks' live tiles still match
    // these. Anywhere else (title, password screen, ending credits) the same tile numbers hold other graphics.
    private static final int[][] GFX = { { 6,0x8000,0x0000,0x09A0 },{ 4,0x8D60,0x0000,0x0520 },{ 1,0x8D60,0x1000,0x0400 },{ 6,0x9DA0,0x1000,0x0150 },
    { 5,0x8D60,0x1200,0x0450 },{ 6,0x9EF0,0x1800,0x0800 },{ 1,0x9160,0x0C00,0x0400 },{ 6,0xA6F0,0x1000,0x0260 },{ 6,0xA950,0x1700,0x0070 },
    { 2,0x8D60,0x0C00,0x0400 },{ 6,0xA9C0,0x1000,0x02E0 },{ 6,0xACA0,0x1200,0x0600 },{ 6,0xB2A0,0x1900,0x0090 },{ 5,0x91B0,0x1D00,0x0300 },
    { 2,0x9160,0x0C00,0x0400 },{ 6,0xB330,0x1700,0x00C0 },{ 4,0x9360,0x1E00,0x0200 },{ 3,0x8D60,0x0C00,0x0400 },{ 6,0xB3F0,0x1700,0x00C0 },
    { 3,0x9160,0x0C00,0x0400 },{ 6,0x89A0,0x0C00,0x0100 },{ 6,0x8BE0,0x1400,0x0500 },{ 6,0x9980,0x1FC0,0x0040 },{ 6,0xB4C0,0x1000,0x0400 },
    { 6,0xB4C0,0x0A00,0x00A0 },{ 6,0x9980,0x0FC0,0x0040 },{ 6,0xB4C0,0x1D00,0x02A0 },{ 6,0x90E0,0x0000,0x07B0 },{ 6,0x9890,0x1F40,0x0010 } };
    private static final int[][] AREA_GFX = { { 0,3,4,5,6,0x19,0x16 }, { 0,4,5,7,8,9,0x19,0x16 }, { 0,4,5,0x0A,0x0F,0x10,0x11,0x19,0x16 },
    { 0,5,0x0A,0x0B,0x0C,0x0D,0x0E,0x1A,0x1C,0x19,0x16 }, { 0,4,5,0x0A,0x12,0x13,0x19,0x16 } };
    private static final int[] START_GFX = { 0x15,0,0x14,0x17,0x18,0x19,0x16 };
    static byte[] areaChr(byte[] rom, int area) {
        byte[] chr = new byte[0x2000];
        int[] a = AREA_GFX[area];
        for (int k = 0; k < START_GFX.length + a.length; k++) {
            int[] g = GFX[k < START_GFX.length ? START_GFX[k] : a[k - START_GFX.length]];
            int s = romOff(g[0], g[1]);
            for (int i = 0; i < g[3] && s + i < rom.length; i++) chr[(g[2] + i) & 0x1FFF] = rom[s + i];
        }
        return chr;
    }
    private static int bdHash(int x, int y, int s) {
        int h = x * 374761393 + y * 668265263 + s * 1274126177;
        h = (h ^ (h >>> 13)) * 1274126177;
        return (h ^ (h >>> 16)) & 0x7FFFFFFF;
    }
    private void buildBackdrop(byte[] f) {
        int area = u8(f[0x74]) & 15;
        byte[] r = rom;
        if (area != bdArea || r != bdRom) {
            bdPool = macroPool(r, area); bdChr = bdPool.length > 0 ? areaChr(r, area) : null; bdArea = area; bdRom = r; bdDirty = true;
        }
        if (!bdDirty) return;
        bdDirty = false;
        // entries whose tiles are all drawn in the current pattern table
        int total = 0; int[] w = new int[bdPool.length / 6];
        for (int e = 0; e < w.length; e++) {
            boolean ok = true;
            for (int i = 0; i < 4 && ok; i++) {
                int t = bdPool[e * 6 + i];
                ok = !emptyTile[t] && bgBase == 0x1000 && bdChr != null;
                for (int j = 0; j < 16 && ok; j++) ok = f[CHR + 0x1000 + t * 16 + j] == bdChr[0x1000 + t * 16 + j];
            }
            w[e] = ok ? bdPool[e * 6 + 5] : 0;
        }
        // only the area's 3 most-placed blocks (its main rock), so the backdrop reads as one wall, not a jumble
        for (int keep = 0, cut = Integer.MAX_VALUE; keep < 3; keep++) {
            int best = 0; for (int e = 0; e < w.length; e++) if (w[e] < cut && w[e] > best) best = w[e];
            if (best == 0) break; cut = best;
            if (keep == 2) for (int e = 0; e < w.length; e++) if (w[e] < best) w[e] = 0;
        }
        for (int e = 0; e < w.length; e++) total += w[e];
        bdOk = total > 0;
        if (!bdOk) return;
        for (int by = 0; by < 8; by++) for (int bx = 0; bx < 8; bx++) {
            int h = bdHash(bx, by, area + 1) % total, e = 0;
            while (h >= w[e]) { h -= w[e]; e++; }
            int pal = bdPool[e * 6 + 4];
            for (int q = 0; q < 16; q++) {                 // 2x2 blocks = 4x4 tiles per clump
                int tx = bx * 32 + (q & 3) * 8, ty = by * 32 + (q >> 2) * 8, t = bdPool[e * 6 + ((q >> 2) & 1) * 2 + (q & 1)];
                for (int y = 0; y < 8; y++) {
                    int lo = u8(f[CHR + bgBase + t * 16 + y]), hi = u8(f[CHR + bgBase + t * 16 + y + 8]);
                    for (int x = 0; x < 8; x++) {
                        int c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1), k = ((ty + y) * 256 + tx + x) * 4;
                        int col = c == 0 ? 0 : rgb(u8(f[PAL + pal * 4 + c]));
                        bd.put(k, (byte) (col >> 16)); bd.put(k + 1, (byte) (col >> 8)); bd.put(k + 2, (byte) col); bd.put(k + 3, (byte) 255);
                    }
                }
            }
        }
        bd.position(0);
        tex(caveTex, 256, 256, bd, GLES20.GL_REPEAT);
    }
    private void followScroll() {
        if (lastOx != Integer.MIN_VALUE) {
            int dx = Math.floorMod(ox - lastOx + 256, 512) - 256, dy = Math.floorMod(oy - lastOy + 240, 480) - 240;
            if (Math.abs(dx) <= 16) accX = (accX + dx + 512) % 512;
            if (Math.abs(dy) <= 16) accY = (accY + dy + 512) % 512;
        }
        lastOx = ox; lastOy = oy;
    }

    private float u0, v0, u1, v1;
    private void uvRect(int set, int t) {
        int idx = set * 256 + t; float e = 0.02f;
        u0 = ((idx % 64) * 8 + e) / 512f; v0 = ((idx / 64) * 8 + e) / 256f;
        u1 = ((idx % 64) * 8 + 8 - e) / 512f; v1 = ((idx / 64) * 8 + 8 - e) / 256f;
    }

    private void push(float x, float y, float z, float u, float v, float shade, float kind) {
        if (nv * 7 + 7 > verts.length) verts = Arrays.copyOf(verts, verts.length * 2);
        int o = nv * 7;
        verts[o] = x; verts[o + 1] = y; verts[o + 2] = z; verts[o + 3] = u; verts[o + 4] = v; verts[o + 5] = shade; verts[o + 6] = kind;
        nv++;
    }
    /** Quad a,b,c,d (x,y,z each) with uv per corner. */
    private void quad(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz,
                      float ua, float va, float ub, float vb, float uc, float vc, float ud, float vd, float shade, float kind) {
        push(ax, ay, az, ua, va, shade, kind); push(bx, by, bz, ub, vb, shade, kind); push(cx, cy, cz, uc, vc, shade, kind);
        push(ax, ay, az, ua, va, shade, kind); push(cx, cy, cz, uc, vc, shade, kind); push(dx, dy, dz, ud, vd, shade, kind);
    }

    private int caveStart, caveCount, hudStart, spriteStart;
    private boolean maskDirty;
    private final boolean[] emptyTile = new boolean[256], passable = new boolean[256], shapedTile = new boolean[256];
    static final int SHAPED_MIN = 24;     // see-through pixels (of 64) for a solid tile to be built as a cut-out stack
    private final int[] transCells = new int[40 * 40 * 2];
    private int nTrans;
    private float lightX = 128, lightY = -120, camDist = 400;
    private boolean hasLight;
    // Samus isn't always findable (the screw attack cycles her sprites through other palettes): the light stays where
    // she was last seen for a moment and fades in/out instead of switching, so the room never flashes to full light.
    private int lightMiss = 999; private float lightAmt;

    /** Lava: $F0 (surface) / $F1 (body), recognised by their pattern (Brinstar, Norfair, Kraid and Ridley all load the
     *  same lava graphics there). Drawn as a liquid volume through the block depth instead of on the back wall, so the
     *  cave backdrop never shows through it. */
    private static final int[] LAVA_PAT = { 0x45,0xd7,0xff,0xff,0xfd,0xff,0xbf,0xfb,0,0,0,0,0x02,0,0x40,0x04,
                                            0xff,0xbb,0xff,0xff,0xef,0xff,0x7f,0xfd,0,0x44,0,0,0x10,0,0x80,0x02 };
    private boolean lavaOk;
    private boolean isLava(int t) { return lavaOk && (t == 0xF0 || t == 0xF1); }
    /** Item ball ($88 $89 / $8A $8B, the same tiles in every area): a power-up held by a Chozo statue. A ball looks the
     *  same from every side, so it's a flat picture turned to face the camera, centred in the block depth instead of a
     *  cube. Depth-tested, so the statue's hand in front still overlaps its lower edge. */
    private static final int[] BALL = { 0x88, 0x89, 0x8A, 0x8B };
    private void ball(float cx, float cy, int set, float x0, float y0) {
        double p = Math.toRadians(pitch), y = Math.toRadians(yaw);
        float rx = (float) Math.cos(y), rz = (float) -Math.sin(y);
        float ux = (float) (-Math.sin(p) * Math.sin(y)), uy = (float) Math.cos(p), uz = (float) (-Math.sin(p) * Math.cos(y));
        for (int i = 0; i < 4; i++) {
            float a0 = x0 + (i & 1) * 8 - cx, a1 = a0 + 8, b0 = y0 - (i >> 1) * 8 - cy, b1 = b0 - 8;
            uvRect(set, BALL[i]);
            quad(cx + rx * a0 + ux * b1, cy + uy * b1, rz * a0 + uz * b1,
                 cx + rx * a1 + ux * b1, cy + uy * b1, rz * a1 + uz * b1,
                 cx + rx * a1 + ux * b0, cy + uy * b0, rz * a1 + uz * b0,
                 cx + rx * a0 + ux * b0, cy + uy * b0, rz * a0 + uz * b0,
                 u0, v1, u1, v1, u1, v0, u0, v0, 1f, 2);
        }
    }

    private void buildScene(byte[] f) {
        nv = 0;
        int D = depth;
        float zf = D / 2f, zb = -D / 2f;
        float backZ = back.equals("far") ? -D * 2.5f : zb - 0.01f;
        // Only the cells the NES screen shows (256x224). The game writes the next room strip just outside that
        // window while scrolling, so building more would show terrain drawing in.
        int tx0 = fdiv(ox, 8), ty0 = fdiv(oy + 8, 8), cols = fdiv(ox + 255, 8) - tx0 + 1, rows = fdiv(oy + 231, 8) - ty0 + 1;
        // NES sprite priority: a "behind background" sprite pixel is hidden wherever the background pixel is opaque.
        // Screen-space mask of opaque background pixels (door frames excluded: they sit recessed behind sprites in 3D).
        for (int i = 0; i < 256 * 240; i++) mask.put(i, (byte) 0);
        for (int ty = fdiv(oy, 8); ty <= fdiv(oy + 239, 8); ty++) for (int tx = fdiv(ox, 8); tx <= fdiv(ox + 255, 8); tx++) {
            ntTile(f, tx * 8, ty * 8);
            int t = ntT;
            if (t == 0xFF || t == 0xA0 || t == 0xA1) continue;
            for (int py = 0; py < 8; py++) {
                int sy = ty * 8 + py - oy;
                if (sy < 0 || sy >= 240) continue;
                int bits = (u8(f[CHR + bgBase + t * 16 + py]) | u8(f[CHR + bgBase + t * 16 + py + 8]));
                if (bits == 0) continue;
                for (int px = 0; px < 8; px++) {
                    int sx = tx * 8 + px - ox;
                    if (sx >= 0 && sx < 256 && ((bits >> (7 - px)) & 1) != 0) mask.put(sy * 256 + sx, (byte) 255);
                }
            }
        }
        maskDirty = true;
        // tiles with no visible pixels (e.g. $4E, the invisible solid column behind a door bubble) never become blocks
        for (int t = 0; t < 256; t++) {
            boolean e = true;
            for (int i = 0; i < 16 && e; i++) e = f[CHR + bgBase + t * 16 + i] == 0;
            emptyTile[t] = e;
            // Solid tiles with many see-through pixels (round shapes, crescents, rings, bubbly rock) are built as a
            // stack of cut-out layers through the block depth, like the door bubbles, so only drawn pixels have volume.
            int holes = 0;
            for (int y = 0; y < 8; y++) { int b = u8(f[CHR + bgBase + t * 16 + y]) | u8(f[CHR + bgBase + t * 16 + y + 8]); holes += 8 - Integer.bitCount(b); }
            shapedTile[t] = t < 0xA0 && !e && holes >= SHAPED_MIN;
        }
        boolean[][] solid = new boolean[rows + 2][cols + 2], lava = new boolean[rows + 2][cols + 2], ball = new boolean[rows + 2][cols + 2],
                    pass = new boolean[rows + 2][cols + 2], full = new boolean[rows + 2][cols + 2];
        nTrans = 0;
        lavaOk = true;
        for (int i = 0; i < 32 && lavaOk; i++) lavaOk = u8(f[CHR + bgBase + 0xF0 * 16 + i]) == LAVA_PAT[i];
        for (int r = -1; r <= rows; r++) for (int c = -1; c <= cols; c++) {
            ntTile(f, (tx0 + c) * 8, (ty0 + r) * 8);
            solid[r + 1][c + 1] = ntT != 0xFF && ntT < 0xA0 && !emptyTile[ntT];
            full[r + 1][c + 1] = solid[r + 1][c + 1] && !shapedTile[ntT];     // hides a neighbour's side face
            lava[r + 1][c + 1] = isLava(ntT);
            pass[r + 1][c + 1] = passable[ntT];
        }
        for (int r = -1; r < rows; r++) for (int c = -1; c < cols; c++) {       // item balls
            ntTile(f, (tx0 + c) * 8, (ty0 + r) * 8); if (ntT != 0x88) continue;
            int p = ntP;
            ntTile(f, (tx0 + c + 1) * 8, (ty0 + r) * 8); if (ntT != 0x89) continue;
            ntTile(f, (tx0 + c) * 8, (ty0 + r + 1) * 8); if (ntT != 0x8A) continue;
            ntTile(f, (tx0 + c + 1) * 8, (ty0 + r + 1) * 8); if (ntT != 0x8B) continue;
            ball[r + 1][c + 1] = ball[r + 1][c + 2] = ball[r + 2][c + 1] = ball[r + 2][c + 2] = true;
            solid[r + 1][c + 1] = solid[r + 1][c + 2] = solid[r + 2][c + 1] = solid[r + 2][c + 2] = false;
            float x0 = (tx0 + c) * 8 - ox, y0 = -((ty0 + r) * 8 - oy);
            ball(x0 + 8, y0 - 7.5f, p, x0, y0);
        }
        for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
            int wx = (tx0 + c) * 8, wy = (ty0 + r) * 8;
            ntTile(f, wx, wy);
            int t = ntT, p = ntP;
            if (t == 0xFF || ball[r + 1][c + 1]) continue;
            float x0 = wx - ox, x1 = x0 + 8, y0 = -(wy - oy), y1 = y0 - 8;
            uvRect(p, t);
            if (lava[r + 1][c + 1]) {
                // liquid volume: front face (wave pixels cut out), flat surface 2 px into the wave tile, open sides
                quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1f, 2);
                float ys = lava[r][c + 1] ? y0 : y0 - 2;
                if (!lava[r][c + 1]) {
                    float a = u0, b = v0, cc = u1, d = v1;
                    uvRect(p, 0xF1);
                    quad(x0, ys, zf, x1, ys, zf, x1, ys, zb, x0, ys, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.92f, 0);
                    u0 = a; v0 = b; u1 = cc; v1 = d;
                }
                if (!lava[r + 1][c] && !solid[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, ys, zf, x0, ys, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.7f, 0);
                if (!lava[r + 1][c + 2] && !solid[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, ys, zb, x1, ys, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.7f, 0);
                continue;
            }
            if ((t == 0xA0 || t == 0xA1) && !emptyTile[t]) {
                // door frame (walkable): a frame set into the wall, shallow so Samus stays visible walking through
                float zt = zb + D * 0.4f;
                quad(x0, y1, zt, x1, y1, zt, x1, y0, zt, x0, y0, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.9f, 0);
                if (!solid[r][c + 1]) quad(x0, y0, zt, x1, y0, zt, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.78f, 0);
                if (!solid[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zt, x0, y1, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.45f, 0);
                if (!solid[r + 1][c]) quad(x0, y1, zb, x0, y1, zt, x0, y0, zt, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 0);
                if (!solid[r + 1][c + 2]) quad(x1, y1, zt, x1, y1, zb, x1, y0, zb, x1, y0, zt, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 0);
                continue;
            }
            if (pass[r + 1][c + 1] && nTrans < transCells.length / 2) {
                // walk-through tile drawn like a solid one (hidden passages, pipes, but also bushes and other decor):
                // the tile itself on the back wall, as in 2D, plus a faint see-through block (drawn later)
                if (!back.equals("off")) quad(x0, y1, backZ, x1, y1, backZ, x1, y0, backZ, x0, y0, backZ, u0, v1, u1, v1, u1, v0, u0, v0, 0.62f, 1);
                transCells[nTrans * 2] = r * 64 + c; transCells[nTrans * 2 + 1] = p << 8 | t; nTrans++; continue;
            }
            if (!solid[r + 1][c + 1]) {
                if (!back.equals("off") && !emptyTile[t]) quad(x0, y1, backZ, x1, y1, backZ, x1, y0, backZ, x0, y0, backZ, u0, v1, u1, v1, u1, v0, u0, v0, 0.62f, 1);
                continue;
            }
            if (shapedTile[t]) {
                float step = Math.max(1f, D / 8f);                        // 8 layers through the block (overdraw stays modest)
                for (float zz = zb + step / 2; zz < zf; zz += step) quad(x0, y1, zz, x1, y1, zz, x1, y0, zz, x0, y0, zz, u0, v1, u1, v1, u1, v0, u0, v0, 0.72f, 2);
                quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1f, 2);
                continue;
            }
            quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1f, 0);
            if (!full[r][c + 1]) quad(x0, y0, zf, x1, y0, zf, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.86f, 0);
            if (!full[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zf, x0, y1, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.45f, 0);
            if (!full[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, y0, zf, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.66f, 0);
            if (!full[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, y0, zb, x1, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.66f, 0);
        }
        // solid objects (doors, elevator, statues): slabs through the block depth, depth-tested with the blocks
        findSlabs(f, u8(f[0x74]) & 15);
        for (int s = 0; s < 64; s++) {
            int o = 0x200 + s * 4, y = u8(f[o]) + 1, t = u8(f[o + 1]), at = u8(f[o + 2]), x = u8(f[o + 3]);
            if (y >= 240 || !slab[s] || isHud(y, t, x)) continue;
            uvRect(4 + (at & 3), t);
            float ua = (at & 0x40) != 0 ? u1 : u0, ub = (at & 0x40) != 0 ? u0 : u1, va = (at & 0x80) != 0 ? v1 : v0, vb = (at & 0x80) != 0 ? v0 : v1;
            float X0 = x, X1 = x + 8, Y0 = -y, Y1 = -y - 8;
            for (float zz = zb + 0.5f; zz < zf; zz += 1f)
                quad(X0, Y1, zz, X1, Y1, zz, X1, Y0, zz, X0, Y0, zz, ua, vb, ub, vb, ub, va, ua, va, 0.72f, 2);
            quad(X0, Y1, zf, X1, Y1, zf, X1, Y0, zf, X0, Y0, zf, ua, vb, ub, vb, ub, va, ua, va, 1f, 2);
        }
        // backdrop plane, half-speed parallax. Texels are scaled for the plane's depth so one texel shows at the size
        // of one game pixel. 256x256 repeating texture.
        caveStart = nv; caveCount = 0;
        if (cave && bdOk) {
            float z = -D * 3 - 24, X0 = -1400, X1 = 1656, Yt = 900, Yb = -1300, k = 0.5f;
            float sc = (camDist - z) / camDist;
            float ua = (X0 - 128) / (256 * sc) + accX * k / 256f, ub = (X1 - 128) / (256 * sc) + accX * k / 256f;
            float va = (-Yt - 120) / (256 * sc) + accY * k / 256f, vb = (-Yb - 120) / (256 * sc) + accY * k / 256f;
            quad(X0, Yb, z, X1, Yb, z, X1, Yt, z, X0, Yt, z, ua, vb, ub, vb, ub, va, ua, va, 1, 3);
            caveCount = nv - caveStart;
        }
        // see-through blocks (passable tiles that look solid), drawn blended after the scene
        transStart = nv;
        for (int i = 0; i < nTrans; i++) {
            int r = transCells[i * 2] / 64, c = transCells[i * 2] % 64, p = transCells[i * 2 + 1] >> 8, t = transCells[i * 2 + 1] & 255;
            float x0 = (tx0 + c) * 8 - ox, x1 = x0 + 8, y0 = -((ty0 + r) * 8 - oy), y1 = y0 - 8;
            uvRect(p, t);
            quad(x0, y1, zf, x1, y1, zf, x1, y0, zf, x0, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 1.1f, 6);
            if (!solid[r][c + 1] && !pass[r][c + 1]) quad(x0, y0, zf, x1, y0, zf, x1, y0, zb, x0, y0, zb, u0, v0, u1, v0, u1, v1, u0, v1, 0.95f, 6);
            if (!solid[r + 2][c + 1] && !pass[r + 2][c + 1]) quad(x0, y1, zb, x1, y1, zb, x1, y1, zf, x0, y1, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.6f, 6);
            if (!solid[r + 1][c] && !pass[r + 1][c]) quad(x0, y1, zb, x0, y1, zf, x0, y0, zf, x0, y0, zb, u0, v1, u1, v1, u1, v0, u0, v0, 0.75f, 6);
            if (!solid[r + 1][c + 2] && !pass[r + 1][c + 2]) quad(x1, y1, zf, x1, y1, zb, x1, y0, zb, x1, y0, zf, u0, v1, u1, v1, u1, v0, u0, v0, 0.75f, 6);
        }
        transCount = nv - transStart;
        // Other sprites: cut-outs in the middle of the block depth, drawn last WITHOUT depth testing, so anything that
        // overlaps terrain in the original (crawlers on walls, items on pedestals) is never cut off by a block.
        spriteStart = nv;
        boolean found = false;
        {   // light: centre of Samus = the palette-0 sprites around the lowest-numbered one (was the top of one tile)
            int ax = -1, ay = 0, bx0 = 999, by0 = 999, bx1 = -999, by1 = -999;
            for (int s = 0; s < 64; s++) {
                int o = 0x200 + s * 4, y = u8(f[o]) + 1, t = u8(f[o + 1]), at = u8(f[o + 2]), x = u8(f[o + 3]);
                if (y >= 240 || (at & 3) != 0 || isHud(y, t, x) || slab[s]) continue;
                if (ax < 0) { ax = x; ay = y; }
                if (Math.abs(x - ax) > 24 || Math.abs(y - ay) > 40) continue;
                bx0 = Math.min(bx0, x); by0 = Math.min(by0, y); bx1 = Math.max(bx1, x + 8); by1 = Math.max(by1, y + 8);
            }
            int[] sam = samusOnScreen(f);
            float tx = lightX, ty = lightY;
            if (sam != null) { found = true; tx = sam[0]; ty = -sam[1]; }               // from the game's own RAM
            else if (ax >= 0) { found = true; tx = (bx0 + bx1) / 2f; ty = -(by0 + by1) / 2f; }
            // ease toward the target: Samus never moves far in a frame, so a stray detection can't strobe the room
            float k = lightAmt < 0.05f ? 1f : 0.35f;
            lightX += (tx - lightX) * k; lightY += (ty - lightY) * k;
            lightMiss = found ? 0 : Math.min(lightMiss + 1, 999);
            hasLight = lightMiss < 45;
            lightAmt += ((hasLight ? 1f : 0f) - lightAmt) * 0.08f;
        }
        for (int s = 63; s >= 0; s--) {          // back to front, so lower OAM slots end up on top like the NES
            int o = 0x200 + s * 4, y = u8(f[o]) + 1, t = u8(f[o + 1]), at = u8(f[o + 2]), x = u8(f[o + 3]);
            if (y >= 240 || isHud(y, t, x) || slab[s]) continue;
            uvRect(4 + (at & 3), t);
            float ua = (at & 0x40) != 0 ? u1 : u0, ub = (at & 0x40) != 0 ? u0 : u1, va = (at & 0x80) != 0 ? v1 : v0, vb = (at & 0x80) != 0 ? v0 : v1;
            float X0 = x, X1 = x + 8, Y0 = -y, Y1 = -y - 8;
            quad(X0, Y1, 0, X1, Y1, 0, X1, Y0, 0, X0, Y0, 0, ua, vb, ub, vb, ub, va, ua, va, 1f, (at & 0x20) != 0 ? 5 : 2);
        }
        // HUD sprites in viewport pixels (drawn with an orthographic matrix)
        hudStart = nv;
        float asp = 256f * 8 / 7 / 224f, rw = W, rh = W / asp;
        if (rh > H) { rh = H; rw = H * asp; }
        float rx = (W - rw) / 2, ry = (H - rh) / 2, sx = rw / 256f, sy = rh / 224f;
        for (int s = 0; s < 64; s++) {
            int o = 0x200 + s * 4, y = u8(f[o]) + 1, t = u8(f[o + 1]), at = u8(f[o + 2]), x = u8(f[o + 3]);
            if (y >= 240 || !isHud(y, t, x) || t == 0xFF) continue;
            uvRect(4 + (at & 3), t);
            float ua = (at & 0x40) != 0 ? u1 : u0, ub = (at & 0x40) != 0 ? u0 : u1, va = (at & 0x80) != 0 ? v1 : v0, vb = (at & 0x80) != 0 ? v0 : v1;
            float X0 = rx + x * sx, X1 = rx + (x + 8) * sx, Y0 = ry + (y - 8) * sy, Y1 = ry + (y + 0) * sy;
            quad(X0, Y1, 0, X1, Y1, 0, X1, Y0, 0, X0, Y0, 0, ua, vb, ub, vb, ub, va, ua, va, 1f, 4);
        }
    }

    @Override public void onDrawFrame(GL10 unused) {
        synchronized (lock) {
            if (fresh) { byte[] t = current; current = pending; pending = t; fresh = false; }
        }
        byte[] f = current;
        view(f);
        buildAtlas(f);
        followScroll();
        buildScene(f);
        if (cave) buildBackdrop(f);    // after buildScene: uses its emptyTile[]; the plane shows from the next frame

        // camera eases toward the preset
        float[] pr = preset(camPreset);
        pitch += (pr[0] - pitch) * 0.12f; yaw += (pr[1] - yaw) * 0.12f; zoom += (pr[2] - zoom) * 0.12f;
        int bgc = rgb(u8(f[PAL]));
        float br = ((bgc >> 16) & 255) / 255f, bg = ((bgc >> 8) & 255) / 255f, bb = (bgc & 255) / 255f;
        float[] tint = tint(f);
        float fr = 0, fg = 0, fbb = 0;                       // fades go to NES black ($0F)
        GLES20.glClearColor(fr, fg, fbb, 1);
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glEnable(GLES20.GL_CULL_FACE);

        float fov = 32f, asp = W / (float) H;
        float halfH = Math.max(112f, 128f * 8 / 7 / asp) / zoom;
        float dist = halfH / (float) Math.tan(Math.toRadians(fov / 2)) + depth;
        camDist = dist;
        double p = Math.toRadians(pitch), y = Math.toRadians(yaw);
        float cx = 128, cy = -120;
        float ex = cx + (float) (dist * Math.sin(y) * Math.cos(p)), ey = cy + (float) (dist * Math.sin(p)), ez = (float) (dist * Math.cos(y) * Math.cos(p));
        float[] proj = new float[16], viewM = new float[16], stretch = new float[16], tmp = new float[16], mvp = new float[16];
        Matrix.perspectiveM(proj, 0, fov, asp, 10, 5000);
        Matrix.setLookAtM(viewM, 0, ex, ey, ez, cx, cy, 0, 0, 1, 0);
        Matrix.setIdentityM(stretch, 0);
        stretch[0] = 8f / 7f; stretch[12] = 128 - 128 * 8f / 7f;     // NES pixels are 8:7
        Matrix.multiplyMM(tmp, 0, viewM, 0, stretch, 0);
        Matrix.multiplyMM(mvp, 0, proj, 0, tmp, 0);

        if (fb.capacity() < nv * 7) fb = ByteBuffer.allocateDirect(verts.length * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        fb.clear(); fb.put(verts, 0, nv * 7); fb.position(0);
        GLES20.glUseProgram(prog);
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo);
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, nv * 7 * 4, fb, GLES20.GL_STREAM_DRAW);
        int stride = 7 * 4;
        GLES20.glEnableVertexAttribArray(aPos); GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, stride, 0);
        GLES20.glEnableVertexAttribArray(aUv); GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, stride, 12);
        GLES20.glEnableVertexAttribArray(aShade); GLES20.glVertexAttribPointer(aShade, 1, GLES20.GL_FLOAT, false, stride, 20);
        GLES20.glEnableVertexAttribArray(aKind); GLES20.glVertexAttribPointer(aKind, 1, GLES20.GL_FLOAT, false, stride, 24);
        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0);
        GLES20.glUniform3f(uBg, br, bg, bb);
        GLES20.glUniform1i(uEdge, 1);                         // clip to the NES screen window
        GLES20.glUniform1f(uLightOn, light ? lightAmt : 0);
        GLES20.glUniform2f(uLight, lightX, lightY);
        GLES20.glUniform3f(uTint, tint[0], tint[1], tint[2]);
        GLES20.glUniform3f(uFog, fr, fg, fbb);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, atlasTex); GLES20.glUniform1i(uTex, 0);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE1); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, caveTex); GLES20.glUniform1i(uCave, 1);
        if (maskDirty) {
            mask.position(0);
            GLES20.glActiveTexture(GLES20.GL_TEXTURE2); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, maskTex);
            GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
            GLES20.glTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0, 256, 240, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, mask);
            maskDirty = false;
        }
        GLES20.glActiveTexture(GLES20.GL_TEXTURE2); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, maskTex); GLES20.glUniform1i(uMask, 2);
        GLES20.glUniform1i(uMode, 0); GLES20.glUniform1f(uAlpha, 1f);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, caveStart);
        if (caveCount > 0) { GLES20.glUniform1i(uMode, 1); GLES20.glDrawArrays(GLES20.GL_TRIANGLES, caveStart, caveCount); }
        if (transCount > 0) {   // see-through blocks: blended, no depth writes
            GLES20.glUniform1i(uMode, 0); GLES20.glUniform1f(uAlpha, 0.42f);
            GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA);
            GLES20.glDepthMask(false);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, transStart, transCount);
            GLES20.glDepthMask(true); GLES20.glDisable(GLES20.GL_BLEND); GLES20.glUniform1f(uAlpha, 1f);
        }
        if (hudStart > spriteStart) {   // sprites (after the see-through blocks, so Samus shows clearly inside them)
            GLES20.glUniform1i(uMode, 0);
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, spriteStart, hudStart - spriteStart);
            GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        }
        if (nv > hudStart) {    // HUD: orthographic, viewport pixels, no depth
            float[] ortho = new float[16];
            Matrix.orthoM(ortho, 0, 0, W, H, 0, -1, 1);
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);
            GLES20.glDisable(GLES20.GL_CULL_FACE);
            GLES20.glUniformMatrix4fv(uMvp, 1, false, ortho, 0);
            GLES20.glUniform1i(uMode, 2);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, hudStart, nv - hudStart);
        }
    }

    private static float[] preset(String n) {
        switch (n) {
            case "diorama": return new float[] { 30, -24, 0.92f };
            case "side": return new float[] { 0, 0, 1 };
            case "low": return new float[] { -10, 18, 1.05f };
            case "far": return new float[] { 22, 0, 0.62f };
            default: return new float[] { 16, 0, 1 };
        }
    }

    private static float[] tint(byte[] f) {
        float[] best = { .5f, .5f, .5f }; int bl = -1;
        for (int i = 1; i < 4; i++) {
            int c = rgb(u8(f[PAL + i])), l = ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255);
            if (l > bl) { bl = l; best = new float[] { ((c >> 16) & 255) / 255f, ((c >> 8) & 255) / 255f, (c & 255) / 255f }; }
        }
        return best;
    }

    /** Samus's centre on screen from RAM (object slot $0300: $0300 action, $030C nametable, $030D Y, $030E X), or
     *  null outside normal play. Colour matching alone also caught frozen enemies and beam shots. $030C is the
     *  nametable along the room's scroll direction, so take whichever reading puts her on the screen. */
    private int[] samusOnScreen(byte[] f) {
        int mode = u8(f[0x1E]);                        // 3 = in play, 8 = Samus's entrance
        if (u8(f[0x1D]) != 0 || (mode != 3 && mode != 8) || u8(f[0x300]) == 0) return null;
        int h = u8(f[0x30C]) & 1, X = u8(f[0x30E]), Y = u8(f[0x30D]);
        // h is the nametable along the scroll direction. In side-scrolling rooms the vertical nametable bit can be
        // set while the screen doesn't scroll vertically (mirrored), so the height is also tried modulo 240.
        for (int k = 0; k < 4; k++) {
            int sx = Math.floorMod(X + ((k & 1) == 0 ? 256 * h : 0) - ox, 512);
            int sy = Math.floorMod(Y + ((k & 1) == 1 ? 240 * h : 0) - oy, 480);
            if (k >= 2) sy %= 240;
            if (sx < 256 && sy < 240) return new int[] { sx, sy };
        }
        return null;
    }
    private static int fdiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
}
