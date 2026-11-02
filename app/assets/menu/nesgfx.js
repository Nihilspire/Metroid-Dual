/* nesgfx.js - draws NES tiles, the game's font and sprites onto a canvas. All graphics come from the
 * player's ROM (or live PPU memory) at runtime; the page ships none. */
(function (root) {
  var G = {};
  G.PAL = [0x747474,0x24188C,0x0000A8,0x44009C,0x8C0074,0xA80010,0xA40000,0x7C0800,0x402C00,0x004400,0x005000,0x003C14,0x183C5C,0,0,0,
    0xBCBCBC,0x0070EC,0x2038EC,0x8000F0,0xBC00BC,0xE40058,0xD82800,0xC84C0C,0x887000,0x009400,0x00A800,0x009038,0x008088,0,0,0,
    0xFCFCFC,0x3CBCFC,0x5C94FC,0xCC88FC,0xF478FC,0xFC74B4,0xFC7460,0xFC9838,0xF0BC3C,0x80D010,0x4CDC48,0x58F898,0x00E8D8,0x787878,0,0,
    0xFCFCFC,0xA8E4FC,0xC4D4FC,0xD4C8FC,0xFCC4FC,0xFCC4D8,0xFCBCB0,0xFCD8A8,0xFCE4A0,0xE0FCA0,0xA8F0BC,0xB0FCCC,0x9CFCF0,0xC4C4C4,0,0];
  G.css = function (i) { return '#' + ('00000' + G.PAL[i & 63].toString(16)).slice(-6); };
  G.b64 = function (s) { var b = atob(s || ''), a = new Uint8Array(b.length); for (var i = 0; i < b.length; i++) a[i] = b.charCodeAt(i); return a; };

  /** 2bpp tile -> 64 pixel values */
  G.decode = function (data, off) {
    var px = new Uint8Array(64);
    for (var y = 0; y < 8; y++) {
      var lo = data[off + y], hi = data[off + y + 8];
      for (var x = 0; x < 8; x++) px[y * 8 + x] = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
    }
    return px;
  };
  G.drawTile = function (ctx, px, x, y, colors, hflip, vflip) {
    for (var yy = 0; yy < 8; yy++) for (var xx = 0; xx < 8; xx++) {
      var v = px[(vflip ? 7 - yy : yy) * 8 + (hflip ? 7 - xx : xx)];
      if (v && colors[v]) { ctx.fillStyle = colors[v]; ctx.fillRect(x + xx, y + yy, 1, 1); }
    }
  };

  // ---- font: ROM tiles 0-63 (0-9 A-Z a-z ? -), any set pixel is ink. A few extra glyphs drawn here.
  var glyphs = null, cache = {};
  var EXTRA = { ':': [0, 0, 24, 24, 0, 24, 24, 0], '/': [2, 6, 12, 24, 48, 96, 64, 0], '.': [0, 0, 0, 0, 0, 24, 24, 0],
                '!': [24, 24, 24, 24, 24, 0, 24, 0], '%': [98, 102, 12, 24, 48, 102, 70, 0], '+': [0, 24, 24, 126, 24, 24, 0, 0],
                '>': [32, 48, 56, 60, 56, 48, 32, 0], '<': [4, 12, 28, 60, 28, 12, 4, 0], "'": [24, 24, 48, 0, 0, 0, 0, 0],
                ',': [0, 0, 0, 0, 0, 24, 24, 48], '(': [12, 24, 48, 48, 48, 24, 12, 0], ')': [48, 24, 12, 12, 12, 24, 48, 0],
                '#': [0, 0, 0, 0, 0, 0, 0, 0], '^': [24, 60, 126, 24, 24, 24, 24, 0], 'v': [24, 24, 24, 24, 126, 60, 24, 0] };
  G.setFont = function (data) {
    glyphs = {};
    var map = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz?-';
    for (var t = 0; t < 64; t++) {
      var px = G.decode(data, t * 16), m = new Uint8Array(64);
      for (var i = 0; i < 64; i++) m[i] = px[i] ? 1 : 0;
      glyphs[map[t]] = m;
    }
    for (var k in EXTRA) { var mm = new Uint8Array(64); for (var y = 0; y < 8; y++) for (var x = 0; x < 8; x++) mm[y * 8 + x] = (EXTRA[k][y] >> (7 - x)) & 1; glyphs[k] = mm; }
    cache = {};
  };
  G.hasFont = function () { return !!glyphs; };
  function glyphCanvas(ch, color) {
    var key = ch + color;
    if (cache[key]) return cache[key];
    var g = glyphs && glyphs[ch];
    if (!g) return null;
    var c = document.createElement('canvas'); c.width = 8; c.height = 8;
    var x = c.getContext('2d'); x.fillStyle = color;
    for (var i = 0; i < 64; i++) if (g[i]) x.fillRect(i & 7, i >> 3, 1, 1);
    return (cache[key] = c);
  }
  /** Text at 8 px per character. Falls back to a system font before a ROM is chosen. */
  G.text = function (ctx, s, x, y, color, scale) {
    scale = scale || 1;
    s = String(s);
    if (!glyphs) {
      ctx.fillStyle = color; ctx.font = 'bold ' + (8 * scale) + 'px monospace'; ctx.textBaseline = 'top';
      for (var j = 0; j < s.length; j++) ctx.fillText(s[j], x + j * 8 * scale, y);
      return;
    }
    for (var i = 0; i < s.length; i++) {
      var ch = s[i], gc = glyphCanvas(ch, color);
      if (!gc && ch >= 'a' && ch <= 'z') gc = glyphCanvas(ch.toUpperCase(), color);
      if (gc) ctx.drawImage(gc, x + i * 8 * scale, y, 8 * scale, 8 * scale);
    }
  };
  G.textC = function (ctx, s, cx, y, color, scale) { scale = scale || 1; G.text(ctx, s, Math.round(cx - String(s).length * 4 * scale), y, color, scale); };
  G.textR = function (ctx, s, rx, y, color, scale) { scale = scale || 1; G.text(ctx, s, rx - String(s).length * 8 * scale, y, color, scale); };

  /** 2x2 metasprite: tiles [tl, tr, bl, br], 0x100 bit = horizontal flip of that tile */
  G.meta = function (ctx, chr, tiles, x, y, colors) {
    for (var i = 0; i < 4; i++) {
      var t = tiles[i] & 255, hf = (tiles[i] & 0x100) !== 0;
      G.drawTile(ctx, G.decode(chr, t * 16), x + (i & 1) * 8, y + (i >> 1) * 8, colors, hf, false);
    }
  };
  root.NesGfx = G;
})(this);
