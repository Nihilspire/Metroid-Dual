package com.jack.metroiddual;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

/**
 * Non-blocking audio with dynamic rate control: the game runs locked to the display, and the sound is
 * resampled by a fraction of a percent so the buffer stays half full (no pops, no drift, no pacing by audio).
 */
final class AudioOut {
    private final AudioTrack track;
    final int rate, bufFrames;
    private long written;              // frames written
    private final short[] out = new short[16384];
    private double phase;              // position between input frames
    private short lastL, lastR;
    volatile double fill;              // 0..1, for the stats overlay

    AudioOut(int rate) {
        this.rate = rate;
        int min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        int bytes = Math.max(min * 2, rate * 4 * 80 / 1000);          // ~80 ms
        track = new AudioTrack(AudioManager.STREAM_MUSIC, rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT,
                bytes, AudioTrack.MODE_STREAM);
        bufFrames = bytes / 4;
        // pre-fill half the buffer with silence so the first frames don't underrun
        short[] z = new short[bufFrames];             // half the buffer in frames x2 channels
        int w = track.write(z, 0, z.length);
        written = w > 0 ? w / 2 : 0;
    }

    void play() { try { track.play(); } catch (Exception ignored) {} }
    void pause() { try { track.pause(); } catch (Exception ignored) {} }
    void release() { try { track.release(); } catch (Exception ignored) {} }

    long pending() {
        long played = track.getPlaybackHeadPosition() & 0xFFFFFFFFL;
        long p = written - played;
        return p < 0 ? 0 : p;
    }

    /**
     * in: interleaved stereo, n shorts. baseRatio = output frames per input frame before correction
     * (core fps / actual emulated fps).
     */
    void write(short[] in, int n, double baseRatio) {
        int frames = n / 2;
        if (frames <= 0) return;
        long pend = pending();
        fill = pend / (double) bufFrames;
        double err = (bufFrames / 2.0 - pend) / (bufFrames / 2.0);     // + when too empty
        if (err > 1) err = 1; else if (err < -1) err = -1;
        double ratio = baseRatio * (1 + 0.005 * err);
        double step = 1.0 / ratio;                                      // input frames per output frame
        int o = 0;
        while (o + 2 <= out.length) {
            int i = (int) phase;
            if (i >= frames) break;
            double f = phase - i;
            int l0 = i == 0 ? lastL : in[(i - 1) * 2], r0 = i == 0 ? lastR : in[(i - 1) * 2 + 1];
            int l1 = in[i * 2], r1 = in[i * 2 + 1];
            out[o++] = (short) (l0 + (l1 - l0) * f);
            out[o++] = (short) (r0 + (r1 - r0) * f);
            phase += step;
        }
        phase -= frames;
        lastL = in[(frames - 1) * 2]; lastR = in[(frames - 1) * 2 + 1];
        // never block: drop what doesn't fit (only happens if the device stalls)
        int room = (int) (bufFrames - pend) * 2;
        int len = Math.min(o, Math.max(0, room));
        if (len > 0) {
            int w = track.write(out, 0, len, AudioTrack.WRITE_NON_BLOCKING);
            if (w > 0) written += w / 2;
        }
    }

    /** Pauses can drift the counters; re-prime when resuming. */
    void resync() {
        try { track.pause(); track.flush(); } catch (Exception ignored) {}
        written = track.getPlaybackHeadPosition() & 0xFFFFFFFFL;
        short[] z = new short[bufFrames];
        int w = track.write(z, 0, z.length, AudioTrack.WRITE_NON_BLOCKING);
        if (w > 0) written += w / 2;
        phase = 0;
    }
}
