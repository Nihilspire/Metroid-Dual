package com.jack.metroiddual;

import android.content.Context;
import android.graphics.Rect;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.json.JSONObject;

import java.util.List;

/**
 * Places the game pane and the Link pane.
 *  - Surface Duo spanned across both screens: one pane per screen, split at the hinge (DisplayMask API).
 *  - Split forced in settings ("lr"/"tb"): halves of the window.
 *  - Second display (handheld with two screens, external monitor): Link goes to a Presentation; game fills this window.
 *  - Single screen: game fills the window (16:9 borders around it); optional Link side panel or full overlay.
 */
final class DualLayout extends ViewGroup {
    final MainActivity act;
    int split;          // 0 none, 1 left|right, 2 top/bottom
    boolean side;       // Link side panel on a single screen
    Rect hinge;         // hinge in window coordinates, if any
    String how = "single", hingeSource = "";

    DualLayout(MainActivity a) { super(a); act = a; }

    // Pane rectangles, computed once per pass and used for BOTH measuring and placing. (Measuring every pane at the
    // full window size and then placing it in half the window left its contents full-size and cut off: the game
    // centred on the hinge and the Link page running off the bottom on a spanned Surface Duo.)
    private Rect gRect = new Rect(), kRect;
    private int linkMode;          // 0 hidden, 1 in its own pane, 2 overlay over everything

    @Override protected void onMeasure(int ws, int hs) {
        int w = MeasureSpec.getSize(ws), h = MeasureSpec.getSize(hs);
        setMeasuredDimension(w, h);
        compute(w, h);
        measurePane(act.gamePane, gRect.width(), gRect.height());
        if (linkMode == 1) measurePane(act.linkPane, kRect.width(), kRect.height());
        else if (linkMode == 2) measurePane(act.linkPane, w, h);
        else measurePane(act.linkPane, 0, 0);
    }

    private static void measurePane(View v, int w, int h) {
        v.measure(MeasureSpec.makeMeasureSpec(Math.max(0, w), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(Math.max(0, h), MeasureSpec.EXACTLY));
    }

    @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int W = r - l, H = b - t;
        compute(W, H);
        View game = act.gamePane, link = act.linkPane;
        game.layout(gRect.left, gRect.top, gRect.right, gRect.bottom);
        if (linkMode == 1) { if (link.getVisibility() != VISIBLE) link.setVisibility(VISIBLE); link.layout(kRect.left, kRect.top, kRect.right, kRect.bottom); }
        else if (linkMode == 2) { if (link.getVisibility() != VISIBLE) link.setVisibility(VISIBLE); link.layout(0, 0, W, H); }
        else { if (link.getVisibility() != GONE) link.setVisibility(GONE); link.layout(0, 0, 0, 0); }
        act.setHud();
    }

    private void compute(int W, int H) {
        String mode = act.prefs.getString("split", "auto");
        boolean swap = act.prefs.getBoolean("swap", false);
        hinge = null; split = 0; side = false;
        Rect g = new Rect(0, 0, W, H), k = null;
        hingeSource = "";
        if (mode.equals("auto")) {
            hinge = findHinge(W, H);
            if (hinge != null) hingeSource = "displaymask";
            else { hinge = duoFallback(W, H); if (hinge != null) hingeSource = "duo model"; }
            // The Duo's two screens are equal halves of a spanned window, so the window's shape decides the hinge's
            // direction: taller than wide = one screen above the other (laptop posture), horizontal hinge; wider than
            // tall = side by side (book posture), vertical hinge. Only the hinge thickness is taken from the API, whose
            // rectangle can come back in the unrotated orientation.
            if (hinge != null) {
                int th = Math.max(0, Math.min(200, Math.min(hinge.width(), hinge.height())));
                hinge = H > W ? new Rect(0, (H - th) / 2, W, (H - th) / 2 + th) : new Rect((W - th) / 2, 0, (W - th) / 2 + th, H);
            }
        }
        if (hinge != null) {
            if (hinge.height() > hinge.width() || (hinge.height() == hinge.width() && W >= H)) { split = 1; g.set(0, 0, hinge.left, H); k = new Rect(hinge.right, 0, W, H); }
            else { split = 2; g.set(0, 0, W, hinge.top); k = new Rect(0, hinge.bottom, W, H); }
            how = "hinge";
        } else if (mode.equals("lr") || mode.equals("tb")) {
            int gap = Math.round(act.prefs.getInt("gap", 0) * getResources().getDisplayMetrics().density);
            if (mode.equals("lr")) { split = 1; g.set(0, 0, (W - gap) / 2, H); k = new Rect((W + gap) / 2, 0, W, H); }
            else { split = 2; g.set(0, 0, W, (H - gap) / 2); k = new Rect(0, (H + gap) / 2, W, H); }
            how = "split";
        } else if (act.presentation != null) {
            how = "external";
        } else if (act.prefs.getBoolean("side", false) && W > H) {
            int gw = Math.min(Math.round(H * 1.36f), W * 2 / 3);
            if (W - gw >= 200 * getResources().getDisplayMetrics().density * 0.9f) {
                side = true;
                g.set(0, 0, gw, H); k = new Rect(gw, 0, W, H);
                how = "side";
            } else how = "single";
        } else how = "single";
        if (k != null && swap) { Rect tmp = g; g = k; k = tmp; }
        gRect = g; kRect = k;
        if (act.presentation != null && split == 0 && !side) linkMode = 0;
        else if (k != null) linkMode = 1;
        else if (act.linkOverlay) linkMode = 2;
        else linkMode = 0;
    }

    /** Surface Duo: com.microsoft.device.display.DisplayMask, through reflection (absent elsewhere). */
    private Rect findHinge(int W, int H) {
        try {
            Class<?> dm = Class.forName("com.microsoft.device.display.DisplayMask");
            Object mask = dm.getMethod("fromResourcesRectApproach", Context.class).invoke(null, getContext());
            WindowManager wm = (WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE);
            int rot = wm.getDefaultDisplay().getRotation();
            @SuppressWarnings("unchecked")
            List<Rect> rects = (List<Rect>) dm.getMethod("getBoundingRectsForRotation", int.class).invoke(mask, rot);
            if (rects == null || rects.isEmpty()) return null;
            Rect h = new Rect(rects.get(0));
            int[] loc = new int[2];
            getLocationOnScreen(loc);
            h.offset(-loc[0], -loc[1]);
            // Only a split if the app is spanned across both screens: the window's long side must be about two
            // screens plus the hinge (the hinge rect's own orientation isn't trusted, see onLayout).
            int longSide = Math.max(W, H), shortSide = Math.min(W, H);
            if (longSide < shortSide * 1.42f) return null;            // single Duo screens are at most ~1.41:1
            Rect inner = new Rect(20, 20, W - 20, H - 20);
            Rect flipped = new Rect(h.top, h.left, h.bottom, h.right);  // same rect if reported unrotated
            if (!Rect.intersects(inner, h) && !Rect.intersects(inner, flipped)) return null;
            return h;
        } catch (Throwable e) {
            return null;
        }
    }

    /**
     * Surface Duo without a usable DisplayMask (some system versions): recognise the device and a spanned window by
     * size, and put the known hinge in the middle. Single screens: Duo 1350x1800, Duo 2 1344x1892 (px). Hinge: Duo 84 px,
     * Duo 2 66 px. A spanned window is two screens plus the hinge along its long side.
     */
    private Rect duoFallback(int W, int H) {
        String model = (android.os.Build.MODEL == null ? "" : android.os.Build.MODEL).toLowerCase();
        String maker = (android.os.Build.MANUFACTURER == null ? "" : android.os.Build.MANUFACTURER).toLowerCase();
        if (!model.contains("duo") || !maker.contains("microsoft")) return null;
        boolean duo2 = model.contains("2");
        int hingePx = duo2 ? 66 : 84, shortSide = duo2 ? 1344 : 1350;
        int[] loc = new int[2];
        getLocationOnScreen(loc);
        // the window spans both screens if its long side is about two screens + hinge
        if (W >= H && W >= 2 * shortSide - 40) {
            int left = shortSide - loc[0];
            if (left <= 0 || left + hingePx >= W) left = (W - hingePx) / 2;
            return new Rect(left, 0, left + hingePx, H);
        }
        if (H > W && H >= 2 * shortSide - 40) {
            int top = shortSide - loc[1];
            if (top <= 0 || top + hingePx >= H) top = (H - hingePx) / 2;
            return new Rect(0, top, W, top + hingePx);
        }
        return null;
    }

    JSONObject describe() {
        JSONObject o = new JSONObject();
        try {
            o.put("how", how);
            o.put("source", hingeSource);
            o.put("size", getWidth() + "x" + getHeight());
            o.put("model", android.os.Build.MODEL);
            o.put("split", split);
            o.put("external", act.presentation != null);
            o.put("rotation", rotationName());
        } catch (Exception ignored) {}
        return o;
    }

    private String rotationName() {
        WindowManager wm = (WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE);
        int r = wm.getDefaultDisplay().getRotation();
        return r == Surface.ROTATION_0 ? "0" : r == Surface.ROTATION_90 ? "90" : r == Surface.ROTATION_180 ? "180" : "270";
    }
}
