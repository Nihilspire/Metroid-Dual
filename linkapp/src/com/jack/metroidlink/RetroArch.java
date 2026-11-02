package com.jack.metroidlink;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Talks to RetroArch over its network command interface (UDP, default port 55355; RetroArch: Settings > Network >
 * Network Commands ON). A background thread keeps a cache of the RAM ranges the Link page asks for, so the page never
 * waits on the network:
 *   READ_CORE_MEMORY <hex addr> <count>  ->  "READ_CORE_MEMORY <addr> XX XX ..."  or  "... -1 <error>"
 *   (falls back to READ_CORE_RAM for older RetroArch builds), GET_STATUS, SAVE_STATE_SLOT n, LOAD_STATE_SLOT n,
 *   MENU_TOGGLE. Addresses are the NES CPU map FCEUmm exposes: RAM $0000-$07FF, cartridge RAM $6000-$7FFF;
 *   palette RAM at $80003000.
 */
final class RetroArch {
    volatile String host = "127.0.0.1";
    volatile int port = 55355;

    private DatagramSocket sock;
    private InetAddress addr;
    private String addrFor = "";
    private volatile String spec = "", specCached = "", memHex = "", palHex = "";
    private volatile long lastOk, lastReply;
    private volatile String status = "", error = "";
    private volatile boolean useRam;          // READ_CORE_RAM instead of READ_CORE_MEMORY
    private Thread thread;
    private volatile boolean alive;

    void start() {
        if (thread != null) return;
        alive = true;
        thread = new Thread(new Runnable() { public void run() { loop(); } }, "retroarch");
        thread.setDaemon(true);
        thread.start();
    }
    void stop() {
        alive = false;
        if (thread != null) thread.interrupt();
        thread = null;
        synchronized (this) { if (sock != null) sock.close(); sock = null; }
    }

    /** The page's RAM request ("1d:2,2d:1,..." hex address : decimal length). Returns the newest cached bytes as
     *  hex in the same order, or "" until a full read for this request has arrived. */
    String mem(String s) {
        if (!s.equals(spec)) spec = s;
        return s.equals(specCached) && connected() ? memHex : "";
    }
    String pal() { return connected() ? palHex : ""; }
    boolean connected() { return System.currentTimeMillis() - lastOk < 1500; }
    /** RetroArch is answering and has a game loaded (the page then asks for memory). */
    boolean running() { return System.currentTimeMillis() - lastReply < 2000 && !status.startsWith("GET_STATUS CONTENTLESS"); }

    /** Two lines for the Link page's waiting screen, joined by "|". */
    String statusText() {
        String where = host + ":" + port;
        if (System.currentTimeMillis() - lastReply > 2000) return "WAITING FOR RETROARCH|" + where;
        if (status.startsWith("GET_STATUS CONTENTLESS")) return "RETROARCH: NO GAME RUNNING|" + where;
        if (!error.isEmpty()) return error + "|" + where;
        return "CONNECTED|" + where;
    }

    void command(final String cmd) {
        new Thread(new Runnable() { public void run() { send(cmd); } }).start();
    }

    // ------------------------------------------------------------------ network
    private synchronized boolean ensure() {
        try {
            String want = host;
            if (sock == null) { sock = new DatagramSocket(); sock.setSoTimeout(120); }
            if (addr == null || !want.equals(addrFor)) { addr = InetAddress.getByName(want); addrFor = want; }
            return true;
        } catch (Exception e) { error = "BAD HOST"; return false; }
    }
    private synchronized void send(String cmd) {
        if (!ensure()) return;
        try {
            byte[] b = cmd.getBytes(StandardCharsets.US_ASCII);
            sock.send(new DatagramPacket(b, b.length, addr, port));
        } catch (Exception ignored) {}
    }
    /** Sends cmd and waits for a reply starting with prefix (older stray replies are skipped). */
    private synchronized String ask(String cmd, String prefix) {
        if (!ensure()) return null;
        try {
            byte[] b = cmd.getBytes(StandardCharsets.US_ASCII);
            sock.send(new DatagramPacket(b, b.length, addr, port));
            byte[] buf = new byte[8192];
            long until = System.currentTimeMillis() + 150;
            while (System.currentTimeMillis() < until) {
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                try { sock.receive(p); } catch (SocketTimeoutException t) { return null; }
                String r = new String(p.getData(), 0, p.getLength(), StandardCharsets.US_ASCII).trim();
                lastReply = System.currentTimeMillis();
                if (r.startsWith(prefix)) return r;
            }
        } catch (Exception e) { /* unreachable host etc. */ }
        return null;
    }

    /** Reads n bytes at a, or null. */
    private byte[] read(long a, int n) {
        String hex = Long.toHexString(a);
        String cmd = useRam ? "READ_CORE_RAM" : "READ_CORE_MEMORY";
        String r = ask(String.format(Locale.US, "%s %s %d", cmd, hex, n), cmd + " " + hex + " ");
        if (r == null) {
            // RetroArch is answering other commands but not this one: try the other read command next time
            if (System.currentTimeMillis() - lastReply < 500) useRam = !useRam;
            return null;
        }
        String[] t = r.split(" ");
        if (t.length >= 3 && t[2].equals("-1")) {
            if (!useRam && r.contains("no memory map")) useRam = true;
            error = "CAN'T READ GAME MEMORY";
            return null;
        }
        if (t.length - 2 < n) return null;
        byte[] out = new byte[n];
        try { for (int i = 0; i < n; i++) out[i] = (byte) Integer.parseInt(t[2 + i], 16); } catch (NumberFormatException e) { return null; }
        return out;
    }

    private static final class Range { long a; int n; Range(long a, int n) { this.a = a; this.n = n; } }

    private void loop() {
        long nextStatus = 0, nextPal = 0;
        while (alive) {
            long t = System.currentTimeMillis();
            if (t >= nextStatus) {
                String r = ask("GET_STATUS", "GET_STATUS");
                if (r != null) status = r;
                nextStatus = t + 1000;
            }
            String s = spec;
            if (!s.isEmpty() && !status.startsWith("GET_STATUS CONTENTLESS")) {
                String hex = readSpec(s);
                if (hex != null) { memHex = hex; specCached = s; lastOk = System.currentTimeMillis(); error = ""; }
                if (hex != null && t >= nextPal) {
                    byte[] p = read(0x80003000L, 32);
                    if (p != null) { StringBuilder b = new StringBuilder(); for (byte v : p) b.append(String.format(Locale.US, "%02x", v & 255)); palHex = b.toString(); }
                    nextPal = t + 1000;
                }
            }
            try { Thread.sleep(connected() ? 30 : 250); } catch (InterruptedException e) { return; }
        }
    }

    /** Reads every range of the spec, merging close ranges in the same 1 KB page (RetroArch reads stop at the end of
     *  a memory descriptor, and FCEUmm maps memory in 1 KB pages), and returns hex in spec order. */
    private String readSpec(String s) {
        List<Range> want = new ArrayList<>();
        for (String part : s.split(",")) {
            String[] kv = part.split(":");
            try { want.add(new Range(Long.parseLong(kv[0], 16), kv.length > 1 ? Integer.parseInt(kv[1]) : 1)); } catch (Exception e) { return null; }
        }
        List<Range> sorted = new ArrayList<>(want);
        Collections.sort(sorted, new java.util.Comparator<Range>() { public int compare(Range x, Range y) { return Long.compare(x.a, y.a); } });
        List<Range> merged = new ArrayList<>();
        for (Range r : sorted) {
            Range m = merged.isEmpty() ? null : merged.get(merged.size() - 1);
            long end = r.a + r.n;
            if (m != null && r.a - (m.a + m.n) <= 128 && (m.a >> 10) == ((end - 1) >> 10) && end - m.a <= 512) {
                m.n = (int) Math.max(m.n, end - m.a);
            } else merged.add(new Range(r.a, r.n));
        }
        List<byte[]> data = new ArrayList<>();
        for (Range m : merged) { byte[] d = read(m.a, m.n); if (d == null) return null; data.add(d); }
        StringBuilder out = new StringBuilder();
        for (Range r : want) {
            for (int i = 0; i < merged.size(); i++) {
                Range m = merged.get(i);
                if (r.a >= m.a && r.a + r.n <= m.a + m.n) {
                    byte[] d = data.get(i);
                    for (int k = 0; k < r.n; k++) out.append(String.format(Locale.US, "%02x", d[(int) (r.a - m.a) + k] & 255));
                    break;
                }
            }
        }
        return out.toString();
    }
}
