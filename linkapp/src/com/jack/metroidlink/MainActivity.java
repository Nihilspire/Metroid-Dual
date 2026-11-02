package com.jack.metroidlink;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.Base64;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.EditText;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.CRC32;

/**
 * Metroid Link: the Metroid Dual companion screen on its own, for a game running in RetroArch (FCEUmm core).
 * The Link page (assets/link, shared with Metroid Dual) reads the game through window.Native, which here is
 * backed by RetroArch's network commands instead of a built-in emulator. The player's ROM is needed for the map,
 * items and graphics; it stays on the device.
 */
public class MainActivity extends Activity {
    static final String VERSION = "0.3";
    static final long METROID_PRG_CRC = 0x70080810L;
    static final int PICK_ROM = 1, SLOTS = 4;

    final RetroArch ra = new RetroArch();
    final Handler ui = new Handler(Looper.getMainLooper());
    SharedPreferences prefs;
    WebView web;
    File romFile;
    volatile byte[] rom;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        prefs = getSharedPreferences("metroidlink", MODE_PRIVATE);
        romFile = new File(getFilesDir(), "rom.nes");
        loadRom();
        ra.host = prefs.getString("host", "127.0.0.1");
        ra.port = prefs.getInt("port", 55355);
        ra.start();

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        web.setBackgroundColor(0xFF000000);
        web.setVerticalScrollBarEnabled(false); web.setHorizontalScrollBarEnabled(false);
        web.addJavascriptInterface(new Bridge(), "Native");
        web.loadUrl("file:///android_asset/link/index.html");
        setContentView(web);
        hideBars();
    }

    @Override protected void onResume() { super.onResume(); hideBars(); }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) hideBars(); }
    @Override protected void onDestroy() { ra.stop(); super.onDestroy(); }

    @SuppressWarnings("deprecation")
    void hideBars() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    // ------------------------------------------------------------------ ROM
    void loadRom() {
        rom = null;
        if (!romFile.exists()) return;
        try {
            byte[] b = new byte[(int) romFile.length()];
            InputStream in = new FileInputStream(romFile); int n = 0; while (n < b.length) { int r = in.read(b, n, b.length - n); if (r < 0) break; n += r; } in.close();
            if (b.length >= 16 + 0x20000) rom = b;
        } catch (Exception ignored) {}
    }
    void pickRom() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try { startActivityForResult(i, PICK_ROM); } catch (Exception e) { Toast.makeText(this, "No file picker available", Toast.LENGTH_LONG).show(); }
    }
    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != PICK_ROM || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        try {
            InputStream in = getContentResolver().openInputStream(u);
            ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] buf = new byte[65536]; int r;
            while ((r = in.read(buf)) > 0 && o.size() < 0x100000) o.write(buf, 0, r);
            in.close();
            byte[] b = o.toByteArray();
            if (b.length < 16 + 0x20000 || b[0] != 'N' || b[1] != 'E' || b[2] != 'S') { Toast.makeText(this, "That isn't an NES ROM", Toast.LENGTH_LONG).show(); return; }
            CRC32 crc = new CRC32(); crc.update(b, 16, b.length - 16);
            if (crc.getValue() != METROID_PRG_CRC) Toast.makeText(this, "Not Metroid (USA) - the map may be wrong", Toast.LENGTH_LONG).show();
            FileOutputStream f = new FileOutputStream(romFile); f.write(b); f.close();
            loadRom();
            web.evaluateJavascript("window.onRom&&onRom()", null);
        } catch (Exception e) { Toast.makeText(this, "Couldn't read the file", Toast.LENGTH_LONG).show(); }
    }

    void editHost() {
        final EditText t = new EditText(this);
        t.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        t.setText(ra.host + ":" + ra.port);
        t.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this)
            .setTitle("RetroArch address")
            .setMessage("127.0.0.1 when RetroArch runs on this device. In RetroArch: Settings > Network > Network Commands ON (port 55355).")
            .setView(t)
            .setPositiveButton("OK", new android.content.DialogInterface.OnClickListener() { public void onClick(android.content.DialogInterface d, int w) {
                String v = t.getText().toString().trim(); String h = v; int p = 55355;
                int c = v.lastIndexOf(':');
                if (c > 0) { h = v.substring(0, c); try { p = Integer.parseInt(v.substring(c + 1)); } catch (NumberFormatException ignored) {} }
                if (h.isEmpty()) h = "127.0.0.1";
                ra.host = h; ra.port = p;
                prefs.edit().putString("host", h).putInt("port", p).apply();
            } })
            .setNegativeButton("Cancel", null)
            .show();
    }

    // ------------------------------------------------------------------ window.Native
    final class Bridge {
        @JavascriptInterface public String version() { return VERSION; }
        @JavascriptInterface public boolean dual() { return true; }
        @JavascriptInterface public boolean running() { return ra.running(); }
        @JavascriptInterface public String mem(String spec) { return ra.mem(spec); }
        @JavascriptInterface public String pal() { return ra.pal(); }
        @JavascriptInterface public String chr() { return ""; }        // pattern tables aren't exposed; sprites come from the ROM
        @JavascriptInterface public String rom(int off, int len) {
            byte[] r = rom;
            if (r == null || off < 0 || len < 0 || off + len > r.length) return "";
            return Base64.encodeToString(r, off, len, Base64.NO_WRAP);
        }
        @JavascriptInterface public boolean overlay() { return false; }
        @JavascriptInterface public void close() {}
        @JavascriptInterface public boolean devUsed() { return false; }
        @JavascriptInterface public String slots() {
            try {
                JSONArray a = new JSONArray();
                for (int i = 1; i <= SLOTS; i++) { JSONObject o = new JSONObject(); o.put("n", i); o.put("time", prefs.getLong("slot" + i, 0)); a.put(o); }
                return a.toString();
            } catch (Exception e) { return "[]"; }
        }
        @JavascriptInterface public boolean saveSlot(int n) {
            if (!ra.connected() || n < 1 || n > SLOTS) return false;
            ra.command("SAVE_STATE_SLOT " + n);
            prefs.edit().putLong("slot" + n, System.currentTimeMillis()).apply();
            return true;
        }
        @JavascriptInterface public boolean loadSlot(int n) {
            if (!ra.connected() || n < 1 || n > SLOTS) return false;
            ra.command("LOAD_STATE_SLOT " + n);
            return true;
        }
        @JavascriptInterface public String thumb(int n) { return ""; }
        @JavascriptInterface public void menu() { ra.command("MENU_TOGGLE"); }
        @JavascriptInterface public String raStatus() { return ra.statusText(); }
        @JavascriptInterface public String raHost() { return ra.host + ":" + ra.port; }
        @JavascriptInterface public void raSetHost() { ui.post(new Runnable() { public void run() { editHost(); } }); }
        @JavascriptInterface public void pickRom() { ui.post(new Runnable() { public void run() { MainActivity.this.pickRom(); } }); }
    }
}
