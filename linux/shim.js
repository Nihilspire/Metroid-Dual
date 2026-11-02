/* shim.js - the small slice of the browser that link.js / menu.js use, on top of native canvas functions.
 * Native side (jsshim.c): __cnew(w,h)->id, __csize(id,w,h), __cfill(id,x,y,w,h,rgba), __cdraw(dst,src,dx,dy,dw,dh),
 * __csave(id), __crestore(id), __cclip(id,x,y,w,h), __read(path), __write(path,text), __now(). */
(function (g) {
  'use strict';
  g.window = g;
  var colorCache = {};
  function parseColor(c) {
    if (typeof c !== 'string') return 0xFF000000;
    var v = colorCache[c];
    if (v !== undefined) return v;
    var r = 0, gg = 0, b = 0, a = 255, m;
    if (c[0] === '#') {
      if (c.length === 4) { r = parseInt(c[1] + c[1], 16); gg = parseInt(c[2] + c[2], 16); b = parseInt(c[3] + c[3], 16); }
      else { r = parseInt(c.substr(1, 2), 16); gg = parseInt(c.substr(3, 2), 16); b = parseInt(c.substr(5, 2), 16); }
    } else if ((m = /rgba?\(([^)]+)\)/.exec(c))) {
      var p = m[1].split(',').map(parseFloat);
      r = p[0]; gg = p[1]; b = p[2]; if (p.length > 3) a = Math.round(p[3] * 255);
    }
    v = ((a & 255) << 24 | (b & 255) << 16 | (gg & 255) << 8 | (r & 255)) >>> 0;
    colorCache[c] = v;
    return v;
  }
  function Ctx(canvas) { this.canvas = canvas; this.fillStyle = '#000'; this.font = ''; this.textBaseline = 'top'; this.imageSmoothingEnabled = false; this._path = null; }
  Ctx.prototype.fillRect = function (x, y, w, h) { __cfill(this.canvas._id, x, y, w, h, parseColor(this.fillStyle)); };
  Ctx.prototype.clearRect = function (x, y, w, h) { __cfill(this.canvas._id, x, y, w, h, 0, true); };
  Ctx.prototype.drawImage = function (img, x, y, w, h) {
    if (!img || img._id === undefined || img._id < 0) return;
    __cdraw(this.canvas._id, img._id, x, y, w === undefined ? img.width : w, h === undefined ? img.height : h);
  };
  Ctx.prototype.save = function () { __csave(this.canvas._id); };
  Ctx.prototype.restore = function () { __crestore(this.canvas._id); };
  Ctx.prototype.beginPath = function () { this._path = null; };
  Ctx.prototype.rect = function (x, y, w, h) { this._path = [x, y, w, h]; };
  Ctx.prototype.clip = function () { if (this._path) __cclip(this.canvas._id, this._path[0], this._path[1], this._path[2], this._path[3]); };
  Ctx.prototype.fillText = function () {};                // only used before a ROM is chosen
  Ctx.prototype.createImageData = function (w, h) { return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; };
  Ctx.prototype.putImageData = function () {};
  function Canvas(w, h) { this._w = w || 300; this._h = h || 150; this._id = __cnew(this._w, this._h); this.style = {}; this._ctx = null; this._ls = {}; }
  Object.defineProperty(Canvas.prototype, 'width', { get: function () { return this._w; }, set: function (v) { this._w = v | 0; __csize(this._id, this._w, this._h); } });
  Object.defineProperty(Canvas.prototype, 'height', { get: function () { return this._h; }, set: function (v) { this._h = v | 0; __csize(this._id, this._w, this._h); } });
  Canvas.prototype.getContext = function () { return this._ctx || (this._ctx = new Ctx(this)); };
  Canvas.prototype.addEventListener = function (type, fn, capture) {
    (this._ls[type] = this._ls[type] || []).push({ fn: fn, capture: !!capture });
    this._ls[type].sort(function (a, b) { return (b.capture ? 1 : 0) - (a.capture ? 1 : 0); });
  };
  Canvas.prototype.setPointerCapture = function () {};
  Canvas.prototype.toDataURL = function () { return ''; };
  Canvas.prototype.getBoundingClientRect = function () { return { left: 0, top: 0, width: g.innerWidth, height: g.innerHeight }; };

  var main = new Canvas(256, 240);
  g.__mainCanvas = main;
  var winListeners = {};
  g.document = {
    getElementById: function () { return main; },
    createElement: function (tag) { return new Canvas(300, 150); },
    body: { classList: { add: function () {}, remove: function () {} } }
  };
  g.addEventListener = function (type, fn) { (winListeners[type] = winListeners[type] || []).push(fn); };
  g.devicePixelRatio = 1;

  // timers
  var timers = [], nextId = 1;
  g.setInterval = function (fn, ms) { var t = { id: nextId++, fn: fn, ms: Math.max(1, ms), at: __now() + ms, rep: true }; timers.push(t); return t.id; };
  g.setTimeout = function (fn, ms) { var t = { id: nextId++, fn: fn, ms: ms || 0, at: __now() + (ms || 0), rep: false }; timers.push(t); return t.id; };
  g.clearInterval = g.clearTimeout = function (id) { timers = timers.filter(function (t) { return t.id !== id; }); };
  g.requestAnimationFrame = function (fn) { return g.setTimeout(function () { fn(__now()); }, 16); };
  g.__runTimers = function () {
    var now = __now(), due = timers.filter(function (t) { return t.at <= now; });
    due.forEach(function (t) {
      if (t.rep) { t.at = Math.max(t.at + t.ms, now); } else timers = timers.filter(function (x) { return x !== t; });
      try { t.fn(); } catch (e) { __log('timer: ' + e + '\n' + (e.stack || '')); }
    });
  };

  // localStorage backed by a JSON file
  var lsPath = g.__lsPath, lsData = {};
  try { lsData = JSON.parse(__read(lsPath) || '{}'); } catch (e) { lsData = {}; }
  g.localStorage = {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(lsData, k) ? lsData[k] : null; },
    setItem: function (k, v) { lsData[k] = String(v); __write(lsPath, JSON.stringify(lsData)); },
    removeItem: function (k) { delete lsData[k]; __write(lsPath, JSON.stringify(lsData)); }
  };

  // base64
  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  g.atob = function (s) {
    s = String(s).replace(/[^A-Za-z0-9+/]/g, ''); var out = '', buf = 0, bits = 0;
    for (var i = 0; i < s.length; i++) { buf = (buf << 6) | B64.indexOf(s[i]); bits += 6; if (bits >= 8) { bits -= 8; out += String.fromCharCode((buf >> bits) & 255); } }
    return out;
  };
  g.btoa = function (s) {
    var out = '', i = 0;
    for (; i + 2 < s.length; i += 3) { var n = s.charCodeAt(i) << 16 | s.charCodeAt(i + 1) << 8 | s.charCodeAt(i + 2); out += B64[n >> 18 & 63] + B64[n >> 12 & 63] + B64[n >> 6 & 63] + B64[n & 63]; }
    if (i < s.length) { var m = s.charCodeAt(i) << 16 | (i + 1 < s.length ? s.charCodeAt(i + 1) << 8 : 0); out += B64[m >> 18 & 63] + B64[m >> 12 & 63] + (i + 1 < s.length ? B64[m >> 6 & 63] : '=') + '='; }
    return out;
  };
  // Image: only the menu's save-state thumbnails ("slot:N" URLs from Host.thumb)
  g.Image = function () { this.width = 0; this.height = 0; this._id = -1; this.onload = null; };
  Object.defineProperty(g.Image.prototype, 'src', {
    get: function () { return this._src || ''; },
    set: function (v) {
      var self = this; this._src = v; this._id = __cimg(String(v));
      if (this._id >= 0) g.setTimeout(function () { if (self.onload) self.onload(); }, 0);
    }
  });

  // input + resize, called from C
  g.__pointer = function (type, x, y, id) {
    var ev = { clientX: x, clientY: y, pointerId: id, _stop: false, touches: [{ clientX: x, clientY: y }],
               preventDefault: function () {}, stopPropagation: function () {}, stopImmediatePropagation: function () { this._stop = true; } };
    var names = type === 'down' ? ['pointerdown', 'touchstart'] : type === 'move' ? ['pointermove', 'touchmove'] : ['pointerup', 'touchend'];
    names.forEach(function (n) {
      var ls = main._ls[n] || [];
      for (var i = 0; i < ls.length && !ev._stop; i++) { try { ls[i].fn(ev); } catch (e) { __log(n + ': ' + e + '\n' + (e.stack || '')); } }
      ev._stop = false;
    });
  };
  g.__resize = function (w, h) {
    g.innerWidth = w; g.innerHeight = h;
    (winListeners.resize || []).forEach(function (fn) { try { fn(); } catch (e) { __log('resize: ' + e); } });
  };
})(this);
