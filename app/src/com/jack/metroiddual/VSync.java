package com.jack.metroiddual;

import android.os.Handler;
import android.os.HandlerThread;
import android.view.Choreographer;

/** Display vsync ticks on their own thread (so UI work never delays them) plus a measured refresh period. */
final class VSync implements Choreographer.FrameCallback {
    private final Object lock = new Object();
    private long count, lastNanos;
    private volatile double periodNs = 1e9 / 60.0;
    private volatile boolean running;
    private Choreographer chor;
    private final Handler handler;

    VSync() {
        HandlerThread t = new HandlerThread("vsync", android.os.Process.THREAD_PRIORITY_DISPLAY);
        t.start();
        handler = new Handler(t.getLooper());
        handler.post(new Runnable() { public void run() { chor = Choreographer.getInstance(); } });
    }

    void start() {
        handler.post(new Runnable() { public void run() {
            if (running) return;
            running = true; lastNanos = 0;
            chor.postFrameCallback(VSync.this);
        } });
    }

    void stop() { handler.post(new Runnable() { public void run() { running = false; chor.removeFrameCallback(VSync.this); } }); }

    @Override public void doFrame(long nanos) {
        if (lastNanos != 0) {
            long d = nanos - lastNanos;
            // ignore skipped vsyncs; smooth the rest
            if (d > periodNs * 0.5 && d < periodNs * 1.5) periodNs = periodNs * 0.95 + d * 0.05;
            else if (d > 4_000_000 && d < 40_000_000 && count < 30) periodNs = d;
        }
        lastNanos = nanos;
        synchronized (lock) { count++; lock.notifyAll(); }
        if (running) chor.postFrameCallback(this);
    }

    /** Blocks until a vsync newer than 'seen' arrives; returns the vsync counter, or -1 on timeout. */
    long waitAfter(long seen, long timeoutMs) {
        long end = System.currentTimeMillis() + timeoutMs;
        synchronized (lock) {
            while (count <= seen) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) return -1;
                try { lock.wait(left); } catch (InterruptedException e) { return -1; }
            }
            return count;
        }
    }

    long count() { synchronized (lock) { return count; } }
    double hz() { return 1e9 / periodNs; }
}
