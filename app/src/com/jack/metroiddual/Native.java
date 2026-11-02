package com.jack.metroiddual;

import java.nio.ByteBuffer;

/** JNI entry points into libmetroiddual.so (libretro-fceumm + glue in native/nesjni.c). */
public final class Native {
    static { System.loadLibrary("metroiddual"); }
    private Native() {}
    public static native int init(String sysDir, String saveDir);
    public static native int load(String romPath);
    public static native void unload();
    public static native void reset();
    public static native double fps();
    public static native double rate();
    public static native void setPalette(String name);
    public static native void setOverclock(boolean on);
    /** Runs one frame; returns (w << 16 | h) when a new picture was copied into dst. */
    public static native int runFrame(int pad, ByteBuffer dst);
    public static native int audio(short[] out);
    public static native int peek(int addr);
    public static native void poke(int addr, int value);
    /** "6876:16,4F:2" -> hex string of the bytes, in order. */
    public static native String mem(String spec);
    /** PPU $0000-$3FFF into a 16 KB array. */
    public static native void ppu(byte[] out);
    /** RAM 0x800 | nametables 0x1000 | palette 0x20 | CHR 0x2000 (0x3820 bytes) for the 3D view. */
    public static native void snapshot(byte[] out);
    public static native int stateSave(String path);
    public static native int stateLoad(String path);
}
